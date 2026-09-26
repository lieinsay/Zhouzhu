#pragma once

#include <memory>
#include <string>
#include <vector>

#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "farm.h"
#include "ground_host.h"
#include "settlement.h"
#include "shared_looks.h"
#include "village_prep.h"
#include "village_sim.h"

// 一处聚落的村民（近处）：地面宿主（Island / WorldVillage / FlatMap）的子节点，变换留在原点（宿主的局部坐标就是它的局部坐标），要排在 Settlement 后面
// （宿主 rebuilt 时聚落先重盖，这里才重招人）。导航格铺宿主给的窗口（岛上是整座岛，岛群里是村子周围），村子附近有水而窗口够不着时往水那边扩。
// 农田是宿主指给的那一片（host_farm，可以挂在别的宿主下：岛群里的村种 World 下的农田），地块格换到自己宿主的坐标里用。
//
// 村子的一切——谁住哪、在干什么、背着什么、仓、水缸、认领的地——都在一本账（VillageState）里，这里不另存。
// 账本可以是自己开的（甲乙两岛、平板岛：一直在近处），也可以是远处账房借来的（set_ledger，岛群：走近了借来，离树时收回再还回去）。
// 每个物理帧把账本推进到世界钟（WorldClock）的此刻（village_sim，近处：沿导航格找路走），再照着账本摆小人；钟一下跳得很远（拨钟）就先估着走过去再展开。
// 借来的账本如果还在远处（没展开），先在远处把它结到此刻，再展开：地块放回农田，人摆到各自此刻该在的地方。
// 清单改了（布置模式）就按新清单重招人，仓和水缸留着。
//
// 导航格与挑水的线可以由上层在后台备好了交过来（set_prepared_nav，照哪张清单备的对得上才用），省得招人那一帧现算（邑治要三百多毫秒）。
// 几何是占位的小人：身子、头、斗笠、手里的家伙（锄 / 扁担水桶 / 长矛），头顶一行字「名字 · 在干什么」；材质与网格问宿主要共用的一份（host_looks）。
// 村民没有碰撞，进门就藏起来。
class Villagers : public godot::Node3D {
    GDCLASS(Villagers, godot::Node3D)

public:
    enum Phase {
        INDOORS,
        WALKING,
        WORKING,
    };

private:
    struct Body {
        godot::Node3D *root = nullptr;
        godot::Node3D *tool = nullptr; // 手里家伙的支点（干活时摆动）
        godot::Node3D *load = nullptr; // 背着的东西（粮袋 / 桶里的水）
        godot::Label3D *label = nullptr;
        std::string label_key; // 头顶那行字上回按什么写的；变了才重写
        double yaw = 0.0;
        double anim = 0.0;
    };

    godot::NodePath settlement_path = "../Settlement";
    int per_house = 2;
    int max_folk = 0;
    double walk_speed = 1.3;
    int64_t seed = 1;
    godot::Dictionary starting_store; // 自己开账时仓里有什么：物品 id → 份数

    GroundHost *host = nullptr;
    std::unique_ptr<MapGround> ground; // 宿主给的地面；宿主 rebuilt 时换新
    Settlement *settlement = nullptr;
    Farm *farm = nullptr; // 宿主指给的农田；没有为 nullptr
    std::unique_ptr<FieldBook> fields; // 包着 farm，好让村民动地时 Farm 重画
    PlotLattice plots; // 农田的地块格，换到本宿主的坐标
    std::vector<WaterLine> water; // 挑水去的水中线（本宿主坐标）
    NavGrid nav;
    SimRules rules;
    std::shared_ptr<VillageState> state; // 账本
    std::shared_ptr<ClaimRegistry> taken; // 全图认领表（远处账房的）；自己开账时为空
    bool borrowed = false; // 账本是借来的：离树时收回
    std::vector<Body> bodies;
    SharedLooks *looks = nullptr; // 宿主给的，或者 own_looks
    std::unique_ptr<SharedLooks> own_looks;
    godot::Ref<godot::StandardMaterial3D> mat_skin, mat_hat, mat_wood, mat_water, mat_sack;
    godot::Ref<godot::StandardMaterial3D> mat_job[4];
    std::shared_ptr<VillageNav> prepared_nav; // 后台备好的导航格与挑水线
    uint64_t prepared_nav_sig = 0; // 照哪张清单备的（layout_signature）

