#include "world_terrain.h"

#include <algorithm>
#include <cmath>

namespace {

// 地表类（world.json landcover.classes 的下标）→ 细节噪声幅度（米）
constexpr double DETAIL_AMP[] = {
    0.0, // 虚空
    6.0, // 崖缘
    7.0, // 裸岩
    4.0, // 高山草甸
    3.5, // 林地
    3.5, // 灌丛
    2.5, // 草坡
    1.0, // 可耕地
    1.0, // 梯田
    0.5, // 湿地
    0.3, // 河道
    0.3, // 湖
};
constexpr int DETAIL_CLASSES = int(sizeof(DETAIL_AMP) / sizeof(DETAIL_AMP[0]));
constexpr double DETAIL_FEATURE_M = 240.0;
constexpr int DETAIL_OCTAVES = 6; // 240 m → 7.5 m
constexpr double UNDERSIDE_AMP_M = 8.0;
constexpr double UNDERSIDE_FEATURE_M = 160.0;
constexpr double MIN_THICKNESS_M = 2.0;

constexpr uint64_t STREAM_COAST = 1;
constexpr uint64_t STREAM_DETAIL = 2;
constexpr uint64_t STREAM_UNDERSIDE = 3;

uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double lattice(uint64_t p_seed, int64_t p_i, int64_t p_j) {
    const uint64_t h = mix(p_seed ^ mix(uint64_t(p_i) * 0x9E3779B97F4A7C15ull ^ mix(uint64_t(p_j) + 0x632BE59BD9B4E019ull)));
    return double(h >> 11) * (2.0 / 9007199254740992.0) - 1.0; // [-1, 1)
}

double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// [0, 1] 夹住的 smoothstep
double smooth01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

double hash_noise(uint64_t p_seed, double p_x, double p_z) {
    const double fx = std::floor(p_x), fz = std::floor(p_z);
    const int64_t i = int64_t(fx), j = int64_t(fz);
    const double tx = fade(p_x - fx), tz = fade(p_z - fz);
    const double a = lattice(p_seed, i, j), b = lattice(p_seed, i + 1, j);
    const double c = lattice(p_seed, i, j + 1), d = lattice(p_seed, i + 1, j + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

double hash_fbm(uint64_t p_seed, double p_x, double p_z, double p_feature, int p_octaves, double p_persistence) {
    double sum = 0.0, norm = 0.0, amp = 1.0, freq = 1.0 / p_feature;
    for (int k = 0; k < p_octaves; k++) {
        // 每层错开一点再转个角度，免得各层格点对齐成网纹
        const double c = 0.8, s = 0.6;
        const double x = p_x * freq, z = p_z * freq;
        const double rx = k % 2 ? c * x - s * z : x, rz = k % 2 ? s * x + c * z : z;
        sum += amp * hash_noise(mix(p_seed + uint64_t(k)), rx + 17.3 * k, rz - 9.1 * k);
        norm += amp;
        amp *= p_persistence;
        freq *= 2.0;
    }
    return sum / norm;
}

WorldTerrain::WorldTerrain(const WorldGrid &p_grid, const std::vector<WorldIsland> &p_islands, uint64_t p_seed, const RiverRules &p_river_rules) :
        grid(&p_grid), seed(p_seed) {
    int top = 0;
    for (const WorldIsland &isl : p_islands) {
        top = std::max(top, isl.id);
    }
    for (uint8_t id : p_grid.island) {
        top = std::max(top, int(id));
    }
    islands.resize(size_t(top) + 1);
    for (size_t k = 0; k < islands.size(); k++) {
        islands[k].id = int(k);
    }
    for (const WorldIsland &isl : p_islands) {
        if (isl.id >= 0) {
            islands[size_t(isl.id)] = isl;
        }
    }
    rivers = WorldRivers::build(p_grid, p_river_rules, p_seed, [this](double x, double z) {
        return grid->shore_at(x, z) + COAST_AMP_M * coast_noise(x, z);
    });
}

const WorldIsland &WorldTerrain::island_info(int p_id) const {
    return islands[size_t(std::clamp(p_id, 0, int(islands.size()) - 1))];
}

double WorldTerrain::coast_noise(double p_x, double p_z) const {
    return hash_fbm(seed ^ STREAM_COAST, p_x, p_z, COAST_FEATURE_M, 4, 0.55);
}

double WorldTerrain::detail(double p_x, double p_z) const {
    // 幅度按四邻格双线性过渡：取最近格的话，格界上会有一道台阶
    const WorldGrid &g = *grid;
    const double fc = g.col_of(p_x), fr = g.row_of(p_z);
    const int c = int(std::floor(fc)), r = int(std::floor(fr));
    const double tc = fc - c, tr = fr - r;
    auto amp = [&](int rr, int cc) {
        rr = std::clamp(rr, 0, g.rows - 1);
        cc = std::clamp(cc, 0, g.cols - 1);
        const int cls = g.landcover[g.index(rr, cc)];
        return cls > 0 && cls < DETAIL_CLASSES ? DETAIL_AMP[cls] : 2.0; // 虚空格（岸线外推出来的地）按草坡算
    };
    const double a = amp(r, c) + (amp(r, c + 1) - amp(r, c)) * tc;
    const double b = amp(r + 1, c) + (amp(r + 1, c + 1) - amp(r + 1, c)) * tc;
    return (a + (b - a) * tr) * hash_fbm(seed ^ STREAM_DETAIL, p_x, p_z, DETAIL_FEATURE_M, DETAIL_OCTAVES);
}

TerrainSample WorldTerrain::sample_raw(double p_x, double p_z) const {
    TerrainSample s;
    const WorldGrid &g = *grid;
    s.shore = g.shore_at(p_x, p_z) + COAST_AMP_M * coast_noise(p_x, p_z);
    const size_t cell = g.nearest_cell(p_x, p_z);
    s.island = g.island[cell];
    s.landcover = g.landcover[cell];
    s.stream = g.stream[cell] != 0;
    s.land = s.shore > 0.0;
    RiverHit hits[WorldRivers::MAX_HITS];
    const int n_hits = rivers ? rivers->near(p_x, p_z, hits, WorldRivers::MAX_HITS) : 0;
    if (n_hits == 0) {
        s.top = g.height_at(p_x, p_z) + detail(p_x, p_z);
    } else {
        // 河：细节噪声在滩外渐显；顶面在各条河的横断面与原地形之间取最低
        const RiverRules &rr = rivers->get_rules();
        double fade = 1.0;
        for (int k = 0; k < n_hits; k++) {
            fade = std::min(fade, smooth01((hits[k].distance - hits[k].at.core()) / rr.blend_m));
        }
        const double natural = g.height_at(p_x, p_z) + fade * detail(p_x, p_z);
        double top = 1e18;
        for (int k = 0; k < n_hits; k++) {
            const RiverHit &h = hits[k];
            const double target = river_profile(h.at, h.distance, rr);
            const double w = 1.0 - smooth01((h.distance - h.at.core()) / rr.blend_m);
            top = std::min(top, natural + w * (target - natural));
            s.bed = std::max(s.bed, float(1.0 - smooth01((h.distance - h.at.half()) / 1.5)));
            s.bank = std::max(s.bank, float(w));
        }
        s.top = top;
    }
    const WorldIsland &isl = island_info(s.island);
    const double inward = std::clamp(s.shore / std::max(1.0, isl.max_shore_m), 0.0, 1.0);
    s.bottom = isl.rim_m - isl.cliff_m - isl.keel_m * std::sqrt(inward) + UNDERSIDE_AMP_M * hash_fbm(seed ^ STREAM_UNDERSIDE, p_x, p_z, UNDERSIDE_FEATURE_M, 3);
    s.bottom = std::min(s.bottom, s.top - MIN_THICKNESS_M);
    return s;
}

namespace {

int64_t pad_key(int p_i, int p_j) {
    return (int64_t(p_i) << 32) ^ int64_t(uint32_t(p_j));
}

} // namespace

void WorldTerrain::set_pads(const std::vector<DockPlan> &p_pads) {
    pads.clear();
    pad_buckets.clear();
    for (const DockPlan &pad : p_pads) {
        if (!pad.ok) {
            continue;
        }
        const int index = int(pads.size());
        pads.push_back(pad);
        const double reach = pad.apron_m + PAD_REACH_M;
        const int i0 = int(std::floor((pad.z - reach) / PAD_BUCKET_M)), i1 = int(std::floor((pad.z + reach) / PAD_BUCKET_M));
        const int j0 = int(std::floor((pad.x - reach) / PAD_BUCKET_M)), j1 = int(std::floor((pad.x + reach) / PAD_BUCKET_M));
        for (int i = i0; i <= i1; i++) {
            for (int j = j0; j <= j1; j++) {
                pad_buckets[pad_key(i, j)].push_back(index);
            }
        }
    }
}

TerrainSample WorldTerrain::sample(double p_x, double p_z) const {
    TerrainSample s = sample_raw(p_x, p_z);
    if (!pad_buckets.empty()) {
        const auto found = pad_buckets.find(pad_key(int(std::floor(p_z / PAD_BUCKET_M)), int(std::floor(p_x / PAD_BUCKET_M))));
        if (found != pad_buckets.end()) {
            for (int index : found->second) {
                const DockPlan &pad = pads[size_t(index)];
                // 河从坪上过：坪在河道、河漫滩与渐回带里让给河（生成器常把码头放在河口）
                const double w = dock_pad_weight(pad, p_x, p_z) * (1.0 - double(s.bank));
                if (w > 0.0) {
                    s.top += w * (pad.y - s.top);
                }
            }
        }
    }
    for (const YardPlan &yard : yards) {
        if (std::abs(p_x - yard.x) <= 0.5 * (yard.length + yard.width) + YARD_REACH_M && std::abs(p_z - yard.z) <= 0.5 * (yard.length + yard.width) + YARD_REACH_M &&
                yard.outside_distance(p_x, p_z) <= YARD_REACH_M) {
            s.top = yard_carve(yard, yard_rules, p_x, p_z, s.top);
        }
    }
    s.bottom = std::min(s.bottom, s.top - MIN_THICKNESS_M);
    return s;
}

bool WorldTerrain::near_pad(double p_x, double p_z, double p_margin) const {
    const auto found = pad_buckets.find(pad_key(int(std::floor(p_z / PAD_BUCKET_M)), int(std::floor(p_x / PAD_BUCKET_M))));
    if (found == pad_buckets.end()) {
        return false;
    }
    for (int index : found->second) {
        const DockPlan &pad = pads[size_t(index)];
        if (std::hypot(p_x - pad.x, p_z - pad.z) <= pad.apron_m + p_margin) {
            return true;
        }
    }
    return false;
}

void WorldTerrain::set_yards(const std::vector<YardPlan> &p_yards, const YardRules &p_rules) {
    yards.clear();
    yard_rules = p_rules;
    for (const YardPlan &yard : p_yards) {
        if (yard.present) {
            yards.push_back(yard);
        }
    }
}

bool WorldTerrain::is_land(double p_x, double p_z) const {
    return grid->shore_at(p_x, p_z) + COAST_AMP_M * coast_noise(p_x, p_z) > 0.0;
}

bool WorldTerrain::surface(double p_x, double p_z, double &r_y) const {
    const TerrainSample s = sample(p_x, p_z);
    r_y = s.top;
    return s.land;
}
