#include "world_ground.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double SHORE_PROBE_M = 50.0; // 岸距梯度的差分步长
constexpr double SEARCH_INLAND_M = 400.0; // 从生成器的点往岛内退这么远起量
constexpr double SEARCH_OFFSHORE_M = 300.0; // 往岛外探这么远起找
constexpr int LANDCOVER_FARMLAND = 7; // world.json landcover.classes：可耕地
constexpr int LANDCOVER_TERRACE = 8; // 梯田
constexpr int LANDCOVER_RIVER = 10; // 河道
constexpr int LANDCOVER_LAKE = 11; // 湖

double segment_distance(double p_px, double p_pz, double p_ax, double p_az, double p_bx, double p_bz) {
    const double dx = p_bx - p_ax, dz = p_bz - p_az;
    const double len2 = dx * dx + dz * dz;
    const double t = len2 > 0.0 ? std::clamp(((p_px - p_ax) * dx + (p_pz - p_az) * dz) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p_px - (p_ax + t * dx), p_pz - (p_az + t * dz));
}

bool in_grid(const WorldGrid &g, int r, int c) {
    return r >= 0 && c >= 0 && r < g.rows && c < g.cols;
}

bool is_stream(const WorldGrid &g, int r, int c) {
    return in_grid(g, r, c) && g.stream[g.index(r, c)] != 0;
}

// 有水的格：溪涧、河道、湖
bool is_water(const WorldGrid &g, int r, int c) {
    if (!in_grid(g, r, c)) {
        return false;
    }
    const size_t k = g.index(r, c);
    return g.stream[k] != 0 || g.landcover[k] == LANDCOVER_RIVER || g.landcover[k] == LANDCOVER_LAKE;
}

// 3×3 邻域里 p_pred 为真的格：格心与它相邻的同类格心连成中线，量到中线的最近距离
template <typename Pred>
double centerline_distance(const WorldGrid &g, double p_wx, double p_wz, Pred p_pred) {
    const int c0 = int(std::lround(g.col_of(p_wx))), r0 = int(std::lround(g.row_of(p_wz)));
    double best = 1e18;
    for (int r = r0 - 1; r <= r0 + 1; r++) {
        for (int c = c0 - 1; c <= c0 + 1; c++) {
            if (!p_pred(g, r, c)) {
                continue;
            }
            const double ax = g.x0 + c * g.res, az = g.z0 + r * g.res;
            best = std::min(best, std::hypot(p_wx - ax, p_wz - az));
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if ((dr || dc) && p_pred(g, r + dr, c + dc)) {
                        best = std::min(best, segment_distance(p_wx, p_wz, ax, az, ax + dc * g.res, az + dr * g.res));
                    }
                }
            }
        }
    }
    return best;
}

// 船坞坪的地：原地形的陆地，且不压溪涧河滩、不是水面、不在河漫滩上
class WorldYardGround : public WorldDockGround {
    WorldGround map;

public:
    WorldYardGround(const WorldTerrain &p_terrain, const WorldGroundRules &p_rules) :
            WorldDockGround(p_terrain), map(p_terrain, 0.0, 0.0, p_rules) {}
    bool land(double p_x, double p_z) const override {
        return WorldDockGround::land(p_x, p_z) && map.water_at(p_x, p_z) == WaterKind::NONE && !map.on_floodplain(p_x, p_z);
    }
};

} // namespace

bool WorldDockGround::ground(double p_x, double p_z, double &r_y) const {
    const TerrainSample s = terrain.sample_raw(p_x, p_z);
    r_y = s.top;
    return s.land;
}

DockRules world_dock_rules() {
    DockRules rules;
    rules.step_m = 1.0;
    rules.back_m = 2.0;
    rules.back_count = 4;
    rules.apron_m = 30.0;
    rules.pier_min_m = 9.0;
    rules.berth_half_length_m = 30.0;
    rules.reach_probe_m = 60.0;
    rules.lattice_m = 2.0;
    return rules;
}

