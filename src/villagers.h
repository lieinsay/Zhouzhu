#pragma once

#include <memory>
#include <vector>

#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "farm.h"
#include "ground_host.h"
#include "settlement.h"
#include "villager_model.h"

// 一处聚落的村民：地面宿主（Island / WorldVillage / FlatMap）的子节点，变换留在原点（宿主的局部坐标就是它的局部坐标），要排在 Settlement 后面
// （宿主 rebuilt 时聚落先重盖，这里才重招人）。导航格铺宿主给的窗口（岛上是整座岛，岛群里是村子周围），村子附近有水而窗口够不着时往水那边扩。
// 农田是宿主指给的那一片（host_farm，可以挂在别的宿主下：岛群里的村种 World 下的农田），地块格换到自己宿主的坐标里用；
// 村子附近有能垦的地才有农夫，work.water_reach_m 以内有水才有挑水的，否则除了守望都闲着。max_folk 给人数封顶（0 = 不封）。
//
// 岛生成好、聚落清单就绪后按清单招人（assign_residents）、建导航格；清单改了（布置模式）就整村重招。
// 每个物理帧：钟点往前走；每个村民要么在屋里（藏起来，到了出工的钟点再错开几秒出门），要么沿路点走（贴地），
// 要么在干活（计时，到点把结果落下去：开荒 / 播种 / 收割走 Farm::perform，粮食进村里的仓，水挑回家），
// 干完一样就问 decide_task 下一样、找路。找不到路的活作废、原地歇一阵；够不着的荒地记下来不再挑。
//
// 村民的全部状态——在哪、在干什么、背着什么、各自的随机流——连同村里的仓、谁认领了哪块地、钟点，都在这里。
// 农夫只动自己认领的地，玩家垦的地他们不碰。村民没有碰撞（人能穿过去），也不进屋——进门就藏起来。
// 几何是占位的小人：身子、头、斗笠、手里的家伙（锄 / 扁担水桶 / 长矛），头顶一行字「名字 · 在干什么」。
class Villagers : public godot::Node3D {
    GDCLASS(Villagers, godot::Node3D)

public:
    enum Phase {
        INDOORS,
        WALKING,
        WORKING,
    };

private:
    struct Folk {
        VillagerMind mind;
        Rng rng = Rng(1);
        Task task;
        Phase phase = INDOORS;
        std::vector<NavPoint> path;
        size_t next = 0;
        double timer = 0.0; // 干活剩下的秒数；在屋里时是出门前还要磨蹭的秒数
        double speed = 1.3;
        double yaw = 0.0;
        double anim = 0.0;
        godot::Node3D *root = nullptr;
        godot::Node3D *tool = nullptr; // 手里家伙的支点（干活时摆动）
        godot::Node3D *load = nullptr; // 背着的东西（粮袋 / 桶里的水）
        godot::Label3D *label = nullptr;
    };

    godot::NodePath settlement_path = "../Settlement";
    int per_house = 2;
    int max_folk = 0;
    double start_hour = 7.5;
    double walk_speed = 1.3;
    int64_t seed = 1;
    godot::Dictionary starting_store; // 村里的仓开局有什么：物品 id → 份数

    GroundHost *host = nullptr;
    std::unique_ptr<MapGround> ground; // 宿主给的地面；宿主 rebuilt 时换新
    Settlement *settlement = nullptr;
    Farm *farm = nullptr; // 宿主指给的农田；没有为 nullptr
    PlotLattice plots; // 农田的地块格，换到本宿主的坐标
    std::vector<WaterLine> water; // 挑水去的水中线（本宿主坐标）
    NavRules nav_rules;
    DayRules day;
    WorkRules work;
    NavGrid nav;
    double clock = 0.0; // 从第 0 天子夜起的秒数
    Inventory store;
    std::vector<Claim> claims; // owner = −1 的是够不着的荒地
    std::vector<Folk> folk;
    int tallies[16] = {}; // 按 TaskKind 记干完了几件
    int path_failures = 0;
    godot::Ref<godot::StandardMaterial3D> mat_skin, mat_hat, mat_wood, mat_water, mat_sack;
    godot::Ref<godot::StandardMaterial3D> mat_job[4];

    void on_host_rebuilt();
    godot::Transform3D host_transform() const { return host->host_node()->get_global_transform(); }
    double ground_y(double p_x, double p_z) const; // 局部坐标的地面高；没有地面 0
    void on_layout_changed();
    void populate();
    void clear_folk();
    void build_body(Folk &r_folk, int p_index);
    VillageView view() const;
    void assign(int p_index); // 决策下一件事并找路
    void finish(int p_index); // 干完了：把结果落下去
    void enter_home(Folk &r_folk);
    void step_walk(Folk &r_folk, double p_delta);
    void place_body(Folk &r_folk);
    void refresh_label(const Folk &r_folk);
    void remove_claim(int p_i, int p_j);
    double hour() const { return read_clock(clock, day).hour; }

protected:
    static void _bind_methods();

public:
    Villagers();

    void _ready() override;
    void _physics_process(double p_delta) override;

    // 给测试与 HUD 查
    int get_villager_count() const { return int(folk.size()); }
    godot::String get_villager_name(int p_index) const;
    godot::String get_villager_job(int p_index) const; // 职业 id
    godot::String get_villager_task(int p_index) const; // 活计 id
    godot::String get_villager_activity(int p_index) const; // 头顶那行字
    Phase get_villager_phase(int p_index) const;
    godot::Vector3 get_villager_position(int p_index) const; // 全局，脚底
    godot::Vector3 get_villager_target(int p_index) const; // 全局：眼下这件事要站的地方
    godot::String get_villager_carrying(int p_index) const;
    int get_villager_home(int p_index) const;
    bool is_villager_visible(int p_index) const;
    godot::Array get_claimed_cells() const; // 农夫认领的地（Vector2i），不含够不着的
    int get_store_count(const godot::String &p_item) const;
    int get_tally(const godot::String &p_task) const;
    int get_path_failures() const { return path_failures; }
    double get_hour() const { return hour(); }
    int get_day() const { return read_clock(clock, day).day; }
    godot::String get_clock_text() const;
    void set_hour(double p_hour); // 拨钟：跳到今天（或明天，若已过）的这个钟点
    double get_nav_height(const godot::Vector3 &p_global) const; // 该处地面高（全局）；没有地面 NAN

    void set_settlement_path(const godot::NodePath &p_path) { settlement_path = p_path; }
    godot::NodePath get_settlement_path() const { return settlement_path; }
    Farm *get_farm() const { return farm; }
    bool has_water() const { return !water.empty(); }
    void set_per_house(int p_value) { per_house = p_value; }
    int get_per_house() const { return per_house; }
    void set_max_folk(int p_value) { max_folk = p_value; }
    int get_max_folk() const { return max_folk; }
    void set_day_seconds(double p_value) { day.day_seconds = p_value > 1.0 ? p_value : 1.0; }
    double get_day_seconds() const { return day.day_seconds; }
    void set_start_hour(double p_value) { start_hour = p_value; }
    double get_start_hour() const { return start_hour; }
    void set_walk_speed(double p_value) { walk_speed = p_value; }
    double get_walk_speed() const { return walk_speed; }
    void set_seed(int64_t p_value) { seed = p_value; }
    int64_t get_seed() const { return seed; }
    void set_starting_store(const godot::Dictionary &p_value) { starting_store = p_value; }
    godot::Dictionary get_starting_store() const { return starting_store; }
};

VARIANT_ENUM_CAST(Villagers::Phase);
