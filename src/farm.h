#pragma once

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <memory>
#include <vector>

#include "farm_model.h"
#include "ground_host.h"

// 一片农田：地面宿主（GroundHost：岛、岛群、平板岛）的子节点，变换留在原点（宿主的局部坐标就是它的局部坐标）。
// 地块格由宿主的地面给（岛上是高度场的格子，岛群里是与岛群坐标对齐的 2.5 m 格）；宿主每次 rebuilt 后重来一遍。
// 勘察按需做（FarmModel 记着）；这里负责推进生长、把地块画出来（垄 + 作物苗，几何体占位，粟与菽画法不同；每块地一个节点，放在它的角上，
// 岛群里离原点几十公里也不抖），以及回答上层「这一格是什么、种的什么、能做什么」。谁在干活、背包里有什么，它不知道——那是编排的事：
// 播种时上层告诉它种哪种作物，收获时它报出收了几份（产物是什么，问作物表）。
//
// 格子用 Vector2i(i, j) 指：地块格里以节点 (i, j) 为左上角的那一格（i 沿 z、j 沿 x）。NO_CELL = 没有这一格。
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
        LAND_YARD,
    };

    static const godot::Vector2i NO_CELL;

private:
    FarmRules rules;
    double search_radius = 120.0; // find_nearest_arable 在无边的地块格上找多远
    GroundHost *host = nullptr;
    double clock_seen = -1.0; // 上一帧读到的世界钟（秒）；生长按钟走过的游戏小时推进
    std::unique_ptr<MapGround> ground; // 宿主给的地面；宿主 rebuilt 时换新
    FarmModel model;

    godot::HashMap<godot::Vector2i, godot::MeshInstance3D *> plot_nodes;
    godot::MeshInstance3D *highlight = nullptr;
    godot::Vector2i highlighted = godot::Vector2i(-1, -1);
    godot::Ref<godot::Material> plot_material; // 弯曲标准材质（顶点色）
    godot::Ref<godot::StandardMaterial3D> highlight_material;

    void on_host_rebuilt();
    void apply_rules();
    void refresh_plot(const godot::Vector2i &p_cell);
    bool has_ground(const godot::Vector2i &p_cell) const;
    godot::Transform3D host_transform() const { return host->host_node()->get_global_transform(); }
    godot::Vector3 cell_corner(const godot::Vector2i &p_cell) const; // 格子 (i, j) 左上角节点（宿主坐标，y = 0）

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;

    // 全局坐标落在哪一格；那里没有地面返回 NO_CELL
    godot::Vector2i cell_at(const godot::Vector3 &p_global) const;
    godot::Vector3 get_cell_center(const godot::Vector2i &p_cell) const; // 全局坐标，贴地
    // 离 p_global 最近的可垦荒地（有边的格子找遍，无边的找 search_radius 以内）；没有返回 NO_CELL
    godot::Vector2i find_nearest_arable(const godot::Vector3 &p_global) const;

    Land get_land(const godot::Vector2i &p_cell) const;
    State get_plot(const godot::Vector2i &p_cell) const;
    double get_growth(const godot::Vector2i &p_cell) const;
    // 此刻这一格的时令倍数（有气候的地图按那一格的海拔与日子：冷了、雪盖着不长；没有气候是 1）
    double get_season_factor(const godot::Vector2i &p_cell) const;
    double get_moisture(const godot::Vector2i &p_cell) const;
    int get_plot_count(State p_state) const;

    // 这一格种的作物 id；没种返回空串
    godot::String get_crop(const godot::Vector2i &p_cell) const;
    // 作物表：收一格得几份（不认识的作物返回 0）
    int get_crop_yield(const godot::String &p_crop) const;

    Action available_action(const godot::Vector2i &p_cell, bool p_has_seed) const;
    // 干一样活；p_crop 只在播种时用（作物 id）。返回收获的份数（开垦、播种为 0）；做不了返回 0 且什么都不变
    int perform(const godot::Vector2i &p_cell, Action p_action, const godot::String &p_crop);

    // 给同在 C++ 里的上层读（村民据此挑地；动地仍走 perform，好让地块重画）
    const FarmModel &get_model() const { return model; }
    // 村子走远 / 走近时整块搬地（收回、展开）：直接改模型，改完调 refresh_cells 重画那些格
    FarmModel &edit_model() { return model; }
    void refresh_cells(const std::vector<godot::Vector2i> &p_cells);
    GroundHost *get_host() const { return host; }

    // 目标格的描边；NO_CELL = 不画
    void set_highlight(const godot::Vector2i &p_cell, const godot::Color &p_color);

    void set_max_river_distance(double p_value);
    double get_max_river_distance() const { return rules.max_river_m; }
    void set_grow_hours(double p_value);
    double get_grow_hours() const { return rules.grow_hours; }
    void set_search_radius(double p_value) { search_radius = p_value; }
    double get_search_radius() const { return search_radius; }
};

VARIANT_ENUM_CAST(Farm::State);
VARIANT_ENUM_CAST(Farm::Action);
VARIANT_ENUM_CAST(Farm::Land);
