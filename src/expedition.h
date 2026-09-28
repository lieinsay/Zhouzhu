#pragma once

#include <vector>

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "aim.h"
#include "build_mode.h"
#include "dig_hand.h"
#include "farm_hand.h"
#include "freight_hand.h"
#include "god_works.h"
#include "gather_hand.h"
#include "helm.h"
#include "interior_edit.h"
#include "item_slots.h"
#include "map_host.h"
#include "ground_works.h"
#include "pack_hand.h"
#include "piece_hand.h"
#include "place_hand.h"
#include "player.h"
#include "refit_bench.h"
#include "ship.h"
#include "world_map.h"

class StylePanel;
class DevWorksPanel;
class WorksView;

// 玩家编排：哪张地图都是它（甲乙两岛 main.tscn、岛群 world_2051.tscn、平板岛测试图），场景只配属性。
// 地图只认地图宿主（MapHost，map 属性指过去）：地面、码头、船坞、农田、脚下的聚落、流式焦点、岛群地图面板都问它。
// 玩法是几个与场景无关的部件，这里持有、按优先级调度：
//   掌舵（Helm）：上舵、飞（R/F 换档）、叫靠泊 / 入坞、离舵；
//   背包与手持（PackHand，Tab）、农事（FarmHand，按住 E）、改装（RefitBench，画样台按 E）、布置模式（BuildMode，B，开发者）、
//   跑货（FreightHand：港口村的村仓前接托运、打包、交货，船上货位前装卸；资源点的堆场把背包里的原料打成箱、抱着自家的箱子在堆场 / 村仓拆回背包；
//   抱着货箱不能掌舵、不能改船，船上有货也不能改船）、采集（GatherHand：资源点的采掘面前按住 E，一份份采进背包）、
//   室内编辑（InteriorEdit，T，开发者：布置模式里光标下那座，或人站在里面的那座）、
//   摆放（PlaceHand：手上拿着能摆的东西时身前出虚影、E 摆、X 收起；E 打开木箱 / 工作台 / 窑）、
//   搭建（PieceHand，切片十一：手上拿着搭建件时准星处出吸附好的虚影、E 摆、X 拆、E 开关门；G 挑整座样式一次摆下）、
//   挖与垒（DigHand：手上拿着木锹 / 石镐按住 E 挖准星那一格、拿着土按 E 垒）。
//   开发者营造（god 下 F8）：面板里挑挖、垒、一样搭建件或一套样式拿在手上，挖垒与搭建就用它、不看背包不扣料；挖垒带笔刷（[ ] 换）、按一下改一级。
// 营造的账（摆件、搭建件、改地形）按脚下那处地面问地图宿主要（map_works_at：岛群每一群一份、存进世界存档）；地图不给的（甲乙两岛、平板岛）用编排自带的一份（全局坐标、不能改地形）。
// 准星：拿着搭建件、锹镐、土时屏幕中心一个点，相机往前的射线打到的第一样东西就是准星落点（每帧打一回交给搭建与挖垒）。
// 背包（Tab）照 MC：36 格、前 9 格是快捷栏（1–9 / 滚轮选，拿在手上的就是选中那格）；人在船上时船舱（每节储存区 12 格，账在这里）列在背包上面，
// 开着木箱时是木箱；右边是配方，身边 3 m 有工作台 / 窑就多几条能合。开着背包时鼠标放出来，点法照 MC，键盘也能全做。
// 另外：开局把船停在出发码头的泊位上、人放在它的停机坪上；HUD（位置、目标、背包、钟点、提示）；M 开合岛群地图、开发者点图瞬移；
// 站稳记落脚点、掉进云海回位并召回漂远的船；到了目的码头那座岛就算到了（arrived），之后目标改指回程；接了托运时目标改指目的村的船台。
// 泊位候选：地图的码头、船坞，加上各处聚落里盖好的船台（Settlement 组里找，船停不下的带着原因）；都够不着时，
// 船下是块落得下的平地就多一个「野泊」（wild_landing 按地图的地面算、船身压不压着房子问 Ship），落下后放舷梯、上舵时收起。
// 「现在是谁在受控」「哪个面板开着」「到没到过对岸」「上一处站稳的地方」这些状态只在这里（和它肚子里的部件）；
// Player、Ship、Farm、Shipyard、Settlement、各面板互不相识，各自只回答自己的事。
class Expedition : public godot::Node3D {
    GDCLASS(Expedition, godot::Node3D)

public:
    enum State {
        ON_FOOT,
        PILOTING,
        MOORING,
    };
    static constexpr const char *MAP_GROUP = "world_map"; // 有岛群地图面板的编排进这个组（开发者工具据此知道这张图按 M 有没有地图）

private:
    godot::NodePath map_path = "Map";
    godot::NodePath ship_path = "Ship"; // 可以不接：那就只能走
    godot::NodePath player_path = "Player";
    int origin_dock = 0; // 地图的码头号
    int destination_dock = 1;
    double helm_range = 2.2; // 离舵多近能上舵；离画样台多近能改船
    double dock_range = 45.0; // 离泊位多近能叫靠泊
    double leave_helm_speed = 3.0; // 半空中离舵：船速须低于此
    double kill_altitude = -30.0; // 掉到这以下算坠入云海
    double reach = 1.8; // 够得着身前（视线方向）多远的那一格地
    double arrive_radius = 500.0; // 在目的码头那座岛上、离码头这么近站稳就算到了
    double recall_distance = 60.0; // 坠落回位时船离落脚点比这远，就召回离那儿最近的码头
    double land_search_m = 3000.0; // 瞬移落点是云海时，往外找多远的陆地
    double focus_lead_s = 3.0; // 掌舵时流式焦点提前船头几秒
    double notice_seconds = 2.5;
    godot::Dictionary starting_pack; // 开局背包：物品 id → 份数
    bool dev_tools = true; // 开发者工具（布置模式、地图瞬移）开不开

