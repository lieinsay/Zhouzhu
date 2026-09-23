#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "day_clock.h"
#include "farm_model.h"
#include "field_book.h"
#include "ground_query.h"
#include "inventory.h"
#include "map_ground.h"
#include "rng.h"
#include "settlement_model.h"

// 村民：住在聚落的房子里，按钟点出门、沿路走到干活的地方、干完一样再找下一样、收工回家。不含 Godot 头。
//
// 分四块，全是「值进、值出」：
//   导航格（NavGrid）——地面 + 建筑清单 → 哪里走得了；A* 找路再拉直，给出宿主坐标的路点。
//   钟点在 day_clock.h（DayRules：几点出工几点收工、午间歇晌）。
//   职业与住户（assign_residents）——按聚落里有什么建筑、附近有没有能种的地和水，分派谁住哪、干哪一行。
//   决策（decide_task）——一个村民眼下该去哪、到了干什么、干多久。随机数流存在村民身上，由上层传进来。
// 村民的状态（在哪、在干什么、背着什么、认领了哪些地）都在上层（Villagers 节点）；这里不存任何东西。
//
// 坐标是聚落宿主的局部坐标（岛的局部坐标 / 村心为原点的岛群坐标），x 向东、z 向南、y 向上。

// ---------------------------------------------------------------- 导航格

struct NavRules {
    double cell_m = 1.25; // 导航格边长：高度场格子（2.5 m）的一半，房子之间的窄道也分得清
    double max_grade = 0.6; // 相邻两格之间的坡度上限（高差 / 水平距离）；岛缘的崖壁在 0.7 以上
    double building_margin_m = 0.9; // 占地外再让出这么多（台基外扩 0.6 + 人的半个身子）
    double water_cost = 4.0; // 蹚水的代价倍率：有干路就不下河
};

struct NavPoint {
    double x = 0.0, z = 0.0;
};

struct NavGrid {
    int cols = 0, rows = 0; // 列沿 x、行沿 z
    double cell_m = 1.0;
    double x0 = 0.0, z0 = 0.0; // 格 (0, 0) 的左上角
    std::vector<uint8_t> walk; // 0 = 走不了，1 = 干地，2 = 水里（河）
    std::vector<float> y; // 格心地面高

    enum : uint8_t { BLOCKED = 0,
        DRY = 1,
        WET = 2 };

    size_t index(int p_r, int p_c) const { return size_t(p_r) * size_t(cols) + size_t(p_c); }
    bool in_range(int p_r, int p_c) const { return p_r >= 0 && p_c >= 0 && p_r < rows && p_c < cols; }
    uint8_t at(int p_r, int p_c) const { return in_range(p_r, p_c) ? walk[index(p_r, p_c)] : uint8_t(BLOCKED); }
    bool walkable(int p_r, int p_c) const { return at(p_r, p_c) != BLOCKED; }
    // 点落在哪一格（越界返回 false）
    bool cell_of(double p_x, double p_z, int &r_r, int &r_c) const;
    NavPoint center(int p_r, int p_c) const { return { x0 + (p_c + 0.5) * cell_m, z0 + (p_r + 0.5) * cell_m }; }
    // 点所在的格走得了吗（越界 = 走不了）
    bool walkable_at(double p_x, double p_z) const;
    bool dry_at(double p_x, double p_z) const;
    int walkable_count() const;
};

// 在 (x0, z0) 起、cols × rows 格的窗口里建导航格（只在村子周围建，不必铺满整座岛）
NavGrid build_nav_grid(const NavGround &p_ground, double p_x0, double p_z0, int p_cols, int p_rows, const SettlementLayout &p_layout, const NavRules &p_rules);

