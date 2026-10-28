#pragma once

#include <functional>
#include <vector>

#include "house_template.h"

// 一座建筑的几何：外壳 + 房屋模板 → 一堆盒子（和几个爬梯区域）。不含 Godot 头，无状态。
//
// 坐标是建筑自己的局部坐标（原点在地坪中心，+z 是正面，见 house_template.h）；上层（Settlement）把它们合成网格与碰撞。
// 拼法：
//   · 台基：埋到 −plinth_h、露出地坪一圈 PLINTH_MARGIN。
//   · 外门前的坡道：从台基边往外下到地面（地面高由上层给的 ground_drop 量），碰撞是一块斜板，看上去是几级台阶。
//   · 墙：按层切开，每层每面墙在那层的门窗处开洞（窗下留窗台、洞上留过梁）；门框、窗棂只画不挡。
//   · 楼板：底层画一层木地板（只画）；楼上每层一块楼板，扣掉下层梯子的梯口。
//   · 隔墙：扣掉门洞，门洞上留过梁。
//   · 天花板：檐口高处一整块，带碰撞——人在屋里时第三人称相机的弹簧臂顶到它就停在屋里。屋顶（悬山）由上层照外壳尺寸画。
//   · 家具：几块小盒子拼个样子（只画）+ 一块占地大小的碰撞（挡人的那几种）。
//   · 梯子：一块梯板（挡人，人贴着它爬）+ 几根横档 + 一个爬梯区域（与船上的梯子同一套：区域的 +z 指向人站的那侧）。
// 不是房屋的（船台）：台基 + 前沿一道坡道 + land_berth 的台面陈设，只有地面一组（外加空的顶组）；模板不看。
// 每个盒子带分组：0 = 地面上的（台基、坡道），1 + L = 第 L 层的（那层的墙、楼板、隔墙、家具、从那层起的梯子），最后一组 = 顶（天花板、屋顶）。
// 分组给编辑器剖开看用：编辑第 L 层时藏起顶和 L 层以上。

enum class PartMaterial {
    STONE, // 台基、台阶
    WALL, // 夯土墙、隔墙
    FLOOR, // 木地板、楼板
    WOOD, // 家具、梯子
    DARK_WOOD, // 门框、窗棂、天花板
    ROOF,
    CLAY, // 灶、缸
    CLOTH, // 被褥、麻袋、织物
    STRAW, // 席、粮囤
    GRAIN,
    LACQUER, // 鼓、供桌上的朱漆
    BRICK, // 砖墙（搭建件）
    METAL, // 现有器物的铰链、箍和固定件；只影响美术表现。
    LEATHER, // 鼓皮、皮革附件。
    THATCH, // 茅草屋面，与编织席面分开。
    COUNT,
};

enum class VisualShape { BOX, BEVEL_BOX, VAT, BIN, SACK, DRUM, DISC_Z, RING_Y, RING_Z, ELLIPSOID, ROLLER_X, TILE_EDGE, HULL };

struct BoxPart {
    double sx = 1.0, sy = 1.0, sz = 1.0; // 尺寸（转之前）
    double x = 0.0, y = 0.0, z = 0.0; // 中心
    double yaw = 0.0; // 先绕 y 转（与 Godot 的 Basis(Y, yaw) 同）
    double pitch = 0.0; // 再绕自己的 x 转（坡道用：正值让 +z 那头往下）
    PartMaterial mat = PartMaterial::WALL;
    bool visual = true;
    bool solid = true;
    int group = 0;
    VisualShape shape = VisualShape::BOX; // 仅画面形体；碰撞与占地仍使用原定义。
    bool furniture_visual=false;
    std::string model_id; // 仅非可见/非碰撞的引用标记使用，x/y/z/yaw是现有模板的实体锚点。
};

struct LadderZone {
    double x = 0.0, y = 0.0, z = 0.0; // 区域中心
    double sx = 1.0, sy = 1.0, sz = 1.0;
    double yaw = 0.0; // 区域的 +z = 人站的那侧
    int group = 0;
    double foot_x = 0.0, foot_y = 0.0, foot_z = 0.0; // 梯脚站人处（给测试走过去）
    double top_x = 0.0, top_y = 0.0, top_z = 0.0; // 上去之后的落脚处
};

// 外门：门外坡道脚下的一点（地面高）与门里的一点（地坪高）
struct DoorEntry {
    WallSide side = WallSide::FRONT;
    double out_x = 0.0, out_y = 0.0, out_z = 0.0;
    double in_x = 0.0, in_y = 0.0, in_z = 0.0;
};

struct BuildingParts {
    std::vector<BoxPart> boxes;
    std::vector<LadderZone> ladders;
    std::vector<DoorEntry> doors;
    int group_count = 0; // 组数：地面 + 各层 + 顶
    int roof_group() const { return group_count - 1; }
};

namespace house_dims {
constexpr double PLINTH_MARGIN = 0.6; // 台基比墙脚往外多这么多
constexpr double ROOF_OVERHANG = 0.9; // 出檐
constexpr double RAMP_SLOPE_DEG = 30.0; // 坡道一般的坡度
constexpr double RAMP_MAX_RUN = 4.5; // 坡道再长就改陡（最陡 RAMP_STEEP_DEG）
constexpr double RAMP_STEEP_DEG = 40.0;
constexpr double STEP_RISE = 0.18; // 画出来的每级台阶多高
} // namespace house_dims

// 局部坐标 (x, z) 处，地坪比地面高多少（正数）；没有地面返回 NaN
using GroundDrop = std::function<double(double, double)>;

BuildingParts building_parts(const BuildingDef &p_def, const HouseTemplate &p_template, double p_plinth_h, const GroundDrop &p_ground_drop);

// 一件家具的样子（局部坐标，已按朝向转好、落在 p_base_y 那层）：画的盒子，挡人的那几种再加一块占地大小的碰撞
void furniture_parts(const Furnishing &p_item, double p_base_y, int p_group, std::vector<BoxPart> &r_boxes);

// 点在不在盒子里（算上转角）
bool part_contains(const BoxPart &p_part, double p_x, double p_y, double p_z, double p_margin = 0.0);
