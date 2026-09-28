#include "ship_cabins.h"

#include <algorithm>
#include <cmath>

#include "crops.h"

namespace {

constexpr double EPS = 1e-6;
const int N4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }; // (dx, dz)

struct Rect {
    double x0, z0, x1, z1;
};

bool overlaps(const Rect &a, const Rect &b) {
    return a.x0 < b.x1 - EPS && b.x0 < a.x1 - EPS && a.z0 < b.z1 - EPS && b.z0 < a.z1 - EPS;
}

// 从墙上一点 (p_x, p_z) 往 (p_ox, p_oz) 那边伸 p_depth 深、沿墙 ±p_half 宽的一块
Rect zone(double p_x, double p_z, int p_ox, int p_oz, double p_depth, double p_half) {
    if (p_ox != 0) {
        return { std::min(p_x, p_x + p_ox * p_depth), p_z - p_half, std::max(p_x, p_x + p_ox * p_depth), p_z + p_half };
    }
    return { p_x - p_half, std::min(p_z, p_z + p_oz * p_depth), p_x + p_half, std::max(p_z, p_z + p_oz * p_depth) };
}

int cell_index(double p_v, double p_size) {
    return int(std::floor((p_v + 0.5 * p_size) / p_size));
}

bool furnishable(const ShipLayout &p_layout, const Cell &p_cell) {
    const Module *m = p_layout.at(p_cell);
    return m && modules::def(m->kind).furnish;
}

std::string module_name(const ShipLayout &p_layout, const Cell &p_cell) {
    const Module *m = p_layout.at(p_cell);
    return m ? modules::def(m->kind).name : std::string();
}

} // namespace

CellSpace cell_space(const ShipLayout &p_layout, const Cell &p_cell) {
    const Module *m = p_layout.at(p_cell);
    if (!m) {
        return CellSpace::NONE;
    }
    if (!p_layout.has({ p_cell.x, p_cell.y + 1, p_cell.z })) {
        return CellSpace::DECK;
    }
    return modules::def(m->kind).enterable ? CellSpace::ROOM : CellSpace::SOLID;
}

SideKind cell_side(const ShipLayout &p_layout, const Cell &p_cell, int p_dx, int p_dz) {
    const CellSpace self = cell_space(p_layout, p_cell);
    const Cell n{ p_cell.x + p_dx, p_cell.y, p_cell.z + p_dz };
    const CellSpace other = cell_space(p_layout, n);
    if (self == CellSpace::ROOM) {
        switch (other) {
            case CellSpace::NONE:
                return SideKind::WINDOW;
            case CellSpace::DECK:
                return SideKind::DOOR;
            case CellSpace::ROOM:
                return p_layout.module_index_at(n) == p_layout.module_index_at(p_cell) ? SideKind::OPEN : SideKind::DOOR;
            case CellSpace::SOLID:
                return SideKind::WALL;
        }
    }
    if (self == CellSpace::DECK) {
        switch (other) {
            case CellSpace::NONE:
                return SideKind::RAIL; // 同一层那边没有节：邻列更矮或没有
            case CellSpace::DECK:
                return SideKind::OPEN;
            case CellSpace::ROOM:
                return SideKind::DOOR;
            case CellSpace::SOLID:
                return SideKind::WALL;
        }
    }
    return SideKind::WALL;
}

int ship_level_at(double p_y) {
    const int level = int(std::floor((p_y + 0.3) / CELL_HEIGHT_M));
    return std::clamp(level, 0, MAX_LEVELS - 1);
}

bool in_cabin(const ShipLayout &p_layout, double p_x, double p_y, double p_z) {
    const Cell c{ int(std::lround(p_x / CELL_WIDTH_M)), ship_level_at(p_y), int(std::lround(p_z / CELL_LENGTH_M)) };
    return cell_space(p_layout, c) == CellSpace::ROOM;
}

bool ShipFloor::height(double p_x, double p_z, double &r_y) const {
    const Cell c{ int(std::lround(p_x / CELL_WIDTH_M)), level, int(std::lround(p_z / CELL_LENGTH_M)) };
    const CellSpace space = cell_space(layout, c);
    if (space != CellSpace::DECK && space != CellSpace::ROOM) {
        return false;
    }
    r_y = cell_deck_y(level);
    return true;
}

