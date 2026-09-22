#include "island_field.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "island_river.h"
#include "value_noise.h"

namespace {

constexpr double PI = 3.14159265358979323846;

// 以下常量与 skyisle-gen config/default.toml 的 [island] 段同名同值。
constexpr double AGE_YOUNG = 0.3;
constexpr double AGE_OLD = 0.65;
constexpr double SHAPE_PAD = 1.35; // 局部栅格半宽 = pad × 半长轴
constexpr double WARP_AMP = 0.18; // 域扭曲幅度（× 等效半径）
constexpr double SHAPE_NOISE_AMP = 0.45; // 轮廓噪声幅度（相对椭圆穹顶 1）
constexpr int SHAPE_OCTAVES = 5;
constexpr double CONE_RADIUS_REL = 1.25; // 锥脚落在岸线之外，岸缘才有高度
constexpr double CONE_PROFILE_POW = 1.5;
constexpr double GULLY_AMP = 0.35;
constexpr double PLATEAU_RISE = 0.3;
constexpr double PLATEAU_VALLEY_DEPTH = 0.5;
constexpr double MOUND_AMP = 0.15;
constexpr double TALUS_DEG = 38.0;

// 这里没有移植水流侵蚀，只留热力坍塌 + 坡面扩散，让坡能走人；要河的岛由 island_river 事后刻一条。
constexpr int RELAX_ROUNDS = 12;
constexpr double DIFFUSION_K = 0.1;

constexpr double BERTH_HALF_LENGTH = 16.0; // 泊位两侧要空出的岸线（半个最长的船身还多一点）

// 船坞坪
constexpr double YARD_GAP = 6.0; // 坪与码头中线（栈桥、出生点、路头所在）之间留的空：路要从这儿上山
constexpr double YARD_LAND_MARGIN = 2.0; // 坪四周还得有这么宽的陆地
constexpr double YARD_CUT_SLOPE = 1.0; // 坪外挖方 45°
constexpr double YARD_FILL_SLOPE = 0.8;

enum Stream : uint64_t {
    STREAM_SHAPE = 1,
    STREAM_FORM = 2,
    STREAM_BOTTOM = 3,
};

const int N8[8][2] = { { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, -1 }, { 0, 1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } };

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

// f > tau 的最大 4 连通块 → r_mask，返回块内节点数。
int largest_component(const std::vector<double> &p_f, double p_tau, int p_n, std::vector<uint8_t> &r_mask,
        std::vector<int> &r_label, std::vector<int> &r_stack) {
    std::fill(r_label.begin(), r_label.end(), 0);
    int best_id = 0, best_count = 0, next_id = 0;
    const int total = p_n * p_n;
    for (int start = 0; start < total; start++) {
        if (r_label[start] != 0 || !(p_f[start] > p_tau)) {
            continue;
        }
        next_id++;
        int count = 0;
        r_stack.clear();
        r_stack.push_back(start);
        r_label[start] = next_id;
        while (!r_stack.empty()) {
            const int k = r_stack.back();
            r_stack.pop_back();
            count++;
            const int i = k / p_n, j = k % p_n;
            const int nb[4] = { i > 0 ? k - p_n : -1, i < p_n - 1 ? k + p_n : -1, j > 0 ? k - 1 : -1, j < p_n - 1 ? k + 1 : -1 };
            for (int m : nb) {
                if (m >= 0 && r_label[m] == 0 && p_f[m] > p_tau) {
                    r_label[m] = next_id;
                    r_stack.push_back(m);
                }
            }
        }
        if (count > best_count) {
            best_count = count;
            best_id = next_id;
        }
    }
    for (int k = 0; k < total; k++) {
        r_mask[k] = (best_id != 0 && r_label[k] == best_id) ? 1 : 0;
    }
    return best_count;
}

// 岛形：椭圆穹顶 + 域扭曲 + 轮廓噪声，按面积二分反解阈值。
void build_shape(const IslandParams &p, IslandField &r) {
    Rng rng = Rng(p.seed).fork(STREAM_SHAPE);
    const double R = std::sqrt(p.area_m2 / PI);
    const double a = R * std::sqrt(p.elongation), b = R / std::sqrt(p.elongation);
    const double half = SHAPE_PAD * a;
    r.n = std::max(5, int(std::ceil(2.0 * half / p.cell_m)) + 1);
    r.cell_m = p.cell_m;
    const int n = r.n;
    const double ext = r.half_extent();

    const double warp_feature = std::max(0.8 * R, 3.0 * p.cell_m);
    FractalNoise warp_x(rng, -ext, -ext, ext, ext, warp_feature, 2, 0.5);
    FractalNoise warp_z(rng, -ext, -ext, ext, ext, warp_feature, 2, 0.5);
    FractalNoise outline(rng, -ext, -ext, ext, ext, std::max(0.5 * R, 3.0 * p.cell_m), SHAPE_OCTAVES, 0.55);
    const double wamp = WARP_AMP * R;
    const double ct = std::cos(p.theta), st = std::sin(p.theta);

    std::vector<double> f(size_t(n) * n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const double x = r.coord(j), z = r.coord(i);
            const double xw = x + wamp * warp_x.sample(x, z);
            const double zw = z + wamp * warp_z.sample(x, z);
            const double u = (xw * ct + zw * st) / a;
            const double v = (-xw * st + zw * ct) / b;
            f[r.index(i, j)] = 1.0 - (u * u + v * v) + SHAPE_NOISE_AMP * outline.sample(x, z);
        }
    }

