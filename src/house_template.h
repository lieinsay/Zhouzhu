#pragma once

#include <string>
#include <vector>

#include "buildings.h"

// 房屋模板：一种外壳（BuildingDef）里面长什么样。不含 Godot 头。
//
// 外壳管占地、台基、墙高、分几层；模板管：外墙上哪儿开门开窗、屋里的隔墙和隔墙上的门洞、家具摆在哪、层与层之间的梯子。
// 同一种外壳可以有好几套模板（民居有三间、织户、农户……），清单里一座建筑写 "house/zhihu@…" 就用那一套，
// 只写 "house@…" 就按位置自动挑一套（见 TemplateLibrary），所以自动铺的村子也家家不一样。
//
// 坐标：建筑自己的局部坐标，原点在地坪中心，x 沿面阔、z 沿进深、+z 是正面（门那一面），与 settlement_model 的 u / v 同。
// 屋内净空：|x| ≤ width/2 − WALL_T，|z| ≤ depth/2 − WALL_T（外墙厚 WALL_T，外皮就是占地边）。
// 层：第 L 层的地面在 L × (wall_m / levels) 高。门窗、隔墙、家具、梯子都记着自己在哪一层；梯子从它那层通到上一层。
//
// 文本（game/buildings/<外壳>/<款式>.txt），一行一条，# 开头是注释，行尾可以带 @层（缺省 0 层）：
//   id house/sanjian            模板 id = 外壳/款式
//   name 三间                    显示名
//   auto no                     不参与自动挑（缺省参与）
//   door front 0 1.3            外墙开门：哪一面（front/right/back/left）、沿墙的位置（前后两面是 x，左右两面是 z）、宽
//   window front -3.6 1.2       外墙开窗：同上
//   wall -1.75,-2.95 -1.75,2.95 隔墙：两个端点（x,z），须横平竖直
//   doorway -1.75,1.0           隔墙上的门洞：落在哪段隔墙上
//   item bed 3.9,-1.9 0         家具：种类、位置（x,z，家具中心）、朝向（0–3，每档转 90°；0 = 家具的正面朝 +z）
//   ladder -1.5,-1.3 0          梯子：梯板的位置（x,z）、朝向（0–3：人站在梯板的哪一侧，0 = +z 侧，面朝 −z 爬），通到上一层

// 家具表：占地（面宽 w × 进深 d，未转时 w 沿 x）、高；solid = 挡人（席子不挡）
struct FurnitureDef {
    const char *id;
    const char *name;
    double w, d, h;
    bool solid;
};

namespace furniture {

extern const FurnitureDef DEFS[];
extern const int COUNT;
const FurnitureDef *by_id(const std::string &p_id);
int index_of(const std::string &p_id);

} // namespace furniture

enum class WallSide {
    FRONT = 0, // +z
    RIGHT = 1, // +x
    BACK = 2, // −z
    LEFT = 3, // −x
};

struct Opening {
    bool window = false; // false = 门
    WallSide side = WallSide::FRONT;
    double at = 0.0; // 沿墙的位置：前后两面是 x，左右两面是 z
    double width = 1.3;
    int level = 0;
};

struct Partition {
    double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0; // 横平竖直：x0 == x1 或 z0 == z1
    int level = 0;
};

struct Doorway {
    double x = 0.0, z = 0.0; // 门洞中心，落在某段隔墙上
    int level = 0;
};

struct Furnishing {
    std::string kind; // FurnitureDef::id
    double x = 0.0, z = 0.0; // 中心
    int quarter = 0; // 朝向：quarter × 90°（绕 y 轴，与建筑的 yaw 同向）
    int level = 0;
};

struct LadderSpot {
    double x = 0.0, z = 0.0; // 梯板的中心线
    int quarter = 0; // 人站在梯板的哪一侧：(sin, cos)(quarter × 90°)
    int level = 0; // 从这一层通到 level + 1
};

struct HouseTemplate {
    std::string id; // "house/sanjian"
    std::string name; // 显示名
    bool auto_pick = true; // 清单里没写款式的房子能不能自动挑到它（另存出来的新模板默认不能，免得满村的房子都跟着换）
    std::vector<Opening> openings;
    std::vector<Partition> partitions;
    std::vector<Doorway> doorways;
    std::vector<Furnishing> items;
    std::vector<LadderSpot> ladders;

    std::string shell() const; // id 的前半：外壳（BuildingDef::id）
    std::string style() const; // id 的后半：款式
    bool operator==(const HouseTemplate &p_other) const;
    bool operator!=(const HouseTemplate &p_other) const { return !(*this == p_other); }
};

// 尺寸（米）：外墙、隔墙、门窗的高低、楼板、梯子。几何（building_parts）与规矩（interior_rules）都用这一份
namespace house_dims {
constexpr double WALL_T = 0.3; // 外墙厚
constexpr double PARTITION_T = 0.2; // 隔墙厚
constexpr double DOOR_H = 2.5; // 外门高（层高不够时压到层高 − 0.4）
constexpr double DOORWAY_W = 1.0; // 隔墙门洞宽
constexpr double DOORWAY_H = 2.3;
constexpr double WINDOW_SILL = 1.0; // 窗台高（离本层地面）
constexpr double WINDOW_TOP = 2.3;
constexpr double SLAB_T = 0.2; // 楼板厚
constexpr double CEILING_T = 0.2; // 天花板厚（顶在檐口高）
constexpr double LADDER_W = 0.9;
constexpr double HATCH_ALONG = 1.4; // 楼板上的梯口：沿梯板方向多宽
constexpr double HATCH_DEPTH = 1.3; // 从梯板往人站的那侧伸多深
constexpr double PLAYER_R = 0.35; // 人（胶囊）半径：走不走得过按它算
} // namespace house_dims

// 某层地面离地坪多高
double level_floor(const BuildingDef &p_def, int p_level);
double level_height(const BuildingDef &p_def);
// 屋内净空的半宽 / 半深
double interior_half_x(const BuildingDef &p_def);
double interior_half_z(const BuildingDef &p_def);
// 墙面：沿墙方向的单位向量、朝外的法线；这一面墙多长（外皮）
void side_axes(WallSide p_side, double &r_along_x, double &r_along_z, double &r_out_x, double &r_out_z);
double side_length(const BuildingDef &p_def, WallSide p_side);
// 开口在屋内贴墙那一点（离墙内皮 p_inset 米）
void opening_point(const BuildingDef &p_def, const Opening &p_opening, double p_inset, double &r_x, double &r_z);
// 家具转了 quarter 之后沿 x / z 的尺寸
void furniture_extent(const FurnitureDef &p_def, int p_quarter, double &r_sx, double &r_sz);
// 梯子人站那侧的方向
void ladder_outward(int p_quarter, double &r_x, double &r_z);

const char *side_name(WallSide p_side); // "front" …
bool parse_side(const std::string &p_text, WallSide &r_side);
const char *side_label(WallSide p_side); // 「正面」…

// 只在正面当中开一扇门、别的什么都没有：库里没有这种外壳的模板时用它
HouseTemplate bare_template(const BuildingDef &p_def);

// 文本
std::vector<std::string> template_lines(const HouseTemplate &p_template);
// 认不出的行记在 r_errors 里（「第 3 行：……」）并跳过；没有 id 行或外壳不认识返回 false
bool parse_template(const std::vector<std::string> &p_lines, HouseTemplate &r_template, std::vector<std::string> *r_errors = nullptr);