std::string ship_prop_problem(const ShipLayout &p_layout, const PropDef &p_def, double p_x, double p_z, int p_quarter, int p_level,
        const ShipPropRules &p_rules) {
    double hx = 0.0, hz = 0.0;
    prop_half_extent(p_def, p_quarter, hx, hz);
    const Rect rect{ p_x - hx, p_z - hz, p_x + hx, p_z + hz };

    // 中心那一节
    const Cell center{ int(std::lround(p_x / CELL_WIDTH_M)), p_level, int(std::lround(p_z / CELL_LENGTH_M)) };
    switch (cell_space(p_layout, center)) {
        case CellSpace::NONE:
            return "船外头摆不了";
        case CellSpace::SOLID:
            return "封死的舱里摆不了";
        default:
            break;
    }
    if (!furnishable(p_layout, center)) {
        return module_name(p_layout, center) + "上摆不了东西：摆在空节上";
    }

    // 占地压着的每一节：不通的那几面离墙、栏留一点，门口留着（先说这个：伸出船边就是压着舷栏）；再看压着的节都能不能摆
    const int ix0 = cell_index(rect.x0 + EPS, CELL_WIDTH_M), ix1 = cell_index(rect.x1 - EPS, CELL_WIDTH_M);
    const int iz0 = cell_index(rect.z0 + EPS, CELL_LENGTH_M), iz1 = cell_index(rect.z1 - EPS, CELL_LENGTH_M);
    for (int iz = iz0; iz <= iz1; iz++) {
        for (int ix = ix0; ix <= ix1; ix++) {
            const Cell c{ ix, p_level, iz };
            const CellSpace space = cell_space(p_layout, c);
            if (space != CellSpace::DECK && space != CellSpace::ROOM) {
                continue;
            }
            const double cx = cell_center_x(ix), cz = cell_center_z(iz);
            for (const auto &d : N4) {
                const SideKind side = cell_side(p_layout, c, d[0], d[1]);
                if (side == SideKind::OPEN) {
                    continue;
                }
                const double inset = p_rules.edge_gap_m + (side == SideKind::RAIL ? SHIP_RAIL_T_M : (space == CellSpace::ROOM ? CABIN_WALL_M : 0.0));
                const double limit = d[0] != 0 ? cx + d[0] * (0.5 * CELL_WIDTH_M - inset) : cz + d[1] * (0.5 * CELL_LENGTH_M - inset);
                const bool beyond = (d[0] > 0 && rect.x1 > limit + EPS) || (d[0] < 0 && rect.x0 < limit - EPS) || (d[1] > 0 && rect.z1 > limit + EPS) ||
                        (d[1] < 0 && rect.z0 < limit - EPS);
                if (beyond) {
                    return side == SideKind::RAIL ? "贴着舷栏了" : "贴着舱壁了";
                }
                if (side == SideKind::DOOR) {
                    const double ex = cx + d[0] * 0.5 * CELL_WIDTH_M, ez = cz + d[1] * 0.5 * CELL_LENGTH_M;
                    if (overlaps(rect, zone(ex, ez, -d[0], -d[1], p_rules.door_clear_m, 0.5 * CABIN_DOOR_W_M + 0.2))) {
                        return "挡着舱门了";
                    }
                }
            }
        }
    }
    for (int iz = iz0; iz <= iz1; iz++) {
        for (int ix = ix0; ix <= ix1; ix++) {
            const Cell c{ ix, p_level, iz };
            const CellSpace space = cell_space(p_layout, c);
            if (space == CellSpace::NONE) {
                return "伸到船外头了";
            }
            if (space == CellSpace::SOLID || !furnishable(p_layout, c)) {
                return "占到" + module_name(p_layout, c) + "上了";
            }
        }
    }

    // 梯子：梯脚那一层的甲板上、梯顶那一层的甲板上，贴墙那一块留着
    for (const Ladder &l : layout_ladders(p_layout)) {
        const int ox = l.low_x - l.high_x, oz = l.low_z - l.high_z; // 高列 → 低列
        const double wx = cell_center_x(l.high_x) + ox * 0.5 * CELL_WIDTH_M + oz * SHIP_LADDER_ALONG_M;
        const double wz = cell_center_z(l.high_z) + oz * 0.5 * CELL_LENGTH_M - ox * SHIP_LADDER_ALONG_M;
        const double half = 0.8;
        if (p_level == l.from_level - 1 && overlaps(rect, zone(wx, wz, ox, oz, p_rules.ladder_clear_m, half))) {
            return "挡着梯子了";
        }
        if (p_level == l.to_level - 1 && overlaps(rect, zone(wx, wz, -ox, -oz, p_rules.ladder_clear_m, half))) {
            return "挡着梯子了";
        }
    }

    // 舷门：舷门那一排两头的船边，跳板接在这儿
    if (p_level == 0) {
        const double half = SHIP_GATE_HALF_M + 0.3;
        if (overlaps(rect, zone(port_edge_x(p_layout), 0.0, 1, 0, p_rules.gate_clear_m, half)) ||
                overlaps(rect, zone(starboard_edge_x(p_layout), 0.0, -1, 0, p_rules.gate_clear_m, half))) {
            return "挡着舷门了";
        }
    }
    return std::string();
}

std::string ship_props_problem(const ShipLayout &p_layout, const Placements &p_props, const ShipPropRules &p_rules) {
    for (const PlacedProp &p : p_props.get_props()) {
        const PropDef *def = props::find(p.item);
        if (!def) {
            continue;
        }
        const int level = ship_level_at(p.y);
        const Cell under{ int(std::lround(p.x / CELL_WIDTH_M)), level, int(std::lround(p.z / CELL_LENGTH_M)) };
        const std::string why = cell_space(p_layout, under) == CellSpace::NONE ? std::string("脚下的节没了")
                                                                               : ship_prop_problem(p_layout, *def, p.x, p.z, p.quarter, level, p_rules);
        if (!why.empty()) {
            return catalog::item_name(p.item) + "放不下了（" + why + "）";
        }
    }
    return std::string();
}
