#include "world_river.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <sstream>

#include "world_terrain.h" // hash_fbm

namespace {

constexpr uint64_t SEED_MEANDER = 0x4D45414E44ull;
constexpr uint64_t SEED_WIDTH = 0x5749445448ull;
constexpr uint64_t SEED_WANDER = 0x57414E44ull;
constexpr int CHAIKIN_ROUNDS = 3;
constexpr int RELAX_ROUNDS = 40; // 8 m 点距下，拐角约摊开 ±50 m
constexpr double MEANDER_TAPER_M = 60.0; // 曲流在两头这么长一段内收到零
constexpr double JOIN_BLEND_M = 80.0; // 支流末段这么长一段挪到干流中线上
constexpr double SECONDS_PER_DAY = 86400.0;

// 快、慢两个线性水库（每日）：降水按份额进库，出流 = 库存 × 系数
constexpr double QUICK_SHARE = 0.6, QUICK_RATE = 0.35;
constexpr double SLOW_SHARE = 0.4, SLOW_RATE = 0.03;
constexpr int SPIN_UP_YEARS = 3;

double smoothstep(double p_t) {
    const double t = std::clamp(p_t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

int64_t bucket_key(int p_i, int p_j) {
    return (int64_t(p_i) << 32) ^ int64_t(uint32_t(p_j));
}

struct RawPoint {
    double x, z, acc;
    int64_t cell = -1; // 来自哪一格（抽稀磨圆后按最近的原格补上；河宽水深从这格取）
};

// 端点不动的 Chaikin 磨角
std::vector<RawPoint> chaikin(const std::vector<RawPoint> &p_in) {
    if (p_in.size() < 3) {
        return p_in;
    }
    std::vector<RawPoint> out;
    out.reserve(p_in.size() * 2);
    out.push_back(p_in.front());
    const size_t n = p_in.size();
    for (size_t i = 0; i + 1 < n; i++) {
        const RawPoint &a = p_in[i], &b = p_in[i + 1];
        auto mix = [&](double t) { return RawPoint{ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.acc + (b.acc - a.acc) * t }; };
        if (i > 0) {
            out.push_back(mix(0.25));
        }
        if (i + 2 < n) {
            out.push_back(mix(0.75));
        }
    }
    out.push_back(p_in.back());
    return out;
}

// Douglas–Peucker：离原折线不超过 p_tolerance 的前提下丢点（端点保留）。D8 的格心连线横平竖直、拐角是 45°/90°，
// 先抽稀再磨圆，弯才是大半径的
std::vector<RawPoint> simplify(const std::vector<RawPoint> &p_in, double p_tolerance) {
    if (p_in.size() < 3) {
        return p_in;
    }
    std::vector<char> keep(p_in.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<size_t, size_t>> stack{ { 0, p_in.size() - 1 } };
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        double worst = -1.0;
        size_t at = a;
        for (size_t i = a + 1; i < b; i++) {
            const double dx = p_in[b].x - p_in[a].x, dz = p_in[b].z - p_in[a].z;
            const double len2 = dx * dx + dz * dz;
            const double t = len2 > 0.0 ? std::clamp(((p_in[i].x - p_in[a].x) * dx + (p_in[i].z - p_in[a].z) * dz) / len2, 0.0, 1.0) : 0.0;
            const double d = std::hypot(p_in[i].x - (p_in[a].x + t * dx), p_in[i].z - (p_in[a].z + t * dz));
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst > p_tolerance) {
            keep[at] = 1;
            stack.push_back({ a, at });
            stack.push_back({ at, b });
        }
    }
    std::vector<RawPoint> out;
    for (size_t i = 0; i < p_in.size(); i++) {
        if (keep[i]) {
            out.push_back(p_in[i]);
        }
    }
    return out;
}

// 按弧长等距重采样（末点保留）
std::vector<RawPoint> resample(const std::vector<RawPoint> &p_in, double p_step) {
    std::vector<RawPoint> out;
    if (p_in.size() < 2) {
        return p_in;
    }
    std::vector<double> cum(p_in.size(), 0.0);
    for (size_t i = 1; i < p_in.size(); i++) {
        cum[i] = cum[i - 1] + std::hypot(p_in[i].x - p_in[i - 1].x, p_in[i].z - p_in[i - 1].z);
    }
    const double total = cum.back();
    const int n = std::max(1, int(std::ceil(total / p_step)));
    size_t seg = 0;
    for (int k = 0; k <= n; k++) {
        const double s = total * k / n;
        while (seg + 2 < p_in.size() && cum[seg + 1] < s) {
            seg++;
        }
        const double len = cum[seg + 1] - cum[seg];
        const double t = len > 1e-9 ? std::clamp((s - cum[seg]) / len, 0.0, 1.0) : 0.0;
        const RawPoint &a = p_in[seg], &b = p_in[seg + 1];
        out.push_back({ a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.acc + (b.acc - a.acc) * t });
    }
    return out;
}

void closest_on_segment(double p_px, double p_pz, double p_ax, double p_az, double p_bx, double p_bz, double &r_t, double &r_d, double &r_side) {
    const double dx = p_bx - p_ax, dz = p_bz - p_az;
    const double len2 = dx * dx + dz * dz;
    r_t = len2 > 0.0 ? std::clamp(((p_px - p_ax) * dx + (p_pz - p_az) * dz) / len2, 0.0, 1.0) : 0.0;
    r_d = std::hypot(p_px - (p_ax + r_t * dx), p_pz - (p_az + r_t * dz));
    // 顺水看左正：x 东、z 南，(dx, dz) × (p − a) 的 y 分量取负
    r_side = (dz * (p_px - p_ax) - dx * (p_pz - p_az)) >= 0.0 ? 1.0 : -1.0;
}

RiverVertex lerp_vertex(const RiverVertex &a, const RiverVertex &b, double t) {
    RiverVertex v;
    v.x = a.x + (b.x - a.x) * t;
    v.z = a.z + (b.z - a.z) * t;
    v.s = a.s + (b.s - a.s) * t;
    v.surface = a.surface + (b.surface - a.surface) * t;
    v.source_surface = a.source_surface + (b.source_surface - a.source_surface) * t;
    v.width = a.width + (b.width - a.width) * t;
    v.depth = a.depth + (b.depth - a.depth) * t;
    v.floodplain = a.floodplain + (b.floodplain - a.floodplain) * t;
    v.acc = a.acc + (b.acc - a.acc) * t;
    v.dry = a.dry + (b.dry - a.dry) * t;
    v.mean_flow = a.mean_flow + (b.mean_flow - a.mean_flow) * t;
    v.bankfull_flow = a.bankfull_flow + (b.bankfull_flow - a.bankfull_flow) * t;
    v.authoritative = a.authoritative;
    v.slope=a.slope+(b.slope-a.slope)*t;
    v.d50_mm=a.d50_mm+(b.d50_mm-a.d50_mm)*t;
    v.ssc_mg_l=a.ssc_mg_l+(b.ssc_mg_l-a.ssc_mg_l)*t;
    v.planform=t<0.5 ? a.planform : b.planform;
    v.level=t<0.5 ? a.level : b.level;
    v.bed_class=t<0.5 ? a.bed_class : b.bed_class;
    v.confine=t<0.5 ? a.confine : b.confine;
    v.floodplain_left=a.floodplain_left+(b.floodplain_left-a.floodplain_left)*t;
    v.floodplain_right=a.floodplain_right+(b.floodplain_right-a.floodplain_right)*t;
    v.basin = a.basin;
    return v;
}

// 折线上离 (x, z) 最近的点
RiverVertex closest_on_stem(const RiverStem &p_stem, double p_x, double p_z) {
    double best = 1e18;
    RiverVertex out = p_stem.v.front();
    for (size_t i = 0; i + 1 < p_stem.v.size(); i++) {
        double t, d, side;
        closest_on_segment(p_x, p_z, p_stem.v[i].x, p_stem.v[i].z, p_stem.v[i + 1].x, p_stem.v[i + 1].z, t, d, side);
        if (d < best) {
            best = d;
            out = lerp_vertex(p_stem.v[i], p_stem.v[i + 1], t);
        }
    }
    return out;
}

} // namespace

double RiverRules::mean_discharge(double p_acc_km2) const {
    return p_acc_km2 * 1e6 * precip_mm * 1e-3 * runoff / (year_days * SECONDS_PER_DAY);
}

double RiverRules::dry_threshold(double p_acc_km2) const {
    return std::clamp(dry_k * std::pow(std::max(p_acc_km2, 1e-3), -dry_exp), dry_min, dry_max);
}

double river_profile(const RiverVertex &p_at, double p_d, const RiverRules &p_rules) {
    const double b = std::max(1e-3, p_at.half());
    const double flat = p_rules.bed_flat * b;
    const double d = std::abs(p_d);
    if (p_at.source_data() && d<=b) return p_at.bed()+p_at.depth*std::pow(d/b,0.4/0.26);
    if (d <= flat) {
        return p_at.bed();
    }
    if (d <= b) {
        const double u = (d - flat) / std::max(1e-6, b - flat);
        return p_at.bed() + p_at.depth * u * u;
    }
    return p_at.surface + p_rules.floodplain_slope * (std::min(d, p_at.core_at(p_d)) - b);
}

RiverStage river_stage(const RiverVertex &p_at, double p_runoff, const RiverRules &p_rules) {
    RiverStage st;
    const double dry = std::clamp(p_at.dry, 0.0, 0.999);
    st.flow = p_at.source_data()
            ? (p_at.bankfull_flow > 0.0 ? std::max(0.0, p_runoff) * p_at.mean_flow / p_at.bankfull_flow : 0.0)
            : std::min(std::max(0.0, p_runoff - dry) / ((1.0 - dry) * p_rules.bankfull_ratio), p_rules.flood_max);
    st.depth = p_at.depth * std::pow(st.flow, p_rules.stage_exp);
    st.level = p_at.bed() + st.depth;
    const double b = p_at.half();
    // 岸坡是 u²：水深 d 时水边在 flat + (b − flat)·√(d / 平岸深)
    const double flat = p_rules.bed_flat * b;
    st.half_width = st.depth <= 0.0          ? 0.0
            : st.depth <= p_at.depth ? flat + (b - flat) * std::sqrt(st.depth / std::max(1e-6, p_at.depth))
                                     : std::min(b + (st.depth - p_at.depth) / p_rules.floodplain_slope, p_at.core());
    if (p_at.source_data() && st.flow<=1) st.half_width=b*std::pow(st.flow,0.26);
    st.half_left = std::min(st.half_width, p_at.core_at(1));
    st.half_right = std::min(st.half_width, p_at.core_at(-1));
    st.discharge = st.flow * (p_at.source_data() ? p_at.bankfull_flow : p_rules.mean_discharge(p_at.acc) * p_rules.bankfull_ratio);
    const double area = (st.half_left + st.half_right) * st.depth;
    st.speed = area > 1e-12 ? st.discharge / area : 0.0;
    st.wet = st.depth > p_rules.wet_depth_m;
    return st;
}

std::shared_ptr<const WorldRivers> WorldRivers::build(const WorldGrid &g, const RiverRules &p_rules, uint64_t p_seed, const ShoreFn &p_shore) {
    auto net = std::make_shared<WorldRivers>();
    net->rules = p_rules;
    if (g.authoritative_rivers) {
        net->stems.resize(g.river_sources.size());
        for (size_t si = 0; si < g.river_sources.size(); si++) {
            const auto &src = g.river_sources[si];
            auto &stem = net->stems[si];
            stem.island = src.island; stem.parent = src.down; stem.mouth = src.exit == 1;
            stem.outlet_drop = src.outlet_drop;
            std::vector<RiverVertex> raw;
            for (const auto &p : src.points) {
                RiverVertex v;
                v.authoritative = true;
                v.x = g.x0 + double(p[1]) * g.res;
                v.z = g.z0 + double(p[0]) * g.res;
                v.acc = p[3]; v.mean_flow = p[4]; v.bankfull_flow = p[5];
                v.width = p[6]; v.depth = p[7];
                v.surface = double(p[11]) + v.depth;
                v.source_surface = p[10];
                v.level = int(p[2]); v.bed_class = int(p[14]);
                v.slope=p[12]; v.d50_mm=p[13]; v.planform=int(p[15]);
                v.ssc_mg_l=p[16]; v.confine=int(p[19]);
                v.floodplain_left=std::max(0.0,double(p[17]));
                v.floodplain_right=std::max(0.0,double(p[18]));
                v.floodplain = std::max(v.floodplain_left,v.floodplain_right);
                v.basin = src.basin >= 0 ? src.basin : int(g.basin_runoff.size()) - 1;
                raw.push_back(v);
            }
            // join is a point index in the downstream segment, not in this segment.
            // The generator omits that shared cell from the tributary's own points.
            if (!raw.empty() && src.down >= 0 && src.join >= 0) {
                const auto &down = g.river_sources[size_t(src.down)];
                if (size_t(src.join) < down.points.size()) {
                    const auto &p = down.points[size_t(src.join)];
                    auto join = raw.back();
                    join.x = g.x0 + double(p[1]) * g.res;
                    join.z = g.z0 + double(p[0]) * g.res;
                    join.surface = double(p[11]) + join.depth;
                    join.source_surface = p[10];
                    raw.push_back(join);
                }
            }
            double arc = 0;
            for (size_t i = 0; i + 1 < raw.size(); i++) {
                const double length = std::hypot(raw[i+1].x - raw[i].x, raw[i+1].z - raw[i].z);
                const int steps = std::max(1, int(std::ceil(length / p_rules.step_m)));
                for (int j = 0; j < steps; j++) {
                    auto v = lerp_vertex(raw[i], raw[i+1], double(j) / steps);
                    v.s = arc + length * j / steps;
                    stem.v.push_back(v);
                }
                arc += length;
            }
            if (!raw.empty()) { raw.back().s = arc; stem.v.push_back(raw.back()); }
            // Extend the exported outlet cell to the subcell coastline, keeping the upstream path fixed.
            if (stem.mouth && stem.v.size() >= 2) {
                auto a = stem.v[stem.v.size()-2], end = stem.v.back();
                const double len = std::hypot(end.x-a.x, end.z-a.z);
                if (len > 0 && p_shore(end.x,end.z) > 0) {
                    double best = 2.0 * g.res, bx = 0, bz = 0;
                    // An outlet's D8 tangent can follow the coast. Find the nearest
                    // zero crossing instead of leaving its waterfall inland.
                    for (int k=0;k<32;k++) {
                        const double angle=6.283185307179586*k/32.0;
                        const double dx=std::cos(angle), dz=std::sin(angle);
                        double lo=0, hi=0;
                        for (double d=g.res/8;d<=best;d+=g.res/8) {
                            if (p_shore(end.x+dx*d,end.z+dz*d)<=0) { hi=d; break; }
                            lo=d;
                        }
                        if (hi==0) continue;
                        for (int n=0;n<24;n++) {
                            const double m=(lo+hi)*0.5;
                            (p_shore(end.x+dx*m,end.z+dz*m)>0 ? lo : hi)=m;
                        }
                        if (hi<best) { best=hi; bx=dx; bz=dz; }
                    }
                    if (bx!=0 || bz!=0) {
                        const auto start=end;
                        end.x+=bx*best; end.z+=bz*best; end.s+=best;
                        const int steps=std::max(1,int(std::ceil(best/p_rules.step_m)));
                        for (int j=1;j<=steps;j++) stem.v.push_back(lerp_vertex(start,end,double(j)/steps));
                    } else stem.mouth=false;
                }
            }
        }
        net->index_segments();
        return net;
    }
    if (!g.has_hydro()) {
        return net;
    }
    const RiverRules &rules = p_rules;
    const size_t n = size_t(g.rows) * size_t(g.cols);
    auto channel = [&](size_t k) { return g.flow_acc[k] >= rules.min_acc_km2 && g.shore[k] > 0.0f; };
    auto receiver = [&](size_t k) -> int64_t {
        const uint8_t code = g.flow[k];
        const int dir = code & 0x0F;
        if (dir < 1 || dir > 8 || (code & WorldGrid::FLOW_TO_VOID)) {
            return -1;
        }
        const int r = int(k / size_t(g.cols)) + WorldGrid::FLOW_OFFSETS[dir - 1][0];
        const int c = int(k % size_t(g.cols)) + WorldGrid::FLOW_OFFSETS[dir - 1][1];
        if (r < 0 || c < 0 || r >= g.rows || c >= g.cols) {
            return -1;
        }
        return int64_t(g.index(r, c));
    };
    // 上游表：链表（head[格] → 第一个上游格，next[格] → 同一下游的下一个上游格）
    std::vector<int32_t> head(n, -1), next(n, -1);
    std::vector<size_t> outlets;
    for (size_t k = 0; k < n; k++) {
        if (!channel(k)) {
            continue;
        }
        const int64_t r = receiver(k);
        if (r >= 0 && channel(size_t(r))) {
            next[k] = head[size_t(r)];
            head[size_t(r)] = int32_t(k);
        } else {
            outlets.push_back(k);
        }
    }
    // 大河先走：出口按汇流面积从大到小
    std::sort(outlets.begin(), outlets.end(), [&](size_t a, size_t b) { return g.flow_acc[a] > g.flow_acc[b] || (g.flow_acc[a] == g.flow_acc[b] && a < b); });

    auto center = [&](size_t k, double &x, double &z) {
        x = g.x0 + double(k % size_t(g.cols)) * g.res;
        z = g.z0 + double(k / size_t(g.cols)) * g.res;
    };

    struct Pending {
        size_t start;
        int parent; // 河号
        int64_t junction; // 汇入干流的那一格（出口为 −1）
    };
    std::deque<Pending> queue;
    for (size_t k : outlets) {
        queue.push_back({ k, -1, -1 });
    }
    while (!queue.empty()) {
        const Pending job = queue.front();
        queue.pop_front();
        // 逆流而上：每格挑汇流最大的上游格，其余排成支流
        std::vector<size_t> cells;
        size_t k = job.start;
        const int stem_id = int(net->stems.size());
        for (;;) {
            cells.push_back(k);
            int32_t best = -1;
            for (int32_t d = head[k]; d >= 0; d = next[size_t(d)]) {
                if (best < 0 || g.flow_acc[size_t(d)] > g.flow_acc[size_t(best)]) {
                    best = d;
                }
            }
            for (int32_t d = head[k]; d >= 0; d = next[size_t(d)]) {
                if (d != best) {
                    queue.push_back({ size_t(d), stem_id, int64_t(k) });
                }
            }
            if (best < 0) {
                break;
            }
            k = size_t(best);
        }
        std::reverse(cells.begin(), cells.end()); // 上游 → 下游

        std::vector<RawPoint> raw;
        for (size_t c : cells) {
            double x, z;
            center(c, x, z);
            raw.push_back({ x, z, double(g.flow_acc[c]), int64_t(c) });
        }
        const size_t last = cells.back();
        const uint8_t code = g.flow[last];
        bool to_void = false;
        if (job.junction >= 0) {
            double x, z;
            center(size_t(job.junction), x, z);
            raw.push_back({ x, z, raw.back().acc, raw.back().cell });
        } else if ((code & WorldGrid::FLOW_TO_VOID) && (code & 0x0F) >= 1 && (code & 0x0F) <= 8) {
            const int *off = WorldGrid::FLOW_OFFSETS[(code & 0x0F) - 1];
            double x, z;
            center(last, x, z);
            raw.push_back({ x + off[1] * g.res, z + off[0] * g.res, raw.back().acc, raw.back().cell });
            to_void = true;
        }
        if (raw.size() < 2) {
            // 孤零零一格、又没有去处（洼地）：画不成河
            net->stems.push_back(RiverStem()); // 占住河号，支流的 parent 仍然对得上
            continue;
        }
        const std::vector<RawPoint> cells_raw = raw;
        raw = simplify(raw, rules.simplify_m);
        for (int round = 0; round < CHAIKIN_ROUNDS; round++) {
            raw = chaikin(raw);
        }
        std::vector<RawPoint> pts = resample(raw, rules.step_m);
        // 松弛：(前 + 2·本 + 后) / 4 反复几轮（两端不动），拐角摊成大半径的弯
        for (int round = 0; round < RELAX_ROUNDS && pts.size() > 2; round++) {
            std::vector<RawPoint> prev = pts;
            for (size_t i = 1; i + 1 < pts.size(); i++) {
                pts[i].x = 0.25 * (prev[i - 1].x + 2.0 * prev[i].x + prev[i + 1].x);
                pts[i].z = 0.25 * (prev[i - 1].z + 2.0 * prev[i].z + prev[i + 1].z);
            }
        }
        // 汇流面积取最近的原格（抽稀后线性插出来的会把汇流口的跳变抹平）
        for (size_t i = 0, j = 0; i < pts.size(); i++) {
            auto dist = [&](size_t c) { return std::hypot(pts[i].x - cells_raw[c].x, pts[i].z - cells_raw[c].z); };
            while (j + 1 < cells_raw.size() && dist(j + 1) <= dist(j)) {
                j++;
            }
            pts[i].acc = cells_raw[j].acc;
            pts[i].cell = cells_raw[j].cell;
        }

        RiverStem stem;
        stem.parent = job.parent;
        stem.island = g.island[cells.front()];
        const uint64_t stem_seed = p_seed ^ (uint64_t(stem_id) * 0x9E3779B97F4A7C15ull);
        // 弧长与每点的基本宽（曲流摆幅、波长按它定）
        std::vector<double> s(pts.size(), 0.0);
        for (size_t i = 1; i < pts.size(); i++) {
            s[i] = s[i - 1] + std::hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z);
        }
        const double length = s.back();
        auto base_width = [&](double acc) {
            return std::max(rules.min_width_m, rules.width_k * std::pow(rules.mean_discharge(acc) * rules.bankfull_ratio, rules.width_exp));
        };
        // 曲流：沿法线摆，两头收到零
        std::vector<RawPoint> bent = pts;
        for (size_t i = 0; i < pts.size(); i++) {
            const size_t a = i > 0 ? i - 1 : i, b = i + 1 < pts.size() ? i + 1 : i;
            double tx = pts[b].x - pts[a].x, tz = pts[b].z - pts[a].z;
            const double tl = std::hypot(tx, tz);
            if (tl < 1e-9) {
                continue;
            }
            tx /= tl;
            tz /= tl;
            const double w = base_width(pts[i].acc);
            const double amp = std::clamp(rules.meander_k * w, rules.meander_min_m, rules.meander_max_m);
            const double wave = std::clamp(rules.meander_wave_k * w, 60.0, 300.0);
            const double taper = smoothstep(s[i] / MEANDER_TAPER_M) * smoothstep((length - s[i]) / MEANDER_TAPER_M);
            const double off = taper * (amp * hash_fbm(stem_seed ^ SEED_MEANDER, s[i], 0.0, wave, 3) +
                                               rules.wander_m * hash_fbm(stem_seed ^ SEED_WANDER, s[i], 0.0, rules.wander_wave_m, 2));
            bent[i].x += -tz * off;
            bent[i].z += tx * off;
        }
        pts = bent;

        // 支流：末段挪到干流的中线上
        RiverVertex join;
        const bool joined = stem.parent >= 0 && !net->stems[size_t(stem.parent)].v.empty();
        if (joined) {
            join = closest_on_stem(net->stems[size_t(stem.parent)], pts.back().x, pts.back().z);
            const double dx = join.x - pts.back().x, dz = join.z - pts.back().z;
            for (size_t i = 0; i < pts.size(); i++) {
                const double w = smoothstep((s[i] - (length - JOIN_BLEND_M)) / JOIN_BLEND_M);
                pts[i].x += dx * w;
                pts[i].z += dz * w;
            }
        }
        // 河口：截在岸线上
        if (to_void) {
            size_t cut = pts.size();
            for (size_t i = 0; i < pts.size(); i++) {
                if (p_shore(pts[i].x, pts[i].z) <= 0.0) {
                    cut = i;
                    break;
                }
            }
            if (cut == 0) {
                net->stems.push_back(RiverStem());
                continue;
            }
            if (cut < pts.size()) {
                RawPoint lo = pts[cut - 1], hi = pts[cut];
                for (int it = 0; it < 20; it++) {
                    const RawPoint mid{ 0.5 * (lo.x + hi.x), 0.5 * (lo.z + hi.z), 0.5 * (lo.acc + hi.acc) };
                    (p_shore(mid.x, mid.z) > 0.0 ? lo : hi) = mid;
                }
                lo.cell = pts[cut - 1].cell;
                pts.resize(cut);
                if (std::hypot(lo.x - pts.back().x, lo.z - pts.back().z) > 0.5) {
                    pts.push_back(lo);
                }
                stem.mouth = true;
            }
            if (pts.size() < 2) {
                net->stems.push_back(RiverStem());
                continue;
            }
        }

        // 每点的尺度
        stem.v.resize(pts.size());
        double arc = 0.0;
        for (size_t i = 0; i < pts.size(); i++) {
            RiverVertex &v = stem.v[i];
            if (i > 0) {
                arc += std::hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z);
            }
            v.x = pts[i].x;
            v.z = pts[i].z;
            v.s = arc;
            v.acc = pts[i].acc;
            const double q = rules.mean_discharge(v.acc) * rules.bankfull_ratio;
            const double ramp = smoothstep(arc / rules.head_ramp_m);
            const double jitter = 1.0 + rules.width_jitter * hash_fbm(stem_seed ^ SEED_WIDTH, arc, 0.0, 90.0, 2);
            v.width = base_width(v.acc) * jitter * (0.5 + 0.5 * ramp);
            v.depth = std::max(rules.min_depth_m, rules.depth_k * std::pow(q, rules.depth_exp)) * (0.35 + 0.65 * ramp);
            if (g.has_channels() && pts[i].cell >= 0) {
                // 生成器的河道（栅格 v3）：宽深取大——生成器夸张过的河宽更贴它自己下切出的河谷，游戏的公式兜住它没算宽的小溪
                const size_t c = size_t(pts[i].cell);
                v.width = std::max(v.width, double(g.river_width[c]) * jitter * (0.5 + 0.5 * ramp));
                v.depth = std::max(v.depth, double(g.river_depth[c]) * (0.35 + 0.65 * ramp));
            }
            v.floodplain = std::clamp(rules.floodplain_k * v.width, rules.floodplain_min_m, rules.floodplain_max_m);
            v.dry = rules.dry_threshold(v.acc);
        }
        // 平岸水面：从下游往上游，不低于下游一点加最小落差
        const size_t m = stem.v.size();
        for (size_t i = 0; i < m; i++) {
            stem.v[i].surface = g.height_at(stem.v[i].x, stem.v[i].z) - rules.entrench_m;
            if (g.has_channels() && pts[i].cell >= 0 && g.river_width[size_t(pts[i].cell)] > 0.0f) {
                // 生成器的河道格高程就是下切后的河床：中线摆离格心时双三次会爬上沟坡，水面不高于生成器的 河床 + 水深
                const size_t c = size_t(pts[i].cell);
                stem.v[i].surface = std::min(stem.v[i].surface, double(g.height[c]) + double(g.river_depth[c]));
            }
        }
        if (joined) {
            stem.v[m - 1].surface = join.surface;
        }
        for (size_t i = m - 1; i-- > 0;) {
            stem.v[i].surface = std::max(stem.v[i].surface, stem.v[i + 1].surface + rules.min_fall * (stem.v[i + 1].s - stem.v[i].s));
        }
        net->stems.push_back(std::move(stem));
    }
    net->index_segments();
    return net;
}

