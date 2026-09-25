#pragma once

#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "island.h"
#include "map_host.h"

// 几座游戏尺度的岛拼成的一张地图（甲乙两岛，main.tscn）：地图宿主（MapHost）。
// islands 列出组成它的岛（Island 节点路径，顺序即码头号：第 k 座岛的码头号就是 k）。
// 每座岛一个码头；船坞、农田、聚落都是岛下面挂的子节点。站稳 = 脚下 1 m 内是某座岛的地面。
class IslandMap : public godot::Node3D, public MapHost {
    GDCLASS(IslandMap, godot::Node3D)

    godot::TypedArray<godot::NodePath> islands;
    std::vector<Island *> resolved;

    Island *island(int p_index) const { return resolved[size_t(p_index)]; }
    int nearest_island(const godot::Vector3 &p_global) const; // 离岛心最近的那座（下标）

protected:
    static void _bind_methods();

public:
    void _ready() override;

    // MapHost
    godot::Node3D *map_node() override { return this; }
    bool map_ready() const override;
    double map_surface(const godot::Vector3 &p_global) const override;
    bool map_water_at(double p_x, double p_z) const override; // 岛上的河（野泊的舷梯不落进去）
    int map_island_at(const godot::Vector3 &p_global) const override;
    int map_dock_count() const override { return int(resolved.size()); }
    godot::Transform3D map_dock_berth(int p_index) const override { return island(p_index)->get_berth_contact(); }
    godot::Transform3D map_dock_spawn(int p_index) const override { return island(p_index)->get_spawn_transform(); }
    godot::Vector3 map_dock_position(int p_index) const override;
    int map_dock_island(int p_index) const override { return p_index; }
    godot::String map_dock_label(int p_index) const override { return island(p_index)->get_label(); }
    // 两岛就一个去处：目标行只说去没去过对岸，航向行说「对岸泊位 / 回程泊位」
    godot::String map_goal_text(int p_index, bool p_arrived, const godot::Vector3 &p_global) const override;
    godot::String map_course_name(int p_index, bool p_arrived) const override;
    std::vector<Shipyard *> map_shipyards() const override;
    Farm *map_farm_at(const godot::Vector3 &p_global) const override;
    GroundHost *map_settlement_at(const godot::Vector3 &p_global) const override;

    void set_islands(const godot::TypedArray<godot::NodePath> &p_value) { islands = p_value; }
    godot::TypedArray<godot::NodePath> get_islands() const { return islands; }
};
