#include "world_ground.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double SHORE_PROBE_M = 50.0; // 岸距梯度的差分步长
constexpr double SEARCH_INLAND_M = 400.0; // 从生成器的点往岛内退这么远起量
constexpr double SEARCH_OFFSHORE_M = 300.0; // 往岛外探这么远起找
constexpr int LANDCOVER_RIVER = 10; // world.json landcover.classes：河道
constexpr int LANDCOVER_LAKE = 11; // 湖

double segment_distance(double p_px, double p_pz, double p_ax, double p_az, double p_bx, double p_bz) {
    const double dx = p_bx - p_ax, dz = p_bz - p_az;
    const double len2 = dx * dx + dz * dz;
    const double t = len2 > 0.0 ? std::clamp(((p_px - p_ax) * dx + (p_pz - p_az) * dz) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p_px - (p_ax + t * dx), p_pz - (p_az + t * dz));
}

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

WorldSiteGround::WorldSiteGround(const WorldTerrain &p_terrain, double p_origin_x, double p_origin_z, double p_pad_radius, const WorldSiteRules &p_rules) :
        terrain(p_terrain), origin_x(p_origin_x), origin_z(p_origin_z), rules(p_rules) {
    for (const DockPlan &pad : p_terrain.get_pads()) {
        if (std::hypot(pad.x - origin_x, pad.z - origin_z) <= p_pad_radius + pad.apron_m) {
            pads.push_back(pad);
        }
    }
}

bool WorldSiteGround::site_height(double p_x, double p_z, double &r_y) const {
    return terrain.surface(origin_x + p_x, origin_z + p_z, r_y);
}

double WorldSiteGround::stream_distance(double p_wx, double p_wz) const {
    const WorldGrid &g = terrain.get_grid();
    const int c0 = int(std::lround(g.col_of(p_wx))), r0 = int(std::lround(g.row_of(p_wz)));
    double best = 1e18;
    auto is_stream = [&g](int r, int c) {
        return r >= 0 && c >= 0 && r < g.rows && c < g.cols && g.stream[g.index(r, c)] != 0;
    };
    // 3×3 邻域里的溪涧格：格心与它相邻的溪涧格心连成中线
    for (int r = r0 - 1; r <= r0 + 1; r++) {
        for (int c = c0 - 1; c <= c0 + 1; c++) {
            if (!is_stream(r, c)) {
                continue;
            }
            const double ax = g.x0 + c * g.res, az = g.z0 + r * g.res;
            best = std::min(best, std::hypot(p_wx - ax, p_wz - az));
            for (int dr = -1; dr <= 1; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    if ((dr || dc) && is_stream(r + dr, c + dc)) {
                        best = std::min(best, segment_distance(p_wx, p_wz, ax, az, ax + dc * g.res, az + dr * g.res));
                    }
                }
            }
        }
    }
    return best;
}

SiteBlock WorldSiteGround::site_block(double p_x, double p_z) const {
    const double wx = origin_x + p_x, wz = origin_z + p_z;
    const WorldGrid &g = terrain.get_grid();
    const int cover = g.landcover[g.nearest_cell(wx, wz)];
    if (cover == LANDCOVER_RIVER || cover == LANDCOVER_LAKE) {
        return { 0, "这是水面，盖不了" };
    }
    if (stream_distance(wx, wz) <= rules.stream_half_width_m) {
        return { 0, "压着溪涧了" };
    }
    for (const DockPlan &pad : pads) {
        if (std::hypot(wx - pad.x, wz - pad.z) <= pad.apron_m + rules.pad_margin_m) {
            return { 1, "码头的停机坪上不能盖" };
        }
    }
    return SiteBlock();
}
