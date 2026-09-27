#include "build_pieces.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

using namespace piece_dims;

namespace {

constexpr double TAU = 6.283185307179586;
constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double NEW_PENALTY_M = 0.6; // 新开一座比吸到已有的一座多算这么远（挨着能接上就接上）
constexpr double OUTSIDE_PENALTY_M = 0.5; // 坡顶、梯子摆在没有地坪的格上多算这么远（墙顶两边一样近时摆进屋里那格）

const char *role_name(PieceRole p_role) {
    switch (p_role) {
        case PieceRole::FOUNDATION:
            return "地基";
        case PieceRole::FLOOR:
            return "楼板";
        case PieceRole::WALL:
            return "墙";
        case PieceRole::DOOR:
            return "门";
        case PieceRole::WINDOW:
            return "窗";
        case PieceRole::GABLE:
            return "山墙";
        case PieceRole::ROOF:
            return "顶";
        case PieceRole::PILLAR:
            return "柱";
        case PieceRole::LADDER:
            return "梯子";
    }
    return "件";
}

double wrap_angle(double p_a) {
    double a = std::fmod(p_a, TAU);
    if (a < 0.0) {
        a += TAU;
    }
    return a;
}

// 坡顶往哪边低 → 那一边是格的第几条边（cell_edge 的 side）
int low_side(int p_dir) {
    static const int SIDE[4] = { 1, 3, 0, 2 };
    return SIDE[((p_dir % 4) + 4) % 4];
}

// 点到盒子（中心 c、半尺寸 h）的距离（在里面为 0）
double box_distance(double p_x, double p_y, double p_z, double cx, double cy, double cz, double hx, double hy, double hz) {
    const double dx = std::max(0.0, std::abs(p_x - cx) - hx), dy = std::max(0.0, std::abs(p_y - cy) - hy), dz = std::max(0.0, std::abs(p_z - cz) - hz);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

const PieceDef pieces::DEFS[] = {
    { "foundation", PieceRole::FOUNDATION, PieceSlot::CELL, PartMaterial::STONE },
    { "floorboard", PieceRole::FLOOR, PieceSlot::CELL, PartMaterial::FLOOR },
    { "earth_wall", PieceRole::WALL, PieceSlot::EDGE, PartMaterial::WALL },
    { "brick_wall", PieceRole::WALL, PieceSlot::EDGE, PartMaterial::BRICK },
    { "door_wall", PieceRole::DOOR, PieceSlot::EDGE, PartMaterial::WALL },
    { "window_wall", PieceRole::WINDOW, PieceSlot::EDGE, PartMaterial::WALL },
    { "gable", PieceRole::GABLE, PieceSlot::EDGE, PartMaterial::WALL },
    { "thatch_roof", PieceRole::ROOF, PieceSlot::CELL, PartMaterial::STRAW },
    { "tile_roof", PieceRole::ROOF, PieceSlot::CELL, PartMaterial::ROOF },
    { "pillar", PieceRole::PILLAR, PieceSlot::CORNER, PartMaterial::DARK_WOOD },
    { "ladder", PieceRole::LADDER, PieceSlot::CELL, PartMaterial::WOOD },
};
const int pieces::COUNT = int(sizeof(pieces::DEFS) / sizeof(pieces::DEFS[0]));

const PieceDef *pieces::find(const std::string &p_item) {
    for (int k = 0; k < COUNT; k++) {
        if (p_item == DEFS[k].item) {
            return &DEFS[k];
        }
    }
    return nullptr;
}

bool pieces::is_wall_role(PieceRole p_role) {
    return p_role == PieceRole::WALL || p_role == PieceRole::DOOR || p_role == PieceRole::WINDOW;
}

// ---------------------------------------------------------------- 一座

void Structure::to_host(double p_lx, double p_ly, double p_lz, double &r_x, double &r_y, double &r_z) const {
    const double c = std::cos(yaw), s = std::sin(yaw);
    r_x = x + c * p_lx + s * p_lz;
    r_y = y + p_ly;
    r_z = z - s * p_lx + c * p_lz;
}

void Structure::to_local(double p_x, double p_y, double p_z, double &r_lx, double &r_ly, double &r_lz) const {
    const double c = std::cos(yaw), s = std::sin(yaw);
    const double dx = p_x - x, dz = p_z - z;
    r_lx = c * dx - s * dz;
    r_ly = p_y - y;
    r_lz = s * dx + c * dz;
}

const Piece *Structure::find(int p_piece) const {
    for (const Piece &p : pieces) {
        if (p.id == p_piece) {
            return &p;
        }
    }
    return nullptr;
}

const Piece *Structure::at(PieceRole p_role, const PieceSpot &p_spot) const {
    switch (p_role) {
        case PieceRole::FOUNDATION:
        case PieceRole::FLOOR:
            return cell_piece(p_spot.level == 0 ? PieceRole::FOUNDATION : PieceRole::FLOOR, p_spot.x, p_spot.z, p_spot.level);
        case PieceRole::ROOF:
            return roof_at(p_spot.x, p_spot.z, p_spot.level);
        case PieceRole::LADDER:
            return cell_piece(PieceRole::LADDER, p_spot.x, p_spot.z, p_spot.level);
        case PieceRole::WALL:
        case PieceRole::DOOR:
        case PieceRole::WINDOW:
            return wall_at(p_spot.x, p_spot.z, p_spot.dir, p_spot.level);
        case PieceRole::GABLE:
            return gable_at(p_spot.x, p_spot.z, p_spot.dir, p_spot.level);
        case PieceRole::PILLAR:
            return pillar_at(p_spot.x, p_spot.z, p_spot.level);
    }
    return nullptr;
}

const Piece *Structure::cell_piece(PieceRole p_role, int p_x, int p_z, int p_level) const {
    for (const Piece &p : pieces) {
        const PieceDef *def = pieces::find(p.item);
        if (def && def->role == p_role && p.spot.slot == PieceSlot::CELL && p.spot.x == p_x && p.spot.z == p_z && p.spot.level == p_level) {
            return &p;
        }
    }
    return nullptr;
}

const Piece *Structure::roof_at(int p_x, int p_z, int p_level) const {
    return cell_piece(PieceRole::ROOF, p_x, p_z, p_level);
}

const Piece *Structure::wall_at(int p_x, int p_z, int p_dir, int p_level) const {
    for (const Piece &p : pieces) {
        const PieceDef *def = pieces::find(p.item);
        if (def && pieces::is_wall_role(def->role) && p.spot.x == p_x && p.spot.z == p_z && p.spot.dir == p_dir && p.spot.level == p_level) {
            return &p;
        }
    }
    return nullptr;
}

const Piece *Structure::gable_at(int p_x, int p_z, int p_dir, int p_level) const {
    for (const Piece &p : pieces) {
        const PieceDef *def = pieces::find(p.item);
        if (def && def->role == PieceRole::GABLE && p.spot.x == p_x && p.spot.z == p_z && p.spot.dir == p_dir && p.spot.level == p_level) {
            return &p;
        }
    }
    return nullptr;
}

const Piece *Structure::pillar_at(int p_x, int p_z, int p_level) const {
    for (const Piece &p : pieces) {
        const PieceDef *def = pieces::find(p.item);
        if (def && def->role == PieceRole::PILLAR && p.spot.x == p_x && p.spot.z == p_z && p.spot.level == p_level) {
            return &p;
        }
    }
    return nullptr;
}

bool Structure::has_floor(int p_x, int p_z, int p_level) const {
    return cell_piece(p_level == 0 ? PieceRole::FOUNDATION : PieceRole::FLOOR, p_x, p_z, p_level) != nullptr;
}

EdgeRef cell_edge(int p_x, int p_z, int p_side) {
    switch (p_side) {
        case 0:
            return { p_x, p_z, 0 };
        case 1:
            return { p_x, p_z + 1, 0 };
        case 2:
            return { p_x, p_z, 1 };
        default:
            return { p_x + 1, p_z, 1 };
    }
}

void edge_cells(const EdgeRef &p_edge, int &r_x0, int &r_z0, int &r_x1, int &r_z1) {
    if (p_edge.dir == 0) {
        r_x0 = p_edge.x, r_z0 = p_edge.z - 1;
    } else {
        r_x0 = p_edge.x - 1, r_z0 = p_edge.z;
    }
    r_x1 = p_edge.x, r_z1 = p_edge.z;
}

bool roof_edge_heights(const Piece &p_roof, const EdgeRef &p_edge, double &r_h0, double &r_h1) {
    const int rx = p_roof.spot.x, rz = p_roof.spot.z, t = p_roof.spot.tier, d = ((p_roof.spot.dir % 4) + 4) % 4;
    // 这条边在格里的位置：沿 x 的边 v 固定（0 北、1 南），沿 z 的边 u 固定（0 西、1 东）
    double u0, v0, u1, v1;
    if (p_edge.dir == 0) {
        if (p_edge.x != rx || (p_edge.z != rz && p_edge.z != rz + 1)) {
            return false;
        }
        u0 = 0.0, u1 = 1.0, v0 = v1 = p_edge.z == rz ? 0.0 : 1.0;
    } else {
        if (p_edge.z != rz || (p_edge.x != rx && p_edge.x != rx + 1)) {
            return false;
        }
        v0 = 0.0, v1 = 1.0, u0 = u1 = p_edge.x == rx ? 0.0 : 1.0;
    }
    auto h = [&](double u, double v) {
        switch (d) {
            case 0:
                return (t + 1.0 - v) * ROOF_RISE_M; // 往 +z 低
            case 1:
                return (t + 1.0 - u) * ROOF_RISE_M;
            case 2:
                return (t + v) * ROOF_RISE_M;
            default:
                return (t + u) * ROOF_RISE_M;
        }
    };
    r_h0 = h(u0, v0);
    r_h1 = h(u1, v1);
    return true;
}

// ---------------------------------------------------------------- 支撑

namespace {

bool wall_below_any_side(const Structure &p_s, int p_x, int p_z, int p_level) {
    for (int side = 0; side < 4; side++) {
        const EdgeRef e = cell_edge(p_x, p_z, side);
        if (p_s.wall_at(e.x, e.z, e.dir, p_level)) {
            return true;
        }
    }
    return false;
}

bool pillar_any_corner(const Structure &p_s, int p_x, int p_z, int p_level) {
    for (int k = 0; k < 4; k++) {
        if (p_s.pillar_at(p_x + (k & 1), p_z + (k >> 1), p_level)) {
            return true;
        }
    }
    return false;
}

const int NB[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };

// 挨着这条边、这一层、坡顶底在这条边上高过墙顶的那件坡顶（高的那头最高的）；没有 nullptr
const Piece *gable_roof(const Structure &p_s, const EdgeRef &p_edge, int p_level, double &r_h0, double &r_h1) {
    int x0, z0, x1, z1;
    edge_cells(p_edge, x0, z0, x1, z1);
    const Piece *best = nullptr;
    double best_top = 1e-6;
    for (const Piece *roof : { p_s.roof_at(x0, z0, p_level), p_s.roof_at(x1, z1, p_level) }) {
        double h0 = 0.0, h1 = 0.0;
        if (roof && roof_edge_heights(*roof, p_edge, h0, h1) && std::max(h0, h1) > best_top) {
            best = roof;
            best_top = std::max(h0, h1);
            r_h0 = h0;
            r_h1 = h1;
        }
    }
    return best;
}

} // namespace

int door_outside(const Structure &p_structure, const PieceSpot &p_spot) {
    int x0, z0, x1, z1;
    edge_cells({ p_spot.x, p_spot.z, p_spot.dir }, x0, z0, x1, z1);
    const bool a = p_structure.has_floor(x0, z0, p_spot.level), b = p_structure.has_floor(x1, z1, p_spot.level);
    if (a == b) {
        return 0;
    }
    return a ? 1 : -1; // 负向那格（x0, z0）有地坪：门外在正向
}

double door_drop(const Structure &p_structure, const PieceSpot &p_spot, const LandingGround &p_ground) {
    if (p_spot.level != 0) {
        return 0.0;
    }
    const int out = door_outside(p_structure, p_spot);
    if (out == 0) {
        return 0.0;
    }
    constexpr double PROBE_M = 0.8;
    const double G = GRID_M;
    const double lx = p_spot.dir == 0 ? (p_spot.x + 0.5) * G : p_spot.x * G + out * PROBE_M;
    const double lz = p_spot.dir == 0 ? p_spot.z * G + out * PROBE_M : (p_spot.z + 0.5) * G;
    double hx, hy, hz;
    p_structure.to_host(lx, 0.0, lz, hx, hy, hz);
    double y = 0.0;
    if (!p_ground.height(hx, hz, y)) {
        return 0.0;
    }
    return std::max(0.0, p_structure.y - y);
}

bool gable_shape(const Structure &p_structure, const EdgeRef &p_edge, int p_level, double &r_h0, double &r_h1) {
    return gable_roof(p_structure, p_edge, p_level, r_h0, r_h1) != nullptr;
}

bool piece_supported(const Structure &p_s, const Piece &p_piece) {
    const PieceDef *def = pieces::find(p_piece.item);
    if (!def) {
        return false;
    }
    const PieceSpot &sp = p_piece.spot;
    switch (def->role) {
        case PieceRole::FOUNDATION:
            return true; // 地面撑着（摆的时候量过；有地基的格不让改地形）
        case PieceRole::FLOOR:
            if (sp.level < 1 || p_s.cell_piece(PieceRole::LADDER, sp.x, sp.z, sp.level - 1)) {
                return false;
            }
            if (wall_below_any_side(p_s, sp.x, sp.z, sp.level - 1) || pillar_any_corner(p_s, sp.x, sp.z, sp.level - 1)) {
                return true;
            }
            for (const auto &nb : NB) {
                if (p_s.cell_piece(PieceRole::FLOOR, sp.x + nb[0], sp.z + nb[1], sp.level)) {
                    return true;
                }
            }
            return false;
        case PieceRole::WALL:
        case PieceRole::DOOR:
        case PieceRole::WINDOW: {
            int x0, z0, x1, z1;
            edge_cells({ sp.x, sp.z, sp.dir }, x0, z0, x1, z1);
            return p_s.has_floor(x0, z0, sp.level) || p_s.has_floor(x1, z1, sp.level) || (sp.level > 0 && p_s.wall_at(sp.x, sp.z, sp.dir, sp.level - 1));
        }
        case PieceRole::GABLE: {
            double h0 = 0.0, h1 = 0.0;
            return p_s.wall_at(sp.x, sp.z, sp.dir, sp.level) && gable_roof(p_s, { sp.x, sp.z, sp.dir }, sp.level, h0, h1);
        }
        case PieceRole::ROOF:
            if (wall_below_any_side(p_s, sp.x, sp.z, sp.level) || pillar_any_corner(p_s, sp.x, sp.z, sp.level)) {
                return true;
            }
            for (const auto &nb : NB) {
                if (p_s.roof_at(sp.x + nb[0], sp.z + nb[1], sp.level)) {
                    return true;
                }
            }
            return false;
        case PieceRole::PILLAR:
            for (int k = 0; k < 4; k++) {
                if (p_s.has_floor(sp.x - (k & 1), sp.z - (k >> 1), sp.level)) {
                    return true;
                }
            }
            return sp.level > 0 && p_s.pillar_at(sp.x, sp.z, sp.level - 1);
        case PieceRole::LADDER:
            return p_s.has_floor(sp.x, sp.z, sp.level) && !p_s.cell_piece(PieceRole::FLOOR, sp.x, sp.z, sp.level + 1);
    }
    return false;
}

// ---------------------------------------------------------------- 外包盒

namespace {

void spot_bounds(const Structure &p_s, PieceRole p_role, const PieceSpot &p_spot, double p_bottom, double &r_cx, double &r_cy, double &r_cz, double &r_hx, double &r_hy,
        double &r_hz) {
    const double G = GRID_M, H = STOREY_M, L = p_spot.level;
    const double cx = (p_spot.x + 0.5) * G, cz = (p_spot.z + 0.5) * G;
    switch (p_role) {
        case PieceRole::FOUNDATION:
            r_cx = cx, r_cz = cz, r_cy = 0.5 * p_bottom, r_hx = r_hz = 0.5 * G, r_hy = std::max(0.05, -0.5 * p_bottom);
            return;
        case PieceRole::FLOOR:
            r_cx = cx, r_cz = cz, r_cy = L * H - 0.5 * FLOOR_T, r_hx = r_hz = 0.5 * G, r_hy = 0.5 * FLOOR_T;
            return;
        case PieceRole::WALL:
        case PieceRole::DOOR:
        case PieceRole::WINDOW:
            r_cy = L * H + 0.5 * H, r_hy = 0.5 * H;
            if (p_spot.dir == 0) {
                r_cx = cx, r_cz = p_spot.z * G, r_hx = 0.5 * (G + WALL_T), r_hz = 0.5 * WALL_T;
            } else {
                r_cx = p_spot.x * G, r_cz = cz, r_hx = 0.5 * WALL_T, r_hz = 0.5 * (G + WALL_T);
            }
            return;
        case PieceRole::GABLE: {
            double h0 = ROOF_RISE_M, h1 = 0.0;
            gable_roof(p_s, { p_spot.x, p_spot.z, p_spot.dir }, p_spot.level, h0, h1);
            const double top = std::max({ h0, h1, 0.2 });
            r_cy = (L + 1) * H + 0.5 * top, r_hy = 0.5 * top;
            if (p_spot.dir == 0) {
                r_cx = cx, r_cz = p_spot.z * G, r_hx = 0.5 * G, r_hz = 0.5 * WALL_T;
            } else {
                r_cx = p_spot.x * G, r_cz = cz, r_hx = 0.5 * WALL_T, r_hz = 0.5 * G;
            }
            return;
        }
        case PieceRole::ROOF:
            r_cx = cx, r_cz = cz, r_hx = r_hz = 0.5 * G;
            r_cy = (L + 1) * H + (p_spot.tier + 0.5) * ROOF_RISE_M + 0.5 * ROOF_T, r_hy = 0.5 * ROOF_RISE_M + 0.5 * ROOF_T;
            return;
        case PieceRole::PILLAR:
            r_cx = p_spot.x * G, r_cz = p_spot.z * G, r_cy = L * H + 0.5 * H, r_hx = r_hz = 0.5 * PILLAR_W, r_hy = 0.5 * H;
            return;
        case PieceRole::LADDER: {
            // 靠着 dir 那一边，离边 0.25 m
            const int side = low_side(p_spot.dir);
            const double off = 0.5 * G - 0.25;
            r_cx = cx + (side == 3 ? off : side == 2 ? -off : 0.0);
            r_cz = cz + (side == 1 ? off : side == 0 ? -off : 0.0);
            r_cy = L * H + 0.5 * H, r_hy = 0.5 * H;
            r_hx = side >= 2 ? 0.25 : 0.45;
            r_hz = side >= 2 ? 0.45 : 0.25;
            return;
        }
    }
    r_cx = cx, r_cy = L * H, r_cz = cz, r_hx = r_hy = r_hz = 0.5;
}

} // namespace

void piece_bounds(const Structure &p_s, const Piece &p_piece, double &r_cx, double &r_cy, double &r_cz, double &r_hx, double &r_hy, double &r_hz) {
    const PieceDef *def = pieces::find(p_piece.item);
    spot_bounds(p_s, def ? def->role : PieceRole::FLOOR, p_piece.spot, p_piece.bottom, r_cx, r_cy, r_cz, r_hx, r_hy, r_hz);
    // 开着的门：门扇转到了墙的一面，外包盒往那一面伸出一扇门宽（对着门扇也点得着）
    if (def && def->role == PieceRole::DOOR && p_piece.open != 0) {
        const double s = p_piece.open > 0 ? 1.0 : -1.0, reach = DOOR_W;
        double &c = p_piece.spot.dir == 0 ? r_cz : r_cx;
        double &h = p_piece.spot.dir == 0 ? r_hz : r_hx;
        const double lo = std::min(c - h, c + s * (h + reach)), hi = std::max(c + h, c + s * (h + reach));
        c = 0.5 * (lo + hi);
        h = 0.5 * (hi - lo);
    }
}

int piece_at(const Structure &p_s, double p_lx, double p_ly, double p_lz, double p_margin) {
    int best = -1;
    double best_d = INF, best_vol = INF;
    for (const Piece &p : p_s.pieces) {
        double cx, cy, cz, hx, hy, hz;
        piece_bounds(p_s, p, cx, cy, cz, hx, hy, hz);
        const double d = box_distance(p_lx, p_ly, p_lz, cx, cy, cz, hx, hy, hz);
        if (d > p_margin) {
            continue;
        }
        const double vol = hx * hy * hz;
        if (d < best_d - 1e-9 || (d < best_d + 1e-9 && vol < best_vol)) {
            best = p.id;
            best_d = d;
            best_vol = vol;
        }
    }
    return best;
}

// ---------------------------------------------------------------- 一堆

int Structures::index_of(int p_structure) const {
    for (size_t k = 0; k < list.size(); k++) {
        if (list[k].id == p_structure) {
            return int(k);
        }
    }
    return -1;
}

const Structure *Structures::find(int p_structure) const {
    const int k = index_of(p_structure);
    return k >= 0 ? &list[size_t(k)] : nullptr;
}

Structure *Structures::edit(int p_structure) {
    const int k = index_of(p_structure);
    return k >= 0 ? &list[size_t(k)] : nullptr;
}

int Structures::piece_count() const {
    int n = 0;
    for (const Structure &s : list) {
        n += int(s.pieces.size());
    }
    return n;
}

std::pair<int, int> Structures::add(const std::string &p_item, const PiecePlan &p_plan) {
    Structure *target = nullptr;
    if (p_plan.structure >= 0 && p_plan.structure < int(list.size())) {
        target = &list[size_t(p_plan.structure)];
    } else {
        Structure s = p_plan.frame;
        s.pieces.clear();
        s.id = next_structure++;
        list.push_back(s);
        target = &list.back();
    }
    Piece piece;
    piece.id = next_piece++;
    piece.item = p_item;
    piece.spot = p_plan.spot;
    piece.bottom = p_plan.bottom;
    target->pieces.push_back(piece);
    revision++;
    return { target->id, piece.id };
}

int Structures::add_structure(const Structure &p_structure) {
    Structure s = p_structure;
    s.id = next_structure++;
    for (Piece &p : s.pieces) {
        p.id = next_piece++;
    }
    list.push_back(s);
    revision++;
    return s.id;
}

bool Structures::remove(int p_structure, int p_piece, std::string &r_why) {
    const int k = index_of(p_structure);
    if (k < 0) {
        r_why = "没有这一座";
        return false;
    }
    Structure &s = list[size_t(k)];
    const auto it = std::find_if(s.pieces.begin(), s.pieces.end(), [&](const Piece &p) { return p.id == p_piece; });
    if (it == s.pieces.end()) {
        r_why = "没有这一件";
        return false;
    }
    const Piece removed = *it;
    const size_t at = size_t(it - s.pieces.begin());
    s.pieces.erase(it);
    const Piece *orphan = nullptr;
    for (const Piece &p : s.pieces) {
        if (!piece_supported(s, p)) {
            orphan = &p;
            break;
        }
    }
    if (orphan) {
        const PieceDef *def = pieces::find(orphan->item);
        r_why = std::string("先拆靠它撑着的") + (def ? role_name(def->role) : "件");
        s.pieces.insert(s.pieces.begin() + int64_t(at), removed);
        return false;
    }
    if (s.pieces.empty()) {
        list.erase(list.begin() + k);
    }
    revision++;
    return true;
}

bool Structures::set_open(int p_structure, int p_piece, int p_open) {
    Structure *s = edit(p_structure);
    if (!s) {
        return false;
    }
    for (Piece &p : s->pieces) {
        if (p.id == p_piece) {
            p.open = p_open;
            revision++;
            return true;
        }
    }
    return false;
}

void Structures::assign(const std::vector<Structure> &p_list) {
    list = p_list;
    next_structure = 1;
    next_piece = 1;
    for (const Structure &s : list) {
        next_structure = std::max(next_structure, s.id + 1);
        for (const Piece &p : s.pieces) {
            next_piece = std::max(next_piece, p.id + 1);
        }
    }
    revision++;
}

void Structures::shift(const std::function<void(double &, double &, double &, double &)> &p_move) {
    for (Structure &s : list) {
        p_move(s.x, s.y, s.z, s.yaw);
    }
    revision++;
}

// ---------------------------------------------------------------- 摆

namespace {

struct Candidate {
    int structure = -1; // −1 = 新开一座
    PieceSpot spot;
    double score = INF; // 准星离它多远（加上偏好）
    bool ok = false;
    std::string why;
    double bottom = 0.0;
    double sx = 0.0, sy = 0.0, sz = 0.0; // 吸附点（局部）
};

// 地基那一格的地面：九个点（四角往里 5 cm、四边中点、中心）都要有地、不在水里；给出最高、最低
bool foundation_ground(const Structure &p_frame, int p_x, int p_z, const LandingGround &p_ground, double &r_hi, double &r_lo, std::string &r_why) {
    r_hi = -INF;
    r_lo = INF;
    const double G = GRID_M;
    const double inset = 0.05;
    const double us[3] = { inset, 0.5 * G, G - inset };
    for (double u : us) {
        for (double v : us) {
            double hx, hy, hz;
            p_frame.to_host(p_x * G + u, 0.0, p_z * G + v, hx, hy, hz);
            double y = 0.0;
            if (!p_ground.height(hx, hz, y)) {
                r_why = "这儿没有地";
                return false;
            }
            if (p_ground.water(hx, hz)) {
                r_why = "水里摆不了";
                return false;
            }
            r_hi = std::max(r_hi, y);
            r_lo = std::min(r_lo, y);
        }
    }
    return true;
}

// 坡顶的默认朝向：往这一座地坪的中心外面低；正在中心就按人的视线（往人这边低）
int roof_default_dir(const Structure &p_s, int p_x, int p_z, int p_level, double p_look_yaw) {
    double sx = 0.0, sz = 0.0;
    int n = 0;
    for (const Piece &p : p_s.pieces) {
        const PieceDef *def = pieces::find(p.item);
        if (def && (def->role == PieceRole::FOUNDATION || def->role == PieceRole::FLOOR) && p.spot.level == p_level) {
            sx += p.spot.x + 0.5;
            sz += p.spot.z + 0.5;
            n++;
        }
    }
    double vx = 0.0, vz = 0.0;
    if (n > 0) {
        vx = p_x + 0.5 - sx / n;
        vz = p_z + 0.5 - sz / n;
    }
    if (std::abs(vx) < 0.25 && std::abs(vz) < 0.25) {
        // 视线 (−sin, −cos)（宿主）换到局部，往人这边低 = 朝视线的反方向
        const double a = p_look_yaw - p_s.yaw;
        vx = std::sin(a);
        vz = std::cos(a);
    }
    if (std::abs(vx) > std::abs(vz)) {
        return vx > 0.0 ? 1 : 3;
    }
    return vz > 0.0 ? 0 : 2;
}

// 梯子默认靠着准星最近的那一边
int ladder_default_dir(double p_lx, double p_lz, int p_x, int p_z) {
    const double u = p_lx / GRID_M - p_x, v = p_lz / GRID_M - p_z;
    const double d[4] = { 1.0 - v, 1.0 - u, v, u }; // 到 +z、+x、−z、−x 那一边的距离
    return int(std::min_element(d, d + 4) - d);
}

void evaluate(const Structure &p_s, const PieceDef &p_def, Candidate &r_c, const PieceAim &p_aim, const LandingGround &p_ground, const PieceRules &p_rules) {
    const PieceSpot &sp = r_c.spot;
    // 已经有一件（新开的一座是空的）
    if (r_c.structure >= 0 && p_s.at(p_def.role, sp)) {
        r_c.why = std::string("这儿已经有") + role_name(p_def.role);
        return;
    }
    if (sp.level >= piece_dims::MAX_LEVELS) {
        r_c.why = "盖不了那么高";
        return;
    }
    Piece probe;
    probe.item = p_def.item;
    probe.spot = sp;
    if (p_def.role == PieceRole::FOUNDATION) {
        double hi = 0.0, lo = 0.0;
        if (!foundation_ground(p_s, sp.x, sp.z, p_ground, hi, lo, r_c.why)) {
            return;
        }
        if (r_c.structure < 0) {
            // 新开一座：地坪 = 最高处 + 至少露出的那一截（p_s 是按新开算的原点，y 在这里定）
            if (hi - lo > p_rules.max_plinth_m - p_rules.min_plinth_m) {
                r_c.why = "地太陡：先把高处挖低或低处垒高";
                return;
            }
            r_c.bottom = lo - (hi + p_rules.min_plinth_m) - p_rules.bury_m;
        } else {
            if (hi > p_s.y - 0.02) {
                r_c.why = "地面高过这一座的地坪：先挖低";
                return;
            }
            if (p_s.y - lo > p_rules.max_plinth_m) {
                r_c.why = "离地太高：先垒土";
                return;
            }
            // 挨着已有的地基
            bool touching = false;
            for (const auto &nb : NB) {
                touching = touching || p_s.has_floor(sp.x + nb[0], sp.z + nb[1], 0);
            }
            if (!touching) {
                r_c.why = "要挨着这一座的地基";
                return;
            }
            r_c.bottom = lo - p_s.y - p_rules.bury_m;
        }
    } else if (!piece_supported(p_s, probe)) {
        switch (p_def.role) {
            case PieceRole::FLOOR:
                r_c.why = p_s.cell_piece(PieceRole::LADDER, sp.x, sp.z, sp.level - 1) ? "下面是梯口，留着别铺" : "楼板要搭在下一层的墙或柱上";
                break;
            case PieceRole::WALL:
            case PieceRole::DOOR:
            case PieceRole::WINDOW:
                r_c.why = "墙要立在地基、楼板或下一层的墙上";
                break;
            case PieceRole::GABLE:
                r_c.why = "山墙补在墙顶和坡顶之间：先有墙和坡顶";
                break;
            case PieceRole::ROOF:
                r_c.why = "顶要搭在墙、柱或挨着的顶上";
                break;
            case PieceRole::PILLAR:
                r_c.why = "柱要立在地基、楼板或下一层的柱上";
                break;
            case PieceRole::LADDER:
                r_c.why = p_s.has_floor(sp.x, sp.z, sp.level) ? "楼上这一格铺了楼板：梯口要空着" : "梯子要立在地基或楼板上";
                break;
            default:
                r_c.why = "没有撑着";
                break;
        }
        return;
    }
    // 0 层的门：门外铺台阶下到地面
    if (p_def.role == PieceRole::DOOR && sp.level == 0) {
        r_c.bottom = -door_drop(p_s, sp, p_ground);
    }
    // 够不够得着（按吸附点）
    double hx, hy, hz;
    p_s.to_host(r_c.sx, r_c.sy, r_c.sz, hx, hy, hz);
    if (std::hypot(hx - p_aim.from_x, hz - p_aim.from_z) > p_rules.reach_m) {
        r_c.why = "够不着那儿";
        return;
    }
    r_c.ok = true;
}

} // namespace

PiecePlan plan_piece(const Structures &p_structures, const PieceDef &p_def, const PieceAim &p_aim, const LandingGround &p_ground, const PieceRules &p_rules) {
    const double G = GRID_M, H = STOREY_M;
    std::vector<Candidate> cands;
    std::vector<Structure> frames; // 与 cands 对应的那一座（新开的一座在这里造出原点）
    const auto &list = p_structures.get();
    for (int si = 0; si < int(list.size()); si++) {
        const Structure &s = list[size_t(si)];
        double lx, ly, lz;
        s.to_local(p_aim.x, p_aim.y, p_aim.z, lx, ly, lz);
        const int cx = int(std::floor(lx / G)), cz = int(std::floor(lz / G)), lv = int(std::floor(ly / H + 1e-6));
        // 先粗看这一座离准星远不远：最近的一件的外包盒
        double nearest = INF;
        for (const Piece &p : s.pieces) {
            double bx, by, bz, ex, ey, ez;
            piece_bounds(s, p, bx, by, bz, ex, ey, ez);
            nearest = std::min(nearest, box_distance(lx, ly, lz, bx, by, bz, ex, ey, ez));
        }
        if (nearest > G + p_rules.snap_m) {
            continue;
        }
        auto add = [&](const PieceSpot &p_spot, double p_penalty) {
            Candidate c;
            c.structure = si;
            c.spot = p_spot;
            double bx, by, bz, ex, ey, ez;
            spot_bounds(s, p_def.role, p_spot, -0.5, bx, by, bz, ex, ey, ez);
            c.sx = bx, c.sy = by, c.sz = bz;
            if (p_def.role == PieceRole::FOUNDATION) {
                c.sy = 0.0;
            }
            c.score = box_distance(lx, ly, lz, bx, by, bz, ex, ey, ez) + p_penalty;
            cands.push_back(c);
            frames.push_back(s);
        };
        for (int x = cx - 1; x <= cx + 1; x++) {
            for (int z = cz - 1; z <= cz + 1; z++) {
                for (int level = std::max(0, lv - 1); level <= std::min(piece_dims::MAX_LEVELS - 1, lv + 1); level++) {
                    PieceSpot sp;
                    sp.x = x, sp.z = z, sp.level = level;
                    switch (p_def.role) {
                        case PieceRole::FOUNDATION:
                            if (level == 0) {
                                sp.slot = PieceSlot::CELL;
                                add(sp, 0.0);
                            }
                            break;
                        case PieceRole::FLOOR:
                            if (level >= 1) {
                                sp.slot = PieceSlot::CELL;
                                add(sp, 0.0);
                            }
                            break;
                        case PieceRole::WALL:
                        case PieceRole::DOOR:
                        case PieceRole::WINDOW:
                        case PieceRole::GABLE:
                            sp.slot = PieceSlot::EDGE;
                            for (int dir = 0; dir < 2; dir++) {
                                sp.dir = dir;
                                add(sp, 0.0);
                            }
                            break;
                        case PieceRole::ROOF:
                            sp.slot = PieceSlot::CELL;
                            for (int tier = 0; tier < piece_dims::MAX_TIERS; tier++) {
                                sp.tier = tier;
                                sp.dir = (roof_default_dir(s, x, z, level, p_aim.look_yaw) + p_aim.turns % 4 + 4) % 4;
                                add(sp, s.has_floor(x, z, level) ? 0.0 : OUTSIDE_PENALTY_M);
                            }
                            break;
                        case PieceRole::PILLAR:
                            sp.slot = PieceSlot::CORNER;
                            add(sp, 0.0);
                            break;
                        case PieceRole::LADDER:
                            sp.slot = PieceSlot::CELL;
                            sp.dir = (ladder_default_dir(lx, lz, x, z) + p_aim.turns % 4 + 4) % 4;
                            add(sp, s.has_floor(x, z, level) ? 0.0 : OUTSIDE_PENALTY_M);
                            break;
                    }
                }
            }
        }
    }
    // 地基还能新开一座：准星那一点做格 (0, 0) 的中心，朝向按视线取整到 15°（再加 Q/R）
    if (p_def.role == PieceRole::FOUNDATION) {
        const double step = p_rules.new_turn_deg * TAU / 360.0;
        const double yaw = wrap_angle(std::round(p_aim.look_yaw / step) * step + p_aim.turns * step);
        Structure frame;
        frame.yaw = yaw;
        const double c = std::cos(yaw), s = std::sin(yaw);
        frame.x = p_aim.x - (c * 0.5 * G + s * 0.5 * G);
        frame.z = p_aim.z - (-s * 0.5 * G + c * 0.5 * G);
        frame.y = p_aim.y;
        Candidate cand;
        cand.structure = -1;
        cand.spot.slot = PieceSlot::CELL;
        cand.sx = 0.5 * G, cand.sy = 0.0, cand.sz = 0.5 * G;
        cand.score = NEW_PENALTY_M;
        // 地坪：格里最高的地面 + 露出的一截
        double hi = 0.0, lo = 0.0;
        std::string why;
        if (foundation_ground(frame, 0, 0, p_ground, hi, lo, why)) {
            frame.y = hi + p_rules.min_plinth_m;
        }
        // 离已有的一座（它的地基，水平量）不到 G + snap_m：只能接到它上面，不另开
        for (const Structure &other : list) {
            double lx, ly, lz;
            other.to_local(p_aim.x, p_aim.y, p_aim.z, lx, ly, lz);
            for (const Piece &p : other.pieces) {
                const PieceDef *def = pieces::find(p.item);
                if (def && def->role == PieceRole::FOUNDATION &&
                        box_distance(lx, 0.0, lz, (p.spot.x + 0.5) * G, 0.0, (p.spot.z + 0.5) * G, 0.5 * G, 1.0, 0.5 * G) < G + p_rules.snap_m) {
                    cand.why = "挨着已有的一座：对准它的地基边上接";
                }
            }
        }
        cands.push_back(cand);
        frames.push_back(frame);
    }

    PiecePlan out;
    if (cands.empty()) {
        out.why = p_def.role == PieceRole::FOUNDATION ? "对准地面摆" : "先有地基：对准一座的地基、墙或楼板";
        return out;
    }
    // 近的先评：分数一样时挑能摆的
    std::vector<size_t> order(cands.size());
    for (size_t k = 0; k < order.size(); k++) {
        order[k] = k;
    }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return cands[a].score < cands[b].score; });
    const Candidate *best_bad = nullptr;
    const Structure *best_bad_frame = nullptr;
    for (size_t k : order) {
        Candidate &c = cands[k];
        if (c.score > p_rules.snap_m && c.structure >= 0) {
            break;
        }
        if (c.why.empty()) {
            evaluate(frames[k], p_def, c, p_aim, p_ground, p_rules);
        }
        if (c.ok) {
            out.ok = true;
            out.located = true;
            out.structure = c.structure;
            out.frame = frames[k];
            out.spot = c.spot;
            out.bottom = c.bottom;
            frames[k].to_host(c.sx, c.sy, c.sz, out.snap_x, out.snap_y, out.snap_z);
            return out;
        }
        if (!best_bad) {
            best_bad = &c;
            best_bad_frame = &frames[k];
        }
    }
    if (best_bad) {
        out.located = true;
        out.why = best_bad->why;
        out.structure = best_bad->structure;
        out.frame = *best_bad_frame;
        out.spot = best_bad->spot;
        out.bottom = best_bad->bottom;
        best_bad_frame->to_host(best_bad->sx, best_bad->sy, best_bad->sz, out.snap_x, out.snap_y, out.snap_z);
    } else {
        out.why = "先有地基：对准一座的地基、墙或楼板";
    }
    return out;
}