    const double target = p.area_m2 / (p.cell_m * p.cell_m);
    double lo = *std::min_element(f.begin(), f.end());
    double hi = *std::max_element(f.begin(), f.end());
    std::vector<uint8_t> mask(f.size()), best_mask(f.size());
    std::vector<int> label(f.size()), stack;
    double best_err = 1e18;
    for (int iter = 0; iter < 48; iter++) {
        const double tau = 0.5 * (lo + hi);
        const int count = largest_component(f, tau, n, mask, label, stack);
        const double err = std::abs(count - target);
        if (err < best_err) {
            best_err = err;
            best_mask = mask;
        }
        if (count > target) {
            lo = tau;
        } else {
            hi = tau;
        }
        if (err <= std::max(0.5, 0.002 * target)) {
            break;
        }
    }
    r.mask = best_mask;

    double f_min = 1e18, f_max = -1e18;
    for (size_t k = 0; k < f.size(); k++) {
        if (r.mask[k]) {
            f_min = std::min(f_min, f[k]);
            f_max = std::max(f_max, f[k]);
        }
    }
    r.inside.assign(f.size(), 0.0f);
    for (size_t k = 0; k < f.size(); k++) {
        if (r.mask[k]) {
            r.inside[k] = float(clamp01((f[k] - f_min) / std::max(1e-9, f_max - f_min)));
        }
    }
}

// 围绕 (cx, cz) 的角向噪声 → 放射状沟谷因子 (1 − amp·g(θ))。
struct RadialGullies {
    std::vector<double> lobes;

    explicit RadialGullies(Rng &p_rng) {
        lobes.resize(p_rng.range(9, 16));
        for (double &v : lobes) {
            v = p_rng.uniform();
        }
    }

    double factor(double p_dx, double p_dz) const {
        const int n = int(lobes.size());
        const double fidx = (std::atan2(p_dz, p_dx) + PI) / (2.0 * PI) * n;
        const int i0 = ((int(std::floor(fidx)) % n) + n) % n;
        const double t = smoothstep(fidx - std::floor(fidx));
        return 1.0 - GULLY_AMP * (lobes[i0] * (1 - t) + lobes[(i0 + 1) % n] * t);
    }
};