// 离 (x, z) 最近的可走格心，p_max_m 以内；p_dry_only 只要干地。找不到返回 false
bool nav_nearest_walkable(const NavGrid &p_grid, double p_x, double p_z, double p_max_m, bool p_dry_only, NavPoint &r_point);
// 两点之间直走行不行：沿线每格都走得了、相邻两格坡度不超限
bool nav_line_clear(const NavGrid &p_grid, const NavRules &p_rules, double p_x0, double p_z0, double p_x1, double p_z1);
// A* 找路（八方向，斜走不切角）再拉直。返回自起点之后的路点，末点就是终点（两端各自先吸到最近的可走格）；
// 走不到返回空。起点与终点同格返回只含终点的一段
std::vector<NavPoint> nav_find_path(const NavGrid &p_grid, const NavRules &p_rules, double p_x0, double p_z0, double p_x1, double p_z1);

// ---------------------------------------------------------------- 职业与住户

enum class Job : uint8_t {
    FARMER, // 农：种自己认领的地，收成送进仓
    CARRIER, // 挑水：离家门最近的水边打水，挑到堂屋的公用水缸（没有堂屋就挑回家）
    WATCHMAN, // 守望：望楼下站班，隔一阵绕村巡一圈
    IDLER, // 闲：串门聊天（附近没地可种、没水可挑时，家里的人都是这样）
};

struct JobDef {
    Job job;
    const char *id; // "farmer" / "carrier" / "watchman" / "idler"
    const char *name; // 显示名
};

namespace villagers {
const JobDef &job(Job p_job);
const JobDef *job_by_id(const std::string &p_id); // 没有返回 nullptr
} // namespace villagers

struct Resident {
    std::string name; // UTF-8
    Job job = Job::IDLER;
    int home = -1; // 住哪一座（清单下标）
    int post = -1; // 守望守的是哪座望楼；别的职业 −1
};

// 分派：每座民居 p_per_house 口（头一口在有地可种时种地、否则闲；第二口在有水可挑时挑水、否则闲；再多的闲），
// 堂屋一口（闲），每座望楼一个守望——住在堂屋，没有堂屋就住离望楼最近的民居；聚落里没有能住的房子就没人。
// 名字从名字表里按 p_seed 确定地取，不重复（人多过表就加「二」「三」）。
std::vector<Resident> assign_residents(const SettlementLayout &p_layout, bool p_has_farm, bool p_has_water, int p_per_house, uint64_t p_seed);

// 建筑门前的站位（门外 p_out_m，宿主坐标）
NavPoint building_door(const Building &p_building, double p_out_m = 1.8);

// ---------------------------------------------------------------- 决策

enum class TaskKind : uint8_t {
    GO_HOME, // 回家进门（歇晌、收工）
    TILL, // 开荒
    SOW, // 播种
    HARVEST, // 收割
    TEND, // 照看庄稼（锄草）
    DELIVER, // 把背着的东西放下（粮进仓 / 水进堂屋的缸）
    FETCH_WATER, // 水边打水
    WATCH, // 望楼下站班
    PATROL, // 巡到某座房子前看一眼
    CHAT, // 串门，在人家门口聊一阵
    WAIT, // 原地歇一歇（没事可干 / 找不到路之后）
};

struct TaskDef {
    TaskKind kind;
    const char *id; // "go_home" / "till" / …
    const char *name; // 头顶上显示的「在干什么」
};

namespace villagers {
const TaskDef &task(TaskKind p_kind);
} // namespace villagers

struct Task {
    TaskKind kind = TaskKind::WAIT;
    double x = 0.0, z = 0.0; // 走到哪儿站着干
    double face_x = 0.0, face_z = 0.0; // 干活时面朝哪个点
    double seconds = 0.0; // 到了之后干多久（秒）
    int cell_i = -1, cell_j = -1; // 农活对的是哪一格地
    int building = -1; // 对着哪一座（家 / 仓 / 望楼 / 串门的那家）
    std::string seed; // 播种用哪样种子
};

// 一块被认领的地：谁种的
struct Claim {
    int i = 0, j = 0;
    int owner = 0; // 村民下标
};

