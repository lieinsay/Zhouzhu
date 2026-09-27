#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include "farm_model.h"
#include "planet_frame.h"
#include "resource_scatter.h"
#include "resource_sites.h"
#include "world_sites.h"

class FarGroups;
class GroupWorld;
class Hinterland;
class PlanetView;
class ScatterStream;
class SiteStream;

// 一个岛群离开细版 / 近景时留在内存里的东西（模拟分级，计划 4.6；存档是以后的事）
struct GroupMemory {
    // 细版 → 近景：各村的账（save_village_state 的文本；sites.settlements 下标 → 文本）与存下时的钟（秒）
    std::map<int, std::string> ledgers;
    double ledgers_at = -1.0;
    // 近景 → 远景：农田里的地（玩家垦的、种的）、采场的存量、散布物的采空账
    std::vector<std::pair<uint64_t, Plot>> plots;
    double plots_at = -1.0;
    std::vector<ResourceStock> stocks;
    ScatterLedger scatter;
    bool has_world = false;
};

// 换锚（行星计划 P3，docs/PLAN-PLANET.md 第 4.2 节）：旧锚群平面 → 新锚群平面。点经真球面精确换过去（rebase_point），
// 朝向与速度乘该点处的转动（rebase_rotation：两边的局部系在真空间里对齐，是绕竖直轴的一个小角度）
struct GroupShift {
    GroupFrame from, to;
    int from_node = -1, to_node = -1;
    godot::Vector3 point(const godot::Vector3 &p_old) const;
    godot::Basis rotation(const godot::Vector3 &p_old) const;
    double yaw(const godot::Vector3 &p_old) const; // 该点处转了多少（弧度，Godot 的 y 转角：朝向的 yaw 加它）
};

// 岛群的流式（行星计划 P3，计划 4.4）：一个场景一个（world_2051.tscn），按焦点（人或船）到各群陆地的距离给档——
//   远景（FAR）：只有 FarGroups 的粗版；
//   近景（NEAR）：GroupWorld（后台载入，Farm、Shipyard 跟着）只流地面块与河，没有碰撞，不接焦点——焦点按真球面换到它的平面喂给它选块；
//   细版（DETAIL）：再挂上 SiteStream、ScatterStream、Hinterland（这一群的村子、散布、账房），是锚或即将成为锚；同时细版的 ≤ max_detail（锚 + 最近的一个）。
// 进档带回滞（detail_in / detail_out、near_in / near_out）；载入都在后台（GroupWorld 的 async_load），载入期间 FarGroups 的粗版顶着，
// 地面块挂齐了才让 FarGroups 藏起它的粗版。
// 锚（D7）：物理只在锚群的平面里；邻群的节点（World、SiteStream、ScatterStream）摆一个刚体变换——它的平面坐标换到锚群的平面坐标，
// 在焦点处取线性近似（绕 y 转 + 水平平移；高度沿径向，两边一样）：焦点二三十公里内误差几十厘米，画面上不到一像素；焦点挪了 place_step 才重算。
// 换锚：人或船离哪个群的陆地最近就以它为锚（回滞 shift_margin），只在云海上空换（脚下两群都没有地）；定了之后挂起（is_shift_pending），
// 编排（Expedition）下一个物理帧把人、船、相机、摆出来的东西按 GroupShift 换过去，再 commit_shift：新锚归位、挂上碰撞，旧锚摆上刚体变换、撤掉碰撞，
// PlanetView 换锚。画面只取决于真球面上的位置与相机，所以换锚那一帧不跳。
// 场景里摆的那一群（home，2051）的节点在场景根下（名字照旧：World、SiteStream、ScatterStream、Hinterland，测试与工具按名字找）；
// 别的群建在自己的容器（Group<节点号>）下，子节点同名，所以它们之间的相对路径（../World）照旧。离开细版时村子的账、离开近景时农田与采场的账留在内存（GroupMemory）。
class GroupStream : public godot::Node3D {
    GDCLASS(GroupStream, godot::Node3D)

public:
    enum Tier {
        TIER_FAR,
        TIER_NEAR,
        TIER_DETAIL,
    };

    struct Group {
        int node = -1;
        godot::String data_dir;
        bool home = false; // 场景里摆的那一群
        GroupFrame frame;
        std::vector<double> land_x, land_z; // 粗版的陆地格心（这一群的平面，米）
        double land_pad = 1000.0; // 粗版半格：到格心的距离减去它
        double reach_m = 0.0; // 群心到最远的陆地格心
        WorldSites sites; // sites.tsv：没载入时码头表也要有位置
        Tier tier = TIER_FAR;
        double land_distance = 1e18; // 焦点到它的陆地（米）
        godot::Node *parent = nullptr; // 它的节点挂在哪
        godot::Node3D *container = nullptr;
        GroupWorld *world = nullptr;
        SiteStream *site_stream = nullptr;
        ScatterStream *scatter_stream = nullptr;
        Hinterland *hinterland = nullptr;
        godot::Transform3D placement; // 这一群平面 → 锚群平面（锚群是单位变换）
        godot::Vector3 placed_at; // 上回按焦点在哪算的摆放
        bool placed = false;
        double loaded_at = -1.0; // 载入完的真实时间（秒）
        bool far_hidden = false; // 让 FarGroups 藏起了它的粗版
        GroupMemory memory;
    };

private:
    godot::NodePath planet_path = "../PlanetView";
    godot::NodePath home_path = "../World";
    godot::NodePath far_groups_path = "../FarGroups";
    godot::String worlds_dir = "res://worlds";
    godot::String settlements_dir = "res://settlements";
    double detail_in = 30000.0, detail_out = 40000.0;
    double near_in = 55000.0, near_out = 70000.0;
    double shift_margin = 2000.0;
    double check_interval = 0.25;
    double place_step = 200.0;
    int max_detail = 2;
    bool auto_shift = true;

