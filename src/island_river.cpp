#include "island_river.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

#include "value_noise.h"

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr uint64_t STREAM_RIVER = 4; // 接着 island_field.cpp 的 Stream 往下排

// 河：选线
constexpr double SOURCE_INSIDE = 0.35; // 泉眼至少这么靠腹地
constexpr double SOURCE_BAND_LO = 0.3, SOURCE_BAND_HI = 0.7; // 泉眼取腹地高度的这一段分位：有落差，又不去削峰顶
// 河口离泉眼至少这么远（× 等效半径）：先求一条横穿腹地的长河，不成再退而求其次
constexpr double MOUTH_MIN_RUNS[] = { 1.0, 0.8, 0.6 };
constexpr double UPHILL_COST = 8.0; // 每爬高 1 m，相当于多绕这么多米
constexpr double HEIGHT_COST = 1.5; // 走高处的附加代价：把河赶进谷里
constexpr double MEANDER_COST = 3.0; // 噪声代价：让河拐弯（小了河会沿栅格的八个方向走成水渠）
constexpr double RIM_HUG_COST = 3.0; // 贴着岸缘走的附加代价
constexpr double RIM_HUG_INSIDE = 0.06;
// 码头傍水而建：泉眼尽量取在码头这半边，河道离码头越远代价越高——下了船走不了多远就该见到河
constexpr double SOURCE_NEAR_DOCK = 0.9; // × 等效半径
constexpr double DOCK_PULL_COST = 2.0;
constexpr int SMOOTH_PASSES = 4;

// 河：断面
constexpr double MIN_FALL = 0.0015; // 水面每米至少降这么多
constexpr double SPRING_EXTRA_M = 2.5; // 泉眼处水面多出的半宽
constexpr double SPRING_LENGTH_M = 6.0;
constexpr double BANK_RISE_M = 2.0; // 水边到河滩高度的过渡宽度
constexpr double FLAT_FALL = 0.02; // 河滩朝河倾 2%
constexpr double CUT_SLOPE = 0.55; // 河滩以外，挖方最陡这么陡（约 29°）
constexpr double FILL_SLOPE = 0.45; // 填方

// 路
constexpr double TRAIL_GRADE_SCALE = 0.18; // 选线时坡度的「一倍代价」：走 18% 的坡，代价翻倍；再陡按平方涨
constexpr double TRAIL_MAX_GRADE = 0.32; // 路面纵坡上限（约 18°）
constexpr double TRAIL_CUT_SLOPE = 1.2; // 路堑的边坡没人走，可以陡
constexpr double TRAIL_FILL_SLOPE = 1.0;
constexpr int TRAIL_LEVEL_PASSES = 8;

// 离线远的地方（河湾内侧）到两段线差不多远，高度在两段之间过渡的尺度 = BLEND_BASE_M + BLEND_PER_M × 距离；
// 贴着线的地方尺度趋近于零，取的就是最近点的高度
constexpr double BLEND_BASE_M = 0.4, BLEND_PER_M = 0.25;

const int N8[8][2] = { { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, -1 }, { 0, 1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } };

double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

bool on_rim(const IslandField &r, int i, int j) {
    return !r.in_mask(i - 1, j) || !r.in_mask(i + 1, j) || !r.in_mask(i, j - 1) || !r.in_mask(i, j + 1);
}

// h 上的双线性取样；该处没有地面就退到最近的陆地节点，再没有则返回 false
bool sample(const IslandField &r, const std::vector<double> &h, double p_x, double p_z, double &r_y) {
    const double ext = r.half_extent();
    const double fx = (p_x + ext) / r.cell_m, fz = (p_z + ext) / r.cell_m;
    const int j = int(std::floor(fx)), i = int(std::floor(fz));
    if (r.quad_solid(i, j)) {
        const double tx = fx - j, tz = fz - i;
        r_y = h[r.index(i, j)] * (1 - tz) * (1 - tx) + h[r.index(i, j + 1)] * (1 - tz) * tx +
                h[r.index(i + 1, j)] * tz * (1 - tx) + h[r.index(i + 1, j + 1)] * tz * tx;
        return true;
    }
    const int ni = int(std::lround(fz)), nj = int(std::lround(fx));
    if (r.in_mask(ni, nj)) {
        r_y = h[r.index(ni, nj)];
        return true;
    }
    return false;
}