// ---------------------------------------------------------------- 完整的屋

namespace {

using GridCell = std::pair<int, int>;

struct Level {
    std::set<GridCell> cells; // 地坪格（地基 / 楼板）+ 梯口格（下一层有梯子、这一层没楼板）
    std::map<GridCell, int> env; // 格 → 第几个连片
    std::vector<std::vector<GridCell>> envelopes;
    std::vector<char> sealed; // 连片封得住（墙、顶、山墙、没有缝；门不算）
    std::vector<HouseRoom> rooms;
};

} // namespace

HouseReport check_house(const Structure &p_s) {
    HouseReport report;
    std::vector<Level> levels(size_t(piece_dims::MAX_LEVELS) + 1);
    for (int L = 0; L < piece_dims::MAX_LEVELS; L++) {
        Level &lv = levels[size_t(L)];
        for (const Piece &p : p_s.pieces) {
            const PieceDef *def = pieces::find(p.item);
            if (!def || p.spot.slot != PieceSlot::CELL) {
                continue;
            }
            if ((def->role == PieceRole::FOUNDATION || def->role == PieceRole::FLOOR) && p.spot.level == L) {
                lv.cells.insert({ p.spot.x, p.spot.z });
            }
            if (def->role == PieceRole::LADDER && p.spot.level == L - 1 && !p_s.has_floor(p.spot.x, p.spot.z, L)) {
                lv.cells.insert({ p.spot.x, p.spot.z });
            }
        }
        // 连片：只要挨着就算一片（屋里隔墙不分开：看的是整座封不封得住）
        for (const GridCell &c : lv.cells) {
            if (lv.env.count(c)) {
                continue;
            }
            const int id = int(lv.envelopes.size());
            lv.envelopes.push_back({});
            std::vector<GridCell> stack{ c };
            lv.env[c] = id;
            while (!stack.empty()) {
                const GridCell cur = stack.back();
                stack.pop_back();
                lv.envelopes.back().push_back(cur);
                for (const auto &nb : NB) {
                    const GridCell n{ cur.first + nb[0], cur.second + nb[1] };
                    if (lv.cells.count(n) && !lv.env.count(n)) {
                        lv.env[n] = id;
                        stack.push_back(n);
                    }
                }
            }
        }
    }
    // 从上往下：上一层封得住，下一层头顶有楼板的格才算有顶
    for (int L = piece_dims::MAX_LEVELS - 1; L >= 0; L--) {
        Level &lv = levels[size_t(L)];
        const Level &up = levels[size_t(L) + 1];
        lv.sealed.assign(lv.envelopes.size(), 0);
        for (size_t e = 0; e < lv.envelopes.size(); e++) {
            HouseRoom room;
            room.level = L;
            room.cells = lv.envelopes[e];
            std::set<GridCell> members(room.cells.begin(), room.cells.end());
            for (const GridCell &c : room.cells) {
                const Piece *roof = p_s.roof_at(c.first, c.second, L);
                bool covered = roof != nullptr;
                if (!covered && L + 1 < piece_dims::MAX_LEVELS) {
                    const auto found = up.env.find(c);
                    covered = found != up.env.end() && up.sealed[size_t(found->second)];
                }
                room.missing_roof += covered ? 0 : 1;
                for (int side = 0; side < 4; side++) {
                    const EdgeRef edge = cell_edge(c.first, c.second, side);
                    const GridCell n{ c.first + NB[side][0], c.second + NB[side][1] };
                    const Piece *wall = p_s.wall_at(edge.x, edge.z, edge.dir, L);
                    if (!members.count(n)) {
                        // 屋边：要墙；墙顶到坡顶有缝要山墙
                        if (!wall) {
                            room.missing_walls++;
                        } else if (pieces::find(wall->item)->role == PieceRole::DOOR) {
                            room.doors++;
                        }
                        double h0 = 0.0, h1 = 0.0;
                        if (roof && roof_edge_heights(*roof, edge, h0, h1) && std::max(h0, h1) > 1e-6 && !p_s.gable_at(edge.x, edge.z, edge.dir, L)) {
                            room.missing_gables++;
                        }
                    } else if (side == 1 || side == 3) {
                        // 屋里挨着的两格（每条边只看一次）：都有坡顶就要接得上，接不上要有墙加山墙补
                        const Piece *other = p_s.roof_at(n.first, n.second, L);
                        double a0 = 0.0, a1 = 0.0, b0 = 0.0, b1 = 0.0;
                        if (roof && other && roof_edge_heights(*roof, edge, a0, a1) && roof_edge_heights(*other, edge, b0, b1) &&
                                (std::abs(a0 - b0) > 1e-6 || std::abs(a1 - b1) > 1e-6) && !(wall && p_s.gable_at(edge.x, edge.z, edge.dir, L))) {
                            room.roof_gaps++;
                        }
                    }
                }
            }
            room.enclosed = room.missing_walls == 0;
            lv.sealed[e] = room.enclosed && room.missing_roof == 0 && room.roof_gaps == 0 && room.missing_gables == 0;
            lv.rooms.push_back(room);
        }
    }
    // 报：底层的每一片是一座屋；楼上的片只看封不封得住（门在底层）
    const HouseRoom *best = nullptr;
    int best_missing = 1 << 30;
    for (int L = 0; L < piece_dims::MAX_LEVELS; L++) {
        for (const HouseRoom &room : levels[size_t(L)].rooms) {
            report.rooms.push_back(room);
            if (L != 0) {
                continue;
            }
            if (room.complete()) {
                report.complete = true;
                report.complete_rooms++;
            }
            const int missing = room.missing_walls + room.missing_roof + room.roof_gaps + room.missing_gables + (room.doors > 0 ? 0 : 1);
            if (missing < best_missing) {
                best_missing = missing;
                best = &room;
            }
        }
    }
    if (!report.complete) {
        if (!best) {
            report.missing = "先铺地基";
        } else {
            std::vector<std::string> parts;
            if (best->missing_walls > 0) {
                parts.push_back("墙 " + std::to_string(best->missing_walls) + " 面");
            }
            if (best->missing_roof > 0) {
                parts.push_back("顶 " + std::to_string(best->missing_roof) + " 格");
            }
            if (best->roof_gaps > 0) {
                parts.push_back("顶接缝 " + std::to_string(best->roof_gaps) + " 处");
            }
            if (best->missing_gables > 0) {
                parts.push_back("山墙 " + std::to_string(best->missing_gables) + " 面");
            }
            if (best->doors == 0) {
                parts.push_back("门");
            }
            for (size_t k = 0; k < parts.size(); k++) {
                report.missing += (k > 0 ? "、" : "") + parts[k];
            }
        }
    }
    return report;
}

bool inside_complete_room(const Structure &p_s, const HouseReport &p_report, double p_lx, double p_ly, double p_lz) {
    if (!p_report.complete || p_ly < -0.5) {
        return false;
    }
    const int x = int(std::floor(p_lx / GRID_M)), z = int(std::floor(p_lz / GRID_M));
    const int level = std::clamp(int(std::floor((p_ly + 0.3) / STOREY_M)), 0, piece_dims::MAX_LEVELS - 1);
    for (const HouseRoom &room : p_report.rooms) {
        if (room.level != level) {
            continue;
        }
        const bool here = std::find(room.cells.begin(), room.cells.end(), std::make_pair(x, z)) != room.cells.end();
        if (!here) {
            continue;
        }
        if (level == 0) {
            return room.complete();
        }
        return room.enclosed && room.missing_roof == 0 && room.roof_gaps == 0 && room.missing_gables == 0;
    }
    return false;
}