    PlanetView *planet = nullptr;
    FarGroups *far_groups = nullptr;
    std::vector<std::unique_ptr<Group>> groups;
    bool ready = false;
    int anchor = -1;
    int pending = -1;
    godot::Vector3 focus;
    bool has_focus = false;
    double since_check = 1e9;
    int shifts = 0;
    int loads = 0, unloads = 0;

    bool setup();
    void update_tiers();
    void update_anchor_choice();
    void update_placements(bool p_force);
    void feed_focus();
    void poll_loading();
    void set_tier(Group &r_group, Tier p_tier);
    void load_world(Group &r_group, bool p_sync);
    void unload_world(Group &r_group);
    void load_detail(Group &r_group);
    void unload_detail(Group &r_group);
    void place(Group &r_group, const godot::Transform3D &p_placement);
    godot::Transform3D placement_for(const Group &p_group) const;
    double land_distance(const Group &p_group, const godot::Vector3 &p_anchor_point) const;
    void set_far_hidden(Group &r_group, bool p_hidden);
    Group *find(int p_node) const;
    int index_of(const Group *p_group) const;

protected:
    static void _bind_methods();

public:
    static constexpr const char *GROUP = "group_stream";

    void _ready() override;
    void _process(double p_delta) override;

    bool is_ready() const { return ready; }
    // 焦点（锚群平面）：地图宿主（PlanetHost）每帧喂；没喂过就用锚群 World 的焦点
    void set_focus(const godot::Vector3 &p_focus);
    godot::Vector3 get_focus() const { return focus; }

    // 查询（地图宿主、测试）
    int get_group_count() const { return int(groups.size()); }
    Group *group_at(int p_index) const { return p_index >= 0 && p_index < int(groups.size()) ? groups[size_t(p_index)].get() : nullptr; }
    Group *group_of(int p_node) const { return find(p_node); }
    Group *anchor_group() const { return group_at(anchor); }
    int get_anchor_node() const { return anchor >= 0 ? groups[size_t(anchor)]->node : -1; }
    // 锚群平面上的一点 → 某群的平面（精确，经真球面）；反过来
    PlanetVec to_group(const Group &p_group, const godot::Vector3 &p_anchor_point) const;
    godot::Vector3 from_group(const Group &p_group, const PlanetVec &p_group_point) const;
    // 某群平面上某点处的朝向换到锚群平面（码头的泊位、出生位姿）
    godot::Transform3D transform_from_group(const Group &p_group, const godot::Transform3D &p_group_xform) const;
    // 这一点（锚群平面）在哪一群的陆地上 / 离哪一群的陆地最近（p_within 以内）；没有返回 nullptr
    Group *group_near(const godot::Vector3 &p_anchor_point, double p_within) const;

    // 换锚
    bool is_shift_pending() const { return pending >= 0; }
    bool begin_shift(GroupShift &r_shift) const;
    void commit_shift();
    // 瞬移、回位落到别的群的陆地上：先把它载入（同步）、升到细版，定下换锚（挂起，编排接着换）；已经是锚返回 false
    bool request_shift_toward(const godot::Vector3 &p_anchor_point);
    // 测试：直接定下换到某群（它得已经是细版、载入完了）
    bool force_shift_to(int p_node);

    // 给脚本
    godot::PackedInt32Array get_group_nodes() const;
    // { node, tier（0 远景 1 近景 2 细版）, anchor, land_km, loaded, loading, tiles, pending_tiles, hidden_far, ledgers_saved, plots_saved }
    godot::Dictionary get_group_state(int p_node) const;
    GroupWorld *get_group_world(int p_node) const;
    SiteStream *get_group_sites(int p_node) const;
    Hinterland *get_group_hinterland(int p_node) const;
    int get_shift_count() const { return shifts; }
    godot::Dictionary get_stats() const; // { groups, anchor, shifts, loads, unloads, detail, near }

    void set_auto_shift(bool p_value) { auto_shift = p_value; }
    bool get_auto_shift() const { return auto_shift; }
    void set_planet_path(const godot::NodePath &p_value) { planet_path = p_value; }
    godot::NodePath get_planet_path() const { return planet_path; }
    void set_home_path(const godot::NodePath &p_value) { home_path = p_value; }
    godot::NodePath get_home_path() const { return home_path; }
    void set_far_groups_path(const godot::NodePath &p_value) { far_groups_path = p_value; }
    godot::NodePath get_far_groups_path() const { return far_groups_path; }
    void set_worlds_dir(const godot::String &p_value) { worlds_dir = p_value; }
    godot::String get_worlds_dir() const { return worlds_dir; }
    void set_settlements_dir(const godot::String &p_value) { settlements_dir = p_value; }
    godot::String get_settlements_dir() const { return settlements_dir; }
    void set_detail_in(double p_value) { detail_in = p_value; }
    double get_detail_in() const { return detail_in; }
    void set_detail_out(double p_value) { detail_out = p_value; }
    double get_detail_out() const { return detail_out; }
    void set_near_in(double p_value) { near_in = p_value; }
    double get_near_in() const { return near_in; }
    void set_near_out(double p_value) { near_out = p_value; }
    double get_near_out() const { return near_out; }
    void set_shift_margin(double p_value) { shift_margin = p_value; }
    double get_shift_margin() const { return shift_margin; }
};

VARIANT_ENUM_CAST(GroupStream::Tier);
