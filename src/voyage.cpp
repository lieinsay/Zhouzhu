#include "voyage.h"

#include <cmath>
#include <initializer_list>

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "crops.h"
#include "input_actions.h"
#include "ship_layout.h"

using namespace godot;

namespace {

constexpr double RECALL_DISTANCE = 60.0; // 坠落回位时，船离得比这远就把它召回最近的码头
constexpr double IN_YARD_TOLERANCE = 0.5; // 船舷离坞位接触线这么近就算停在坞里
constexpr double REFIT_MOUSE_SENSITIVITY = 0.004;
constexpr double NOTICE_SECONDS = 2.5;
constexpr int PROGRESS_CELLS = 8;

const Color TARGET_READY(1.0f, 0.96f, 0.72f, 0.95f); // 这一格有活可干
const Color TARGET_IDLE(1.0f, 1.0f, 1.0f, 0.4f); // 是块田，但眼下没什么可做

// 每样活要按住多久
double work_seconds(Farm::Action p_action) {
    switch (p_action) {
        case Farm::ACTION_TILL:
            return 1.5;
        case Farm::ACTION_SOW:
            return 0.6;
        case Farm::ACTION_HARVEST:
            return 0.8;
        case Farm::ACTION_NONE:
            break;
    }
    return 0.0;
}

String work_name(Farm::Action p_action) {
    switch (p_action) {
        case Farm::ACTION_TILL:
            return String(U"开垦");
        case Farm::ACTION_SOW:
            return String(U"播种");
        case Farm::ACTION_HARVEST:
            return String(U"收获");
        case Farm::ACTION_NONE:
            break;
    }
    return String();
}

Label *make_label(int p_font_size) {
    Label *label = memnew(Label);
    label->add_theme_font_size_override("font_size", p_font_size);
    label->add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85f));
    label->add_theme_constant_override("outline_size", 6);
    return label;
}

double yaw_of(const Transform3D &p_transform) {
    return p_transform.basis.get_euler().y;
}

String name_of(const std::string &p_item) {
    return String::utf8(catalog::item_name(p_item).c_str());
}

std::string to_std(const String &p_string) {
    return std::string(p_string.utf8().get_data());
}

} // namespace

Voyage::Voyage() {
    starting_pack["millet"] = 5;
    starting_pack["bean"] = 3;
}

