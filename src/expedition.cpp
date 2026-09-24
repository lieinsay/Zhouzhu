#include "expedition.h"

#include <cmath>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "farm.h"
#include "input_actions.h"
#include "land_berth.h"
#include "settlement.h"
#include "shipyard.h"
#include "world_clock.h"

using namespace godot;

namespace {

constexpr double MAP_FIRST_VIEW_M = 16000.0; // 第一次打开地图时屏幕宽对应多少米
constexpr double LAND_RING_STEP_M = 25.0;
constexpr double INLAND_NUDGE_M = 20.0; // 找到的陆地再往里挪这么多，别落在崖边上

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

template <typename T>
T *top_right_panel(CanvasLayer *p_layer, const char *p_name) {
    T *panel = memnew(T);
    panel->set_name(p_name);
    panel->set_anchors_preset(Control::PRESET_TOP_RIGHT);
    panel->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
    panel->set_v_grow_direction(Control::GROW_DIRECTION_END);
    panel->set_offset(SIDE_RIGHT, -24);
    panel->set_offset(SIDE_TOP, 18);
    panel->set_visible(false);
    p_layer->add_child(panel);
    return panel;
}

Label *bottom_label(CanvasLayer *p_layer, int p_font_size, int p_offset) {
    Label *label = make_label(p_font_size);
    label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    label->set_anchors_preset(Control::PRESET_CENTER_BOTTOM);
    label->set_h_grow_direction(Control::GROW_DIRECTION_BOTH);
    label->set_v_grow_direction(Control::GROW_DIRECTION_BEGIN);
    label->set_offset(SIDE_BOTTOM, p_offset);
    label->set_offset(SIDE_TOP, p_offset);
    p_layer->add_child(label);
    return label;
}

} // namespace

Expedition::Expedition() {
    starting_pack["millet"] = 5;
    starting_pack["bean"] = 3;
}

