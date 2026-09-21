#pragma once

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "farm_model.h"
#include "island.h"

// 一座岛上的农田：Island 的子节点，变换留在原点（岛的局部坐标就是它的局部坐标）。
// 岛每次 rebuilt 后重新勘察；地块表在 FarmModel 里，这里负责推进生长、把地块画出来（垄 + 粟苗，几何体占位），
// 以及回答上层「这一格是什么、能做什么」。谁在干活、粟有多少，它不知道——那是 Voyage 的事。
//
// 格子用 Vector2i(i, j) 指：高度场里以节点 (i, j) 为左上角的那一格（i 沿 z、j 沿 x）。(-1, -1) = 没有这一格。
class Farm : public godot::Node3D {
    GDCLASS(Farm, godot::Node3D)

public:
    // 与 farm_model.h 里的三个枚举一一对应（那边不含 Godot 头，绑不进脚本）
    enum State {
        PLOT_WILD,
        PLOT_TILLED,
        PLOT_GROWING,
        PLOT_RIPE,
    };
    enum Action {
        ACTION_NONE,
        ACTION_TILL,
        ACTION_SOW,
        ACTION_HARVEST,
    };
    enum Land {
        LAND_ARABLE,
        LAND_NO_GROUND,
        LAND_WATER,
        LAND_TOO_DRY,
        LAND_TOO_STEEP,
        LAND_DOCK,
        LAND_TRAIL,
    };

    static const godot::Vector2i NO_CELL;

private:
    FarmRules rules;
    FarmModel model;
    Island *island = nullptr;

    godot::HashMap<godot::Vector2i, godot::MeshInstance3D *> plot_nodes;
    godot::MeshInstance3D *highlight = nullptr;
    godot::Vector2i highlighted = godot::Vector2i(-1, -1);
    godot::Ref<godot::StandardMaterial3D> plot_material;
    godot::Ref<godot::StandardMaterial3D> highlight_material;

    void on_island_rebuilt();
    void apply_rules();
    void refresh_plot(const godot::Vector2i &p_cell);
    bool has_ground(const godot::Vector2i &p_cell) const;

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;

    // 全局坐标落在哪一格；不在岛上返回 NO_CELL
    godot::Vector2i cell_at(const godot::Vector3 &p_global) const;
    godot::Vector3 get_cell_center(const godot::Vector2i &p_cell) const; // 全局坐标，贴地
    // 离 p_global 最近的可垦荒地；没有返回 NO_CELL
    godot::Vector2i find_nearest_arable(const godot::Vector3 &p_global) const;

    Land get_land(const godot::Vector2i &p_cell) const;
    State get_plot(const godot::Vector2i &p_cell) const;
    double get_growth(const godot::Vector2i &p_cell) const;
    double get_moisture(const godot::Vector2i &p_cell) const;
    int get_plot_count(State p_state) const;

    Action available_action(const godot::Vector2i &p_cell, int p_grain) const;
    // 返回粟的增减；做不了返回 0 且什么都不变
    int perform(const godot::Vector2i &p_cell, Action p_action, int p_grain);

    // 目标格的描边；NO_CELL = 不画
    void set_highlight(const godot::Vector2i &p_cell, const godot::Color &p_color);

    void set_max_river_distance(double p_value);
    double get_max_river_distance() const { return rules.max_river_m; }
    void set_grow_seconds(double p_value);
    double get_grow_seconds() const { return rules.grow_seconds; }
    void set_harvest_yield(int p_value);
    int get_harvest_yield() const { return rules.harvest_yield; }
};

VARIANT_ENUM_CAST(Farm::State);
VARIANT_ENUM_CAST(Farm::Action);
VARIANT_ENUM_CAST(Farm::Land);
