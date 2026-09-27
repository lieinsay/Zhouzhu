#include "piece_mesh.h"

#include <algorithm>
#include <cmath>

#include "house_template.h"
#include "mesh_kit.h"

using namespace mesh_kit;
using namespace piece_dims;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double FRAME_W = 0.08; // 门框、窗框
constexpr double LEAF_T = 0.06;
constexpr double BAR_W = 0.05; // 窗棂
constexpr double LADDER_INSET = 0.2; // 梯板离靠着的那条边多远
constexpr double RUNG_STEP = 0.35;
constexpr double LADDER_BOARD_T = 0.08;
constexpr double LADDER_ZONE_OVER = 1.2; // 与房子里的梯子同：区域超出上一层楼面这么高
constexpr double LADDER_ZONE_DEPTH = 1.0;
constexpr double RIDGE_H = 0.18, RIDGE_D = 0.32;
constexpr double RAMP_W = 1.4; // 门外台阶多宽
constexpr double RAMP_SLOPE_DEG = 30.0; // 斜板多陡（人走得上去：Player 的 floor_max_angle 45°）
constexpr double RAMP_MAX_RUN = 2.4;
constexpr double STEP_RISE = 0.18; // 画出来的每级台阶多高
constexpr double RAMP_T = 0.12; // 斜板（碰撞）多厚

struct Out {
    StructureMesh &mesh;

    void box(PartMaterial p_mat, const Xform &p_xf, const V3 &p_size, bool p_solid) {
        append_box(mesh.parts[size_t(p_mat)], p_xf, p_size);
        if (p_solid) {
            append_box_faces(mesh.collision, p_xf, p_size);
        }
    }
    // 轴对齐的盒子：从 lo 到 hi（局部坐标）
    void span(PartMaterial p_mat, const V3 &p_lo, const V3 &p_hi, bool p_solid) {
        const V3 c = (p_lo + p_hi) * 0.5;
        const V3 s{ std::abs(p_hi.x - p_lo.x), std::abs(p_hi.y - p_lo.y), std::abs(p_hi.z - p_lo.z) };
        if (s.x <= 1e-6 || s.y <= 1e-6 || s.z <= 1e-6) {
            return;
        }
        box(p_mat, Xform(0.0, 0.0, c), s, p_solid);
    }
};

// 边上的坐标：a 沿边（0 在边的中点）、n 沿边的法线（dir 0 沿 +z、dir 1 沿 +x）、y 就是 y
struct EdgeFrame {
    int dir = 0;
    double cx = 0.0, cz = 0.0; // 边的中点

    V3 at(double p_a, double p_y, double p_n) const {
        return dir == 0 ? V3{ cx + p_a, p_y, cz + p_n } : V3{ cx + p_n, p_y, cz + p_a };
    }
    // 边坐标里的轴对齐盒子
    void span(Out &r_out, PartMaterial p_mat, double p_a0, double p_a1, double p_y0, double p_y1, double p_n0, double p_n1, bool p_solid) const {
        r_out.span(p_mat, at(std::min(p_a0, p_a1), std::min(p_y0, p_y1), std::min(p_n0, p_n1)), at(std::max(p_a0, p_a1), std::max(p_y0, p_y1), std::max(p_n0, p_n1)), p_solid);
    }
};

EdgeFrame edge_frame(const PieceSpot &p_spot) {
    EdgeFrame f;
    f.dir = p_spot.dir;
    if (p_spot.dir == 0) {
        f.cx = (p_spot.x + 0.5) * GRID_M, f.cz = p_spot.z * GRID_M;
    } else {
        f.cx = p_spot.x * GRID_M, f.cz = (p_spot.z + 0.5) * GRID_M;
    }
    return f;
}

// 坡顶往哪边低（dir）→ yaw（局部 +z 转到那边）
double dir_yaw(int p_dir) {
    return ((p_dir % 4) + 4) % 4 * 0.5 * PI;
}

void dir_vector(int p_dir, int &r_dx, int &r_dz) {
    static const int DX[4] = { 0, 1, 0, -1 }, DZ[4] = { 1, 0, -1, 0 };
    const int d = ((p_dir % 4) + 4) % 4;
    r_dx = DX[d];
    r_dz = DZ[d];
}