// 一条沿线带高度与半宽的折线（河：水面高；路：路面高），以及某一点相对于它的位置
struct LinePoint {
    double x = 0.0, z = 0.0, level = 0.0, half_width = 0.0;
};

struct Nearby {
    double dist = 0.0, level = 0.0, half_width = 0.0;
};

class Polyline {
    std::vector<LinePoint> points;
    mutable std::vector<std::pair<double, double>> scratch; // 每段的 (距离, 参数)

public:
    explicit Polyline(std::vector<LinePoint> p_points) :
            points(std::move(p_points)), scratch(points.size()) {}

    // 高度不直接取最近点的：拐弯内侧最近点会在两段之间跳，地面跟着断成坎。按「比最近的远多少」软加权
    Nearby near(double p_x, double p_z) const {
        Nearby out;
        out.dist = std::numeric_limits<double>::infinity();
        for (size_t s = 0; s + 1 < points.size(); s++) {
            const LinePoint &a = points[s], &b = points[s + 1];
            const double ux = b.x - a.x, uz = b.z - a.z;
            const double t = std::clamp(((p_x - a.x) * ux + (p_z - a.z) * uz) / std::max(1e-9, ux * ux + uz * uz), 0.0, 1.0);
            scratch[s] = { std::hypot(p_x - (a.x + t * ux), p_z - (a.z + t * uz)), t };
            out.dist = std::min(out.dist, scratch[s].first);
        }
        double total = 0.0;
        for (size_t s = 0; s + 1 < points.size(); s++) {
            const double w = std::exp(-(scratch[s].first - out.dist) / (BLEND_BASE_M + BLEND_PER_M * out.dist));
            const double t = scratch[s].second;
            out.level += w * (points[s].level + t * (points[s + 1].level - points[s].level));
            out.half_width += w * (points[s].half_width + t * (points[s + 1].half_width - points[s].half_width));
            total += w;
        }
        out.level /= total;
        out.half_width /= total;
        return out;
    }
};

// 节点序列 → 抹平的折线（端点不动）
std::vector<LinePoint> smooth_course(const IslandField &r, const std::vector<int> &p_course) {
    std::vector<LinePoint> line(p_course.size());
    for (size_t k = 0; k < p_course.size(); k++) {
        line[k].x = r.coord(p_course[k] % r.n);
        line[k].z = r.coord(p_course[k] / r.n);
    }
    for (int pass = 0; pass < SMOOTH_PASSES; pass++) {
        std::vector<LinePoint> next = line;
        for (size_t k = 1; k + 1 < line.size(); k++) {
            next[k].x = 0.25 * line[k - 1].x + 0.5 * line[k].x + 0.25 * line[k + 1].x;
            next[k].z = 0.25 * line[k - 1].z + 0.5 * line[k].z + 0.25 * line[k + 1].z;
        }
        line = next;
    }
    return line;
}

// 停机坪是码头的，河与路的土方到它跟前都得收手：离码头 p_inner 以内 0，p_outer 以外 1
double dock_release(const IslandField &r, double p_x, double p_z, double p_inner, double p_outer) {
    const double d = std::hypot(p_x - r.dock.x, p_z - r.dock.z);
    return smoothstep(std::clamp((d - p_inner) / (p_outer - p_inner), 0.0, 1.0));
}