void WorldRivers::index_segments() {
    seg_stem.clear();
    seg_index.clear();
    buckets.clear();
    reach_max = 0.0;
    for (size_t si = 0; si < stems.size(); si++) {
        const RiverStem &stem = stems[si];
        for (size_t i = 0; i + 1 < stem.v.size(); i++) {
            const RiverVertex &a = stem.v[i], &b = stem.v[i + 1];
            const double reach = std::max(a.core(), b.core()) + rules.blend_m;
            reach_max = std::max(reach_max, reach);
            const uint32_t id = uint32_t(seg_stem.size());
            seg_stem.push_back(uint32_t(si));
            seg_index.push_back(uint32_t(i));
            const int i0 = int(std::floor((std::min(a.z, b.z) - reach) / BUCKET_M)), i1 = int(std::floor((std::max(a.z, b.z) + reach) / BUCKET_M));
            const int j0 = int(std::floor((std::min(a.x, b.x) - reach) / BUCKET_M)), j1 = int(std::floor((std::max(a.x, b.x) + reach) / BUCKET_M));
            for (int bi = i0; bi <= i1; bi++) {
                for (int bj = j0; bj <= j1; bj++) {
                    buckets[bucket_key(bi, bj)].push_back(id);
                }
            }
        }
    }
}