void Voyage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_state"), &Voyage::get_state);
    ClassDB::bind_method(D_METHOD("has_arrived"), &Voyage::has_arrived);
    ClassDB::bind_method(D_METHOD("is_working"), &Voyage::is_working);
    ClassDB::bind_method(D_METHOD("is_pack_open"), &Voyage::is_pack_open);
    ClassDB::bind_method(D_METHOD("get_pack_cursor"), &Voyage::get_pack_cursor);
    ClassDB::bind_method(D_METHOD("get_held_seed"), &Voyage::get_held_seed);
    ClassDB::bind_method(D_METHOD("get_item_count", "item"), &Voyage::get_item_count);
    ClassDB::bind_method(D_METHOD("get_pack_items"), &Voyage::get_pack_items);
    ClassDB::bind_method(D_METHOD("is_refit_open"), &Voyage::is_refit_open);
    ClassDB::bind_method(D_METHOD("get_refit_cursor"), &Voyage::get_refit_cursor);
    ClassDB::bind_method(D_METHOD("get_refit_kind"), &Voyage::get_refit_kind);
    ClassDB::bind_method(D_METHOD("is_ship_in_yard"), &Voyage::is_ship_in_yard);
    ClassDB::bind_method(D_METHOD("get_notice"), &Voyage::get_notice);

    ClassDB::bind_method(D_METHOD("set_origin_island_path", "path"), &Voyage::set_origin_island_path);
    ClassDB::bind_method(D_METHOD("get_origin_island_path"), &Voyage::get_origin_island_path);
    ClassDB::bind_method(D_METHOD("set_destination_island_path", "path"), &Voyage::set_destination_island_path);
    ClassDB::bind_method(D_METHOD("get_destination_island_path"), &Voyage::get_destination_island_path);
    ClassDB::bind_method(D_METHOD("set_ship_path", "path"), &Voyage::set_ship_path);
    ClassDB::bind_method(D_METHOD("get_ship_path"), &Voyage::get_ship_path);
    ClassDB::bind_method(D_METHOD("set_player_path", "path"), &Voyage::set_player_path);
    ClassDB::bind_method(D_METHOD("get_player_path"), &Voyage::get_player_path);
    ClassDB::bind_method(D_METHOD("set_helm_range", "value"), &Voyage::set_helm_range);
    ClassDB::bind_method(D_METHOD("get_helm_range"), &Voyage::get_helm_range);
    ClassDB::bind_method(D_METHOD("set_dock_range", "value"), &Voyage::set_dock_range);
    ClassDB::bind_method(D_METHOD("get_dock_range"), &Voyage::get_dock_range);
    ClassDB::bind_method(D_METHOD("set_leave_helm_speed", "value"), &Voyage::set_leave_helm_speed);
    ClassDB::bind_method(D_METHOD("get_leave_helm_speed"), &Voyage::get_leave_helm_speed);
    ClassDB::bind_method(D_METHOD("set_kill_altitude", "value"), &Voyage::set_kill_altitude);
    ClassDB::bind_method(D_METHOD("get_kill_altitude"), &Voyage::get_kill_altitude);
    ClassDB::bind_method(D_METHOD("set_farm_path", "path"), &Voyage::set_farm_path);
    ClassDB::bind_method(D_METHOD("get_farm_path"), &Voyage::get_farm_path);
    ClassDB::bind_method(D_METHOD("set_shipyard_path", "path"), &Voyage::set_shipyard_path);
    ClassDB::bind_method(D_METHOD("get_shipyard_path"), &Voyage::get_shipyard_path);
    ClassDB::bind_method(D_METHOD("set_reach", "value"), &Voyage::set_reach);
    ClassDB::bind_method(D_METHOD("get_reach"), &Voyage::get_reach);
    ClassDB::bind_method(D_METHOD("set_starting_pack", "value"), &Voyage::set_starting_pack);
    ClassDB::bind_method(D_METHOD("get_starting_pack"), &Voyage::get_starting_pack);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "origin_island", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Island"), "set_origin_island_path", "get_origin_island_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "destination_island", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Island"), "set_destination_island_path", "get_destination_island_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "ship", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Ship"), "set_ship_path", "get_ship_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Player"), "set_player_path", "get_player_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "farm", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Farm"), "set_farm_path", "get_farm_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "shipyard", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Shipyard"), "set_shipyard_path", "get_shipyard_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helm_range", PROPERTY_HINT_RANGE, "0.5,10,0.1,suffix:m"), "set_helm_range", "get_helm_range");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_range", PROPERTY_HINT_RANGE, "5,200,1,suffix:m"), "set_dock_range", "get_dock_range");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "leave_helm_speed", PROPERTY_HINT_RANGE, "0,30,0.5,suffix:m/s"), "set_leave_helm_speed", "get_leave_helm_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "kill_altitude", PROPERTY_HINT_RANGE, "-500,500,1,suffix:m"), "set_kill_altitude", "get_kill_altitude");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach", PROPERTY_HINT_RANGE, "0.5,4,0.1,suffix:m"), "set_reach", "get_reach");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "starting_pack"), "set_starting_pack", "get_starting_pack");

    ADD_SIGNAL(MethodInfo("arrived"));
    ADD_SIGNAL(MethodInfo("farm_worked", PropertyInfo(Variant::VECTOR2I, "cell"), PropertyInfo(Variant::INT, "action")));

    BIND_ENUM_CONSTANT(ON_FOOT);
    BIND_ENUM_CONSTANT(PILOTING);
    BIND_ENUM_CONSTANT(MOORING);
}