    MapHost *map = nullptr;
    Ship *ship = nullptr;
    Player *player = nullptr;
    Helm helm;
    PackHand pack;
    ItemSlots hold; // 船舱：格数跟着船的储存区走
    PlaceHand place;
    FarmHand farm_hand;
    FreightHand freight;
    GatherHand gather;
    RefitBench refit;
    BuildMode build;
    InteriorEdit interior;
    DigHand dig;
    PieceHand piece;
    GroundWorks fallback_works; // 地图不给营造账时自带的一份（全局坐标、不能改地形）
    WorksView *fallback_view = nullptr;
    godot::Control *crosshair = nullptr;
    StylePanel *style_panel = nullptr;
    bool style_open = false;
    // 开发者营造（god 下 F8）：面板开没开、光标在第几行（0 = 空手，往下是 god_tool_list 的次序）、手上拿着第几件（−1 没有）、笔刷
    DevWorksPanel *dev_works_panel = nullptr;
    bool dev_works_open = false;
    int god_cursor = 0;
    int god_tool = -1;
    int god_brush = 3;
    bool aim_override = false; // 测试另给一条准星射线
    godot::Vector3 aim_from, aim_to;
    AimHit aim; // 这一帧的准星
    int origin_index = -1; // 地图的码头下标
    int destination_index = -1;
    bool arrived = false;
    bool has_safe_footing = false;
    godot::Vector3 safe_footing;
    godot::Vector3 spawn_point;
    double notice_time = 0.0;
    godot::String notice;
    bool map_open = false;
    bool map_viewed = false;
    // 背包开着时另一边是什么：船舱、摆出来的某件（木箱 / 工作台 / 窑），或什么都没有
    enum PackSide {
        SIDE_NONE,
        SIDE_HOLD,
        SIDE_PROP,
    };
    PackSide pack_side = SIDE_NONE;
    int pack_prop = -1;

    // 野泊：这一个物理帧算好的候选（berths() 一帧里会被问好几回）
    uint64_t wild_frame = ~uint64_t(0);
    bool wild_ok = false;
    Berth wild;