RiverHit WorldRivers::measure(uint32_t p_segment, double p_x, double p_z) const {
    RiverHit hit;
    hit.stem = int(seg_stem[p_segment]);
    hit.segment = int(seg_index[p_segment]);
    const RiverStem &stem = stems[size_t(hit.stem)];
    const RiverVertex &a = stem.v[size_t(hit.segment)], &b = stem.v[size_t(hit.segment) + 1];
    closest_on_segment(p_x, p_z, a.x, a.z, b.x, b.z, hit.t, hit.distance, hit.side);
    hit.at = lerp_vertex(a, b, hit.t);
    return hit;
}

int WorldRivers::near(double p_x, double p_z, RiverHit *r_hits, int p_cap) const {
    if (buckets.empty()) {
        return 0;
    }
    const auto found = buckets.find(bucket_key(int(std::floor(p_z / BUCKET_M)), int(std::floor(p_x / BUCKET_M))));
    if (found == buckets.end()) {
        return 0;
    }
    int count = 0;
    for (uint32_t id : found->second) {
        const RiverHit hit = measure(id, p_x, p_z);
        if (hit.distance >= hit.at.core_at(hit.side) + rules.blend_m) {
            continue;
        }
        int slot = -1;
        for (int k = 0; k < count; k++) {
            if (r_hits[k].stem == hit.stem) {
                slot = k;
                break;
            }
        }
        if (slot >= 0) {
            if (hit.distance < r_hits[slot].distance) {
                r_hits[slot] = hit;
            }
        } else if (count < p_cap) {
            r_hits[count++] = hit;
        } else {
            // 满了：换掉最远的那条
            int far = 0;
            for (int k = 1; k < count; k++) {
                if (r_hits[k].distance > r_hits[far].distance) {
                    far = k;
                }
            }
            if (hit.distance < r_hits[far].distance) {
                r_hits[far] = hit;
            }
        }
    }
    return count;
}