void Voyage::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    input_actions::register_all();

    origin = Object::cast_to<Island>(get_node_or_null(origin_island_path));
    destination = Object::cast_to<Island>(get_node_or_null(destination_island_path));
    ship = Object::cast_to<Ship>(get_node_or_null(ship_path));
    player = Object::cast_to<Player>(get_node_or_null(player_path));
    if (!origin || !destination || !ship || !player) {
        ERR_PRINT("Voyage: origin_island / destination_island / ship / player 有没接上的");
        set_physics_process(false);
        return;
    }
    farm = Object::cast_to<Farm>(get_node_or_null(farm_path));
    shipyard = Object::cast_to<Shipyard>(get_node_or_null(shipyard_path));

    // 开局背包；按表里的顺序放，面板就按这个顺序列
    const Array keys = starting_pack.keys();
    for (int k = 0; k < keys.size(); k++) {
        pack.add(to_std(keys[k]), int(starting_pack[keys[k]]));
    }

    // 子节点先于父节点 _ready，岛此刻已经生成完，泊位与出生点都有了
    ship->place_at(origin->get_berth_contact());
    const Transform3D spawn = origin->get_spawn_transform();
    player->set_global_position(spawn.origin + Vector3(0, 0.1f, 0));
    player->set_look_yaw(yaw_of(spawn));
    player->set_facing_yaw(yaw_of(spawn));
    player->get_camera()->make_current();

    build_hud();
    if (DisplayServer::get_singleton()->get_name() != String("headless")) {
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

void Voyage::build_hud() {
    CanvasLayer *layer = memnew(CanvasLayer);
    layer->set_name("Hud");
    add_child(layer);

    status_label = make_label(20);
    status_label->set_position(Vector2(24, 18));
    layer->add_child(status_label);

    prompt_label = make_label(26);
    prompt_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    prompt_label->set_anchors_preset(Control::PRESET_CENTER_BOTTOM);
    prompt_label->set_h_grow_direction(Control::GROW_DIRECTION_BOTH);
    prompt_label->set_v_grow_direction(Control::GROW_DIRECTION_BEGIN);
    prompt_label->set_offset(SIDE_BOTTOM, -56);
    prompt_label->set_offset(SIDE_TOP, -56);
    layer->add_child(prompt_label);

    pack_panel = memnew(BackpackPanel);
    pack_panel->set_name("Backpack");
    pack_panel->set_anchors_preset(Control::PRESET_TOP_RIGHT);
    pack_panel->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
    pack_panel->set_v_grow_direction(Control::GROW_DIRECTION_END);
    pack_panel->set_offset(SIDE_RIGHT, -24);
    pack_panel->set_offset(SIDE_TOP, 18);
    pack_panel->set_visible(false);
    layer->add_child(pack_panel);

    refit_panel = memnew(RefitPanel);
    refit_panel->set_name("Refit");
    refit_panel->set_anchors_preset(Control::PRESET_TOP_RIGHT);
    refit_panel->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
    refit_panel->set_v_grow_direction(Control::GROW_DIRECTION_END);
    refit_panel->set_offset(SIDE_RIGHT, -24);
    refit_panel->set_offset(SIDE_TOP, 18);
    refit_panel->set_visible(false);
    layer->add_child(refit_panel);

    refit_overlay = memnew(RefitOverlay);
    refit_overlay->set_name("RefitOverlay");
    refit_overlay->set_visible(false);
    add_child(refit_overlay);
}

String Voyage::get_held_seed() const {
    return String::utf8(held_seed.c_str());
}

int Voyage::get_item_count(const String &p_item) const {
    return pack.count(to_std(p_item));
}

PackedStringArray Voyage::get_pack_items() const {
    PackedStringArray items;
    for (const ItemStack &stack : pack.get_stacks()) {
        items.push_back(String::utf8(stack.item.c_str()));
    }
    return items;
}

void Voyage::_unhandled_input(const Ref<InputEvent> &p_event) {
    Input *input = Input::get_singleton();
    if (p_event->is_action_pressed("ui_cancel")) {
        input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        return;
    }
    if (refit_open && input->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED) {
        const Ref<InputEventMouseMotion> motion = p_event;
        if (motion.is_valid()) {
            const Vector2 rel = motion->get_relative();
            refit_overlay->orbit(-rel.x * REFIT_MOUSE_SENSITIVITY, rel.y * REFIT_MOUSE_SENSITIVITY);
            return;
        }
    }
    const Ref<InputEventMouseButton> click = p_event;
    if (click.is_valid() && click->is_pressed() && input->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

void Voyage::_physics_process(double p_delta) {
    const bool interact = Input::get_singleton()->is_action_just_pressed(input_actions::INTERACT);
    notice_time = Math::max(0.0, notice_time - p_delta);

    switch (state) {
        case ON_FOOT: {
            if (Input::get_singleton()->is_action_just_pressed(input_actions::INVENTORY)) {
                if (refit_open) {
                    set_refit_open(false); // Tab 先合上开着的那个
                } else {
                    set_pack_open(!pack_open);
                }
            }
            if (refit_open) {
                browse_refit(interact); // 开着改装面板：E 是加一节
            } else if (pack_open) {
                browse_pack(interact); // 开着背包：E 是拿东西，不是上舵、不是干活
            } else if (interact && near_bench()) {
                interact_at_bench();
            } else if (interact && player->get_global_position().distance_to(ship->get_helm_position()) <= helm_range) {
                if (!ship->can_fly()) {
                    say(String(U"没有动力区，飞不起来：去船坞的画样台加一节"));
                } else {
                    board();
                    break;
                }
            } else {
                tend_farm(p_delta, interact);
            }
            track_footing();
            if (player->get_global_position().y < kill_altitude) {
                respawn();
            }
        } break;

        case PILOTING: {
            seat_player();
            if (!interact) {
                break;
            }
            Transform3D target;
            bool yard = false;
            if (moor_target_in_range(target, yard)) {
                ship->moor_at(target);
                ship->set_controlled(false);
                moor_to_yard = yard;
                state = MOORING;
            } else if (ship->get_speed() <= leave_helm_speed) {
                disembark();
            } else {
                notice = String(U"船还在走，先停稳再离舵");
                notice_time = NOTICE_SECONDS;
            }
        } break;

        case MOORING: {
            seat_player();
            if (!ship->is_mooring()) {
                disembark();
            }
        } break;
    }
    update_hud();
}

void Voyage::board() {
    set_pack_open(false);
    set_refit_open(false);
    drop_work();
    target_cell = Farm::NO_CELL;
    target_action = Farm::ACTION_NONE;
    if (farm) {
        farm->set_highlight(Farm::NO_CELL, Color());
    }
    player->set_controlled(false);
    ship->set_controlled(true);
    ship->get_camera()->make_current();
    seat_player();
    state = PILOTING;
}

void Voyage::disembark() {
    const Transform3D exit = ship->get_exit_transform();
    ship->set_controlled(false);
    player->set_global_position(exit.origin + Vector3(0, 0.05f, 0));
    player->set_look_yaw(yaw_of(exit));
    player->set_controlled(true);
    player->get_camera()->make_current();
    state = ON_FOOT;
}

void Voyage::seat_player() {
    const Transform3D seat = ship->get_seat_transform();
    player->set_global_position(seat.origin);
    player->set_facing_yaw(yaw_of(seat));
}

// 开合背包。开着的时候人站住（输入挡在 Player 门口，物理照常），手上的活作废，描边收起。
void Voyage::set_pack_open(bool p_open) {
    if (pack_open == p_open) {
        return;
    }
    pack_open = p_open;
    player->set_input_blocked(p_open);
    pack_panel->set_visible(p_open);
    if (!p_open) {
        return;
    }
    drop_work();
    target_cell = Farm::NO_CELL;
    target_action = Farm::ACTION_NONE;
    if (farm) {
        farm->set_highlight(Farm::NO_CELL, Color());
    }
    // 光标落在手上拿着的那样东西上；没拿就从头
    pack_cursor = 0;
    const std::vector<ItemStack> &stacks = pack.get_stacks();
    for (int k = 0; k < int(stacks.size()); k++) {
        if (stacks[size_t(k)].item == held_seed) {
            pack_cursor = k;
        }
    }
}

bool Voyage::is_ship_in_yard() const {
    return shipyard && shipyard->is_present() && !ship->is_mooring() &&
            ship->is_at(shipyard->get_cradle_contact(), IN_YARD_TOLERANCE);
}

bool Voyage::near_bench() const {
    return shipyard && shipyard->is_present() && player->get_global_position().distance_to(shipyard->get_bench_position()) <= helm_range;
}

// 在画样台按 E：船在坞里才改得了
void Voyage::interact_at_bench() {
    if (!is_ship_in_yard()) {
        say(String(U"船坞里没有船：掌舵开到船坞跟前按 E 入坞，再来改"));
        return;
    }
    set_refit_open(true);
}

// 开合改装。开着的时候人站住（输入挡在 Player 门口），手上的活作废，相机切到装配视角
void Voyage::set_refit_open(bool p_open) {
    if (refit_open == p_open) {
        return;
    }
    refit_open = p_open;
    player->set_input_blocked(p_open);
    refit_panel->set_visible(p_open);
    refit_overlay->set_visible(p_open);
    if (!p_open) {
        player->get_camera()->make_current();
        return;
    }
    set_pack_open(false);
    drop_work();
    target_cell = Farm::NO_CELL;
    target_action = Farm::ACTION_NONE;
    if (farm) {
        farm->set_highlight(Farm::NO_CELL, Color());
    }
    // 光标落在驾驶区上，选中的模块默认储存区
    const Module *bridge = ship->get_layout_model().bridge();
    refit_cursor = bridge ? bridge->at : Cell{ 0, 0, 0 };
    refit_kind = ModuleKind::STORAGE;
    refit_overlay->show_for(ship->get_layout_model(), ship->get_global_transform());
    refit_overlay->get_camera()->make_current();
    refresh_refit_view();
}

String Voyage::refit_cursor_status(RefitOverlay::CursorState &r_state) const {
    const ShipLayout &layout = ship->get_layout_model();
    const Module *here = layout.at(refit_cursor);
    if (here) {
        r_state = RefitOverlay::CURSOR_SELECTED;
        String text = String(U"这里是") + String::utf8(modules::def(here->kind).name);
        if (here->kind == ModuleKind::BRIDGE) {
            return text + String(U"：换不得、拆不得");
        }
        ShipLayout trial = layout;
        const std::string removable = layout_remove(trial, refit_cursor);
        if (here->kind != refit_kind) {
            trial = layout;
            const std::string swap = layout_set_kind(trial, refit_cursor, refit_kind);
            text += swap.empty() ? String(U"：E 换成") + String::utf8(modules::def(refit_kind).name) : String(U"：") + String::utf8(swap.c_str());
        }
        return text + (removable.empty() ? String(U" · X 拆掉") : String(U" · ") + String::utf8(removable.c_str()));
    }
    ShipLayout trial = layout;
    const std::string reason = layout_place(trial, refit_cursor, refit_kind);
    if (reason.empty()) {
        r_state = RefitOverlay::CURSOR_CAN_PLACE;
        return String(U"空格：E 放一节") + String::utf8(modules::def(refit_kind).name);
    }
    r_state = RefitOverlay::CURSOR_CANNOT_PLACE;
    return String(U"空格：") + String::utf8(reason.c_str());
}

void Voyage::refresh_refit_view() {
    RefitOverlay::CursorState state = RefitOverlay::CURSOR_CANNOT_PLACE;
    const String status = refit_cursor_status(state);
    refit_panel->refresh(refit_cursor, status, state != RefitOverlay::CURSOR_CANNOT_PLACE, refit_kind, ship->get_stats_model());
    refit_overlay->refresh(ship->get_layout_model(), ship->get_global_transform(), refit_cursor, state, layout_candidates(ship->get_layout_model()));
}

// 改船：W/S/A/D 在格子里挪光标、空格/Shift 上下层，1/2/3 选模块，E 放一节或换类型，X 拆掉。
// 每一步直接落到船上（船在坞里重新坐定，舷门仍贴着跳板），不合法就说明为什么。
void Voyage::browse_refit(bool p_interact) {
    Input *input = Input::get_singleton();
    auto pressed = [input](const char *p_action) { return input->is_action_just_pressed(p_action); };
    Cell next = refit_cursor;
    next.z += pressed(input_actions::MOVE_BACK) - pressed(input_actions::MOVE_FORWARD);
    next.x += pressed(input_actions::MOVE_RIGHT) - pressed(input_actions::MOVE_LEFT);
    next.y += pressed(input_actions::ASCEND) - pressed(input_actions::DESCEND);
    if (next != refit_cursor) {
        if (cell_in_range(next)) {
            refit_cursor = next;
        } else {
            say(String(U"船坞能造的范围到头了"));
        }
    }
    if (pressed(input_actions::KIND_1)) {
        refit_kind = ModuleKind::BRIDGE;
    } else if (pressed(input_actions::KIND_2)) {
        refit_kind = ModuleKind::STORAGE;
    } else if (pressed(input_actions::KIND_3)) {
        refit_kind = ModuleKind::POWER;
    }

    ShipLayout draft = ship->get_layout_model();
    std::string reason;
    String done;
    const String kind_name = String::utf8(modules::def(refit_kind).name);
    if (pressed(input_actions::REMOVE)) {
        reason = layout_remove(draft, refit_cursor);
        if (reason.empty()) {
            done = String(U"拆掉了这一节");
        }
    } else if (p_interact) {
        const Module *here = draft.at(refit_cursor);
        if (here) {
            if (here->kind == refit_kind) {
                reason = std::string("这一节已经是") + modules::def(refit_kind).name + "了";
            } else {
                reason = layout_set_kind(draft, refit_cursor, refit_kind);
                if (reason.empty()) {
                    done = String(U"这一节换成了") + kind_name;
                }
            }
        } else {
            reason = layout_place(draft, refit_cursor, refit_kind);
            if (reason.empty()) {
                done = String(U"放了一节") + kind_name;
            }
        }
    }
    if (!reason.empty()) {
        say(String::utf8(reason.c_str()));
    } else if (!done.is_empty()) {
        ship->apply_layout(draft);
        ship->place_at(shipyard->get_cradle_contact(), true); // 船身变了，舷门还得贴着跳板
        say(done);
    }
}

// 翻背包：W/S 挪光标，E 把光标那样种子拿在手上（再按一次放下）。
void Voyage::browse_pack(bool p_interact) {
    const int n = pack.size();
    if (n == 0) {
        pack_cursor = 0;
        return;
    }
    Input *input = Input::get_singleton();
    if (input->is_action_just_pressed(input_actions::MOVE_FORWARD)) {
        pack_cursor = (pack_cursor + n - 1) % n;
    }
    if (input->is_action_just_pressed(input_actions::MOVE_BACK)) {
        pack_cursor = (pack_cursor + 1) % n;
    }
    pack_cursor = Math::clamp(pack_cursor, 0, n - 1);
    if (!p_interact) {
        return;
    }
    const std::string &item = pack.get_stacks()[size_t(pack_cursor)].item;
    if (!catalog::is_seed(item)) {
        say(name_of(item) + String(U"不是种子，种不了"));
    } else if (held_seed == item) {
        held_seed.clear();
        say(String(U"放下了") + name_of(item) + String(U"种"));
    } else {
        held_seed = item;
        say(String(U"手上拿着") + name_of(item) + String(U"种，去田里按住 E 播"));
    }
}

// 步行时的农事：盯着视线前方的那一格；按下 E 起工，按住才算工时，干满了才落到地里。
void Voyage::tend_farm(double p_delta, bool p_interact) {
    if (!farm) {
        return;
    }
    target_cell = Farm::NO_CELL;
    if (player->is_on_floor()) {
        const Vector3 ahead = Basis(Vector3(0, 1, 0), real_t(player->get_look_yaw())).xform(Vector3(0, 0, real_t(-reach)));
        target_cell = farm->cell_at(player->get_global_position() + ahead);
    }
    target_action = farm->available_action(target_cell, has_seed_in_hand());

    // 描边只给「与农事有关」的格子：有活可干的，或者已经是田的
    if (target_action != Farm::ACTION_NONE) {
        farm->set_highlight(target_cell, TARGET_READY);
    } else if (target_cell != Farm::NO_CELL && farm->get_plot(target_cell) != Farm::PLOT_WILD) {
        farm->set_highlight(target_cell, TARGET_IDLE);
    } else {
        farm->set_highlight(Farm::NO_CELL, Color());
    }

    if (p_interact) {
        if (target_action != Farm::ACTION_NONE) {
            work_cell = target_cell;
            work_action = target_action;
            work_time = 0.0;
            player->set_facing_yaw(player->get_look_yaw());
        } else if (target_cell != Farm::NO_CELL) {
            say(refusal(target_cell));
        }
    }
    if (work_action == Farm::ACTION_NONE) {
        return;
    }
    if (!Input::get_singleton()->is_action_pressed(input_actions::INTERACT) || target_cell != work_cell || target_action != work_action) {
        drop_work();
        return;
    }
    work_time += p_delta;
    if (work_time < work_seconds(work_action)) {
        return;
    }
    finish_work();
    emit_signal("farm_worked", work_cell, int(work_action));
    drop_work(); // 下一样活要重新按 E：免得垦完顺手就把种子撒了
}

// 工时够了，活落到地里；种子从背包扣、收成往背包放，都在这一处
void Voyage::finish_work() {
    switch (work_action) {
        case Farm::ACTION_TILL:
            farm->perform(work_cell, Farm::ACTION_TILL, String());
            break;
        case Farm::ACTION_SOW: {
            const CropDef *crop = catalog::crop_of_seed(held_seed);
            if (crop && pack.remove(held_seed, 1)) {
                farm->perform(work_cell, Farm::ACTION_SOW, String(crop->id));
            }
        } break;
        case Farm::ACTION_HARVEST: {
            const CropDef *crop = catalog::crop(to_std(farm->get_crop(work_cell)));
            const int gained = farm->perform(work_cell, Farm::ACTION_HARVEST, String());
            if (crop && gained > 0) {
                pack.add(crop->product, gained);
                say(vformat(String(U"收了 %d 份%s，放进了背包"), gained, name_of(crop->product)));
            }
        } break;
        case Farm::ACTION_NONE:
            break;
    }
}

void Voyage::drop_work() {
    work_action = Farm::ACTION_NONE;
    work_cell = Farm::NO_CELL;
    work_time = 0.0;
}

void Voyage::say(const String &p_notice) {
    notice = p_notice;
    notice_time = p_notice.is_empty() ? 0.0 : NOTICE_SECONDS;
}

// 按了 E 却无事可做：说清楚为什么
String Voyage::refusal(const Vector2i &p_cell) const {
    switch (farm->get_plot(p_cell)) {
        case Farm::PLOT_TILLED:
            return held_seed.empty() ? String(U"手上没有种子：按 Tab 打开背包选一样") : name_of(held_seed) + String(U"种用完了，去背包换一样");
        case Farm::PLOT_GROWING:
            return vformat(String(U"粟还没熟（长了 %d%%）"), int(std::lround(farm->get_growth(p_cell) * 100.0)));
        case Farm::PLOT_RIPE:
        case Farm::PLOT_WILD:
            break;
    }
    switch (farm->get_land(p_cell)) {
        case Farm::LAND_WATER:
            return String(U"这是河");
        case Farm::LAND_TOO_DRY:
            return String(U"离河太远，引不到水，垦了也白垦");
        case Farm::LAND_TOO_STEEP:
            return String(U"坡太陡，垦不成地");
        case Farm::LAND_DOCK:
            return String(U"停机坪上不能垦");
        case Farm::LAND_TRAIL:
            return String(U"这是路，垦了就没法走了");
        case Farm::LAND_YARD:
            return String(U"这是船坞的坪，垦不得");
        case Farm::LAND_ARABLE:
        case Farm::LAND_NO_GROUND:
            break;
    }
    return String();
}

// 记下最近一次站在岛上的位置（坠落后回到这里），顺便判定是否已踏上目的地。
void Voyage::track_footing() {
    if (!player->is_on_floor()) {
        return;
    }
    const Vector3 position = player->get_global_position();
    for (Island *island : { origin, destination }) {
        const double ground = island->get_surface_height(position);
        if (std::isnan(ground) || std::abs(position.y - ground) > 1.0) {
            continue;
        }
        safe_footing = position;
        has_safe_footing = true;
        if (island == destination && !arrived) {
            arrived = true;
            emit_signal("arrived");
        }
    }
}

void Voyage::respawn() {
    const Vector3 target = has_safe_footing ? safe_footing : origin->get_spawn_transform().origin;
    drop_work();
    player->set_velocity(Vector3());
    player->set_global_position(target + Vector3(0, 0.5f, 0));
    // 人回来了船却漂在远处，这一局就死了：把船召回最近的码头
    if (ship->get_global_position().distance_to(target) > RECALL_DISTANCE) {
        ship->place_at(nearest_island(target)->get_berth_contact());
    }
    notice = String(U"坠入云海，回到了上一个落脚点");
    notice_time = NOTICE_SECONDS;
}

bool Voyage::moor_target_in_range(Transform3D &r_target, bool &r_yard) const {
    bool found = false;
    double best_distance = dock_range;
    auto consider = [&](const Transform3D &p_target, bool p_yard) {
        const double distance = ship->get_global_position().distance_to(p_target.origin);
        if (distance <= best_distance) {
            found = true;
            best_distance = distance;
            r_target = p_target;
            r_yard = p_yard;
        }
    };
    for (Island *island : { origin, destination }) {
        consider(island->get_berth_contact(), false);
    }
    if (shipyard && shipyard->is_present()) {
        consider(shipyard->get_cradle_contact(), true);
    }
    return found;
}

Island *Voyage::nearest_island(const Vector3 &p_position) const {
    const double to_origin = p_position.distance_to(origin->get_global_position());
    const double to_destination = p_position.distance_to(destination->get_global_position());
    return to_origin <= to_destination ? origin : destination;
}

void Voyage::update_hud() {
    String status;
    String prompt;
    // 航行目标：没到过对岸就是对岸，到过了就是回程
    Island *target = arrived ? origin : destination;
    const Transform3D target_berth = target->get_berth_contact();

    switch (state) {
        case ON_FOOT: {
            status = arrived ? String(U"已踏上对岸的岛") : String(U"目标：登上空艇，飞到对岸的岛上去");
            const bool near_helm = player->get_global_position().distance_to(ship->get_helm_position()) <= helm_range;
            prompt = near_helm ? (ship->can_fly() ? String(U"按 E 掌舵") : String(U"没有动力区，飞不起来")) : String(U"WASD 移动 · 空格 跳 · Shift 疾跑");
            if (refit_open) {
                refresh_refit_view();
                prompt = String(U"W/S 前后 · A/D 左右 · 空格/Shift 上下 · 1/2/3 选模块 · E 放/换 · X 拆 · Tab 合上");
                break;
            }
            if (near_bench()) {
                prompt = is_ship_in_yard() ? String(U"按 E 改装空艇") : String(U"画样台 · 空艇不在船坞里");
            }
            if (!farm) {
                break;
            }
            // 第二行：背包一眼看全，手上拿着什么
            status += String(U"\n背包");
            if (pack.empty()) {
                status += String(U" 空");
            }
            for (const ItemStack &stack : pack.get_stacks()) {
                status += vformat(String(U" %s %d"), name_of(stack.item), stack.count);
            }
            status += held_seed.empty() ? String(U"    手持 —") : String(U"    手持 ") + name_of(held_seed) + String(U"种");
            const int fields = farm->get_plot_count(Farm::PLOT_TILLED) + farm->get_plot_count(Farm::PLOT_GROWING) + farm->get_plot_count(Farm::PLOT_RIPE);
            if (fields > 0) {
                status += vformat(String(U"    田 %d 块"), fields);
                const int ripe = farm->get_plot_count(Farm::PLOT_RIPE);
                if (ripe > 0) {
                    status += vformat(String(U"（%d 块可收）"), ripe);
                }
            } else {
                status += String(U"    河边的滩地可以开垦");
            }
            if (pack_open) {
                pack_panel->refresh(pack, pack_cursor, held_seed);
                prompt = String(U"W/S 选 · E 拿在手上 · Tab 合上");
                break;
            }
            if (near_helm || near_bench()) {
                break;
            }
            if (work_action != Farm::ACTION_NONE) {
                const int filled = int(PROGRESS_CELLS * work_time / work_seconds(work_action));
                prompt = work_name(work_action) + String(U"中 ") + String(U"■").repeat(filled) + String(U"□").repeat(PROGRESS_CELLS - filled);
            } else if (target_action == Farm::ACTION_SOW) {
                prompt = String(U"按住 E 播") + name_of(held_seed) + String(U"（耗 1 份）");
            } else if (target_action != Farm::ACTION_NONE) {
                prompt = String(U"按住 E ") + work_name(target_action);
            } else if (target_cell != Farm::NO_CELL && farm->get_plot(target_cell) == Farm::PLOT_TILLED) {
                prompt = held_seed.empty() ? String(U"按 Tab 打开背包，选一样种子") : name_of(held_seed) + String(U"种用完了");
            } else if (target_cell != Farm::NO_CELL && farm->get_plot(target_cell) == Farm::PLOT_GROWING) {
                prompt = vformat(String(U"%s苗 · 长了 %d%%"), name_of(to_std(farm->get_crop(target_cell))), int(std::lround(farm->get_growth(target_cell) * 100.0)));
            }
        } break;

        case PILOTING: {
            const Vector3 ship_position = ship->get_global_position();
            const Vector3 to_target = target_berth.origin - ship_position;
            const double distance = Vector2(to_target.x, to_target.z).length();
            // 目标相对船头的方位：正 = 在右舷
            const Vector3 local = ship->get_global_transform().basis.xform_inv(to_target);
            const int relative_deg = int(std::lround(Math::rad_to_deg(std::atan2(double(local.x), double(-local.z)))));
            String bearing = String(U"正前方");
            if (relative_deg > 2) {
                bearing = vformat(String(U"右舷 %d°"), relative_deg);
            } else if (relative_deg < -2) {
                bearing = vformat(String(U"左舷 %d°"), -relative_deg);
            }
            status = vformat(String(U"航速 %d m/s    高度 %d m\n%s %d m    %s    高差 %+d m"),
                    int(std::lround(ship->get_speed())), int(std::lround(ship_position.y)),
                    arrived ? String(U"回程泊位") : String(U"对岸泊位"), int(std::lround(distance)), bearing, int(std::lround(to_target.y)));

            Transform3D moor_target;
            bool yard = false;
            if (moor_target_in_range(moor_target, yard)) {
                prompt = ship->is_at(moor_target, 0.5) ? String(U"按 E 离舵") : (yard ? String(U"按 E 入坞") : String(U"按 E 靠泊"));
            } else if (ship->get_speed() <= leave_helm_speed) {
                prompt = String(U"W/S 进退 · A/D 转舵 · 空格 升 · Shift 降 · E 离舵");
            } else {
                prompt = String(U"W/S 进退 · A/D 转舵 · 空格 升 · Shift 降");
            }
        } break;

        case MOORING: {
            status = moor_to_yard ? String(U"入坞中") : String(U"靠泊中");
            prompt = String(U"……");
        } break;
    }
    if (notice_time > 0.0) {
        prompt = notice;
    }
    status_label->set_text(status);
    prompt_label->set_text(prompt);
}
