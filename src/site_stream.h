#pragma once

#include <map>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include "world.h"
#include "world_resource.h"
#include "world_village.h"

class Hinterland;

// 岛群里的码头与村落，走近了才建、走远了就拆。节点放在场景原点。
// 上层（WorldScene）每帧 set_focus（人，或掌舵时的船）；每隔 check_interval 秒按焦点重算一次：
//   码头：dock_radius 以内建栈桥（便宜，一次建齐）；
//   村与散户：离村子边缘 village_radius 以内铺房子，每帧最多铺一处（由近及远），免得一帧卡住；
//   村民：folk_radius 以内才招，每村 max_folk 封顶；
//   采场与地标：resource_radius 以内摆出采掘面与堆场、泉眼 / 温泉 / 洞穴的样子（便宜，每回最多摆 resource_budget 处，由近及远）。
// 都带回滞：拆的距离比建的远 hysteresis 倍；布置模式改过还没存的村不拆（拆了改动就丢了）。瞬移前调 ensure_near，把落点周围的一次同步建好。
// 这里只持有「哪些建了」；码头几何在 World，村子清单在 WorldVillage，村子的账（人、仓、田）在远处账房 Hinterland（可以不接：那就走近了从头招人）。
class SiteStream : public godot::Node3D {
    GDCLASS(SiteStream, godot::Node3D)

    godot::NodePath world_path = "../World";
    godot::NodePath hinterland_path = "../Hinterland";
    godot::String layout_dir = "res://settlements/2051"; // 布置模式的改动：<layout_dir>/village_<号>.txt、hamlet_<号>.txt
    double dock_radius = 3000.0;
    double village_radius = 2500.0;
    double folk_radius = 900.0;
    double resource_radius = 1500.0;
    int resource_budget = 4;
    double hysteresis = 1.25;
    int per_house = 2;
    int max_folk = 40;
    int64_t seed = 2051;
    double check_interval = 0.25;

    World *world = nullptr;
    Hinterland *hinterland = nullptr;
    godot::Vector3 focus;
    bool has_focus = false;
    double since_check = 0.0;
    std::map<int, WorldDock *> docks; // World 的码头下标 → 节点
    std::map<int, WorldVillage *> villages; // sites.settlements 下标 → 节点
    std::map<int, WorldResource *> resources; // World 的采场下标 → 节点
    std::map<int, WorldResource *> landmarks; // World 的地标下标 → 节点
    int64_t village_version = 0; // 铺了 / 拆了村子就加一（散布据此重查压着房子的）

    // 村子边缘到焦点的距离（村子还没铺时按户数估个半径）
    double edge_distance(int p_index) const;
    void refresh(int p_build_budget);
    WorldVillage *build_village(int p_index);

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _process(double p_delta) override;

    void set_focus(const godot::Vector3 &p_focus);
    godot::Vector3 get_focus() const { return focus; }
    // 同步把 p_point 周围该有的码头、村子、村民都建好（瞬移、开局用）
    void ensure_near(const godot::Vector3 &p_point);

    // 查询（给地图、HUD、测试）
    godot::PackedInt32Array get_built_docks() const; // 生成器的码头号
    godot::PackedInt32Array get_built_villages() const; // 村号（不含散户）
    int get_built_hamlet_count() const;
    WorldVillage *get_village(int p_village_id) const; // 已建的村；没有返回 nullptr
    WorldVillage *get_hamlet(int p_hamlet_id) const;
    WorldDock *get_dock(int p_dock_id) const;
    godot::PackedInt32Array get_built_resources() const; // 摆出来的采场的采场号
    WorldResource *get_resource(int p_working_id) const;
    godot::PackedInt32Array get_built_landmarks() const; // 摆出来的地标的号
    // 采掘面或堆场离 p_point 水平 p_range 以内、最近的那一处摆出来的采场；没有返回 nullptr
    WorldResource *resource_near(const godot::Vector3 &p_point, double p_range) const;
    // 铺出来的村与散户；铺了 / 拆了就变的版本号
    std::vector<WorldVillage *> built_villages() const;
    int64_t get_village_version() const { return village_version; }
    // 该点所在的那处已铺的村 / 散户（离村心不超过它的半径 + p_margin）；最近的那处，没有返回 nullptr
    WorldVillage *village_at(const godot::Vector3 &p_point, double p_margin = 40.0) const;

    Hinterland *get_hinterland() const { return hinterland; }
    void set_hinterland_path(const godot::NodePath &p_value) { hinterland_path = p_value; }
    godot::NodePath get_hinterland_path() const { return hinterland_path; }
    void set_world_path(const godot::NodePath &p_value) { world_path = p_value; }
    godot::NodePath get_world_path() const { return world_path; }
    void set_layout_dir(const godot::String &p_value) { layout_dir = p_value; }
    godot::String get_layout_dir() const { return layout_dir; }
    void set_dock_radius(double p_value) { dock_radius = p_value; }
    double get_dock_radius() const { return dock_radius; }
    void set_village_radius(double p_value) { village_radius = p_value; }
    double get_village_radius() const { return village_radius; }
    void set_folk_radius(double p_value) { folk_radius = p_value; }
    void set_resource_radius(double p_value) { resource_radius = p_value; }
    double get_resource_radius() const { return resource_radius; }
    double get_folk_radius() const { return folk_radius; }
    void set_per_house(int p_value) { per_house = p_value; }
    int get_per_house() const { return per_house; }
    void set_max_folk(int p_value) { max_folk = p_value; }
    int get_max_folk() const { return max_folk; }
    void set_seed(int64_t p_value) { seed = p_value; }
    int64_t get_seed() const { return seed; }
};