// 最小代价路径的骨架：从 p_source 出发，p_step 给出走到邻居的代价（负 = 不可走），p_goal 判终点。
std::vector<int> least_cost_path(const IslandField &r, int p_source, const std::function<double(int, int, double)> &p_step,
        const std::function<bool(int)> &p_goal, const std::function<bool(int)> &p_dead_end) {
    const int n = r.n;
    std::vector<double> cost(size_t(n) * n, std::numeric_limits<double>::infinity());
    std::vector<int> prev(size_t(n) * n, -1);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    cost[p_source] = 0.0;
    open.push({ 0.0, p_source });

    int goal = -1;
    while (!open.empty()) {
        const auto [c, k] = open.top();
        open.pop();
        if (c > cost[k]) {
            continue;
        }
        if (p_goal(k)) {
            goal = k;
            break;
        }
        if (p_dead_end(k)) {
            continue;
        }
        const int i = k / n, j = k % n;
        for (const auto &d : N8) {
            const int bi = i + d[0], bj = j + d[1];
            // 斜着走时两个直邻都得是陆地，免得从两块陆地的角缝里挤过去
            if (!r.in_mask(bi, bj) || (d[0] && d[1] && !(r.in_mask(bi, j) && r.in_mask(i, bj)))) {
                continue;
            }
            const int m = int(r.index(bi, bj));
            const double step = p_step(k, m, r.cell_m * ((d[0] && d[1]) ? std::sqrt(2.0) : 1.0));
            if (step >= 0.0 && c + step < cost[m]) {
                cost[m] = c + step;
                prev[m] = k;
                open.push({ c + step, m });
            }
        }
    }
    std::vector<int> course;
    for (int k = goal; k >= 0; k = prev[k]) {
        course.push_back(k);
    }
    std::reverse(course.begin(), course.end());
    return course;
}

// 泉眼 → 河口的节点序列；找不到返回空。
std::vector<int> trace_river(const IslandParams &p, const IslandField &r, const std::vector<double> &h, double p_keep_out, double p_min_run) {
    Rng rng = Rng(p.seed).fork(STREAM_RIVER);
    const int n = r.n;
    const double R = std::sqrt(p.area_m2 / PI);
    const double ext = r.half_extent();
    const double relief = std::max(1.0, p.peak_m - p.rim_m);
    FractalNoise meander(rng, -ext, -ext, ext, ext, std::max(0.18 * R, 4.0 * p.cell_m), 3, 0.5);

    auto dock_distance = [&](int k) { return std::hypot(r.coord(k % n) - r.dock.x, r.coord(k / n) - r.dock.z); };

    std::vector<int> candidates, near_dock;
    for (int k = 0; k < n * n; k++) {
        if (r.mask[k] && r.inside[k] >= SOURCE_INSIDE && dock_distance(k) >= p_keep_out + 20.0) {
            candidates.push_back(k);
            if (dock_distance(k) <= SOURCE_NEAR_DOCK * R) {
                near_dock.push_back(k);
            }
        }
    }
    if (!near_dock.empty()) {
        candidates = near_dock;
    }
    if (candidates.empty()) {
        return {};
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) { return h[a] < h[b]; });
    const int band_lo = int(SOURCE_BAND_LO * candidates.size());
    const int band_hi = std::max(band_lo + 1, int(SOURCE_BAND_HI * candidates.size()));
    const int source = candidates[std::min(int(candidates.size()) - 1, rng.range(band_lo, band_hi))];
    const double sx = r.coord(source % n), sz = r.coord(source / n);

    return least_cost_path(
            r, source,
            [&](int k, int m, double dist) {
                if (dock_distance(m) < p_keep_out) {
                    return -1.0;
                }
                double weight = 1.0 + MEANDER_COST * (0.5 + 0.5 * meander.sample(r.coord(m % n), r.coord(m / n))) +
                        HEIGHT_COST * (h[m] - p.rim_m) / relief + DOCK_PULL_COST * std::min(1.5, (dock_distance(m) - p_keep_out) / R);
                if (r.inside[m] < RIM_HUG_INSIDE && !on_rim(r, m / n, m % n)) {
                    weight += RIM_HUG_COST;
                }
                return dist * weight + UPHILL_COST * std::max(0.0, h[m] - h[k]);
            },
            [&](int k) {
                return on_rim(r, k / n, k % n) && dock_distance(k) >= p_keep_out + 20.0 &&
                        std::hypot(r.coord(k % n) - sx, r.coord(k / n) - sz) >= p_min_run * R;
            },
            [&](int k) { return on_rim(r, k / n, k % n); }); // 岸缘上不再往前走：河不沿着崖边流
}

} // namespace