// 基形随岛龄：未归一的 shape（岸缘低 → 峰高）。
std::vector<double> build_base_form(const IslandParams &p, const IslandField &r) {
    Rng rng = Rng(p.seed).fork(STREAM_FORM);
    const int n = r.n;
    const double R = std::sqrt(p.area_m2 / PI);
    const double ext = r.half_extent();

    std::vector<int> land;
    for (int k = 0; k < n * n; k++) {
        if (r.mask[k]) {
            land.push_back(k);
        }
    }

    // 峰的位置：向内程度最高的 5% 里挑一个，让它偏离中心一点
    std::vector<int> order = land;
    std::stable_sort(order.begin(), order.end(), [&](int l, int m) { return r.inside[l] > r.inside[m]; });
    const int k_top = std::max(1, int(0.05 * land.size()));
    const int peak_node = order[rng.range(0, k_top)];
    const double px = r.coord(peak_node % n), pz = r.coord(peak_node / n);

    FractalNoise fn_ridge(rng, -ext, -ext, ext, ext, std::max(0.45 * R, 4.0 * p.cell_m), 4, 0.5);
    const double fine_feature = std::max(0.15 * R, 3.0 * p.cell_m);
    const int fine_octaves = std::clamp(int(std::lround(std::log2(fine_feature / (2.5 * p.cell_m)))) + 1, 2, 7);
    FractalNoise fn_fine(rng, -ext, -ext, ext, ext, fine_feature, fine_octaves, 0.55);

    std::vector<double> shape(size_t(n) * n, 0.0);

    switch (r.age_class) {
        case IslandAge::YOUNG: {
            struct Cone {
                double x, z, gain;
                RadialGullies gullies;
            };
            std::vector<Cone> cones;
            const int n_cones = (p.area_m2 > 60000.0 && rng.uniform() < 0.4) ? 2 : 1;
            for (int c = 0; c < n_cones; c++) {
                if (c == 0) {
                    cones.push_back({ px, pz, 1.0, RadialGullies(rng) });
                } else {
                    const double ang = rng.uniform(-PI, PI);
                    const double gain = rng.uniform(0.55, 0.9);
                    cones.push_back({ px + 0.45 * R * std::cos(ang), pz + 0.45 * R * std::sin(ang), gain, RadialGullies(rng) });
                }
            }
            const double Rc = CONE_RADIUS_REL * R;
            for (int k : land) {
                const double x = r.coord(k % n), z = r.coord(k / n);
                double s = 0.0;
                for (const Cone &cone : cones) {
                    const double d = std::hypot(x - cone.x, z - cone.z) / Rc;
                    double v = std::pow(clamp01(1.0 - d), CONE_PROFILE_POW);
                    v *= std::pow(cone.gullies.factor(x - cone.x, z - cone.z), clamp01(d));
                    s = std::max(s, v * cone.gain);
                }
                shape[k] = s * (0.7 + 0.3 * std::sqrt(double(r.inside[k]))) + 0.06 * fn_fine.sample(x, z);
            }
        } break;

        case IslandAge::MID: {
            // 主脊线：过峰、方向随机
            const double ang = rng.uniform(-PI, PI);
            // 次级山峰：1–3 个高斯丘，让分水岭不止一条
            struct Bump {
                double x, z, gain;
            };
            std::vector<Bump> bumps;
            const int n_bumps = rng.range(1, 4);
            for (int c = 0; c < n_bumps; c++) {
                const int k = land[rng.range(0, int(land.size()))];
                bumps.push_back({ r.coord(k % n), r.coord(k / n), rng.uniform(0.4, 0.8) });
            }
            for (int k : land) {
                const double x = r.coord(k % n), z = r.coord(k / n);
                const double ridged = 1.0 - std::abs(fn_ridge.sample(x, z));
                const double dperp = std::abs(-(x - px) * std::sin(ang) + (z - pz) * std::cos(ang));
                double crest = std::exp(-std::pow(dperp / (0.35 * R), 2));
                for (const Bump &bump : bumps) {
                    const double d2 = (x - bump.x) * (x - bump.x) + (z - bump.z) * (z - bump.z);
                    crest = std::max(crest, bump.gain * std::exp(-d2 / std::pow(0.25 * R, 2)));
                }
                const double core = clamp01(1.0 - std::hypot(x - px, z - pz) / (1.4 * R));
                shape[k] = std::pow(double(r.inside[k]), 0.35) * (0.2 + 0.45 * ridged + 0.35 * crest) * (0.55 + 0.45 * core) +
                        0.05 * fn_fine.sample(x, z);
            }
        } break;

        case IslandAge::OLD: {
            for (int k : land) {
                const double x = r.coord(k % n), z = r.coord(k / n);
                const double in = r.inside[k];
                const double plateau = smoothstep(clamp01(in / PLATEAU_RISE));
                const double valley = clamp01(fn_ridge.sample(x, z));
                const double tilt = clamp01(1.0 - std::hypot(x - px, z - pz) / (1.6 * R));
                const double fine = fn_fine.sample(x, z);
                const double mounds = clamp01(fine) * (1.0 - plateau);
                shape[k] = std::sqrt(plateau) * (0.75 + 0.25 * tilt) * (1.0 - PLATEAU_VALLEY_DEPTH * valley * in) +
                        MOUND_AMP * mounds + 0.03 * fine;
            }
        } break;
    }
    return shape;
}

