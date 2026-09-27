#pragma once

#include <functional>
#include <future>
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
#include "ground_works.h"
#include "group_memory.h"
#include "weather.h"
#include "planet_frame.h"
#include "resource_scatter.h"
#include "resource_sites.h"
#include "world_sites.h"

class FarGroups;
class GroupProvider;
class GroupWorld;
class Hinterland;
class PlanetView;
class ScatterStream;
class SiteStream;

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
// 行星计划 P4：
//   岛群包从 GroupProvider 来（仓库 / 世界存档目录 / 按需生成）：开局认得的是已有包的群，生成好一群（group_ready）就加进来——群只往后加，下标不变；
//   离开的群的账另存进世界存档目录（GroupProvider::save_memory），退出再进还在；换场景、关窗前 save_world 把载入着的群的账也存下；
//   跨群瞬移（jump_*）：要去的群先要到包（按需生成）、在后台载入、升到细版、把落点周围的地面块先建好，好了再定下换锚（编排接着换人船、摆到落点）；
//   换锚的规矩补上「挨着的群」：脚下是别的群的地、不是锚群的地，就换过去（不等云海上空、不看回滞）；两群的地叠着的地方不换。
// 锚跟着人走（god 飞到高空绕着行星转、再落地时查出来的：原来只在细版的群之间换锚，高空巡航时一群也进不了细版，锚一直是出发那一群，
// 人在它的平面里跑出几千上万公里、一圈下来平面坐标 4 万公里——超过 AEQD 的对跖点，画面弯曲的展开式失效、锚群的地在平面里够不着）：
//   平面坐标规范化：焦点离锚群心远过对跖点（πR）就「换到锚群自己」（rebase_point(a, a, p) 给出同一点在对跖点以内的写法），坐标不超过半圈；
//   远离锚群时换到近的群：焦点离锚群的陆地远过 far_anchor_m、平着挪得比 far_anchor_speed 慢（停下、往下落、开船），
//   就挑认得的群里离焦点的陆地最近的（比锚群近 far_anchor_gain_m 以上）载入、升细版、换过去——不看脚下有没有地；
//   焦点下面最近的那一群还没有包就向 GroupProvider 要（它比认得的都近、正在生成，就等它生成好了直接换过去，不先载入远处的一群）。
//   高空巡航时只做规范化，停下来、慢下来再换，免得一路上一群群地载入。
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
        bool pinned = false; // 瞬移要去的：不管多远都载入、升到细版
        GroupMemory memory;
        std::shared_future<std::map<int, std::string>> ledgers_saving; // 细版 → 近景时各村的账在后台存成文本；好了并进 memory.ledgers、存进存档目录
        // 玩家营造的账（切片十一：改地形、搭建件、摆件）：登记时从世界存档目录读（works/<节点>.txt），载入时交给这一群的 GroupWorld，
        // 拆了也留着（飞走再回来还在）；改了隔几秒存一回（works_saved 是上回存下的文本）
        std::shared_ptr<GroundWorks> works;
        std::string works_saved;
    };
    enum JumpPhase {
        JUMP_NONE,
        JUMP_GENERATING, // 等 GroupProvider 生成包
        JUMP_LOADING, // 后台载入、升细版、落点周围的地面块
        JUMP_READY,
        JUMP_FAILED,
    };