void carve_island_river(const IslandParams &p, IslandField &r, std::vector<double> &h) {
    const int n = r.n;
    const double keep_out = p.dock_apron_m + p.river_half_width_m + SPRING_EXTRA_M + p.river_flat_m + 6.0;
    std::vector<int> course;
    for (double min_run : MOUTH_MIN_RUNS) {
        course = trace_river(p, r, h, keep_out, min_run);
        if (!course.empty()) {
            break;
        }
    }
    if (course.size() < 4) {
        return;
    }

    // 中线：河口外再探出一格，让崖边的节点也落在河槽里
    std::vector<LinePoint> line = smooth_course(r, course);
    {
        const LinePoint &a = line[line.size() - 2], &b = line.back();
        const double len = std::max(1e-9, std::hypot(b.x - a.x, b.z - a.z));
        LinePoint lip;
        lip.x = b.x + (b.x - a.x) / len * r.cell_m;
        lip.z = b.z + (b.z - a.z) / len * r.cell_m;
        line.push_back(lip);
    }

    // 水面：贴着地面以下一个滩高走，沿程只降不升——遇到鼓包就切过去
    double run = 0.0, ground = h[course.front()];
    for (size_t k = 0; k < line.size(); k++) {
        const double step = k == 0 ? 0.0 : std::hypot(line[k].x - line[k - 1].x, line[k].z - line[k - 1].z);
        run += step;
        sample(r, h, line[k].x, line[k].z, ground); // 取不到（探出岸外的那一点）就沿用上一点的
        const double level = ground - p.river_freeboard_m;
        line[k].level = k == 0 ? level : std::min(line[k - 1].level - MIN_FALL * step, level);
        line[k].half_width = p.river_half_width_m + SPRING_EXTRA_M * std::exp(-run / SPRING_LENGTH_M);
    }

    r.river_dist.assign(size_t(n) * n, std::numeric_limits<float>::max());
    r.river_depth.assign(size_t(n) * n, 0.0f);
    const Polyline polyline(line);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const size_t k = r.index(i, j);
            if (!r.mask[k]) {
                continue;
            }
            const double x = r.coord(j), z = r.coord(i);
            const Nearby here = polyline.near(x, z);
            r.river_dist[k] = float(here.dist);

            const double beyond_water = here.dist - here.half_width;
            if (beyond_water <= 0.0) {
                // 河槽：抛物线断面，河心最深
                const double q = here.dist / here.half_width;
                h[k] = here.level - p.river_depth_m * (1.0 - q * q);
                r.river_depth[k] = float(here.level - h[k]);
            } else {
                // 河滩：水边抬到滩高，再朝外微微上倾；滩外夹在一个张开的楔子里，原地形落在楔内就原样留着
                const double bank = here.level + p.river_freeboard_m * smoothstep(std::min(1.0, beyond_water / BANK_RISE_M)) + FLAT_FALL * beyond_water;
                const double beyond_flat = std::max(0.0, beyond_water - p.river_flat_m);
                const double shaped = std::clamp(h[k], bank - FILL_SLOPE * beyond_flat, bank + CUT_SLOPE * beyond_flat);
                h[k] += dock_release(r, x, z, p.dock_apron_m, 2.0 * p.dock_apron_m) * (shaped - h[k]);
            }
        }
    }

    r.river.resize(line.size());
    for (size_t k = 0; k < line.size(); k++) {
        r.river[k] = { line[k].x, line[k].z, line[k].level, line[k].half_width };
    }
}

