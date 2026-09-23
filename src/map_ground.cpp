#include "map_ground.h"

#include <cmath>

bool PlotLattice::cell_of(double p_x, double p_z, int &r_i, int &r_j) const {
    // 与 IslandField 的 (x + ext) / cell 同一算式：岛上 x0 = 0、origin · cell = ext
    const double fj = (p_x - x0 + origin * cell_m) / cell_m, fi = (p_z - z0 + origin * cell_m) / cell_m;
    if (!std::isfinite(fi) || !std::isfinite(fj) || std::abs(fi) > 2e9 || std::abs(fj) > 2e9) {
        return false;
    }
    r_i = int(std::floor(fi));
    r_j = int(std::floor(fj));
    return contains(r_i, r_j);
}

PlotLattice PlotLattice::shifted(double p_dx, double p_dz) const {
    PlotLattice out = *this;
    out.x0 = x0 - p_dx;
    out.z0 = z0 - p_dz;
    return out;
}

bool MapGround::plot_node(const PlotLattice &p_lattice, int p_i, int p_j, PlotNode &r_node) const {
    const double x = p_lattice.node_x(p_j), z = p_lattice.node_z(p_i);
    r_node = PlotNode();
    r_node.ground = site_height(x, z, r_node.y);
    if (!r_node.ground) {
        return false;
    }
    r_node.wet = water_at(x, z) != WaterKind::NONE;
    r_node.water_m = water_distance(x, z);
    return true;
}

SiteBlock ground_block(const MapGround &p_ground, double p_x, double p_z, const SiteRules &p_rules) {
    switch (p_ground.water_at(p_x, p_z)) {
        case WaterKind::RIVER:
            return { 0, "这是河，盖不了" };
        case WaterKind::STREAM:
            return { 0, "压着溪涧了" };
        case WaterKind::POND:
            return { 0, "这是水面，盖不了" };
        case WaterKind::NONE:
            break;
    }
    if (p_ground.on_dock(p_x, p_z, p_rules.dock_margin_m)) {
        return { 1, "停机坪上不能盖" };
    }
    if (p_ground.on_yard(p_x, p_z, p_rules.yard_margin_m)) {
        return { 2, "这是船坞的坪，盖不得" };
    }
    if (p_ground.on_trail(p_x, p_z, p_rules.trail_margin_m)) {
        return { 3, "压着路了" };
    }
    return SiteBlock();
}