void Expedition::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_state"), &Expedition::get_state);
    ClassDB::bind_method(D_METHOD("has_arrived"), &Expedition::has_arrived);
    ClassDB::bind_method(D_METHOD("get_notice"), &Expedition::get_notice);
    ClassDB::bind_method(D_METHOD("get_prompt"), &Expedition::get_prompt);
    ClassDB::bind_method(D_METHOD("get_status_text"), &Expedition::get_status_text);
    ClassDB::bind_method(D_METHOD("get_spawn_point"), &Expedition::get_spawn_point);
    ClassDB::bind_method(D_METHOD("get_origin_berth"), &Expedition::get_origin_berth);
    ClassDB::bind_method(D_METHOD("get_destination_berth"), &Expedition::get_destination_berth);
    ClassDB::bind_method(D_METHOD("teleport_to", "position"), &Expedition::teleport_to);
    ClassDB::bind_method(D_METHOD("on_teleport_requested", "position"), &Expedition::on_teleport_requested);
    ClassDB::bind_method(D_METHOD("is_map_open"), &Expedition::is_map_open);
    ClassDB::bind_method(D_METHOD("get_map"), &Expedition::get_map);
    ClassDB::bind_method(D_METHOD("get_map_node"), &Expedition::get_map_node);
    ClassDB::bind_method(D_METHOD("is_working"), &Expedition::is_working);
    ClassDB::bind_method(D_METHOD("is_carrying"), &Expedition::is_carrying);
    ClassDB::bind_method(D_METHOD("get_cargo_aboard"), &Expedition::get_cargo_aboard);
    ClassDB::bind_method(D_METHOD("has_freight_order"), &Expedition::has_freight_order);
    ClassDB::bind_method(D_METHOD("get_freight_delivered"), &Expedition::get_freight_delivered);
    ClassDB::bind_method(D_METHOD("get_freight_status"), &Expedition::get_freight_status);
    ClassDB::bind_method(D_METHOD("get_freight_order"), &Expedition::get_freight_order);
    ClassDB::bind_method(D_METHOD("is_pack_open"), &Expedition::is_pack_open);
    ClassDB::bind_method(D_METHOD("get_pack_cursor"), &Expedition::get_pack_cursor);
    ClassDB::bind_method(D_METHOD("get_held_seed"), &Expedition::get_held_seed);
    ClassDB::bind_method(D_METHOD("get_item_count", "item"), &Expedition::get_item_count);
    ClassDB::bind_method(D_METHOD("get_pack_items"), &Expedition::get_pack_items);
    ClassDB::bind_method(D_METHOD("is_refit_open"), &Expedition::is_refit_open);
    ClassDB::bind_method(D_METHOD("get_refit_cursor"), &Expedition::get_refit_cursor);
    ClassDB::bind_method(D_METHOD("get_refit_kind"), &Expedition::get_refit_kind);
    ClassDB::bind_method(D_METHOD("is_ship_in_yard"), &Expedition::is_ship_in_yard);
    ClassDB::bind_method(D_METHOD("is_build_open"), &Expedition::is_build_open);
    ClassDB::bind_method(D_METHOD("get_build_cursor"), &Expedition::get_build_cursor);
    ClassDB::bind_method(D_METHOD("get_build_kind"), &Expedition::get_build_kind);
    ClassDB::bind_method(D_METHOD("get_build_selected"), &Expedition::get_build_selected);
    ClassDB::bind_method(D_METHOD("get_build_yaw_deg"), &Expedition::get_build_yaw_deg);
    ClassDB::bind_method(D_METHOD("get_build_camera_yaw"), &Expedition::get_build_camera_yaw);
    ClassDB::bind_method(D_METHOD("get_build_target"), &Expedition::get_build_target);
    ClassDB::bind_method(D_METHOD("get_build_style"), &Expedition::get_build_style);
    ClassDB::bind_method(D_METHOD("is_interior_open"), &Expedition::is_interior_open);
    ClassDB::bind_method(D_METHOD("get_interior_cursor"), &Expedition::get_interior_cursor);
    ClassDB::bind_method(D_METHOD("get_interior_tool"), &Expedition::get_interior_tool);
    ClassDB::bind_method(D_METHOD("get_interior_level"), &Expedition::get_interior_level);
    ClassDB::bind_method(D_METHOD("get_interior_template"), &Expedition::get_interior_template);
    ClassDB::bind_method(D_METHOD("get_interior_building"), &Expedition::get_interior_building);
    ClassDB::bind_method(D_METHOD("get_interior_camera_yaw"), &Expedition::get_interior_camera_yaw);

    ClassDB::bind_method(D_METHOD("set_map_path", "value"), &Expedition::set_map_path);
    ClassDB::bind_method(D_METHOD("get_map_path"), &Expedition::get_map_path);
    ClassDB::bind_method(D_METHOD("set_ship_path", "value"), &Expedition::set_ship_path);
    ClassDB::bind_method(D_METHOD("get_ship_path"), &Expedition::get_ship_path);
    ClassDB::bind_method(D_METHOD("set_player_path", "value"), &Expedition::set_player_path);
    ClassDB::bind_method(D_METHOD("get_player_path"), &Expedition::get_player_path);
    ClassDB::bind_method(D_METHOD("set_origin_dock", "value"), &Expedition::set_origin_dock);
    ClassDB::bind_method(D_METHOD("get_origin_dock"), &Expedition::get_origin_dock);
    ClassDB::bind_method(D_METHOD("set_destination_dock", "value"), &Expedition::set_destination_dock);
    ClassDB::bind_method(D_METHOD("get_destination_dock"), &Expedition::get_destination_dock);
    ClassDB::bind_method(D_METHOD("set_helm_range", "value"), &Expedition::set_helm_range);
    ClassDB::bind_method(D_METHOD("get_helm_range"), &Expedition::get_helm_range);
    ClassDB::bind_method(D_METHOD("set_dock_range", "value"), &Expedition::set_dock_range);
    ClassDB::bind_method(D_METHOD("get_dock_range"), &Expedition::get_dock_range);
    ClassDB::bind_method(D_METHOD("set_leave_helm_speed", "value"), &Expedition::set_leave_helm_speed);
    ClassDB::bind_method(D_METHOD("get_leave_helm_speed"), &Expedition::get_leave_helm_speed);
    ClassDB::bind_method(D_METHOD("set_kill_altitude", "value"), &Expedition::set_kill_altitude);
    ClassDB::bind_method(D_METHOD("get_kill_altitude"), &Expedition::get_kill_altitude);
    ClassDB::bind_method(D_METHOD("set_reach", "value"), &Expedition::set_reach);
    ClassDB::bind_method(D_METHOD("get_reach"), &Expedition::get_reach);
    ClassDB::bind_method(D_METHOD("set_arrive_radius", "value"), &Expedition::set_arrive_radius);
    ClassDB::bind_method(D_METHOD("get_arrive_radius"), &Expedition::get_arrive_radius);
    ClassDB::bind_method(D_METHOD("set_recall_distance", "value"), &Expedition::set_recall_distance);
    ClassDB::bind_method(D_METHOD("get_recall_distance"), &Expedition::get_recall_distance);
    ClassDB::bind_method(D_METHOD("set_notice_seconds", "value"), &Expedition::set_notice_seconds);
    ClassDB::bind_method(D_METHOD("get_notice_seconds"), &Expedition::get_notice_seconds);
    ClassDB::bind_method(D_METHOD("set_starting_pack", "value"), &Expedition::set_starting_pack);
    ClassDB::bind_method(D_METHOD("get_starting_pack"), &Expedition::get_starting_pack);
    ClassDB::bind_method(D_METHOD("set_dev_tools", "value"), &Expedition::set_dev_tools);
    ClassDB::bind_method(D_METHOD("get_dev_tools"), &Expedition::get_dev_tools);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "map"), "set_map_path", "get_map_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "ship", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Ship"), "set_ship_path", "get_ship_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Player"), "set_player_path", "get_player_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "origin_dock"), "set_origin_dock", "get_origin_dock");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "destination_dock"), "set_destination_dock", "get_destination_dock");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helm_range", PROPERTY_HINT_RANGE, "0.5,10,0.1,suffix:m"), "set_helm_range", "get_helm_range");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_range", PROPERTY_HINT_RANGE, "5,200,1,suffix:m"), "set_dock_range", "get_dock_range");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "leave_helm_speed", PROPERTY_HINT_RANGE, "0,30,0.5,suffix:m/s"), "set_leave_helm_speed", "get_leave_helm_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "kill_altitude", PROPERTY_HINT_RANGE, "-500,500,1,suffix:m"), "set_kill_altitude", "get_kill_altitude");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach", PROPERTY_HINT_RANGE, "0.5,4,0.1,suffix:m"), "set_reach", "get_reach");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "arrive_radius", PROPERTY_HINT_RANGE, "10,1000000,1,suffix:m"), "set_arrive_radius", "get_arrive_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "recall_distance", PROPERTY_HINT_RANGE, "10,10000,1,suffix:m"), "set_recall_distance", "get_recall_distance");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "notice_seconds", PROPERTY_HINT_RANGE, "0.5,10,0.1,suffix:s"), "set_notice_seconds", "get_notice_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "starting_pack"), "set_starting_pack", "get_starting_pack");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dev_tools"), "set_dev_tools", "get_dev_tools");

    ADD_SIGNAL(MethodInfo("arrived"));
    ADD_SIGNAL(MethodInfo("farm_worked", PropertyInfo(Variant::VECTOR2I, "cell"), PropertyInfo(Variant::INT, "action")));

    BIND_ENUM_CONSTANT(ON_FOOT);
    BIND_ENUM_CONSTANT(PILOTING);
    BIND_ENUM_CONSTANT(MOORING);
}