// 热力坍塌（超过休止角的部分推给低处的邻居）+ 坡面扩散。
void relax(IslandField &r, std::vector<double> &h, int p_rounds) {
    const int n = r.n;
    const double talus = std::tan(TALUS_DEG * PI / 180.0);
    std::vector<double> delta(h.size());
    for (int round = 0; round < p_rounds; round++) {
        std::fill(delta.begin(), delta.end(), 0.0);
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (!r.in_mask(i, j)) {
                    continue;
                }
                const size_t k = r.index(i, j);
                for (const auto &d : N8) {
                    if (!r.in_mask(i + d[0], j + d[1])) {
                        continue;
                    }
                    const size_t m = r.index(i + d[0], j + d[1]);
                    const double dist = r.cell_m * ((d[0] && d[1]) ? std::sqrt(2.0) : 1.0);
                    const double excess = (h[k] - h[m]) - talus * dist;
                    if (excess > 0.0) {
                        delta[k] -= 0.125 * excess;
                        delta[m] += 0.125 * excess;
                    }
                }
            }
        }
        for (size_t k = 0; k < h.size(); k++) {
            h[k] += delta[k];
        }
        std::fill(delta.begin(), delta.end(), 0.0);
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (!r.in_mask(i, j)) {
                    continue;
                }
                const size_t k = r.index(i, j);
                const int nb[4][2] = { { i - 1, j }, { i + 1, j }, { i, j - 1 }, { i, j + 1 } };
                for (const auto &q : nb) {
                    if (r.in_mask(q[0], q[1])) {
                        delta[k] += DIFFUSION_K * (h[r.index(q[0], q[1])] - h[k]);
                    }
                }
            }
        }
        for (size_t k = 0; k < h.size(); k++) {
            h[k] += delta[k];
        }
    }
}

bool land_at(const IslandField &r, double p_x, double p_z) {
    const double ext = r.half_extent();
    const int j = int(std::lround((p_x + ext) / r.cell_m));
    const int i = int(std::lround((p_z + ext) / r.cell_m));
    return r.in_mask(i, j);
}

// 码头：沿方位自外向内找到岸缘点，量出栈桥要伸多长，再把它背后的停机坪压平。
void build_dock(const IslandParams &p, IslandField &r, std::vector<double> &h) {
    IslandDock &dock = r.dock;
    dock.out_x = std::cos(p.dock_bearing);
    dock.out_z = std::sin(p.dock_bearing);
    const double step = 0.5 * r.cell_m;

    // 岸缘点：它和它背后 4 格都得是陆地，免得落在一根细岬上
    double t_rim = 0.0;
    for (double t = r.half_extent(); t > 0.0; t -= step) {
        bool solid = true;
        for (int back = 0; back <= 4 && solid; back++) {
            const double tb = t - back * r.cell_m;
            solid = land_at(r, tb * dock.out_x, tb * dock.out_z);
        }
        if (solid) {
            t_rim = t;
            break;
        }
    }
    dock.x = t_rim * dock.out_x;
    dock.z = t_rim * dock.out_z;

    // 泊位上的船平行于岸：船身两侧各留 16 m（够 5 节的船），看这一段岸线最远伸出多少
    const double lat_x = -dock.out_z, lat_z = dock.out_x;
    double reach = 0.0;
    for (double s = -BERTH_HALF_LENGTH; s <= BERTH_HALF_LENGTH; s += step) {
        for (double t = 0.0; t <= 60.0; t += step) {
            if (land_at(r, dock.x + t * dock.out_x + s * lat_x, dock.z + t * dock.out_z + s * lat_z)) {
                reach = std::max(reach, t);
            }
        }
    }
    dock.pier_m = std::max(p.pier_min_m, reach + 3.0);

    // 停机坪高度 = 岸缘点周围半个半径内的平均地面
    const int n = r.n;
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (r.mask[r.index(i, j)] && std::hypot(r.coord(j) - dock.x, r.coord(i) - dock.z) <= 0.5 * p.dock_apron_m) {
                sum += h[r.index(i, j)];
                count++;
            }
        }
    }
    dock.y = count > 0 ? sum / count : p.rim_m;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const size_t k = r.index(i, j);
            if (!r.mask[k]) {
                continue;
            }
            const double d = std::hypot(r.coord(j) - dock.x, r.coord(i) - dock.z) / p.dock_apron_m;
            // 半径以内一半全平，外一半渐回原地形
            const double w = smoothstep(clamp01(2.0 * (1.0 - d)));
            h[k] += w * (dock.y - h[k]);
        }
    }
}