private:
    godot::NodePath planet_path = "../PlanetView";
    godot::NodePath home_path = "../World";
    godot::NodePath far_groups_path = "../FarGroups";
    godot::NodePath provider_path = "../GroupProvider";
    godot::String worlds_dir = "res://worlds";
    godot::String settlements_dir = "res://settlements";
    double detail_in = 30000.0, detail_out = 40000.0;
    double near_in = 55000.0, near_out = 70000.0;
    double shift_margin = 2000.0;
    double far_anchor_m = 150000.0;
    double far_anchor_gain_m = 50000.0;
    double far_anchor_speed = 3000.0;
    double check_interval = 0.25;
    double place_step = 200.0;
    int max_detail = 2;
    bool auto_shift = true;

    PlanetView *planet = nullptr;
    FarGroups *far_groups = nullptr;
    GroupProvider *provider = nullptr;
    std::vector<std::unique_ptr<Group>> groups;
    bool ready = false;
    int anchor = -1;
    int home_node = -1; // 场景里摆的那一群
    int pending = -1;
    godot::Vector3 focus;
    bool has_focus = false;
    double since_check = 1e9;
    int shifts = 0;
    int rebases = 0; // 平面坐标规范化（换到锚群自己）几次
    int far_shifts = 0; // 远离锚群时换到近的群几次
    int far_target = -1; // 为了远离锚群时换过去而载入的那一群（下标）
    double sim_time = 0.0; // 累计的 _process 时间（秒；焦点平着挪多快按它，不按真实时间：无头下模拟比真实快）
    PlanetVec speed_ref; // 上一回量速度时焦点在行星系里的方向（单位向量）
    double speed_ref_t = -1.0;
    double focus_speed = 0.0; // 焦点平着挪多快（米 / 秒，按真球面，换锚不跳）
    void measure_speed();
    bool canonical_focus() const; // 焦点在锚群平面里没超过对跖点
    void update_far_anchor();
    void drop_far_target();
    int loads = 0, unloads = 0;
    // 行星天气场（行星计划 P5）：PlanetView 读好了逐日天气包，就挂到载入了的各群的天气模型上（群与群之间的天气、云团从场里取）
    void attach_weather();
    // 跨群瞬移
    int jump_node = -1;
    JumpPhase jump_phase = JUMP_NONE;
    godot::String jump_error;
    bool jump_landing_set = false;
    godot::Transform3D jump_spawn; // 落点（那一群的平面）
    godot::Transform3D jump_berth;
    bool jump_has_berth = false;
    godot::String jump_where;
    double jump_started = 0.0;
    void update_jump();
    bool find_landing(Group &r_group);
    Group *add_group(int p_node, const godot::String &p_dir);
    // 读一群的「登记」（world.json 开头的群心、sites.tsv、粗版的陆地格、存档目录里的账）：只碰文件与纯 C++ 的数据，能在后台线程里做
    std::unique_ptr<Group> make_group(int p_node, const godot::String &p_dir) const;
    Group *adopt_group(std::unique_ptr<Group> p_group);
    // 按需生成好的群在后台登记（大群的 sites.tsv 几千行，主线程读要二三十毫秒），好了下一帧加进来
    std::map<int, std::future<std::unique_ptr<Group>>> adding;
    void request_add(int p_node, const godot::String &p_dir);
    void poll_adding();
    void on_group_ready(int p_node, const godot::String &p_dir);
    void save_memory(const Group &p_group) const;
    double works_save_timer = 0.0;
    void save_works(Group &r_group, bool p_force); // 营造账变了就存（p_force：不看隔多久）
    void finish_ledgers(Group &r_group, bool p_wait); // 后台存账做完了（或 p_wait 等它做完）：并进 memory.ledgers、存进存档目录
    // 拆一群（降档）不在一帧里拆完（行星计划 P4 量出来的：2051 的村子、地面块一下子拆掉主线程要两三百毫秒）：
    // 节点先停下、改名（同名的新节点照旧叫 World / SiteStream……）、交给「坟地」，每帧按 reap_budget_ms 从叶子往上拆一点
    std::vector<godot::ObjectID> graveyard;
    int buried = 0;
    double reap_budget_ms = 3.0;
    double drop_hold_until = 0.0; // 换锚以后这一会儿先不降档（落地那几帧留给新锚群挂地面、铺村子；旧锚两秒后再拆）
    void bury(godot::Node *p_node);
    void reap();

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
    void _notification(int p_what);

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

    // 跨群瞬移（行星计划 P4）：jump_begin 开始备（没有包就要 GroupProvider 生成）；jump_poll 每帧问——0 还在备 1 好了 −1 失败（r_text 说在做什么 / 为什么）；
    // 好了以后 jump_commit 定下换锚（挂起，编排接着 apply_shift）；换过去以后 jump_landing 给落点：主码头的停机坪与泊位，没有码头就是邑治（锚群平面）
    bool jump_begin(int p_node, godot::String &r_why);
    int jump_poll(double &r_progress, godot::String &r_text);
    bool jump_commit();
    bool jump_landing(godot::Transform3D &r_spawn, godot::Transform3D &r_berth, bool &r_has_berth, godot::String &r_where);
    void jump_end(); // 落地了 / 不去了：放下「要去的」
    int get_jump_node() const { return jump_node; }
    // { node, phase（0 没有 1 生成 2 载入 3 好了 4 失败）, error, seconds }
    godot::Dictionary get_jump_state() const;
    // 换场景、关窗前：载入着的群的账也存进世界存档目录（离开的群的在离开时就存了）
    void save_world();

    // 给脚本
    godot::PackedInt32Array get_group_nodes() const;
    // { node, tier（0 远景 1 近景 2 细版）, anchor, land_km, loaded, loading, tiles, pending_tiles, hidden_far, ledgers_saved, plots_saved }
    godot::Dictionary get_group_state(int p_node) const;
    GroupWorld *get_group_world(int p_node) const;
    // 某群平面上的一点 → 锚群平面；反过来（经真球面，精确；测试、工具用）
    godot::Vector3 to_anchor(int p_node, const godot::Vector3 &p_group_point) const;
    godot::Vector3 from_anchor(int p_node, const godot::Vector3 &p_anchor_point) const;
    SiteStream *get_group_sites(int p_node) const;
    Hinterland *get_group_hinterland(int p_node) const;
    int get_shift_count() const { return shifts; }
    int get_rebase_count() const { return rebases; } // 平面坐标规范化了几次（锚没换）
    int get_graveyard_size() const { return int(graveyard.size()); } // 还没拆完的节点（一群的 World、村子……各算一个）
    godot::Dictionary get_stats() const; // { groups, anchor, shifts, rebases, far_shifts, far_target, focus_speed, loads, unloads, detail, near }

    void set_auto_shift(bool p_value) { auto_shift = p_value; }
    bool get_auto_shift() const { return auto_shift; }
    void set_provider_path(const godot::NodePath &p_value) { provider_path = p_value; }
    godot::NodePath get_provider_path() const { return provider_path; }
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
    void set_far_anchor_m(double p_value) { far_anchor_m = p_value; }
    double get_far_anchor_m() const { return far_anchor_m; }
    void set_far_anchor_gain_m(double p_value) { far_anchor_gain_m = p_value; }
    double get_far_anchor_gain_m() const { return far_anchor_gain_m; }
    void set_far_anchor_speed(double p_value) { far_anchor_speed = p_value; }
    double get_far_anchor_speed() const { return far_anchor_speed; }
};

VARIANT_ENUM_CAST(GroupStream::Tier);
