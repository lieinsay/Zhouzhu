#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "item_slots.h"
#include "recipes.h"
#include "wild_landing.h"

// 能摆在地上的东西（工作台、窑、木箱、火把、篱笆）：表、摆放的规矩、摆出来的账。不含 Godot 头。
//
// 摆：手上拿着（快捷栏选中）一样能摆的东西，人面前出一个虚影——中心在视线前方（离人 1 m 再加上它自己的半个身子），
// 按 grid_m 对齐，朝向按人的视线取整到 90°（正面朝着人）再加玩家自己转的几下。
// 占地四角、四边中点和中心处处要有地、不在水里，最高最低差不过 max_relief_m（底座垫到最低处，不悬空），
// 不和已经摆的挤在一起；房子、船、石头这些实物由场景用碰撞另查。
// 拆：看着够得着的那件（身前 ±60°、pick_range_m 以内最近的）按 X 收回背包；木箱里还有东西不让拆。
// 设施：工作台、窑在身边 station_range_m 以内，合成就能用它的配方（recipes.h）。木箱是个 18 格的容器（item_slots.h）。
//
// 坐标：全局 x 东、z 南；朝向 yaw 与 Godot 的 Basis(Y, yaw) 一致——物件的局部 +z（正面）朝 (sin yaw, cos yaw)，
// 人朝 yaw 看时视线是 (−sin yaw, −cos yaw)，所以正面朝着人的物件 yaw 就是人的 yaw。

enum class PropLook : uint8_t {
    WORKBENCH, // 一张带腿的厚木案
    KILN, // 土坯砌的馒头窑，正面一个火口
    CHEST, // 带盖的木箱
    TORCH, // 插在地上的松明，顶上一团火，照亮周围
    FENCE, // 两根桩、两道横木
};

struct PropDef {
    const char *item;
    double width; // 局部 x（左右）
    double depth; // 局部 z（前后）
    double height;
    PropLook look;
    int slots; // 容器有几格；0 = 不是容器
    int station; // 是哪种设施（Station 的值）；−1 = 不是
    double light_m; // 照多远；0 = 不亮
};

namespace props {

extern const PropDef PROPS[];
extern const int PROP_COUNT;

const PropDef *find(const std::string &p_item); // 不能摆返回 nullptr
inline bool placeable(const std::string &p_item) { return find(p_item) != nullptr; }

} // namespace props

struct PlaceRules {
    double grid_m = 0.5;
    double gap_m = 1.0; // 虚影中心离人：这么远再加上物件朝人那一面的半个身子
    double max_relief_m = 0.5;
    double touch_m = 0.02; // 两件之间至少留这么宽（贴着摆可以）
    double pick_range_m = 2.6; // 离物件（占地的边）多近够得着
    double pick_cone_deg = 60.0; // 身前多宽的扇面里算「看着它」
    double station_range_m = 3.0;
};

// 一次摆放的结果：能不能摆（不能说为什么）、摆在哪、朝哪
struct PlaceCheck {
    bool ok = false;
    std::string why;
    double x = 0.0, y = 0.0, z = 0.0; // 中心，y = 占地里最高处的地面（物件底面）
    double low = 0.0; // 占地里最低处的地面（底座往下垫到这儿）
    int quarter = 0; // 朝向：yaw = quarter × 90°
};

struct PlacedProp {
    int id = 0;
    std::string item;
    double x = 0.0, y = 0.0, z = 0.0, low = 0.0;
    int quarter = 0;
    ItemSlots contents; // 容器里的东西（不是容器为空）
};

// 物件朝 quarter 时占地的半宽（东西向、南北向）
void prop_half_extent(const PropDef &p_def, int p_quarter, double &r_half_x, double &r_half_z);
double quarter_yaw(int p_quarter);
// 人朝 p_look_yaw 看，再转 p_turns 下（每下 90°）：物件的朝向
int facing_quarter(double p_look_yaw, int p_turns);

class Placements {
    std::vector<PlacedProp> props;
    int next_id = 1;
    int revision = 0; // 每摆 / 拆一件加一（场景据此重画）

public:
    const std::vector<PlacedProp> &get_props() const { return props; }
    int get_revision() const { return revision; }
    int size() const { return int(props.size()); }
    PlacedProp *find(int p_id);
    const PlacedProp *find(int p_id) const;

    // 与已摆的哪一件挤在一起（占地的矩形相交，留 p_touch_m）；没有 −1。p_ignore 那件不算
    int overlapping(const PropDef &p_def, double p_x, double p_z, int p_quarter, double p_touch_m, int p_ignore = -1) const;
    // 记下一件（按 check 摆），返回它的 id
    int add(const std::string &p_item, const PlaceCheck &p_check);
    bool remove(int p_id);
    // 人在 (p_x, p_y, p_z) 朝 p_look_yaw 看：够得着、在身前扇面里、离得最近的那件；没有 −1
    int facing(double p_x, double p_y, double p_z, double p_look_yaw, const PlaceRules &p_rules) const;
    // 身边 p_range 以内有哪些设施（station_bit 的或）
    unsigned stations_near(double p_x, double p_y, double p_z, double p_range) const;
};

// 人站在 (p_x, p_z)、朝 p_look_yaw 看、转了 p_turns 下，要摆 p_def：虚影落在哪、能不能摆
PlaceCheck plan_placement(const LandingGround &p_ground, const PropDef &p_def, double p_x, double p_z, double p_look_yaw, int p_turns,
        const Placements &p_placed, const PlaceRules &p_rules);
// 这件能不能收回背包（木箱里有东西不行）；能返回空串
std::string pickup_problem(const PlacedProp &p_prop);
// 点到物件占地矩形的水平距离（在里面为 0）
double prop_distance(const PlacedProp &p_prop, double p_x, double p_z);
