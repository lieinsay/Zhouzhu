#include "village_prep.h"

#include <algorithm>
#include <cmath>
#include <map>

std::vector<BuildingFrame> layout_frames(const MapGround &p_ground, const SettlementLayout &p_layout, const SiteRules &p_rules) {
    const MapSiteGround site(p_ground, p_rules);
    std::vector<BuildingFrame> frames;
    frames.reserve(size_t(p_layout.size()));
    for (const Building &b : p_layout.get_items()) {
        const BuildingDef *def = buildings::by_id(b.kind);
        frames.push_back(def ? building_frame(site, *def, b, p_rules) : BuildingFrame());
    }
    return frames;
}

std::vector<VillageCell> village_cells(const SettlementLayout &p_layout, const std::vector<BuildingFrame> &p_frames, double p_cell_m) {
    std::map<std::pair<int, int>, VillageCell> cells;
    for (int k = 0; k < p_layout.size() && size_t(k) < p_frames.size(); k++) {
        const Building &b = p_layout.at(k);
        const BuildingDef *def = buildings::by_id(b.kind);
        if (!def) {
            continue;
        }
        const int ix = int(std::floor(b.x / p_cell_m)), iz = int(std::floor(b.z / p_cell_m));
        auto found = cells.find({ ix, iz });
        if (found == cells.end()) {
            VillageCell cell;
            cell.ix = ix;
            cell.iz = iz;
            cell.x0 = ix * p_cell_m;
            cell.z0 = iz * p_cell_m;
            cell.min_x = cell.min_z = 1e18;
            cell.max_x = cell.max_z = -1e18;
            found = cells.emplace(std::make_pair(ix, iz), std::move(cell)).first;
        }
        VillageCell &cell = found->second;
        cell.buildings.push_back(k);
        double cx[4], cz[4];
        footprint_corners(*def, b.x, b.z, b.yaw, house_dims::ROOF_OVERHANG, cx, cz);
        for (int c = 0; c < 4; c++) {
            cell.min_x = std::min(cell.min_x, cx[c]);
            cell.max_x = std::max(cell.max_x, cx[c]);
            cell.min_z = std::min(cell.min_z, cz[c]);
            cell.max_z = std::max(cell.max_z, cz[c]);
        }
        append_building_proxy(cell.proxy, *def, b, p_frames[size_t(k)], cell.x0, cell.z0);
    }
    std::vector<VillageCell> out;
    out.reserve(cells.size());
    for (auto &entry : cells) {
        out.push_back(std::move(entry.second));
    }
    return out;
}

double cell_distance(const VillageCell &p_cell, double p_x, double p_z) {
    const double dx = std::max({ p_cell.min_x - p_x, 0.0, p_x - p_cell.max_x });
    const double dz = std::max({ p_cell.min_z - p_z, 0.0, p_z - p_cell.max_z });
    return std::hypot(dx, dz);
}

VillagePrep prepare_world_village(const WorldTerrain &p_terrain, const SiteSettlement &p_site, uint64_t p_seed, const std::vector<std::string> &p_edits,
        const VillagePrepRules &p_rules) {
    VillagePrep out;
    out.plan = plan_world_village(p_terrain, p_site, p_seed, p_rules.village);
    out.layout = apply_layout_edits(out.plan.layout, parse_layout_edits(p_edits), &out.unmatched);
    const WorldGround ground(p_terrain, p_site.x, p_site.z, p_rules.village.ground);
    out.frames = layout_frames(ground, out.layout, p_rules.village.site);
    out.cells = village_cells(out.layout, out.frames, p_rules.cell_m);
    return out;
}

void village_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double p_margin, double &r_x0, double &r_z0, int &r_cols, int &r_rows) {
    // 房子外接框外扩一圈
    double lo_x = 0.0, lo_z = 0.0, hi_x = 0.0, hi_z = 0.0;
    for (const Building &b : p_layout.get_items()) {
        const BuildingDef *def = buildings::by_id(b.kind);
        const double r = def ? 0.5 * std::hypot(def->width_m, def->depth_m) : 0.0;
        lo_x = std::min(lo_x, b.x - r);
        lo_z = std::min(lo_z, b.z - r);
        hi_x = std::max(hi_x, b.x + r);
        hi_z = std::max(hi_z, b.z + r);
    }
    r_x0 = lo_x - p_margin;
    r_z0 = lo_z - p_margin;
    r_cols = int(std::ceil((hi_x - lo_x + 2.0 * p_margin) / p_rules.cell_m));
    r_rows = int(std::ceil((hi_z - lo_z + 2.0 * p_margin) / p_rules.cell_m));
}

VillageNav prepare_village_nav(const MapGround &p_ground, const SettlementLayout &p_layout, const SimRules &p_rules, double p_x0, double p_z0, int p_cols, int p_rows) {
    VillageNav out;
    // 水：村子 water_reach_m 以内的水中线；导航格够不着最近的那处水，就往那边扩
    out.water = village_water(p_ground, p_layout, p_rules.work);
    double cx = 0.0, cz = 0.0, reach = 0.0;
    village_extent(p_layout, cx, cz, reach);
    double near_d = 1e18, near_x = 0.0, near_z = 0.0;
    for (const WaterLine &line : out.water) {
        for (const WaterPoint &p : line) {
            const double d = std::hypot(p.x - cx, p.z - cz);
            if (d < near_d) {
                near_d = d;
                near_x = p.x;
                near_z = p.z;
            }
        }
    }
    double x0 = p_x0, z0 = p_z0;
    int cols = p_cols, rows = p_rows;
    const double cell = p_rules.nav.cell_m;
    const bool water_inside = near_x >= x0 && near_z >= z0 && near_x <= x0 + cols * cell && near_z <= z0 + rows * cell;
    if (!out.water.empty() && cols > 0 && !water_inside) {
        const double margin = 10.0;
        const double lo_x = std::min(x0, near_x - margin), lo_z = std::min(z0, near_z - margin);
        const double hi_x = std::max(x0 + cols * cell, near_x + margin), hi_z = std::max(z0 + rows * cell, near_z + margin);
        cols = int(std::ceil((hi_x - lo_x) / cell));
        rows = int(std::ceil((hi_z - lo_z) / cell));
        x0 = lo_x;
        z0 = lo_z;
    }
    out.nav = build_nav_grid(p_ground, x0, z0, cols, rows, p_layout, p_rules.nav);
    return out;
}