    godot::Label *status_label = nullptr;
    godot::Label *prompt_label = nullptr;
    godot::Label *notice_label = nullptr;
    WorldMap *map_panel = nullptr;

    Say say_fn() {
        return [this](const godot::String &p_text) { say(p_text); };
    }
    void say(const godot::String &p_text);
    void build_hud();
    void update_hud();
    void sync_helm_rules();
    void board();
    // 开合各个面板：同一时刻只开一个；开着的时候人站住、手上的活作废
    void close_panels();
    void set_pack_open(bool p_open, PackSide p_side = SIDE_NONE, int p_prop = -1);
    void sync_pack_side(); // 开着背包时每帧：另一边的容器、身边的设施
    void sync_hold(); // 船舱格数跟上船
    int bay_in_use() const; // 船舱里的东西并叠后占几格
    void place_act(bool p_interact, bool p_remove); // 这一下 E / X 归摆放：收回、打开（开背包）、摆下
    WorksSite works_at(const godot::Vector3 &p_global); // 这一点的营造账（地图给的，或自带的那一份）
    AimHit compute_aim() const;
    void set_style_open(bool p_open);
    void refresh_style_panel();
    // 开发者营造
    std::vector<GodTool> god_tools() const { return god_tool_list(piece.get_style_count()); }
    bool god_tool_on() const; // god 开着、手上拿着开发者工具
    std::string held_now() const; // 挖垒与搭建看的「手上拿着的」：开发者工具，或快捷栏选中那格
    godot::String god_tool_name(const GodTool &p_tool) const;
    void set_dev_works_open(bool p_open);
    void refresh_dev_works_panel();
    void dev_works_browse(bool p_interact);
    void set_map_open(bool p_open);
    void on_foot(double p_delta, bool p_interact);
    void toggle_interior(const godot::Vector3 &p_at);
    std::vector<Berth> berths();
    // 野泊：船原点在 p_at、偏航 p_yaw 时落不落得下（地面 + 船身与舷梯压不压着实物）；落得下给出泊位，否则 r_why 说为什么
    bool plan_wild(const godot::Vector3 &p_at, double p_yaw, Berth &r_berth, godot::String &r_why) const;
    void refresh_wild();
    godot::Vector3 focus_point() const;
    void track_footing();
    int course_target() const; // 航行目标（码头下标）：没到过对岸是目的码头，到过了是回程（接了托运另说，见 freight_target）
    bool freight_target(godot::Transform3D &r_target, godot::String &r_name) const; // 接了托运：目的村的船台（还远就是村心）
    void respawn();
    // 换锚（行星计划 P3）：地图宿主定下了（或瞬移、回位落到别的群要它定），人、船、摆出来的东西、自己记的位置（落脚点、出生点，
    // p_extra 里的点）都换到新锚群的平面，再让宿主收尾。宿主没有挂起的换锚返回 false
    bool apply_shift(const std::vector<godot::Vector3 *> &p_extra = {});
    int shifts = 0; // 换了几次锚
    // 跨群瞬移（行星计划 P4，开发者；行星地图点一下）：宿主把那一群备好（按需生成、后台载入、落点周围的地面），好了换锚、把人船摆到落点
    enum JumpStage {
        JUMP_IDLE,
        JUMP_PREPARE, // 宿主在备
        JUMP_LAND, // 换过去了，等脚下的碰撞挂上
    };
    JumpStage jump_stage = JUMP_IDLE;
    int jump_node = -1;
    uint64_t jump_t0 = 0;
    double jump_progress = 0.0;
    godot::String jump_text;
    godot::Vector3 jump_spawn;
    godot::String jump_where;
    int jump_land_frames = 0;
    double jump_prepared_ms = 0.0;
    godot::Dictionary last_jump; // 上一回瞬移的结果（给测试看）
    int course_node = -1; // 设了航向的群（行星地图，行星计划 P4）
    void update_jump();
    void finish_jump(bool p_ok, const godot::String &p_why);
    double last_shift_ms = 0.0; // 上回换锚这一步花了多久（主线程，毫秒）
    int nearest_open_dock(const godot::Vector3 &p_point) const;
    bool nearest_land(const godot::Vector3 &p_point, godot::Vector3 &r_point) const;
    godot::String clock_text() const;

public:
    godot::String get_status_text() const; // HUD 左上那几行（给测试看）

private:

protected:
    static void _bind_methods();

public:
    Expedition();