// 船坞坪：停机坪旁沿岸的一块矩形，与停机坪同高。岸线不直，先在停机坪两侧各试一遍，
// 找矩形整个落在陆地上的最小进深（取更浅的那一侧），再压平；坪外按固定坡度挖方 / 填方接回原地形。
void build_yard(const IslandParams &p, IslandField &r, std::vector<double> &h) {
    IslandYard &yard = r.yard;
    yard.present = false;
    if (!p.yard) {
        return;
    }
    const IslandDock &dock = r.dock;
    const double lat_x = -dock.out_z, lat_z = dock.out_x;
    const double L = p.yard_length_m, W = p.yard_width_m;

    int best_side = 0;
    double best_inland = 1e18;
    for (int side = 1; side >= -1; side -= 2) {
        for (double inland = 1.0; inland <= p.yard_max_inland_m; inland += 1.0) {
            bool solid = true;
            for (double u = YARD_GAP - YARD_LAND_MARGIN; u <= YARD_GAP + L + YARD_LAND_MARGIN && solid; u += 1.0) {
                for (double v = inland - YARD_LAND_MARGIN; v <= inland + W + YARD_LAND_MARGIN && solid; v += 1.0) {
                    solid = land_at(r, dock.x + side * u * lat_x - v * dock.out_x, dock.z + side * u * lat_z - v * dock.out_z);
                }
            }
            if (solid) {
                if (inland < best_inland) {
                    best_inland = inland;
                    best_side = side;
                }
                break;
            }
        }
    }
    if (best_side == 0) {
        return;
    }
    yard.present = true;
    yard.ux = best_side * lat_x;
    yard.uz = best_side * lat_z;
    yard.vx = -dock.out_x;
    yard.vz = -dock.out_z;
    yard.length = L;
    yard.width = W;
    yard.y = dock.y;
    yard.x = dock.x + (YARD_GAP + 0.5 * L) * yard.ux + (best_inland + 0.5 * W) * yard.vx;
    yard.z = dock.z + (YARD_GAP + 0.5 * L) * yard.uz + (best_inland + 0.5 * W) * yard.vz;

    const int n = r.n;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const size_t k = r.index(i, j);
            if (!r.mask[k]) {
                continue;
            }
            const double beyond = yard.outside_distance(r.coord(j), r.coord(i));
            h[k] = beyond <= 0.0 ? yard.y : std::clamp(h[k], yard.y - YARD_FILL_SLOPE * beyond, yard.y + YARD_CUT_SLOPE * beyond);
        }
    }
}

// 岛底：岸缘下一道崖，往腹地收成倒锥。
void build_bottom(const IslandParams &p, IslandField &r) {
    Rng rng = Rng(p.seed).fork(STREAM_BOTTOM);
    const double R = std::sqrt(p.area_m2 / PI);
    const double ext = r.half_extent();
    FractalNoise rough(rng, -ext, -ext, ext, ext, std::max(0.3 * R, 3.0 * p.cell_m), 4, 0.55);
    FractalNoise lumps(rng, -ext, -ext, ext, ext, std::max(0.5 * R, 3.0 * p.cell_m), 3, 0.5);
    const int n = r.n;
    r.bottom.assign(size_t(n) * n, float(p.rim_m));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const size_t k = r.index(i, j);
            if (!r.mask[k]) {
                continue;
            }
            const double x = r.coord(j), z = r.coord(i);
            const double cliff = p.cliff_m * (0.75 + 0.25 * rough.sample(x, z));
            const double keel = p.keel_m * std::pow(double(r.inside[k]), 0.8) * (0.8 + 0.2 * lumps.sample(x, z));
            r.bottom[k] = float(p.rim_m - cliff - keel);
        }
    }
}

} // namespace