struct WorkRules {
    int plots_per_farmer = 4; // 一个农夫最多认领几格地
    double field_radius_m = 55.0; // 只在家门口这么远以内开荒
    double water_reach_m = 300.0; // 挑水只去村子这么远以内的水
    double till_s = 6.0, sow_s = 3.0, harvest_s = 4.0, tend_s = 8.0;
    double deliver_s = 3.0, fetch_s = 5.0;
    double watch_s = 45.0; // 站一班多久
    int patrol_stops = 3; // 巡一圈看几座房子
    double patrol_s = 4.0;
    double chat_min_s = 8.0, chat_max_s = 20.0;
    double wait_s = 6.0;
    double stroll_min_m = 6.0, stroll_max_m = 15.0; // 独门独户的闲人在门前走多远歇着
    double wild_retry_s = 100.0; // 家门口找不到荒地可开：这么久（钟的秒，一天 40 分钟时是一个钟头）之后再找
};

// 决策要读的「村况」：全是只读的引用，谁也不在这儿改东西。
// 近处有导航格（nav），站不站得住问它；远处没有导航格，问地面（ground）与房子占地（layout）——同一条规矩的粗版本。
struct VillageView {
    const PlotLattice *plots = nullptr; // 农田的地块格，换到宿主坐标（没有农田为 nullptr）
    const std::vector<WaterLine> *water = nullptr; // 挑水去的水中线（宿主坐标）；没有水为空
    const SettlementLayout *layout = nullptr;
    const NavGrid *nav = nullptr; // 近处才有
    const MapGround *ground = nullptr; // 没有导航格时用它判断站不站得住（宿主坐标）
    const FieldBook *fields = nullptr; // 没有农田为 nullptr
    const std::vector<Claim> *claims = nullptr;
    const ClaimRegistry *taken = nullptr; // 全图已被认领的格子（别的村的）；可以为 nullptr
    const ArableMask *arable = nullptr; // 村子周围哪些格能垦（开账时勘察好的）；没有就现勘
    const Inventory *store = nullptr; // 村里的仓
    double water_stock = 0.0, water_cap = 0.0; // 堂屋水缸里有几担、装得下几担（cap ≤ 0 = 不限）
    const DayRules *day = nullptr;
    const WorkRules *work = nullptr;
    const NavRules *nav_rules = nullptr; // 远处判断「挨着房子」用它的 building_margin_m；nullptr 用默认
    double hour = 8.0;
    double now = 0.0; // 钟的秒（只用来记「多久以后再找荒地」）
};

// 这一点站得住吗（干地、不在房子占地里）：有导航格问导航格，没有就问地面与清单
bool view_dry_at(const VillageView &p_view, double p_x, double p_z);

// 一个村民决策时要知道的自己
struct VillagerMind {
    int index = 0; // 自己在村民表里的下标（认领表里的 owner）
    Resident who;
    double x = 0.0, z = 0.0; // 眼下站在哪
    std::string carrying; // 背着什么物品（"water" / 粮食的物品 id）；空 = 空手
    int carry_count = 0;
    int patrol_left = 0; // 守望：这一圈还要巡几座
    double wild_retry_at = 0.0; // 农夫：上回家门口没荒地可开，到这一秒之前不再找
    int water_spot = -1; // 挑水的：去哪打水（找过一次就记下）：−1 还没找，0 没有水，1 有
    double water_x = 0.0, water_z = 0.0, water_face_x = 0.0, water_face_z = 0.0;
};

// 下一件事。p_rng 是这个村民自己的随机流（上层存着，每个村民一条）；r_mind 里只有 patrol_left、wild_retry_at 与记下的打水处会被改
Task decide_task(VillagerMind &r_mind, const VillageView &p_view, Rng &p_rng);

// 农夫挑地的规矩单独露出来，单测用：离 (x, z) 最近、离家门 field_radius_m 以内、本村与别的村都没认领、能开荒、格心与四角站得住的一格。
// 找不到返回 false
bool pick_wild_plot(const VillageView &p_view, double p_home_x, double p_home_z, int &r_i, int &r_j);
// 挑水的站位：水边离 (x, z) 最近、站得住、紧挨着水的一点；r_water 是面朝的水面点。没有水返回 false
bool pick_water_spot(const VillageView &p_view, double p_x, double p_z, NavPoint &r_stand, NavPoint &r_water);
// 地块格 (i, j) 的格心（宿主坐标）
NavPoint plot_center(const PlotLattice &p_plots, int p_i, int p_j);
