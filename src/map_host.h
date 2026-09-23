#pragma once

#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

class Farm;
class GroundHost;
class Shipyard;
class WorldMap;

// 地图宿主：玩家编排（Expedition）只认它。一张地图要让所有玩法跑起来，最少回答这些：
//   地面（全局坐标）——多高、属于哪座岛、站在这儿稳不稳（地面块铺好了没有）；
//   码头——几个、各自的泊位接触线、出生位姿、岸缘点、在哪座岛、叫什么、此刻能不能靠；
//   玩法的落脚处——有哪些船坞、脚下那片农田、脚下那处聚落（布置模式改它、HUD 的钟点看它的村民）。
// 流式地图（岛群）另外要焦点、瞬移前的同步准备；有岛群地图面板的另给面板。全都有默认值的是可选的。
// 地图的差异只写在这里的实现里（Island 两座拼成的 IslandMap、岛群 World、平板岛 FlatMap）；玩法代码里不出现具体地图的类型。
class MapHost {
public:
    virtual ~MapHost() = default;

    virtual godot::Node3D *map_node() = 0;
    virtual bool map_ready() const = 0;

    // 地面（全局坐标）：地面高，没有地面返回 NAN；岛号，没有地面返回 −1
    virtual double map_surface(const godot::Vector3 &p_global) const = 0;
    virtual int map_island_at(const godot::Vector3 &p_global) const = 0;
    // 站在这儿算不算站稳（坠落回位回到这里）：默认 = 脚下 1 m 内有地面
    virtual bool map_footing(const godot::Vector3 &p_global) const;

    // 码头：下标 0 … count − 1；id 是地图自己的码头号（场景属性按 id 配）
    virtual int map_dock_count() const = 0;
    virtual int map_dock_id(int p_index) const { return p_index; }
    virtual bool map_dock_open(int p_index) const { return true; } // 此刻靠得上（岛群：栈桥建好了）
    virtual godot::Transform3D map_dock_berth(int p_index) const = 0; // 泊位接触线
    virtual godot::Transform3D map_dock_spawn(int p_index) const = 0; // 停机坪上面朝栈桥的出生位姿
    virtual godot::Vector3 map_dock_position(int p_index) const = 0; // 岸缘点
    virtual int map_dock_island(int p_index) const = 0;
    virtual godot::String map_dock_label(int p_index) const = 0;
    int map_find_dock(int p_id) const; // 按 id 找下标；没有 −1

    // 玩法的落脚处
    virtual std::vector<Shipyard *> map_shipyards() const = 0;
    virtual Farm *map_farm_at(const godot::Vector3 &p_global) const = 0; // 脚下那片农田；没有 nullptr
    virtual GroundHost *map_settlement_at(const godot::Vector3 &p_global) const = 0; // 脚下那处聚落的宿主；没有 nullptr

    // 流式：焦点（步行时是人，掌舵时是船头前方）；瞬移前同步把落点周围建好；还在建的地面块数
    virtual void map_focus(const godot::Vector3 &p_global) {}
    virtual void map_prepare(const godot::Vector3 &p_global) {}
    virtual int map_pending() const { return 0; }

    // 界面：HUD 第一行的位置文字（空 = 不写）；岛群地图面板（没有返回 nullptr）与开图前的刷新；相机远裁剪（0 = 不改）
    virtual godot::String map_location_text(const godot::Vector3 &p_global) const { return godot::String(); }
    // 航行目标：步行时那一行（默认「目标：乘船去 <码头>（x km）」）、掌舵时航向行里目标的名字（默认「目的 / 回程 <码头>」）
    virtual godot::String map_goal_text(int p_index, bool p_arrived, const godot::Vector3 &p_global) const;
    virtual godot::String map_course_name(int p_index, bool p_arrived) const;
    virtual WorldMap *map_make_panel() { return nullptr; }
    virtual void map_refresh_panel(WorldMap *p_panel) const {}
    virtual double map_view_distance() const { return 0.0; }

    static MapHost *of(godot::Node *p_node) { return dynamic_cast<MapHost *>(p_node); }
};
