#pragma once

#include <functional>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "freight.h"
#include "resource_sites.h"
#include "wild_landing.h"

class Farm;
class GroundHost;
class Shipyard;
class WeatherModel;
class WorldMap;

// 一处村仓（跑货在它门前接单、打包、交货）
struct FreightPost {
    int port = -1; // map_freight_ports 的下标；−1 = 附近没有村仓
    godot::Vector3 position; // 门前（全局）
    Inventory *store = nullptr; // 村仓的账（村子的账本里的仓）；账还没开为 nullptr
    std::string specialty; // 这村的土产（托运的货）
    godot::String name; // 村名
};

// 一处能采的：采场（采集在采掘面前按住 E，打包在堆场），或地上的一株散布物（树、灌丛、石块……，没有堆场）
struct ResourceSpot {
    int site = -1; // 地图自己的采场下标（散布物：块内序号）；−1 = 附近没有
    bool scatter = false; // 是地上的一株散布物
    uint64_t scatter_key = 0; // 散布物的键（resource_scatter.h）
    godot::Vector3 face; // 采掘面 / 那一株的根（全局）
    godot::Vector3 pile; // 堆场（全局；散布物同 face）
    std::string item; // 采出来的物品
    godot::String name; // 「采石场（安山岩 / 凝灰岩）」「阔叶树」
    godot::String verb; // 「凿石」「伐木」
    int grade = 1; // 0 下 1 中 2 上
    double gather_s = 0.0; // 按住 E 几秒一份
    int cap = 0; // 存量上限
    double refill_hours = 0.0; // 从空到满几个钟头
    ResourceStock *stock = nullptr; // 存量账（地图持有）
};

// 换锚（行星计划 P3，docs/PLAN-PLANET.md 第 4.2 节）：旧锚群的平面 → 新锚群的平面。
// 点经真球面换过去；朝向与速度乘该点处的转动（两边的平面轴在真空间里对齐：绕竖直轴的一个小角度，yaw 是它的转角）
struct MapShift {
    std::function<godot::Vector3(const godot::Vector3 &)> point;
    std::function<godot::Basis(const godot::Vector3 &)> rotation;
    std::function<double(const godot::Vector3 &)> yaw;
};

// 一处的天气由哪几份混成（行星计划 P3）：模型、那一点在它的平面里的位置、权（和为 1）
struct WeatherMix {
    WeatherModel *model = nullptr;
    godot::Vector3 local;
    double weight = 1.0;
};

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

    // 跑货（可选，默认没有）：全图能托运的村；离 p_global 水平 p_range 以内、已经铺出来的村仓（船台跳板脚前）；
    // 托运的目的地往哪儿飞（铺出来了是它船台的接触线，还远就是村心）
    virtual const std::vector<FreightPort> &map_freight_ports() const;
    virtual FreightPost map_freight_post_near(const godot::Vector3 &p_global, double p_range) { return FreightPost(); }
    virtual bool map_freight_target(int p_port, godot::Transform3D &r_target) const { return false; }

    // 采集（可选，默认没有）：离 p_global 水平 p_range 以内、已经摆出来的采场（采掘面或堆场够得着的最近那一处）
    virtual ResourceSpot map_resource_near(const godot::Vector3 &p_global, double p_range) { return ResourceSpot(); }
    // 站在 p_global、面朝 p_facing_yaw（Player 的 look_yaw 约定）能采的那一处：采掘面 p_face_range 以内的采场，
    // 或够得着（离它边 reach 以内）、立着的一株散布物，取近的（面前的优先）；默认只认采掘面
    virtual ResourceSpot map_gather_near(const godot::Vector3 &p_global, double p_face_range, double p_facing_yaw);
    // 采走了一份（账已经记了）：散布物采空了要倒下、重摆
    virtual void map_resource_taken(const ResourceSpot &p_spot) {}
    // 野泊问的地面（全局 xz）：多高（默认按 map_surface）、是不是水（默认不是）
    virtual bool map_landing_height(double p_x, double p_z, double &r_y) const;
    virtual bool map_water_at(double p_x, double p_z) const { return false; }
    // 天气（weather.h）：有气候数据的地图给它的天气模型（天与光、HUD、开发者面板读它，开发者覆盖也改在它上面）；
    // 没有返回 nullptr——天与光用一份没有数据的（常年晴、纬度 30°）
    virtual WeatherModel *map_weather() { return nullptr; }
    // 某一点（全局）的天气由哪几份混成：默认就是 map_weather 一份。行星上（PlanetHost）是载入了的各群，按离各群陆地的距离加权（过渡带约 20 km）
    virtual void map_weather_mix(const godot::Vector3 &p_global, std::vector<WeatherMix> &r_out);
    // 这张图上所有的天气模型（开发者覆盖改在每一份上）：默认就是 map_weather 一份
    virtual void map_weather_models(std::vector<WeatherModel *> &r_out);

    // 换锚（可选，只有行星上的宿主 PlanetHost 有）：宿主定下要换（map_shift_pending），编排在下一个物理帧开头
    // map_shift_begin 拿到换法、把人、船、相机、摆出来的东西、自己记的位置都换过去，再 map_shift_commit 让宿主收尾（新锚归位、挂碰撞……）。
    // map_shift_toward：瞬移、回位要落到别的群的地上——宿主把那一群同步载入、定下换锚（返回 true），编排接着照上面换
    virtual bool map_shift_pending() const { return false; }
    virtual bool map_shift_begin(MapShift &r_shift) { return false; }
    virtual void map_shift_commit() {}
    virtual bool map_shift_toward(const godot::Vector3 &p_global) { return false; }

    static MapHost *of(godot::Node *p_node) { return dynamic_cast<MapHost *>(p_node); }
};

// 野泊、摆放问的地面：地图宿主的地面高与水（全局 xz）
class MapLandingGround : public LandingGround {
    const MapHost *map;

public:
    explicit MapLandingGround(const MapHost *p_map) :
            map(p_map) {}
    bool height(double p_x, double p_z, double &r_y) const override { return map->map_landing_height(p_x, p_z, r_y); }
    bool water(double p_x, double p_z) const override { return map->map_water_at(p_x, p_z); }
};