void Expedition::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    input_actions::register_all();
    map = MapHost::of(get_node_or_null(map_path));
    player = Object::cast_to<Player>(get_node_or_null(player_path));
    // 子节点先于父节点 _ready：地图此刻已经生成完 / 读完数据
    if (!map || !player || !map->map_ready()) {
        ERR_PRINT("Expedition: map（地图宿主）/ player 没接上，或地图没准备好");
        set_process(false);
        set_physics_process(false);
        return;
    }
    ship = Object::cast_to<Ship>(get_node_or_null(ship_path));
    origin_index = map->map_find_dock(origin_dock);
    destination_index = map->map_find_dock(destination_dock);
    // 目的码头可以不配（只有一个码头的图）：那就没有航行目标、也不会「到了」
    if (ship && origin_index < 0) {
        ERR_PRINT(vformat("Expedition: 出发码头 #%d 不存在，空艇不上场", origin_dock));
        ship->set_visible(false);
        ship->set_process_mode(PROCESS_MODE_DISABLED);
        ship = nullptr;
    }
    build_hud();
    if (map_panel) {
        add_to_group(MAP_GROUP); // 开发者工具据此知道这张地图按 M 有岛群地图
    }
    const double view = map->map_view_distance();
    for (Camera3D *camera : { player->get_camera(), ship ? ship->get_camera() : nullptr }) {
        if (camera && view > 0.0) {
            camera->set_far(real_t(view));
        }
    }

    if (ship) {
        helm.attach(ship, player);
    }
    sync_helm_rules();
    // 出生在出发码头的停机坪上、面朝栈桥，船停在它的泊位上
    const int spawn_dock = origin_index >= 0 ? origin_index : 0;
    if (map->map_dock_count() > 0) {
        const Transform3D spawn = map->map_dock_spawn(spawn_dock);
        if (ship) {
            ship->place_at(map->map_dock_berth(spawn_dock));
        }
        map->map_prepare(spawn.origin); // 流式地图：落点周围的地面、栈桥、村子先同步建好
        player->set_global_position(spawn.origin + Vector3(0, 0.1f, 0));
        player->set_look_yaw(yaw_of(spawn));
        player->set_facing_yaw(yaw_of(spawn));
    }
    spawn_point = player->get_global_position();
    player->get_camera()->make_current();
    if (DisplayServer::get_singleton()->get_name() != String("headless")) {
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

void Expedition::build_hud() {
    CanvasLayer *layer = memnew(CanvasLayer);
    layer->set_name("Hud");
    add_child(layer);

    status_label = make_label(20);
    status_label->set_position(Vector2(24, 18));
    layer->add_child(status_label);
    prompt_label = bottom_label(layer, 22, -100);
    notice_label = bottom_label(layer, 26, -56);

    pack.setup(starting_pack, top_right_panel<BackpackPanel>(layer, "Backpack"));
    RefitPanel *refit_panel = top_right_panel<RefitPanel>(layer, "Refit");
    RefitOverlay *refit_overlay = memnew(RefitOverlay);
    refit_overlay->set_name("RefitOverlay");
    refit_overlay->set_visible(false);
    add_child(refit_overlay);
    refit.setup(refit_panel, refit_overlay);
    BuildPanel *build_panel = top_right_panel<BuildPanel>(layer, "Build");
    BuildOverlay *build_overlay = memnew(BuildOverlay);
    build_overlay->set_name("BuildOverlay");
    build_overlay->set_visible(false);
    add_child(build_overlay);
    build.setup(build_panel, build_overlay);
    InteriorPanel *interior_panel = top_right_panel<InteriorPanel>(layer, "Interior");
    InteriorOverlay *interior_overlay = memnew(InteriorOverlay);
    interior_overlay->set_name("InteriorOverlay");
    interior_overlay->set_visible(false);
    add_child(interior_overlay);
    interior.setup(interior_panel, interior_overlay);

    map_panel = map->map_make_panel();
    if (map_panel) {
        map_panel->set_name("Map");
        map_panel->set_anchors_preset(Control::PRESET_FULL_RECT);
        map_panel->set_visible(false);
        layer->add_child(map_panel);
        map_panel->set_destination_dock(destination_dock);
        map_panel->set_teleport_enabled(dev_tools);
        map_panel->connect("teleport_requested", Callable(this, "on_teleport_requested"));
    }
}

void Expedition::set_dev_tools(bool p_value) {
    dev_tools = p_value;
    if (map_panel) {
        map_panel->set_teleport_enabled(dev_tools);
    }
}

void Expedition::sync_helm_rules() {
    helm.helm_range = helm_range;
    helm.dock_range = dock_range;
    helm.leave_helm_speed = leave_helm_speed;
}

void Expedition::say(const String &p_text) {
    notice = p_text;
    notice_time = p_text.is_empty() ? 0.0 : notice_seconds;
    if (notice_label) {
        notice_label->set_text(notice);
    }
}

PackedStringArray Expedition::get_pack_items() const {
    PackedStringArray items;
    for (const ItemStack &stack : pack.items().get_stacks()) {
        items.push_back(String::utf8(stack.item.c_str()));
    }
    return items;
}

Dictionary Expedition::get_freight_order() const {
    Dictionary d;
    if (!map || !freight.is_active()) {
        return d;
    }
    const FreightOrder &order = freight.get_book().get_order();
    const std::vector<FreightPort> &ports = map->map_freight_ports();
    d["from"] = ports[size_t(order.from)].site_id;
    d["to"] = ports[size_t(order.to)].site_id;
    d["island"] = ports[size_t(order.to)].island;
    d["item"] = String::utf8(order.item.c_str());
    d["crates"] = order.crates;
    d["per_crate"] = order.per_crate;
    d["fee"] = order.fee;
    return d;
}

Vector3i Expedition::get_refit_cursor() const {
    const Cell c = refit.get_cursor();
    return Vector3i(c.x, c.y, c.z);
}

bool Expedition::is_ship_in_yard() const {
    for (Shipyard *yard : map->map_shipyards()) {
        if (RefitBench::ship_in(ship, yard)) {
            return true;
        }
    }
    return false;
}

Transform3D Expedition::get_origin_berth() const {
    return map && origin_index >= 0 ? map->map_dock_berth(origin_index) : Transform3D();
}

Transform3D Expedition::get_destination_berth() const {
    return map && destination_index >= 0 ? map->map_dock_berth(destination_index) : Transform3D();
}

// ---------------------------------------------------------------- 面板

void Expedition::close_panels() {
    pack.set_open(false);
    refit.close(player);
    build.close(player);
    interior.close(player);
    player->set_input_blocked(map_open);
}

void Expedition::set_pack_open(bool p_open) {
    if (p_open) {
        farm_hand.stand_down();
    }
    pack.set_open(p_open);
    player->set_input_blocked(p_open || map_open);
}

void Expedition::set_map_open(bool p_open) {
    if (!map_panel || map_open == p_open) {
        return;
    }
    map_open = p_open;
    map_panel->set_visible(map_open);
    status_label->set_visible(!map_open); // 地图自己有标题，底下那行别透上来
    prompt_label->set_visible(!map_open);
    player->set_input_blocked(map_open || pack.is_open() || refit.is_open() || build.is_open() || interior.is_open());
    if (ship) {
        ship->set_controlled(!map_open && helm.get_state() == Helm::PILOTING);
    }
    Input::get_singleton()->set_mouse_mode(map_open ? Input::MOUSE_MODE_VISIBLE : Input::MOUSE_MODE_CAPTURED);
    if (map_open) {
        const Vector3 at = player->get_global_position();
        map->map_refresh_panel(map_panel);
        map_panel->look_at_world(at.x, at.z, map_viewed ? 0.0 : MAP_FIRST_VIEW_M); // 之后再开沿用上次的缩放
        map_viewed = true;
        map_panel->set_player(at, player->get_look_yaw());
    }
}

// ---------------------------------------------------------------- 输入与每帧

void Expedition::_unhandled_input(const Ref<InputEvent> &p_event) {
    Input *input = Input::get_singleton();
    if (map_panel && p_event->is_action_pressed(input_actions::MAP)) {
        set_map_open(!map_open);
        return;
    }
    if (p_event->is_action_pressed("ui_cancel")) {
        if (map_open) {
            set_map_open(false);
        } else {
            input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        }
        return;
    }
    const bool captured = input->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED;
    const Ref<InputEventMouseMotion> motion = p_event;
    if (captured && motion.is_valid() && (refit.is_open() || build.is_open() || interior.is_open())) {
        const Vector2 rel = motion->get_relative();
        if (refit.is_open()) {
            refit.orbit(rel.x, rel.y);
        } else if (build.is_open()) {
            build.orbit(rel.x, rel.y);
        } else {
            interior.orbit(rel.x, rel.y);
        }
        return;
    }
    const Ref<InputEventMouseButton> button = p_event;
    if (captured && (build.is_open() || interior.is_open()) && button.is_valid() && button->is_pressed() &&
            (button->get_button_index() == MOUSE_BUTTON_WHEEL_UP || button->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN)) {
        const bool in = button->get_button_index() == MOUSE_BUTTON_WHEEL_UP;
        if (build.is_open()) {
            build.zoom(in);
        } else {
            interior.zoom(in);
        }
        return;
    }
    if (!map_open && button.is_valid() && button->is_pressed() && !captured) {
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

void Expedition::_physics_process(double p_delta) {
    const bool interact = !map_open && Input::get_singleton()->is_action_just_pressed(input_actions::INTERACT);
    notice_time = Math::max(0.0, notice_time - p_delta);
    if (notice_time <= 0.0 && notice_label) {
        notice_label->set_text(String());
    }
    switch (helm.get_state()) {
        case Helm::ON_FOOT:
            on_foot(p_delta, interact);
            break;
        case Helm::PILOTING:
        case Helm::MOORING:
            switch (helm.step(interact, berths())) {
                case Helm::STILL_MOVING:
                    say(String(U"船还在走，先停稳再离舵"));
                    break;
                case Helm::REFUSED:
                    say(helm.get_refusal());
                    break;
                default:
                    break;
            }
            break;
    }
    update_hud();
}

void Expedition::on_foot(double p_delta, bool p_interact) {
    Input *input = Input::get_singleton();
    const Vector3 at = player->get_global_position();
    if (!map_open) {
        if (input->is_action_just_pressed(input_actions::INVENTORY)) {
            if (refit.is_open() || build.is_open() || interior.is_open()) {
                close_panels(); // Tab 先合上开着的那个
            } else {
                set_pack_open(!pack.is_open());
            }
        }
        if (dev_tools && input->is_action_just_pressed(input_actions::BUILD_MODE)) {
            if (build.is_open()) {
                close_panels();
            } else {
                close_panels();
                farm_hand.stand_down();
                player->set_input_blocked(build.open_on(map->map_settlement_at(at), at, say_fn()) || map_open);
            }
        }
        if (dev_tools && input->is_action_just_pressed(input_actions::EDIT_INTERIOR)) {
            toggle_interior(at);
        }
        Shipyard *bench = RefitBench::bench_near(map->map_shipyards(), at, helm_range);
        if (refit.is_open()) {
            refit.browse(ship, p_interact, say_fn()); // 开着改装：E 是加一节
        } else if (interior.is_open()) {
            interior.browse(p_delta, p_interact, say_fn()); // 开着室内编辑：E 是放家具、开门窗……
        } else if (build.is_open()) {
            build.browse(p_delta, p_interact, say_fn()); // 开着布置模式：E 是放一座
        } else if (pack.is_open()) {
            pack.browse(p_interact, say_fn()); // 开着背包：E 是拿东西，不是上舵、不是干活
        } else if (freight.step(map, ship, player, pack, p_interact, say_fn())) {
            farm_hand.stand_down(); // 这一下 E 是村仓前 / 货位前的事
        } else if (p_interact && bench) {
            if (freight.is_holding()) {
                say(String(U"抱着货箱改不了船，先放下"));
            } else if (freight.cargo_aboard() > 0) {
                say(String(U"船上有货，卸了再改船"));
            } else {
                close_panels();
                farm_hand.stand_down();
                player->set_input_blocked(refit.open_at(bench, ship, say_fn()) || map_open);
            }
        } else if (p_interact && ship && helm.near_helm()) {
            if (freight.is_holding()) {
                say(String(U"抱着货箱掌不了舵：先放到货位上"));
            } else if (!ship->can_fly()) {
                say(String(U"没有动力区，飞不起来：去船坞的画样台加一节"));
            } else {
                board();
                return;
            }
        } else if (freight.is_holding()) {
            farm_hand.stand_down(); // 抱着货箱腾不出手干农活
        } else {
            const FarmHand::Worked worked = farm_hand.step(map->map_farm_at(at), player, pack, reach, p_delta, p_interact, say_fn());
            if (worked.done) {
                emit_signal("farm_worked", worked.cell, int(worked.action));
            }
        }
    }
    track_footing();
    if (player->get_global_position().y < kill_altitude) {
        respawn();
    }
}

// T：开着就合上；布置模式里光标下有房子就改那座；否则改人站在里面的那座
void Expedition::toggle_interior(const Vector3 &p_at) {
    if (interior.is_open()) {
        close_panels();
        return;
    }
    Settlement *settlement = nullptr;
    int index = -1;
    if (build.is_open()) {
        settlement = build.get_target();
        index = build.get_selected();
    } else {
        settlement = BuildMode::settlement_of(map->map_settlement_at(p_at));
        index = settlement ? settlement->find_at(p_at) : -1;
    }
    close_panels();
    farm_hand.stand_down();
    player->set_input_blocked(interior.open_on(settlement, index, say_fn()) || map_open);
}

void Expedition::board() {
    close_panels();
    farm_hand.stand_down();
    helm.board();
}

std::vector<Berth> Expedition::berths() const {
    std::vector<Berth> out;
    for (int k = 0; k < map->map_dock_count(); k++) {
        if (map->map_dock_open(k)) {
            out.push_back(Berth{ map->map_dock_berth(k), false, false, map->map_dock_id(k) });
        }
    }
    for (Shipyard *yard : map->map_shipyards()) {
        if (yard->is_present()) {
            out.push_back(Berth{ yard->get_cradle_contact(), true, false, -1 });
        }
    }
    // 各处聚落里的船台：离船远的不算（船附近几百米内才可能叫得上）
    const String problem = ship ? String::utf8(land_berth::ship_problem(ship->get_layout_model()).c_str()) : String();
    const Vector3 near = ship ? ship->get_global_position() : player->get_global_position();
    const TypedArray<Node> settlements = get_tree()->get_nodes_in_group(Settlement::GROUP);
    for (int64_t n = 0; n < settlements.size(); n++) {
        Settlement *settlement = Object::cast_to<Settlement>(settlements[n]);
        if (!settlement || !settlement->is_inside_tree()) {
            continue;
        }
        for (int k : settlement->find_berths()) {
            const Transform3D contact = settlement->get_berth_contact(k);
            if (contact.origin.distance_to(near) > 400.0) {
                continue;
            }
            Berth berth{ contact, false, true, -1 };
            berth.refusal = problem;
            out.push_back(berth);
        }
    }
    return out;
}

Vector3 Expedition::focus_point() const {
    if (helm.get_state() == Helm::ON_FOOT || !ship) {
        return player->get_global_position();
    }
    return ship->get_global_position() + ship->get_velocity() * real_t(focus_lead_s);
}

void Expedition::_process(double) {
    map->map_focus(focus_point());
    if (map_open) {
        map_panel->set_player(player->get_global_position(), player->get_look_yaw());
    }
}

// ---------------------------------------------------------------- 落脚、到达、回位、瞬移

int Expedition::course_target() const {
    return arrived ? origin_index : destination_index;
}

bool Expedition::freight_target(Transform3D &r_target, String &r_name) const {
    const int port = freight.destination_port();
    if (port < 0 || !map->map_freight_target(port, r_target)) {
        return false;
    }
    r_name = freight.port_name(map, port);
    return true;
}

// 记下最近一次站稳的位置（坠落后回到这里），顺便判定是否已踏上目的码头那座岛
void Expedition::track_footing() {
    const Vector3 at = player->get_global_position();
    if (!player->is_on_floor() || !map->map_footing(at)) {
        return;
    }
    safe_footing = at;
    has_safe_footing = true;
    if (arrived || destination_index < 0) {
        return;
    }
    const Vector3 dock = map->map_dock_position(destination_index);
    if (map->map_island_at(at) == map->map_dock_island(destination_index) && Vector2(at.x - dock.x, at.z - dock.z).length() <= arrive_radius) {
        arrived = true;
        say(String(U"到了：") + map->map_dock_label(destination_index));
        emit_signal("arrived");
    }
}

void Expedition::respawn() {
    const Vector3 target = has_safe_footing ? safe_footing : spawn_point;
    farm_hand.stand_down();
    map->map_prepare(target);
    player->set_velocity(Vector3());
    player->set_global_position(target + Vector3(0, 0.5f, 0));
    say(String(U"坠入云海，回到了上一个落脚点"));
    // 人回来了船却漂在远处，这一局就死了：把船召回离落脚点最近的码头
    if (ship && ship->get_global_position().distance_to(target) > recall_distance) {
        const int dock = nearest_open_dock(target);
        if (dock >= 0) {
            ship->place_at(map->map_dock_berth(dock));
        }
    }
}

int Expedition::nearest_open_dock(const Vector3 &p_point) const {
    int best = -1;
    double best_d = 1e18;
    for (int k = 0; k < map->map_dock_count(); k++) {
        if (!map->map_dock_open(k)) {
            continue;
        }
        const double d = map->map_dock_position(k).distance_to(p_point);
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    return best;
}

bool Expedition::nearest_land(const Vector3 &p_point, Vector3 &r_point) const {
    auto surface = [this](double p_x, double p_z) { return map->map_surface(Vector3(real_t(p_x), 0, real_t(p_z))); };
    double y = surface(p_point.x, p_point.z);
    if (!std::isnan(y)) {
        r_point = Vector3(p_point.x, real_t(y), p_point.z);
        return true;
    }
    for (double r = LAND_RING_STEP_M; r <= land_search_m; r += LAND_RING_STEP_M) {
        const int count = std::max(12, int(Math_TAU * r / LAND_RING_STEP_M));
        for (int k = 0; k < count; k++) {
            const double a = Math_TAU * k / count;
            const double dx = std::cos(a), dz = std::sin(a);
            y = surface(p_point.x + r * dx, p_point.z + r * dz);
            if (std::isnan(y)) {
                continue;
            }
            // 再往里挪一点，别正落在崖边上
            const double in_x = p_point.x + (r + INLAND_NUDGE_M) * dx, in_z = p_point.z + (r + INLAND_NUDGE_M) * dz;
            const double in_y = surface(in_x, in_z);
            if (!std::isnan(in_y)) {
                r_point = Vector3(real_t(in_x), real_t(in_y), real_t(in_z));
            } else {
                r_point = Vector3(real_t(p_point.x + r * dx), real_t(y), real_t(p_point.z + r * dz));
            }
            return true;
        }
    }
    return false;
}

bool Expedition::teleport_to(const Vector3 &p_position) {
    Vector3 target;
    if (!map || !player || !nearest_land(p_position, target)) {
        return false;
    }
    map->map_prepare(target);
    player->set_global_position(target + Vector3(0, 0.3f, 0));
    player->set_velocity(Vector3());
    safe_footing = target;
    has_safe_footing = true;
    return true;
}

void Expedition::on_teleport_requested(const Vector3 &p_position) {
    if (!dev_tools) {
        return;
    }
    if (helm.get_state() != Helm::ON_FOOT) {
        say(String(U"掌舵时不能瞬移：先靠泊下船"));
        return;
    }
    if (!teleport_to(p_position)) {
        say(vformat(String(U"那里是云海，%d km 内没有能落脚的地"), int(land_search_m / 1000.0)));
        return;
    }
    const Vector3 at = player->get_global_position();
    say(vformat(String(U"瞬移到岛 #%d（东 %s km，南 %s km）"), map->map_island_at(at), String::num(at.x / 1000.0, 2), String::num(at.z / 1000.0, 2)));
    set_map_open(false);
}

// ---------------------------------------------------------------- HUD

String Expedition::get_status_text() const {
    return status_label ? status_label->get_text() : String();
}

String Expedition::clock_text() const {
    // 全局只有一个钟：到哪都一样
    const WorldClock *clock = WorldClock::get_singleton();
    return clock ? clock->get_clock_text() : String();
}

void Expedition::update_hud() {
    const Vector3 at = player->get_global_position();
    PackedStringArray lines;
    const String location = map->map_location_text(at);
    if (!location.is_empty()) {
        String line = location + (map_panel ? String(U" · M 地图") : String());
        const int pending = map->map_pending();
        if (pending > 0) {
            line += vformat(String(U" · 地面加载中 %d"), pending);
        }
        lines.push_back(line);
    }
    // 航行目标：接了托运就是目的村的船台；否则没到过对岸就是目的码头，到过了就是回程
    const int target = course_target();
    Transform3D freight_to;
    String freight_name;
    const bool toward_freight = freight_target(freight_to, freight_name);
    String prompt;
    switch (helm.get_state()) {
        case Helm::ON_FOOT: {
            if (ship && toward_freight) {
                const double km = Vector2(freight_to.origin.x - at.x, freight_to.origin.z - at.z).length() / 1000.0;
                lines.push_back(vformat(String(U"目标：托运送去%s（%s km）"), freight_name, String::num(km, 1)));
            } else if (ship && target >= 0) {
                lines.push_back(map->map_goal_text(target, arrived, at));
            }
            const String freight_line = freight.status_text(map);
            if (!freight_line.is_empty()) {
                lines.push_back(freight_line);
            }
            const bool near_helm = ship && helm.near_helm();
            prompt = near_helm ? (ship->can_fly() ? String(U"按 E 掌舵") : String(U"没有动力区，飞不起来")) : String(U"WASD 移动 · 空格 跳 · Shift 疾跑");
            if (refit.is_open()) {
                refit.refresh(ship);
                prompt = String(U"W/S 前后 · A/D 左右 · 空格/Shift 上下 · 1/2/3 选模块 · E 放/换 · X 拆 · Tab 合上");
                break;
            }
            if (interior.is_open()) {
                interior.refresh();
                lines.clear();
                lines.push_back(String(U"开发者室内编辑 · ") + interior.get_template_id());
                prompt = String(U"WASD 挪光标 · Z/V 换工具 · Q/R 转向 · 空格/Ctrl 换层 · E 放 · X 拆 · N 另存 · F5 存盘 · Tab 合上");
                break;
            }
            if (build.is_open()) {
                build.refresh();
                lines.clear();
                lines.push_back(String(U"开发者布置模式 · ") + String(build.get_target()->get_host()->host_node()->get_name()));
                prompt = String(U"WASD 挪光标 · Q/R 转向 · 1-5 选建筑 · V/Z 换模板 · E 放 · X 拆 · T 改屋里 · F5 存盘 · Tab 合上");
                break;
            }
            Shipyard *bench = RefitBench::bench_near(map->map_shipyards(), at, helm_range);
            if (bench) {
                prompt = RefitBench::ship_in(ship, bench) ? String(U"按 E 改装空艇") : String(U"画样台 · 空艇不在船坞里");
            }
            // 背包一眼看全，手上拿着什么；脚下有农田就说田的事
            String pack_line = pack.summary();
            const String fields = farm_hand.fields_text();
            if (!fields.is_empty()) {
                pack_line += String(U"    ") + fields;
            }
            lines.push_back(pack_line);
            if (pack.is_open()) {
                pack.refresh_panel();
                prompt = String(U"W/S 选 · E 拿在手上 · Tab 合上");
                break;
            }
            const String cargo = freight.prompt(map);
            if (!cargo.is_empty()) {
                prompt = cargo; // 村仓 / 货位前、抱着货箱：先说这个
                break;
            }
            if (near_helm || bench) {
                break;
            }
            const String work = farm_hand.prompt(pack);
            if (!work.is_empty()) {
                prompt = work;
            }
        } break;
        case Helm::PILOTING:
            if (toward_freight) {
                lines.clear();
                lines.push_back(helm.course_text(freight_to, String(U"托运 ") + freight_name));
            } else if (target >= 0) {
                lines.clear();
                lines.push_back(helm.course_text(map->map_dock_berth(target), map->map_course_name(target, arrived)));
            }
            prompt = helm.piloting_prompt(berths());
            break;
        case Helm::MOORING:
            lines.clear();
            lines.push_back(helm.is_mooring_to_yard() ? String(U"入坞中") : (helm.get_moor_berth().land ? String(U"落上船台") : String(U"靠泊中")));
            prompt = String(U"……");
            break;
    }
    String status = String("\n").join(lines);
    // 第一行末尾：脚下（或船下）那处聚落的钟点——村民按它出工收工
    const String clock = clock_text();
    if (!clock.is_empty()) {
        const int line_end = status.find("\n");
        status = line_end < 0 ? status + String(U"    ") + clock : status.insert(line_end, String(U"    ") + clock);
    }
    status_label->set_text(status);
    prompt_label->set_text(prompt);
}
