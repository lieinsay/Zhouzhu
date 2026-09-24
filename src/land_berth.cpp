#include "land_berth.h"

#include <algorithm>
#include <cmath>

namespace land_berth {

namespace {

constexpr double BLOCK_ALONG = 0.8; // 坞墩沿船身的厚
constexpr double BLOCK_ACROSS = 6.0; // 横跨船体（船体宽 5.2）
constexpr double GANGWAY_T = 0.25;
constexpr double BOLLARD = 0.35, BOLLARD_H = 1.1;

BoxPart part(double p_sx, double p_sy, double p_sz, double p_x, double p_y, double p_z, PartMaterial p_mat, bool p_solid) {
    BoxPart b;
    b.sx = p_sx, b.sy = p_sy, b.sz = p_sz;
    b.x = p_x, b.y = p_y, b.z = p_z;
    b.mat = p_mat;
    b.visual = true;
    b.solid = p_solid;
    b.group = 0;
    return b;
}

} // namespace

void add_parts(const BuildingDef &p_def, std::vector<BoxPart> &r_boxes) {
    // 坞墩：立在格与格的交界处（舷门格中心 ± 3.5 m 起每 7 m 一块），托着船体底，避开浮石
    const double block_h = LIFT_M - HULL_DROP_M, keel_z = CONTACT_Z - 0.5 * CELL_WIDTH_M;
    for (double x = 0.5 * CELL_LENGTH_M; x < 0.5 * p_def.width_m - 1.0; x += CELL_LENGTH_M) {
        for (int side = -1; side <= 1; side += 2) {
            r_boxes.push_back(part(BLOCK_ALONG, block_h, BLOCK_ACROSS, side * x, 0.5 * block_h, keel_z, PartMaterial::DARK_WOOD, true));
        }
    }
    // 跳板：从台面前部斜上到接触线（甲板边缘）
    const double run = GANGWAY_RUN_M, rise = LIFT_M, len = std::hypot(run, rise);
    const double nz = rise / len, ny = run / len; // 板面朝上的法线
    BoxPart plank = part(GANGWAY_W, GANGWAY_T, len, 0.0, 0.5 * rise - ny * 0.5 * GANGWAY_T, CONTACT_Z + 0.5 * run - nz * 0.5 * GANGWAY_T, PartMaterial::WOOD, true);
    plank.pitch = std::atan2(rise, run);
    r_boxes.push_back(plank);
    // 跳板两侧的扶栏（只画）
    for (int side = -1; side <= 1; side += 2) {
        BoxPart rail = part(0.08, 0.08, len, side * 0.5 * GANGWAY_W, 0.5 * rise + 0.9 * ny, CONTACT_Z + 0.5 * run + 0.9 * nz, PartMaterial::DARK_WOOD, false);
        rail.pitch = plank.pitch;
        r_boxes.push_back(rail);
    }
    // 四角系船桩
    const double hx = 0.5 * p_def.width_m - 0.6, hz = 0.5 * p_def.depth_m - 0.6;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            r_boxes.push_back(part(BOLLARD, BOLLARD_H, BOLLARD, sx * hx, 0.5 * BOLLARD_H, sz * hz, PartMaterial::DARK_WOOD, true));
        }
    }
}

std::string ship_problem(const ShipLayout &p_layout) {
    const double width = (p_layout.max_x() - p_layout.min_x() + 1) * CELL_WIDTH_M;
    if (width > MAX_SHIP_WIDTH_M) {
        return "船太宽，船台只停得下一列宽的船";
    }
    const double reach = std::max(-bow_z(p_layout), stern_z(p_layout));
    if (reach > MAX_HALF_LENGTH_M) {
        return "船太长，船头船尾伸出船台了";
    }
    return std::string();
}

} // namespace land_berth