void carve_dock_trail(const IslandParams &p, IslandField &r, std::vector<double> &h) {
    if (!r.has_river()) {
        return;
    }
    const int n = r.n;

    // 起点：停机坪压平的那一圈里、码头正后方
    int source = -1;
    {
        const double sx = r.dock.x - 0.4 * p.dock_apron_m * r.dock.out_x, sz = r.dock.z - 0.4 * p.dock_apron_m * r.dock.out_z;
        const int i = int(std::lround((sz + r.half_extent()) / r.cell_m)), j = int(std::lround((sx + r.half_extent()) / r.cell_m));
        if (r.in_mask(i, j)) {
            source = int(r.index(i, j));
        }
    }
    if (source < 0) {
        return;
    }

    // 终点：河滩上随便哪一点，只要离水边和滩外缘都有点余地
    auto on_floodplain = [&](int k) {
        const double beyond_water = r.river_dist[k] - p.river_half_width_m - SPRING_EXTRA_M;
        return r.river_depth[k] <= 0.0f && beyond_water >= 1.0 && beyond_water <= p.river_flat_m - SPRING_EXTRA_M - 1.0;
    };
    const std::vector<int> course = least_cost_path(
            r, source,
            [&](int k, int m, double dist) {
                if (r.river_depth[m] > 0.0f) {
                    return -1.0;
                }
                const double grade = std::abs(h[m] - h[k]) / dist / TRAIL_GRADE_SCALE;
                return dist * (1.0 + grade * grade + (on_rim(r, m / n, m % n) ? RIM_HUG_COST : 0.0));
            },
            on_floodplain, [](int) { return false; });
    if (course.size() < 3) {
        return;
    }

    // 路面高：先跟着地面，抹平，再把纵坡压到上限以内（两头钉死在停机坪与河滩的地面上）
    std::vector<LinePoint> line = smooth_course(r, course);
    double ground = h[source];
    for (LinePoint &point : line) {
        sample(r, h, point.x, point.z, ground);
        point.level = ground;
        point.half_width = p.trail_half_width_m;
    }
    for (int pass = 0; pass < TRAIL_LEVEL_PASSES; pass++) {
        std::vector<LinePoint> next = line;
        for (size_t k = 1; k + 1 < line.size(); k++) {
            next[k].level = 0.25 * line[k - 1].level + 0.5 * line[k].level + 0.25 * line[k + 1].level;
        }
        line = next;
    }
    auto limit_toward = [&](size_t k, size_t from) {
        const double reach = TRAIL_MAX_GRADE * std::hypot(line[k].x - line[from].x, line[k].z - line[from].z);
        line[k].level = std::clamp(line[k].level, line[from].level - reach, line[from].level + reach);
    };
    for (int round = 0; round < 4; round++) {
        for (size_t k = 1; k + 1 < line.size(); k++) {
            limit_toward(k, k - 1);
        }
        for (size_t k = line.size() - 2; k >= 1; k--) {
            limit_toward(k, k + 1);
        }
    }

    // 路基：路面压平，两侧按边坡接回原地形；不动河槽，到停机坪跟前收手
    r.trail_dist.assign(size_t(n) * n, std::numeric_limits<float>::max());
    const Polyline polyline(line);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const size_t k = r.index(i, j);
            if (!r.mask[k]) {
                continue;
            }
            const double x = r.coord(j), z = r.coord(i);
            const Nearby here = polyline.near(x, z);
            r.trail_dist[k] = float(here.dist);
            if (r.river_depth[k] > 0.0f) {
                continue;
            }
            const double beyond = std::max(0.0, here.dist - here.half_width);
            const double shaped = std::clamp(h[k], here.level - TRAIL_FILL_SLOPE * beyond, here.level + TRAIL_CUT_SLOPE * beyond);
            // 路是从停机坪里接出来的：只让开压得全平的那半个半径
            h[k] += dock_release(r, x, z, 0.5 * p.dock_apron_m, p.dock_apron_m) * (shaped - h[k]);
        }
    }

    r.trail.resize(line.size());
    for (size_t k = 0; k < line.size(); k++) {
        r.trail[k] = { line[k].x, line[k].z, line[k].level };
    }
}
