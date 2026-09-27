#pragma once

#include <string>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "map_host.h"
#include "pack_hand.h"
#include "placeables.h"
#include "placed_props.h"
#include "play_notice.h"
#include "player.h"

// 摆放：手上拿着（快捷栏选中）能摆的东西时，身前出一个虚影（摆得下绿、摆不下红），Q/R 转 90°，按 E 摆下去（从选中那格扣一份）；
// 看着摆出来的东西（虚影没亮绿时）：木箱、工作台、窑按 E 打开背包（木箱的格子列在背包上面，工作台 / 窑的配方亮起来），X 收回背包（木箱要先搬空）。
// 身边 3 m 内有工作台 / 窑，合成就能用它们的配方。资源点的采掘面与堆场周围 3 m 不让摆（人要在那儿干活、打箱）。
// 与场景无关的玩法部件（不是节点），编排持有、步行且没开面板时每帧推一下。摆出来的账（Placements：每件在哪、朝哪、木箱里装着什么）在这里，
// 实物由 PlacedProps 照账摆；地面问地图宿主（MapLandingGround），房子、船、石头挡不挡问 PlacedProps 的碰撞查询。
class PlaceHand {
    Placements placed;
    PlaceRules rules;
    int turns = 0; // 玩家转了几下（每下 90°）
    PlaceCheck ghost;
    bool ghost_on = false;
    std::string ghost_item;
    int facing_id = -1; // 看着的那件
    PlacedProps *view = nullptr;

    void update_ghost(MapHost *p_map, Player *p_player, const PropDef &p_def, bool p_on_ship);

public:
    struct Step {
        bool used = false; // 这一下 E / X 归它
        int open = -1; // 按 E 要打开的那件（木箱 / 工作台 / 窑）
    };

    void setup(PlacedProps *p_view);
    // 每个物理帧（步行、没开面板、没抱货箱时）先看一眼：看着哪件、手上拿着能摆的就出虚影（Q/R 转）。p_on_ship：人在船上（船上不让摆）
    void look(MapHost *p_map, Player *p_player, PackHand &p_pack, bool p_on_ship);
    // 这一下 E（p_interact）/ X（p_remove）归不归它：看着摆出来的东西按 X、看着木箱 / 工作台 / 窑按 E、手上拿着能摆的按 E
    bool claims(const PackHand &p_pack, bool p_interact, bool p_remove) const;
    // 做：收回、打开、摆下
    Step act(PackHand &p_pack, bool p_interact, bool p_remove, const Say &p_say);
    // 虚影亮着，或看着能打开的那件：底部提示、E 都先归它
    bool engaged() const;
    // 收起虚影（开面板、上舵、抱起货箱时）
    void stand_down();

    // 身边的设施（station_bit 的或）
    unsigned stations_near(const godot::Vector3 &p_at) const;
    // 木箱的格子；不是木箱 / 没了返回 nullptr
    ItemSlots *container(int p_id);
    // 「木箱（18 格）」「工作台」
    godot::String title_of(int p_id) const;
    bool is_openable(int p_id) const; // 木箱、工作台、窑

    // 底部提示：看着的那件能干什么、手上的东西摆不摆得下；无事可说返回空
    godot::String prompt(const PackHand &p_pack) const;

    const Placements &get_placed() const { return placed; }
    Placements &edit_placed() { return placed; } // 换锚时编排把账里的位置换过去
    int get_facing() const { return facing_id; }
    bool is_ghost_on() const { return ghost_on; }
    const PlaceCheck &get_ghost() const { return ghost; }
};