IslandAge island_age_class(double p_age) {
    return p_age < AGE_YOUNG ? IslandAge::YOUNG : (p_age < AGE_OLD ? IslandAge::MID : IslandAge::OLD);
}

double IslandYard::outside_distance(double p_x, double p_z) const {
    double u = 0.0, v = 0.0;
    to_local(p_x, p_z, u, v);
    const double du = std::max(0.0, std::abs(u) - 0.5 * length);
    const double dv = std::max(0.0, std::abs(v) - 0.5 * width);
    return std::hypot(du, dv);
}

int IslandField::land_count() const {
    return int(std::accumulate(mask.begin(), mask.end(), size_t(0)));
}

bool IslandField::sample_height(double p_x, double p_z, double &r_y) const {
    const double ext = half_extent();
    const double fx = (p_x + ext) / cell_m, fz = (p_z + ext) / cell_m;
    const int j = int(std::floor(fx)), i = int(std::floor(fz));
    if (!quad_solid(i, j)) {
        return false;
    }
    const double tx = fx - j, tz = fz - i;
    r_y = height[index(i, j)] * (1 - tz) * (1 - tx) + height[index(i, j + 1)] * (1 - tz) * tx +
            height[index(i + 1, j)] * tz * (1 - tx) + height[index(i + 1, j + 1)] * tz * tx;
    return true;
}

IslandField generate_island_field(const IslandParams &p_params) {
    IslandField r;
    r.rim_m = p_params.rim_m;
    r.peak_m = p_params.peak_m;
    r.age_class = island_age_class(p_params.age);

    build_shape(p_params, r);
    std::vector<double> shape = build_base_form(p_params, r);

    // 归一到 岸缘 rim → 峰 peak
    double lo = 1e18, hi = -1e18;
    for (size_t k = 0; k < shape.size(); k++) {
        if (r.mask[k]) {
            lo = std::min(lo, shape[k]);
            hi = std::max(hi, shape[k]);
        }
    }
    std::vector<double> h(shape.size(), p_params.rim_m);
    for (size_t k = 0; k < shape.size(); k++) {
        if (r.mask[k]) {
            h[k] = p_params.rim_m + (p_params.peak_m - p_params.rim_m) * (shape[k] - lo) / std::max(1e-9, hi - lo);
        }
    }

    relax(r, h, RELAX_ROUNDS);

    // 岸缘不向云海收敛：只夹到 rim 以上；再归一，让最高节点恰为 peak
    double h_max = -1e18;
    for (size_t k = 0; k < h.size(); k++) {
        if (r.mask[k]) {
            h[k] = std::max(h[k], p_params.rim_m);
            h_max = std::max(h_max, h[k]);
        }
    }
    if (h_max > p_params.rim_m + 1e-6) {
        const double gain = (p_params.peak_m - p_params.rim_m) / (h_max - p_params.rim_m);
        for (size_t k = 0; k < h.size(); k++) {
            if (r.mask[k]) {
                h[k] = p_params.rim_m + (h[k] - p_params.rim_m) * gain;
            }
        }
    }

    build_dock(p_params, r, h);
    build_yard(p_params, r, h); // 在码头之后：坪与停机坪同高、挨着它
    if (p_params.river) {
        carve_island_river(p_params, r, h); // 在码头之后：河道要绕开停机坪
        if (p_params.trail) {
            carve_dock_trail(p_params, r, h);
        }
    }
    build_bottom(p_params, r);

    r.height.assign(h.size(), float(p_params.rim_m));
    for (size_t k = 0; k < h.size(); k++) {
        if (r.mask[k]) {
            r.height[k] = float(h[k]);
        }
    }
    return r;
}
