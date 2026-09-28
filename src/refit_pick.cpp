#include "refit_pick.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const int N6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

bool contains(const std::vector<Cell> &p_cells, const Cell &p_cell) {
    return std::find(p_cells.begin(), p_cells.end(), p_cell) != p_cells.end();
}

// 落点占的格子的中心（船局部坐标）
void footprint_center(const Cell &p_anchor, ModuleKind p_kind, double &r_x, double &r_y, double &r_z) {
    const CellBox b = footprint_box(p_anchor, p_kind);
    r_x = 0.5 * (b.x0 + b.x1);
    r_y = 0.5 * (b.y0 + b.y1);
    r_z = 0.5 * (b.z0 + b.z1);
}

double distance2_to(const Cell &p_anchor, ModuleKind p_kind, double p_x, double p_y, double p_z) {
    double cx = 0.0, cy = 0.0, cz = 0.0;
    footprint_center(p_anchor, p_kind, cx, cy, cz);
    return (cx - p_x) * (cx - p_x) + (cy - p_y) * (cy - p_y) + (cz - p_z) * (cz - p_z);
}

} // namespace

CellBox cell_box(const Cell &p_cell, bool p_with_hull) {
    CellBox b;
    b.x0 = cell_center_x(p_cell.x) - 0.5 * CELL_WIDTH_M;
    b.x1 = cell_center_x(p_cell.x) + 0.5 * CELL_WIDTH_M;
    b.y0 = cell_deck_y(p_cell.y) - (p_with_hull && p_cell.y == 0 ? HULL_DEPTH_M : 0.0);
    b.y1 = cell_deck_y(p_cell.y) + CELL_HEIGHT_M;
    b.z0 = cell_center_z(p_cell.z) - 0.5 * CELL_LENGTH_M;
    b.z1 = cell_center_z(p_cell.z) + 0.5 * CELL_LENGTH_M;
    return b;
}

CellBox footprint_box(const Cell &p_anchor, ModuleKind p_kind, bool p_with_hull) {
    const ModuleDef &d = modules::def(p_kind);
    const CellBox lo = cell_box(p_anchor, p_with_hull);
    const CellBox hi = cell_box({ p_anchor.x + d.size_x - 1, p_anchor.y + d.size_y - 1, p_anchor.z + d.size_z - 1 }, false);
    return { lo.x0, lo.y0, lo.z0, hi.x1, hi.y1, hi.z1 };
}

bool ray_box(const PickRay &p_ray, const CellBox &p_box, double &r_t) {
    const double o[3] = { p_ray.ox, p_ray.oy, p_ray.oz };
    const double d[3] = { p_ray.dx, p_ray.dy, p_ray.dz };
    const double lo[3] = { p_box.x0, p_box.y0, p_box.z0 };
    const double hi[3] = { p_box.x1, p_box.y1, p_box.z1 };
    double t_in = 0.0, t_out = std::numeric_limits<double>::infinity();
    for (int k = 0; k < 3; k++) {
        if (std::abs(d[k]) < 1e-12) {
            if (o[k] < lo[k] || o[k] > hi[k]) {
                return false;
            }
            continue;
        }
        double t0 = (lo[k] - o[k]) / d[k], t1 = (hi[k] - o[k]) / d[k];
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        t_in = std::max(t_in, t0);
        t_out = std::min(t_out, t1);
        if (t_in > t_out) {
            return false;
        }
    }
    r_t = t_in;
    return true;
}

std::vector<Cell> glow_cells(const std::vector<Cell> &p_placements, ModuleKind p_kind) {
    std::vector<Cell> out;
    for (const Cell &anchor : p_placements) {
        for (const Cell &c : footprint(anchor, p_kind)) {
            if (!contains(out, c)) {
                out.push_back(c);
            }
        }
    }
    return out;
}