    void on_host_rebuilt();
    void on_layout_changed();
    godot::Transform3D host_transform() const { return host->host_node()->get_global_transform(); }
    double ground_y(double p_x, double p_z) const; // 局部坐标的地面高；没有地面 0
    double now() const; // 世界钟的秒
    const DayRules &day() const;
    void prepare(); // 按宿主与清单备好导航格、地块格、水
    void bring_up(bool p_refound); // 开账或接过账本、展开、摆出小人
    VillageWorld world(bool p_near);
    Inventory starting_inventory() const;
    void clear_bodies();
    void build_body(Body &r_body, const PersonState &p_person, int p_index);
    void place_body(Body &r_body, const PersonState &p_person, double p_delta);
    void refresh_label(Body &r_body, const PersonState &p_person);
    void collapse();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    Villagers();

    void _ready() override;
    void _physics_process(double p_delta) override;

    // 借远处账房的账本（进树之前调）：接过来时它在远处就先结到此刻再展开；离树时收回
    void set_ledger(const std::shared_ptr<VillageState> &p_state, const std::shared_ptr<ClaimRegistry> &p_taken);
    // 后台备好的导航格与挑水线（进树之前调）；p_layout_sig 是照哪张清单备的，招人时清单对得上才用
    void set_prepared_nav(const std::shared_ptr<VillageNav> &p_nav, uint64_t p_layout_sig) {
        prepared_nav = p_nav;
        prepared_nav_sig = p_layout_sig;
    }
    const VillageState *get_state() const { return state.get(); }

    // 给测试与 HUD 查
    int get_villager_count() const { return state ? int(state->people.size()) : 0; }
    godot::String get_villager_name(int p_index) const;
    godot::String get_villager_job(int p_index) const; // 职业 id
    godot::String get_villager_task(int p_index) const; // 活计 id
    godot::String get_villager_activity(int p_index) const; // 头顶那行字
    Phase get_villager_phase(int p_index) const;
    godot::Vector3 get_villager_position(int p_index) const; // 全局，脚底
    godot::Vector3 get_villager_target(int p_index) const; // 全局：眼下这件事要站的地方
    godot::Vector3 get_villager_door(int p_index) const; // 全局：自家门前
    godot::String get_villager_carrying(int p_index) const;
    int get_villager_home(int p_index) const;
    bool is_villager_visible(int p_index) const;
    godot::Array get_claimed_cells() const; // 农夫认领的地（Vector2i），不含够不着的
    int get_store_count(const godot::String &p_item) const;
    int get_grain() const; // 仓里加人背着的粮
    double get_water() const; // 水缸里几担
    double get_water_cap() const;
    int get_tally(const godot::String &p_task) const;
    godot::Dictionary get_ledger_summary() const; // 收 / 播 / 吃 / 饿 / 挑进 / 用掉 / 记到几秒
    int get_path_failures() const { return state ? state->tally.path_failures : 0; }
    double get_hour() const;
    int get_day() const;
    godot::String get_clock_text() const;
    double get_nav_height(const godot::Vector3 &p_global) const; // 该处地面高（全局）；没有地面 NAN

    void set_settlement_path(const godot::NodePath &p_path) { settlement_path = p_path; }
    godot::NodePath get_settlement_path() const { return settlement_path; }
    Farm *get_farm() const { return farm; }
    bool has_water() const { return !water.empty(); }
    void set_per_house(int p_value) { per_house = p_value; }
    int get_per_house() const { return per_house; }
    void set_max_folk(int p_value) { max_folk = p_value; }
    int get_max_folk() const { return max_folk; }
    void set_walk_speed(double p_value) { walk_speed = p_value; }
    double get_walk_speed() const { return walk_speed; }
    void set_seed(int64_t p_value) { seed = p_value; }
    int64_t get_seed() const { return seed; }
    void set_starting_store(const godot::Dictionary &p_value) { starting_store = p_value; }
    godot::Dictionary get_starting_store() const { return starting_store; }
};

VARIANT_ENUM_CAST(Villagers::Phase);