void world_shore_normal(const WorldTerrain &p_terrain, double p_x, double p_z, double &r_out_x, double &r_out_z) {
    const WorldGrid &g = p_terrain.get_grid();
    const double gx = g.shore_at(p_x + SHORE_PROBE_M, p_z) - g.shore_at(p_x - SHORE_PROBE_M, p_z);
    const double gz = g.shore_at(p_x, p_z + SHORE_PROBE_M) - g.shore_at(p_x, p_z - SHORE_PROBE_M);
    const double len = std::hypot(gx, gz);
    // 岸距在陆地上为正、往岛内增大：外法线是负梯度
    r_out_x = len > 1e-9 ? -gx / len : 1.0;
    r_out_z = len > 1e-9 ? -gz / len : 0.0;
}

DockPlan plan_world_dock(const WorldTerrain &p_terrain, double p_x, double p_z, const DockRules &p_rules) {
    double out_x = 1.0, out_z = 0.0;
    world_shore_normal(p_terrain, p_x, p_z, out_x, out_z);
    return plan_dock(WorldDockGround(p_terrain), p_x - SEARCH_INLAND_M * out_x, p_z - SEARCH_INLAND_M * out_z, out_x, out_z,
            SEARCH_INLAND_M + SEARCH_OFFSHORE_M, p_rules);
}

YardRules world_yard_rules() {
    YardRules rules;
    rules.max_inland_m = 45.0; // 岸线噪声让岸缘进退 ±28 m：主码头 #7 两侧要退进 27 m 才整块是地
    return rules;
}

YardPlan plan_world_yard(const WorldTerrain &p_terrain, const DockPlan &p_dock, const YardRules &p_rules, const WorldGroundRules &p_ground) {
    if (!p_dock.ok) {
        return YardPlan();
    }
    return plan_yard(WorldYardGround(p_terrain, p_ground), p_dock, p_rules);
}

double WorldGround::stream_distance(double p_wx, double p_wz) const {
    return centerline_distance(terrain.get_grid(), p_wx, p_wz, is_stream);
}

WaterKind WorldGround::water_at(double p_x, double p_z) const {
    const double wx = origin_x + p_x, wz = origin_z + p_z;
    const WorldGrid &g = terrain.get_grid();
    const int cover = g.landcover[g.nearest_cell(wx, wz)];
    if (terrain.has_rivers()) {
        if (cover == LANDCOVER_LAKE) {
            return WaterKind::POND;
        }
        RiverHit hit;
        if (terrain.get_rivers().nearest(wx, wz, 1e9, hit) && hit.distance <= hit.at.half() + rules.channel_margin_m) {
            return WaterKind::STREAM;
        }
        return WaterKind::NONE;
    }
    if (cover == LANDCOVER_RIVER || cover == LANDCOVER_LAKE) {
        return WaterKind::POND;
    }
    if (stream_distance(wx, wz) <= rules.stream_bank_m) {
        return WaterKind::STREAM;
    }
    return WaterKind::NONE;
}

bool WorldGround::on_floodplain(double p_x, double p_z) const {
    RiverHit hit;
    return terrain.has_rivers() && terrain.get_rivers().nearest(origin_x + p_x, origin_z + p_z, 1e9, hit) && hit.distance <= hit.at.core();
}

double WorldGround::water_depth(double p_x, double p_z) const {
    if (!terrain.has_rivers()) {
        return 0.0;
    }
    const double wx = origin_x + p_x, wz = origin_z + p_z;
    RiverHit hit;
    if (!terrain.get_rivers().nearest(wx, wz, 1e9, hit)) {
        return 0.0;
    }
    const RiverStage st = river_stage(hit.at, rules.runoff, terrain.get_rivers().get_rules());
    if (!st.wet || hit.distance > st.half_width) {
        return 0.0;
    }
    double y = 0.0;
    return terrain.surface(wx, wz, y) ? std::max(0.0, st.level - y) : 0.0;
}

double WorldGround::water_distance(double p_x, double p_z) const {
    if (terrain.has_rivers()) {
        RiverHit hit;
        return terrain.get_rivers().nearest(origin_x + p_x, origin_z + p_z, 1e9, hit) ? hit.distance : FAR;
    }
    return centerline_distance(terrain.get_grid(), origin_x + p_x, origin_z + p_z, is_water);
}