// 门外的台阶：从门槛（地坪、墙外皮）往外下到 −drop；画几级石阶，碰撞是一块斜板（人不会自己上坎）
void append_door_ramp(Out &r_out, const EdgeFrame &p_f, int p_out, double p_drop) {
    const double t = 0.5 * WALL_T, w = 0.5 * RAMP_W;
    const double run = std::clamp(p_drop / std::tan(RAMP_SLOPE_DEG * PI / 180.0), 0.35, RAMP_MAX_RUN);
    const int steps = std::max(1, int(std::ceil(p_drop / STEP_RISE)));
    const double rise = p_drop / (steps + 1), tread = run / steps;
    for (int k = 1; k <= steps; k++) {
        const double n0 = t + (k - 1) * tread;
        p_f.span(r_out, PartMaterial::STONE, -w, w, -p_drop - 0.1, -k * rise, p_out * n0, p_out * (t + run), false);
    }
    // 斜板：顶面贴着台阶的前沿线，从 (n = t, y = 0) 到 (n = t + run, y = −drop)
    const V3 a = p_f.at(0.0, 0.0, p_out * t), b = p_f.at(0.0, -p_drop, p_out * (t + run));
    const V3 dir = p_f.dir == 0 ? V3{ 0, 0, double(p_out) } : V3{ double(p_out), 0, 0 };
    const double yaw = std::atan2(dir.x, dir.z), pitch = std::atan2(p_drop, run);
    const V3 up{ dir.x * std::sin(pitch), std::cos(pitch), dir.z * std::sin(pitch) };
    const V3 center = (a + b) * 0.5 - up * (0.5 * RAMP_T);
    const Xform xf(yaw, pitch, center);
    append_box_faces(r_out.mesh.collision, xf, { RAMP_W, RAMP_T, std::sqrt(run * run + p_drop * p_drop) });
}

void append_wall(Out &r_out, const PieceDef &p_def, const Structure &p_s, const Piece &p_piece) {
    const EdgeFrame f = edge_frame(p_piece.spot);
    const double y0 = p_piece.spot.level * STOREY_M, y1 = y0 + STOREY_M;
    const double half = 0.5 * (GRID_M + WALL_T), t = 0.5 * WALL_T;
    const PartMaterial mat = p_def.material;
    switch (p_def.role) {
        case PieceRole::DOOR: {
            const double dw = 0.5 * DOOR_W, top = y0 + DOOR_H;
            f.span(r_out, mat, -half, -dw, y0, y1, -t, t, true);
            f.span(r_out, mat, dw, half, y0, y1, -t, t, true);
            f.span(r_out, mat, -dw, dw, top, y1, -t, t, true);
            // 门框（只画）
            f.span(r_out, PartMaterial::DARK_WOOD, -dw - FRAME_W, -dw, y0, top, -t - 0.02, t + 0.02, false);
            f.span(r_out, PartMaterial::DARK_WOOD, dw, dw + FRAME_W, y0, top, -t - 0.02, t + 0.02, false);
            f.span(r_out, PartMaterial::DARK_WOOD, -dw - FRAME_W, dw + FRAME_W, top, top + FRAME_W, -t - 0.02, t + 0.02, false);
            // 门扇：合页在 −a 那头；开着就转 90° 贴到法线正 / 反那面
            const double gap = 0.02, leaf_h = DOOR_H - 0.04;
            if (p_piece.open == 0) {
                f.span(r_out, PartMaterial::WOOD, -dw + gap, dw - gap, y0 + 0.02, y0 + leaf_h, -0.5 * LEAF_T, 0.5 * LEAF_T, true);
            } else {
                const double s = p_piece.open > 0 ? 1.0 : -1.0;
                f.span(r_out, PartMaterial::WOOD, -dw + gap, -dw + gap + LEAF_T, y0 + 0.02, y0 + leaf_h, s * (t + 0.01), s * (t + DOOR_W - 2.0 * gap), true);
            }
            const int out = door_outside(p_s, p_piece.spot);
            if (p_piece.spot.level == 0 && out != 0 && p_piece.bottom < -0.05) {
                append_door_ramp(r_out, f, out, -p_piece.bottom);
            }
            return;
        }
        case PieceRole::WINDOW: {
            const double ww = 0.5 * WINDOW_W, sill = y0 + WINDOW_SILL, head = sill + WINDOW_H;
            f.span(r_out, mat, -half, half, y0, sill, -t, t, true);
            f.span(r_out, mat, -half, half, head, y1, -t, t, true);
            f.span(r_out, mat, -half, -ww, sill, head, -t, t, true);
            f.span(r_out, mat, ww, half, sill, head, -t, t, true);
            // 窗框与窗棂（只画）
            f.span(r_out, PartMaterial::DARK_WOOD, -ww, ww, sill - 0.04, sill, -t - 0.03, t + 0.03, false);
            for (double a : { -WINDOW_W / 6.0, WINDOW_W / 6.0 }) {
                f.span(r_out, PartMaterial::DARK_WOOD, a - 0.5 * BAR_W, a + 0.5 * BAR_W, sill, head, -0.03, 0.03, false);
            }
            f.span(r_out, PartMaterial::DARK_WOOD, -ww, ww, 0.5 * (sill + head) - 0.5 * BAR_W, 0.5 * (sill + head) + 0.5 * BAR_W, -0.03, 0.03, false);
            return;
        }
        default:
            f.span(r_out, mat, -half, half, y0, y1, -t, t, true);
            return;
    }
}

