#include "island_ground.h"

#include <cmath>

bool IslandGround::nearest_node(double p_x, double p_z, size_t &r_k) const {
    const double ext = field.half_extent();
    const int j = int(std::lround((p_x + ext) / field.cell_m)), i = int(std::lround((p_z + ext) / field.cell_m));
    if (i < 0 || j < 0 || i >= field.n || j >= field.n) {
        return false;
    }
    r_k = field.index(i, j);
    return true;
}

bool IslandGround::walk_height(double p_x, double p_z, double &r_y) const {
    const double ext = field.half_extent();
    const double fx = (p_x + ext) / field.cell_m, fz = (p_z + ext) / field.cell_m;
    const int j = int(std::floor(fx)), i = int(std::floor(fz));
    if (!field.quad_solid(i, j)) {
        return false;
    }
    const double u = fx - j, v = fz - i; // u 沿 j（x），v 沿 i（z）
    const double h00 = field.height[field.index(i, j)], h01 = field.height[field.index(i, j + 1)];
    const double h10 = field.height[field.index(i + 1, j)], h11 = field.height[field.index(i + 1, j + 1)];
    // 与地面网格同样的切法：三角 (00, 01, 10) 与 (01, 11, 10)
    r_y = u + v <= 1.0 ? h00 + (h01 - h00) * u + (h10 - h00) * v : h11 + (h10 - h11) * (1.0 - u) + (h01 - h11) * (1.0 - v);
    return true;
}

double IslandGround::water_depth(double p_x, double p_z) const {
    size_t k = 0;
    if (field.river_depth.empty() || !nearest_node(p_x, p_z, k)) {
        return 0.0;
    }
    return field.river_depth[k];
}

WaterKind IslandGround::water_at(double p_x, double p_z) const {
    size_t k = 0;
    return field.has_river() && nearest_node(p_x, p_z, k) && field.river_depth[k] > 0.0f ? WaterKind::RIVER : WaterKind::NONE;
}

double IslandGround::water_distance(double p_x, double p_z) const {
    size_t k = 0;
    return field.has_river() && nearest_node(p_x, p_z, k) ? double(field.river_dist[k]) : FAR;
}

void IslandGround::water_lines(double, double, double, double, std::vector<WaterLine> &r_lines) const {
    if (field.river.size() < 2) {
        return;
    }
    WaterLine line;
    for (const RiverPoint &p : field.river) {
        line.push_back({ p.x, p.z, p.half_width });
    }
    r_lines.push_back(std::move(line));
}

bool IslandGround::on_dock(double p_x, double p_z, double p_margin) const {
    return std::hypot(p_x - field.dock.x, p_z - field.dock.z) <= dock_apron_m + p_margin;
}

bool IslandGround::on_trail(double p_x, double p_z, double p_margin) const {
    size_t k = 0;
    return field.has_trail() && nearest_node(p_x, p_z, k) && field.trail_dist[k] <= p_margin;
}

PlotLattice IslandGround::plot_lattice() const {
    PlotLattice l;
    l.x0 = 0.0;
    l.z0 = 0.0;
    l.origin = (field.n - 1) * 0.5;
    l.cell_m = field.cell_m;
    l.bounded = true;
    l.i_max = l.j_max = field.n - 1;
    return l;
}

bool IslandGround::plot_node(const PlotLattice &p_lattice, int p_i, int p_j, PlotNode &r_node) const {
    const PlotLattice own = plot_lattice();
    if (p_lattice.cell_m != own.cell_m || p_lattice.origin != own.origin || p_lattice.x0 != own.x0 || p_lattice.z0 != own.z0) {
        return MapGround::plot_node(p_lattice, p_i, p_j, r_node); // 别人的格子：按坐标取
    }
    r_node = PlotNode();
    if (!field.in_mask(p_i, p_j)) {
        return false;
    }
    const size_t k = field.index(p_i, p_j);
    r_node.ground = true;
    r_node.y = field.height[k];
    r_node.wet = field.has_river() && field.river_depth[k] > 0.0f;
    r_node.water_m = field.has_river() ? double(field.river_dist[k]) : FAR;
    return true;
}

void island_nav_window(const IslandField &p_field, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) {
    const double ext = p_field.half_extent();
    r_x0 = -ext;
    r_z0 = -ext;
    r_cols = r_rows = p_field.n < 2 ? 0 : int(std::floor(2.0 * ext / p_rules.cell_m));
}
