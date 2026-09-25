#pragma once

#include <string>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "placeables.h"

// 玩家摆在地上的东西的实物：按摆出来的账（Placements）一件一件摆——工作台、窑、木箱、火把（带一盏暖光）、篱笆，
// 每件是一个带盒子碰撞的 StaticBody3D（碰撞层 1，人绕着走）；地不平时底下垫一块底座垫到最低处。
// 另有一个虚影：手上拿着能摆的东西时，身前那一件的半透明样子，摆得下绿、摆不下红。
// 只画、只答：账在摆放部件（PlaceHand）手里，账变了（revision 变了）就整个重摆。全局坐标，挂在编排下面。
class PlacedProps : public godot::Node3D {
    GDCLASS(PlacedProps, godot::Node3D)

    int revision = -1;
    godot::Node3D *ghost = nullptr;
    std::string ghost_item;
    bool ghost_ok = false;
    godot::Ref<godot::StandardMaterial3D> ghost_good, ghost_bad;

    void ensure_ghost_materials();

protected:
    static void _bind_methods();

public:
    static constexpr const char *PROP_META = "placed_prop"; // 实物节点上记着账里的 id

    // 账变了就重摆
    void sync(const Placements &p_placed);
    void show_ghost(const std::string &p_item, const PlaceCheck &p_check);
    void hide_ghost();
    bool is_ghost_visible() const;
    // 摆在 p_check 处（底面往上抬一点，地面本身不算）的占地盒子碰着的实物（房子、船、石头……；碰撞层 1）；没有 nullptr
    godot::Object *obstacle(const PropDef &p_def, const PlaceCheck &p_check, const godot::TypedArray<godot::RID> &p_exclude) const;
    // 账里 id 那件的实物；没有 nullptr（测试查）
    godot::Node3D *get_prop_node(int p_id) const;
};
