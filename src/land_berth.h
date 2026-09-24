#pragma once

#include <string>
#include <vector>

#include "building_parts.h"
#include "ship_layout.h"

// 船台：村里让空艇落地靠港的那种建筑（建筑表里的 "berth"，不是房屋：没有墙、顶、屋里，村民不住不串门）。不含 Godot 头，无状态。
//
// 局部坐标与别的建筑一样：原点在台面中心（地坪），x 沿面阔（船身方向），+z 是正面。
// 台面前沿当中一道坡道下到地面（人从这儿上下）；台面上一道跳板从前面斜上去，接到船的舷门；
// 跳板顶所在的线就是接触线——船把朝跳板那一侧的舷边贴上去（与船坞的坞位一样），船身往台面后半边伸，坐在一排坞墩上。
// 坞墩立在格与格的交界处，不顶着动力区吊着的浮石。只停得下一列宽、船头船尾离舷门各不超过 MAX_HALF_LENGTH_M 的船。

namespace land_berth {

constexpr double LIFT_M = 5.3; // 停着的船：甲板面在台面之上多高（船体底 1.8 m、浮石底 0.2 m 离台面）
constexpr double GANGWAY_RUN_M = 9.0; // 跳板的水平投影长（约 30°）
constexpr double CONTACT_Z = -1.5; // 接触线（跳板顶）在进深上的位置；船身在它后面（−z）一格宽
constexpr double HULL_DROP_M = 3.5; // 甲板面到船体底（甲板 0.7 + 船体 2.8）
constexpr double GANGWAY_W = 1.8;
constexpr double MAX_SHIP_WIDTH_M = 7.5; // 一列宽
constexpr double MAX_HALF_LENGTH_M = 22.0; // 台面面阔的一半
constexpr double RAMP_W = 2.4; // 前沿坡道的宽
constexpr double POST_Z = 8.0; // 跳板脚前那一点（村仓在这儿接货交货），台面上
constexpr double PILE_X = 4.0, PILE_Z = 6.0; // 货堆摆在跳板脚边

// 台面上的东西：坞墩、跳板、四角的系船桩（加进 p_boxes；台基与前沿坡道由 building_parts 照常拼）
void add_parts(const BuildingDef &p_def, std::vector<BoxPart> &r_boxes);

// 这条船停得下吗：空串 = 停得下，否则原因
std::string ship_problem(const ShipLayout &p_layout);

} // namespace land_berth