void append_gable_piece(Out &r_out, const Structure &p_s, const Piece &p_piece) {
    double h0 = 0.0, h1 = 0.0;
    if (!gable_shape(p_s, { p_piece.spot.x, p_piece.spot.z, p_piece.spot.dir }, p_piece.spot.level, h0, h1)) {
        h0 = ROOF_RISE_M;
        h1 = 0.0; // 坡顶还没有（不该出现：山墙靠坡顶撑着）：画个三角
    }
    const EdgeFrame f = edge_frame(p_piece.spot);
    const double ye = (p_piece.spot.level + 1) * STOREY_M, a = 0.5 * GRID_M;
    std::vector<V3> pts{ f.at(-a, ye, 0.0), f.at(a, ye, 0.0) };
    if (h1 > 1e-6) {
        pts.push_back(f.at(a, ye + h1, 0.0));
    }
    if (h0 > 1e-6) {
        pts.push_back(f.at(-a, ye + h0, 0.0));
    }
    const V3 normal = p_piece.spot.dir == 0 ? V3{ 0, 0, 1 } : V3{ 1, 0, 0 };
    append_slab(r_out.mesh.parts[size_t(PartMaterial::WALL)], &r_out.mesh.collision, pts, normal, WALL_T);
}

void append_roof(Out &r_out, const PieceDef &p_def, const Structure &p_s, const Piece &p_piece) {
    const PieceSpot &sp = p_piece.spot;
    const double G = GRID_M, R = ROOF_RISE_M, ye = (sp.level + 1) * STOREY_M;
    const double theta = std::atan2(R, G), ct = std::cos(theta), st = std::sin(theta);
    int dx = 0, dz = 0;
    dir_vector(sp.dir, dx, dz);
    // 出檐：最下一阶、檐口那边没挨着别的坡顶；出际：两头（垂直于坡的方向）没挨着别的坡顶
    const double eave = sp.tier == 0 && !p_s.roof_at(sp.x + dx, sp.z + dz, sp.level) ? EAVE_M : 0.0;
    // 屋脊方向 +x_r = (cos yaw, −sin yaw)：dir 0 → +x，dir 1 → −z，dir 2 → −x，dir 3 → +z
    const int rx = dz, rz = -dx;
    const double g_pos = p_s.roof_at(sp.x + rx, sp.z + rz, sp.level) ? 0.0 : GABLE_EAVE_M; // +x_r 那头
    const double g_neg = p_s.roof_at(sp.x - rx, sp.z - rz, sp.level) ? 0.0 : GABLE_EAVE_M;
    // 坡顶自己的坐标：原点在格心，+z_r 朝檐口，底面在 z_r = −G/2 高 (t+1)R、z_r = +G/2 高 tR
    const double zc = 0.5 * eave, xc = 0.5 * (g_pos - g_neg);
    const double y_under = ye + (sp.tier + 0.5) * R - zc * (R / G);
    const double len = (G + eave) / ct, width = G + g_pos + g_neg;
    const double yaw = dir_yaw(sp.dir);
    const Xform turn(yaw, 0.0, { (sp.x + 0.5) * G, 0.0, (sp.z + 0.5) * G });
    const V3 center = turn.xform({ xc, 0.0, zc + st * 0.5 * ROOF_T }) + V3{ 0.0, y_under + ct * 0.5 * ROOF_T, 0.0 };
    r_out.box(p_def.material, Xform(yaw, theta, center), { width, ROOF_T, len }, true);
    // 屋脊：高的那条边对面有同一阶反着坡的，补一道（只由往 +z / +x 低的那块出，免得两块各画一道）
    const Piece *other = p_s.roof_at(sp.x - dx, sp.z - dz, sp.level);
    if (other && other->spot.tier == sp.tier && ((other->spot.dir - sp.dir) % 4 + 4) % 4 == 2 && (sp.dir == 0 || sp.dir == 1)) {
        const V3 ridge = turn.xform({ xc, 0.0, -0.5 * G }) + V3{ 0.0, ye + (sp.tier + 1) * R + 0.5 * ROOF_T + 0.5 * RIDGE_H - 0.04, 0.0 };
        r_out.box(p_def.material, Xform(yaw, 0.0, ridge), { width + 0.1, RIDGE_H, RIDGE_D }, false);
    }
}