    void _ready() override;
    void _process(double p_delta) override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    // 把玩家放到该处（全局 xz；落在地面上）。落点没有地就找最近的陆地；都找不到返回 false
    bool teleport_to(const godot::Vector3 &p_position);
    // 跨群瞬移（行星计划 P4，开发者）：去群 p_node 的主码头（没有码头就邑治）；没生成过的当场生成。掌舵时不行。开始了返回 true
    bool request_planet_jump(int p_node);
    void cancel_planet_jump();
    // { node, stage（0 没有 1 在备 2 落地）, progress, text, elapsed_ms } 与上一回的结果 last：{ node, ok, why, total_ms, prepare_ms, where }
    godot::Dictionary get_jump_state() const;
    // 行星地图设航向：HUD 报那一群的方位与距离，到了那一群就清掉；−1 = 不设
    void set_planet_course(int p_node);
    int get_planet_course() const { return course_node; }
    void on_teleport_requested(const godot::Vector3 &p_position);

    // 给测试与 HUD 查
    State get_state() const { return State(helm.get_state()); }
    bool has_arrived() const { return arrived; }
    godot::String get_notice() const { return notice_time > 0.0 ? notice : godot::String(); }
    godot::String get_prompt() const { return prompt_label ? prompt_label->get_text() : godot::String(); }
    godot::Vector3 get_spawn_point() const { return spawn_point; }
    int get_shift_count() const { return shifts; }
    double get_last_shift_ms() const { return last_shift_ms; }
    godot::Transform3D get_origin_berth() const;
    godot::Transform3D get_destination_berth() const;
    bool is_map_open() const { return map_open; }
    WorldMap *get_map() const { return map_panel; }
    godot::Node3D *get_map_node() const { return map ? map->map_node() : nullptr; }
    bool is_working() const { return farm_hand.is_working(); }
    // 采集与野泊（给测试查）
    bool is_gathering() const { return gather.is_working(); }
    int get_gather_site() const { return gather.get_site(); } // 够得着的采掘面是地图的第几处资源点；没有 −1
    bool is_holding_goods() const { return freight.is_holding_goods(); }
    godot::PackedStringArray get_cargo_items() const; // 船上每个货位放着什么（空串 = 空位）
    godot::String get_wild_note() const { return helm.wild_note; }
    bool has_wild_berth() const { return wild_ok; }
    // 测试挑落点用：船在 p_at、偏航 p_yaw 时野泊得下吗 { ok, why, side, run, rise, door, out }（与掌舵时同一套判定）
    godot::Dictionary probe_wild(const godot::Vector3 &p_at, double p_yaw) const;
    // 跑货（给测试查）
    bool is_carrying() const { return freight.is_holding(); }
    int get_cargo_aboard() const { return freight.cargo_aboard(); }
    bool has_freight_order() const { return freight.is_active(); }
    int get_freight_delivered() const { return freight.get_book().get_delivered(); }
    godot::String get_freight_status() const { return map ? freight.status_text(map) : godot::String(); }
    godot::Dictionary get_freight_order() const; // 接下的那一单：from / to（村号）、island（目的村的岛）、item、crates、per_crate、fee；没接单为空
    bool is_pack_open() const { return pack.is_open(); }
    int get_pack_cursor() const { return pack.get_focus_index(); }
    godot::String get_held_seed() const { return godot::String::utf8(pack.held_seed().c_str()); }
    int get_item_count(const godot::String &p_item) const { return pack.items().count(play::to_std(p_item)); }
    godot::PackedStringArray get_pack_items() const;
    // 格子背包、快捷栏、船舱、合成、摆放（给测试查）
    int get_hotbar_selected() const { return pack.get_selected(); }
    godot::String get_held_item() const { return godot::String::utf8(pack.held_item().c_str()); }
    godot::Dictionary get_pack_slot(int p_slot) const; // { item, count }
    godot::Dictionary get_pack_cursor_stack() const; // 鼠标上拖着的 { item, count }
    godot::Vector2i get_pack_focus() const { return godot::Vector2i(pack.get_focus_section(), pack.get_focus_index()); }
    godot::Vector2 get_pack_slot_center(int p_section, int p_index) const { return pack.slot_center(p_section, p_index); } // 屏幕坐标；section 0 背包 1 另一边 2 配方
    godot::String get_pack_other_title() const { return pack.is_open() && pack.get_other() ? pack.get_other_title() : godot::String(); }
    int get_other_count(const godot::String &p_item) const { return pack.get_other() ? pack.get_other()->count(play::to_std(p_item)) : 0; }
    int get_hold_size() const { return hold.size(); }
    int get_hold_count(const godot::String &p_item) const { return hold.count(play::to_std(p_item)); }
    int get_recipe_index(const godot::String &p_id) const;
    int get_stations() const { return int(pack.get_stations()); }
    godot::Array get_placed_props() const; // [{ id, item, x, y, z, quarter, used }]
    godot::Dictionary get_ghost() const; // { on, ok, why, x, y, z, quarter }
    int get_facing_prop() const { return place.get_facing(); }
    bool is_aboard() const { return ship && player && ship->is_aboard(player->get_global_position()); }
    bool is_refit_open() const { return refit.is_open(); }
    // 改装（测试用）：拖着的节点 id（没有空串）；选中的节点 "x,y,z:kind"（没有空串）；鼠标指着的 { type: "node"/"spot"/"", anchor, legal, reason }；
    // 亮着的格子；某张卡片 / 拆除钮在屏幕上的中心；屏幕上哪一点指着锚点 anchor 的落点（kind 是拖着的节点）或节点（kind 空串）——没有返回 (−1, −1)
    godot::String get_refit_drag() const;
    godot::String get_refit_selected() const;
    godot::Dictionary get_refit_hover() const;
    godot::Array get_refit_glow() const;
    godot::Vector2 get_refit_card_center(const godot::String &p_kind) const;
    godot::Vector2 get_refit_remove_center() const { return refit.remove_center(); }
    godot::Vector2 get_refit_aim(const godot::Vector3i &p_anchor, const godot::String &p_kind) const;
    bool is_ship_in_yard() const; // 空艇停在哪座坞的坞位上（改得了）
    bool is_build_open() const { return build.is_open(); }
    godot::Vector3 get_build_cursor() const { return build.is_open() ? build.cursor_snapped() : godot::Vector3(); }
    godot::String get_build_kind() const { return build.get_kind(); }
    int get_build_selected() const { return build.get_selected(); }
    double get_build_yaw_deg() const { return build.get_yaw_deg(); }
    double get_build_camera_yaw() const { return build.get_camera_yaw(); } // 布置视角的相机方位：W 把光标往 (−sin, 0, −cos) 挪
    godot::Node *get_build_target() const { return build.get_target(); } // 布置模式改的是哪个 Settlement
    godot::String get_build_style() const { return build.get_style(); }
    bool is_interior_open() const { return interior.is_open(); }
    godot::Vector3 get_interior_cursor() const { return interior.is_open() ? interior.get_cursor_global() : godot::Vector3(); }
    godot::String get_interior_tool() const { return interior.get_tool_name(); }
    int get_interior_level() const { return interior.get_level(); }
    // 改地形与搭建（切片十一，给测试查 / 驱动）：另给一条准星射线（全局坐标，不用相机）/ 还给相机；
    // 挖垒的准星那一格 { on, ok, why, i, j, working }；搭建的虚影（PieceHand::ghost_state）；准星对着的 (座, 件)；
    // 脚下那处地面的营造账里有几座 [{ id, x, y, z, yaw, pieces, complete, missing }] 与计数 { structures, pieces, props, edited_cells }；
    // 整座样式：名字、开没开面板、拿起第几套；人在不在一座完整的屋里
    void set_aim_ray(const godot::Vector3 &p_from, const godot::Vector3 &p_to);
    void clear_aim_ray() { aim_override = false; }
    godot::Dictionary get_dig_target() const;
    godot::Dictionary get_piece_ghost() const { return piece.ghost_state(); }
    godot::Vector2i get_look_piece() const { return godot::Vector2i(piece.get_look_structure(), piece.get_look_piece()); }
    godot::Array get_structures();
    godot::Dictionary get_works_counts();
    godot::PackedStringArray get_style_names() const;
    bool is_style_open() const { return style_open; }
    int get_style_index() const { return piece.get_style_index(); }
    void select_style(int p_index);
    bool is_inside_house();
    // 开发者营造（给测试查 / 驱动）：拿起清单里第几件（−1 放下；god 没开返回 false）；手上那件 { index, kind, item, style, name, brush, active }；
    // 笔刷；清单每件的名字；面板开没开
    bool set_god_tool(int p_index);
    godot::Dictionary get_god_tool() const;
    void set_god_brush(int p_size);
    int get_god_brush() const { return god_brush; }
    godot::PackedStringArray get_god_tool_names() const;
    bool is_dev_works_open() const { return dev_works_open; }
    godot::Node3D *get_prop_node(int p_id); // 脚下那处地面的营造账里某件摆件的实物（测试查）
    godot::String get_interior_template() const { return interior.is_open() ? interior.get_template_id() : godot::String(); }
    int get_interior_building() const { return interior.is_open() ? interior.get_index() : -1; }
    double get_interior_camera_yaw() const { return interior.get_camera_yaw(); } // 室内编辑视角的相机方位：W 把光标往 (−sin, 0, −cos) 挪