void WorldGround::water_lines(double p_x0, double p_z0, double p_x1, double p_z1, std::vector<WaterLine> &r_lines) const {
    if (terrain.has_rivers()) {
        // 河中线：框里的段按河连成一条条线（断开处另起一条），半宽取此刻（runoff）的水面；干了的不算
        const WorldRivers &rivers = terrain.get_rivers();
        std::vector<std::pair<int, int>> segs;
        rivers.segments_in(origin_x + p_x0, origin_z + p_z0, origin_x + p_x1, origin_z + p_z1, [&](int s, int i) { segs.push_back({ s, i }); });
        std::sort(segs.begin(), segs.end());
        WaterLine line;
        int prev_stem = -1, prev_seg = -1;
        auto flush = [&]() {
            if (line.size() >= 2) {
                r_lines.push_back(line);
            }
            line.clear();
        };
        for (const auto &[s, i] : segs) {
            const RiverStem &stem = rivers.get_stems()[size_t(s)];
            if (s != prev_stem || i != prev_seg + 1) {
                flush();
            }
            for (int e = (line.empty() ? 0 : 1); e < 2; e++) {
                const RiverVertex &v = stem.v[size_t(i + e)];
                const RiverStage st = river_stage(v, rules.runoff, rivers.get_rules());
                if (!st.wet) {
                    flush();
                    continue;
                }
                line.push_back({ v.x - origin_x, v.z - origin_z, st.half_width });
            }
            prev_stem = s;
            prev_seg = i;
        }
        flush();
        return;
    }
    const WorldGrid &g = terrain.get_grid();
    const int c0 = int(std::floor(g.col_of(origin_x + std::min(p_x0, p_x1)))), c1 = int(std::ceil(g.col_of(origin_x + std::max(p_x0, p_x1))));
    const int r0 = int(std::floor(g.row_of(origin_z + std::min(p_z0, p_z1)))), r1 = int(std::ceil(g.row_of(origin_z + std::max(p_z0, p_z1))));
    // 每对相邻的水格连一段（只连「往后」的四个方向，每段只出一次），每段按步长加点
    const int forward[4][2] = { { 0, 1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } };
    for (int r = r0; r <= r1; r++) {
        for (int c = c0; c <= c1; c++) {
            if (!is_water(g, r, c)) {
                continue;
            }
            const double ax = g.x0 + c * g.res - origin_x, az = g.z0 + r * g.res - origin_z;
            for (const auto &d : forward) {
                if (!is_water(g, r + d[0], c + d[1])) {
                    continue;
                }
                const double bx = ax + d[1] * g.res, bz = az + d[0] * g.res;
                const int steps = std::max(1, int(std::ceil(std::hypot(bx - ax, bz - az) / rules.water_line_step_m)));
                WaterLine line;
                for (int s = 0; s <= steps; s++) {
                    const double t = double(s) / steps;
                    line.push_back({ ax + t * (bx - ax), az + t * (bz - az), rules.water_half_width_m });
                }
                r_lines.push_back(std::move(line));
            }
        }
    }
}

bool WorldGround::on_dock(double p_x, double p_z, double p_margin) const {
    return terrain.near_pad(origin_x + p_x, origin_z + p_z, rules.pad_shoulder_m + p_margin);
}

bool WorldGround::on_yard(double p_x, double p_z, double p_margin) const {
    for (const YardPlan &yard : terrain.get_yards()) {
        if (yard.contains(origin_x + p_x, origin_z + p_z, p_margin)) {
            return true;
        }
    }
    return false;
}

bool WorldGround::fertile(double p_x, double p_z) const {
    const WorldGrid &g = terrain.get_grid();
    const int cover = g.landcover[g.nearest_cell(origin_x + p_x, origin_z + p_z)];
    return cover == LANDCOVER_FARMLAND || cover == LANDCOVER_TERRACE;
}

int WorldGround::island_at(double p_x, double p_z) const {
    const WorldGrid &g = terrain.get_grid();
    return g.island[g.nearest_cell(origin_x + p_x, origin_z + p_z)];
}

PlotLattice WorldGround::plot_lattice() const {
    PlotLattice l;
    l.x0 = -origin_x;
    l.z0 = -origin_z;
    l.origin = 0.0;
    l.cell_m = rules.plot_cell_m;
    l.bounded = false;
    return l;
}

bool WorldLandingGround::water(double p_x, double p_z) const {
    return WorldGround(terrain, 0.0, 0.0).water_at(p_x, p_z) != WaterKind::NONE;
}