void append_ladder(Out &r_out, StructureMesh &r_mesh, const Piece &p_piece) {
    const PieceSpot &sp = p_piece.spot;
    const int quarter = ((sp.dir + 2) % 4 + 4) % 4; // 人站在靠着的那边的对面
    double ox = 0.0, oz = 0.0;
    ladder_outward(quarter, ox, oz);
    const double yaw = quarter * 0.5 * PI;
    const double cx = (sp.x + 0.5) * GRID_M, cz = (sp.z + 0.5) * GRID_M;
    const double bx = cx - ox * (0.5 * GRID_M - LADDER_INSET), bz = cz - oz * (0.5 * GRID_M - LADDER_INSET);
    const double y0 = sp.level * STOREY_M, rise = STOREY_M;
    auto at = [&](double p_along, double p_out, double &r_x, double &r_z) {
        r_x = bx + p_along * oz + p_out * ox;
        r_z = bz - p_along * ox + p_out * oz;
    };
    r_out.box(PartMaterial::WOOD, Xform(yaw, 0.0, { bx, y0 + 0.5 * rise, bz }), { house_dims::LADDER_W, rise, LADDER_BOARD_T }, true);
    for (int k = 0; k * RUNG_STEP + 0.3 < rise; k++) {
        double x = 0.0, z = 0.0;
        at(0.0, 0.1, x, z);
        r_out.box(PartMaterial::DARK_WOOD, Xform(yaw, 0.0, { x, y0 + 0.3 + k * RUNG_STEP, z }), { house_dims::LADDER_W + 0.1, 0.05, 0.12 }, false);
    }
    LadderZone zone;
    zone.sx = house_dims::LADDER_W + 0.6;
    zone.sy = rise + LADDER_ZONE_OVER;
    zone.sz = LADDER_ZONE_DEPTH;
    at(0.0, 0.5 * LADDER_ZONE_DEPTH, zone.x, zone.z);
    zone.y = y0 + 0.5 * zone.sy;
    zone.yaw = yaw;
    at(0.0, 0.6, zone.foot_x, zone.foot_z);
    zone.foot_y = y0;
    at(0.0, -0.6, zone.top_x, zone.top_z);
    zone.top_y = y0 + rise;
    r_mesh.ladders.push_back(zone);
}

void append_piece(StructureMesh &r_mesh, const Structure &p_s, const Piece &p_piece) {
    const PieceDef *def = pieces::find(p_piece.item);
    if (!def) {
        return;
    }
    Out out{ r_mesh };
    const PieceSpot &sp = p_piece.spot;
    const double G = GRID_M, H = STOREY_M;
    switch (def->role) {
        case PieceRole::FOUNDATION:
            out.span(PartMaterial::STONE, { sp.x * G, std::min(p_piece.bottom, -0.1), sp.z * G }, { (sp.x + 1) * G, 0.0, (sp.z + 1) * G }, true);
            return;
        case PieceRole::FLOOR:
            out.span(PartMaterial::FLOOR, { sp.x * G, sp.level * H - FLOOR_T, sp.z * G }, { (sp.x + 1) * G, sp.level * H, (sp.z + 1) * G }, true);
            return;
        case PieceRole::WALL:
        case PieceRole::DOOR:
        case PieceRole::WINDOW:
            append_wall(out, *def, p_s, p_piece);
            return;
        case PieceRole::GABLE:
            append_gable_piece(out, p_s, p_piece);
            return;
        case PieceRole::ROOF:
            append_roof(out, *def, p_s, p_piece);
            return;
        case PieceRole::PILLAR: {
            const double w = 0.5 * PILLAR_W;
            out.span(PartMaterial::DARK_WOOD, { sp.x * G - w, sp.level * H, sp.z * G - w }, { sp.x * G + w, (sp.level + 1) * H, sp.z * G + w }, true);
            return;
        }
        case PieceRole::LADDER:
            append_ladder(out, r_mesh, p_piece);
            return;
    }
}

} // namespace

int StructureMesh::triangle_count() const {
    size_t n = 0;
    for (const MeshPart &part : parts) {
        n += part.indices.size() / 3;
    }
    return int(n);
}

StructureMesh build_structure_mesh(const Structure &p_structure) {
    StructureMesh out;
    for (const Piece &p : p_structure.pieces) {
        append_piece(out, p_structure, p);
    }
    return out;
}

StructureMesh build_piece_mesh(const Structure &p_structure, const Piece &p_piece) {
    StructureMesh out;
    append_piece(out, p_structure, p_piece);
    return out;
}