    void set_map_path(const godot::NodePath &p_value) { map_path = p_value; }
    godot::NodePath get_map_path() const { return map_path; }
    void set_ship_path(const godot::NodePath &p_value) { ship_path = p_value; }
    godot::NodePath get_ship_path() const { return ship_path; }
    void set_player_path(const godot::NodePath &p_value) { player_path = p_value; }
    godot::NodePath get_player_path() const { return player_path; }
    void set_origin_dock(int p_value) { origin_dock = p_value; }
    int get_origin_dock() const { return origin_dock; }
    // 运行中改目的码头：重新找下标、没到过（测试与开发者用；地图宿主的码头号，跨群的带群号）
    void set_destination_dock(int p_value);
    int get_destination_dock() const { return destination_dock; }
    void set_helm_range(double p_value) {
        helm_range = p_value;
        sync_helm_rules();
    }
    double get_helm_range() const { return helm_range; }
    void set_dock_range(double p_value) {
        dock_range = p_value;
        sync_helm_rules();
    }
    double get_dock_range() const { return dock_range; }
    void set_leave_helm_speed(double p_value) {
        leave_helm_speed = p_value;
        sync_helm_rules();
    }
    double get_leave_helm_speed() const { return leave_helm_speed; }
    void set_kill_altitude(double p_value) { kill_altitude = p_value; }
    double get_kill_altitude() const { return kill_altitude; }
    void set_reach(double p_value) { reach = p_value; }
    double get_reach() const { return reach; }
    void set_arrive_radius(double p_value) { arrive_radius = p_value; }
    double get_arrive_radius() const { return arrive_radius; }
    void set_recall_distance(double p_value) { recall_distance = p_value; }
    double get_recall_distance() const { return recall_distance; }
    void set_notice_seconds(double p_value) { notice_seconds = p_value; }
    double get_notice_seconds() const { return notice_seconds; }
    void set_starting_pack(const godot::Dictionary &p_value) { starting_pack = p_value; }
    godot::Dictionary get_starting_pack() const { return starting_pack; }
    void set_dev_tools(bool p_value);
    bool get_dev_tools() const { return dev_tools; }
};

VARIANT_ENUM_CAST(Expedition::State);