bool WorldRivers::nearest(double p_x, double p_z, double p_max_distance, RiverHit &r_hit) const {
    RiverHit hits[MAX_HITS];
    const int n = near(p_x, p_z, hits, MAX_HITS);
    bool found = false;
    for (int k = 0; k < n; k++) {
        if (hits[k].distance <= p_max_distance && (!found || hits[k].distance < r_hit.distance)) {
            r_hit = hits[k];
            found = true;
        }
    }
    return found;
}

void WorldRivers::segments_in(double p_x0, double p_z0, double p_x1, double p_z1, const std::function<void(int, int)> &p_visit) const {
    const int i0 = int(std::floor(std::min(p_z0, p_z1) / BUCKET_M)), i1 = int(std::floor(std::max(p_z0, p_z1) / BUCKET_M));
    const int j0 = int(std::floor(std::min(p_x0, p_x1) / BUCKET_M)), j1 = int(std::floor(std::max(p_x0, p_x1) / BUCKET_M));
    std::vector<uint32_t> ids;
    for (int bi = i0; bi <= i1; bi++) {
        for (int bj = j0; bj <= j1; bj++) {
            const auto found = buckets.find(bucket_key(bi, bj));
            if (found != buckets.end()) {
                ids.insert(ids.end(), found->second.begin(), found->second.end());
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const double lx = std::min(p_x0, p_x1), hx = std::max(p_x0, p_x1), lz = std::min(p_z0, p_z1), hz = std::max(p_z0, p_z1);
    for (uint32_t id : ids) {
        const RiverStem &stem = stems[seg_stem[id]];
        const RiverVertex &a = stem.v[seg_index[id]], &b = stem.v[seg_index[id] + 1];
        if (std::max(a.x, b.x) < lx || std::min(a.x, b.x) > hx || std::max(a.z, b.z) < lz || std::min(a.z, b.z) > hz) {
            continue;
        }
        p_visit(int(seg_stem[id]), int(seg_index[id]));
    }
}

double WorldRivers::total_length() const {
    double total = 0.0;
    for (const RiverStem &stem : stems) {
        if (!stem.v.empty()) {
            total += stem.v.back().s;
        }
    }
    return total;
}

// ---------------------------------------------------------------- 水情

bool RiverRegime::parse(const std::string &p_text, RiverRegime &r_regime, std::string &r_error) {
    std::istringstream in(p_text);
    std::string line;
    std::vector<double> precip;
    double annual = 0.0, days = 0.0;
    int line_no = 0;
    while (std::getline(in, line)) {
        line_no++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            const size_t y = line.find("year_days"), p = line.find("precip_mm");
            if (y != std::string::npos) {
                days = std::atof(line.c_str() + y + 9);
            }
            if (p != std::string::npos) {
                annual = std::atof(line.c_str() + p + 9);
            }
            continue;
        }
        std::istringstream row(line);
        int day = -1;
        double mm = -1.0;
        if (!(row >> day >> mm) || day != int(precip.size()) || mm < 0.0) {
            r_error = "weather.tsv 第 " + std::to_string(line_no) + " 行读不了（应为 day<TAB>precip_mm…，日序从 0 连续）";
            return false;
        }
        precip.push_back(mm);
    }
    if (precip.empty()) {
        r_error = "weather.tsv 里没有一天";
        return false;
    }
    if (annual <= 0.0) {
        for (double mm : precip) {
            annual += mm;
        }
    }
    r_regime = from_precip(precip, annual);
    if (days > 0.0 && int(days) != int(precip.size())) {
        r_error = "weather.tsv 的 year_days 与行数对不上";
        return false;
    }
    return true;
}

RiverRegime RiverRegime::from_precip(const std::vector<double> &p_precip_mm, double p_annual_mm) {
    RiverRegime out;
    out.precip = p_annual_mm;
    out.days = double(p_precip_mm.size());
    if (p_precip_mm.empty()) {
        return steady();
    }
    double quick = 0.0, slow = 0.0;
    std::vector<double> flow(p_precip_mm.size(), 0.0);
    for (int year = 0; year <= SPIN_UP_YEARS; year++) {
        for (size_t d = 0; d < p_precip_mm.size(); d++) {
            quick += QUICK_SHARE * p_precip_mm[d];
            slow += SLOW_SHARE * p_precip_mm[d];
            const double q = QUICK_RATE * quick, s = SLOW_RATE * slow;
            quick -= q;
            slow -= s;
            flow[d] = q + s;
        }
    }
    double mean = 0.0;
    for (double q : flow) {
        mean += q;
    }
    mean /= double(flow.size());
    out.daily.resize(flow.size());
    for (size_t d = 0; d < flow.size(); d++) {
        out.daily[d] = mean > 0.0 ? flow[d] / mean : 1.0;
    }
    return out;
}

RiverRegime RiverRegime::steady(double p_runoff) {
    RiverRegime out;
    out.daily.assign(1, p_runoff);
    out.days = 1.0;
    return out;
}

double RiverRegime::runoff(double p_day) const {
    if (daily.empty()) {
        return 1.0;
    }
    const double n = double(daily.size());
    // 日值记在一天的正午：正午之间线性过渡
    double t = std::fmod(p_day - 0.5, n);
    if (t < 0.0) {
        t += n;
    }
    const size_t a = size_t(std::floor(t)) % daily.size(), b = (a + 1) % daily.size();
    const double f = t - std::floor(t);
    return daily[a] + (daily[b] - daily[a]) * f;
}

RiverRegime RiverRegime::from_index(const std::vector<float> &p_index) {
    RiverRegime r;
    r.daily.assign(p_index.begin(), p_index.end());
    r.days = double(p_index.size());
    return r;
}


double river_runoff_index(const std::vector<float> &index,double day) {
    if (index.empty()) return 1.0;
    double t=std::fmod(day-0.5,double(index.size())); if(t<0) t+=index.size();
    const size_t a=size_t(std::floor(t)),b=(a+1)%index.size();
    return index[a]+(index[b]-index[a])*(t-std::floor(t));
}