RefitHover pick_node(const ShipLayout &p_layout, const PickRay &p_ray) {
    RefitHover hover;
    double best = std::numeric_limits<double>::infinity();
    for (const CellPart &c : p_layout.cells()) {
        double t = 0.0;
        if (ray_box(p_ray, cell_box(c.at, true), t) && t < best) {
            best = t;
            hover.type = RefitHover::NODE;
            hover.anchor = p_layout.get_modules()[size_t(c.module)].at;
        }
    }
    return hover;
}

RefitHover pick_spot(const ShipLayout &p_layout, ModuleKind p_kind, const std::vector<Cell> &p_placements, const PickRay &p_ray) {
    RefitHover hover;
    const std::vector<Cell> glow = glow_cells(p_placements, p_kind);
    // 探的格子：亮格 + 贴着船的空格（不亮的那些用来说为什么不能放）
    std::vector<Cell> probes = glow;
    for (const CellPart &c : p_layout.cells()) {
        for (const auto &d : N6) {
            const Cell n{ c.at.x + d[0], c.at.y + d[1], c.at.z + d[2] };
            if (cell_in_range(n) && !p_layout.has(n) && !contains(probes, n)) {
                probes.push_back(n);
            }
        }
    }

    double ship_t = std::numeric_limits<double>::infinity();
    for (const CellPart &c : p_layout.cells()) {
        double t = 0.0;
        if (ray_box(p_ray, cell_box(c.at, true), t)) {
            ship_t = std::min(ship_t, t); // 船身挡着
        }
    }
    // 亮格优先：只有船身挡得住亮格；一格亮的都没指着，才看不亮的空格（说为什么放不了）
    int hit = -1;
    double best = ship_t;
    for (size_t k = 0; k < glow.size(); k++) {
        double t = 0.0;
        if (ray_box(p_ray, cell_box(probes[k]), t) && t < best) {
            best = t;
            hit = int(k);
        }
    }
    if (hit < 0) {
        for (size_t k = glow.size(); k < probes.size(); k++) {
            double t = 0.0;
            if (ray_box(p_ray, cell_box(probes[k]), t) && t < best) {
                best = t;
                hit = int(k);
            }
        }
    }
    if (hit < 0) {
        return hover;
    }
    const Cell probe = probes[size_t(hit)];
    const double px = p_ray.ox + p_ray.dx * best, py = p_ray.oy + p_ray.dy * best, pz = p_ray.oz + p_ray.dz * best;

    hover.type = RefitHover::SPOT;
    if (contains(glow, probe)) {
        double nearest = std::numeric_limits<double>::infinity();
        for (const Cell &anchor : p_placements) {
            const double d2 = distance2_to(anchor, p_kind, px, py, pz);
            if (contains(footprint(anchor, p_kind), probe) && d2 < nearest) {
                nearest = d2;
                hover.anchor = anchor;
            }
        }
        hover.legal = true;
        return hover;
    }

    // 不亮的空格：在盖得住它的锚点里挑一个（先挑整个落在范围里的），说为什么放不了
    const ModuleDef &d = modules::def(p_kind);
    double nearest = std::numeric_limits<double>::infinity();
    bool nearest_in_range = false;
    for (int dy = 0; dy < d.size_y; dy++) {
        for (int dz = 0; dz < d.size_z; dz++) {
            for (int dx = 0; dx < d.size_x; dx++) {
                const Cell anchor{ probe.x - dx, probe.y - dy, probe.z - dz };
                bool in_range = true;
                for (const Cell &c : footprint(anchor, p_kind)) {
                    in_range = in_range && cell_in_range(c);
                }
                const double d2 = distance2_to(anchor, p_kind, px, py, pz);
                if ((in_range && !nearest_in_range) || (in_range == nearest_in_range && d2 < nearest)) {
                    nearest = d2;
                    nearest_in_range = in_range;
                    hover.anchor = anchor;
                }
            }
        }
    }
    ShipLayout trial = p_layout;
    hover.reason = layout_place(trial, hover.anchor, p_kind);
    hover.legal = hover.reason.empty();
    return hover;
}
