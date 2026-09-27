#include "expedition.h"

#include <cmath>
#include <limits>

#include <godot_cpp/classes/time.hpp>

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

#include "atmosphere.h"
#include "planet_view.h"
#include "farm.h"
#include "input_actions.h"
#include "land_berth.h"
#include "placeables.h"
#include "recipes.h"
#include "scatter_stream.h"
#include "settlement.h"
#include "shipyard.h"
#include "wild_landing.h"
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

constexpr double WILD_QUIET_ABOVE_M = 20.0; // 比能野泊的高度再高这么多，就不提野泊的事
constexpr double WILD_MAX_SPEED = 8.0; // 船慢到这么慢才提野泊（巡航时贴地飞过不算）
constexpr double WILD_PREFER_M = 200.0; // 这么近有资源点：舷梯朝着它放

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
    ClassDB::bind_method(D_METHOD("get_shift_count"), &Expedition::get_shift_count);
    ClassDB::bind_method(D_METHOD("get_last_shift_ms"), &Expedition::get_last_shift_ms);
    ClassDB::bind_method(D_METHOD("get_origin_berth"), &Expedition::get_origin_berth);
    ClassDB::bind_method(D_METHOD("get_destination_berth"), &Expedition::get_destination_berth);
    ClassDB::bind_method(D_METHOD("teleport_to", "position"), &Expedition::teleport_to);
    ClassDB::bind_method(D_METHOD("on_teleport_requested", "position"), &Expedition::on_teleport_requested);
    ClassDB::bind_method(D_METHOD("is_map_open"), &Expedition::is_map_open);
    ClassDB::bind_method(D_METHOD("get_map"), &Expedition::get_map);
    ClassDB::bind_method(D_METHOD("get_map_node"), &Expedition::get_map_node);
    ClassDB::bind_method(D_METHOD("is_working"), &Expedition::is_working);
    ClassDB::bind_method(D_METHOD("is_gathering"), &Expedition::is_gathering);
    ClassDB::bind_method(D_METHOD("get_gather_site"), &Expedition::get_gather_site);
    ClassDB::bind_method(D_METHOD("is_holding_goods"), &Expedition::is_holding_goods);
    ClassDB::bind_method(D_METHOD("get_cargo_items"), &Expedition::get_cargo_items);
    ClassDB::bind_method(D_METHOD("get_wild_note"), &Expedition::get_wild_note);
    ClassDB::bind_method(D_METHOD("has_wild_berth"), &Expedition::has_wild_berth);
    ClassDB::bind_method(D_METHOD("probe_wild", "at", "yaw"), &Expedition::probe_wild);
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
    ClassDB::bind_method(D_METHOD("get_hotbar_selected"), &Expedition::get_hotbar_selected);
    ClassDB::bind_method(D_METHOD("get_held_item"), &Expedition::get_held_item);
    ClassDB::bind_method(D_METHOD("get_pack_slot", "slot"), &Expedition::get_pack_slot);
    ClassDB::bind_method(D_METHOD("get_pack_cursor_stack"), &Expedition::get_pack_cursor_stack);
    ClassDB::bind_method(D_METHOD("get_pack_focus"), &Expedition::get_pack_focus);
    ClassDB::bind_method(D_METHOD("get_pack_slot_center", "section", "index"), &Expedition::get_pack_slot_center);
    ClassDB::bind_method(D_METHOD("get_pack_other_title"), &Expedition::get_pack_other_title);
    ClassDB::bind_method(D_METHOD("get_other_count", "item"), &Expedition::get_other_count);
    ClassDB::bind_method(D_METHOD("get_hold_size"), &Expedition::get_hold_size);
    ClassDB::bind_method(D_METHOD("get_hold_count", "item"), &Expedition::get_hold_count);
    ClassDB::bind_method(D_METHOD("get_recipe_index", "id"), &Expedition::get_recipe_index);
    ClassDB::bind_method(D_METHOD("get_stations"), &Expedition::get_stations);
    ClassDB::bind_method(D_METHOD("get_placed_props"), &Expedition::get_placed_props);
    ClassDB::bind_method(D_METHOD("get_ghost"), &Expedition::get_ghost);
    ClassDB::bind_method(D_METHOD("get_facing_prop"), &Expedition::get_facing_prop);
    ClassDB::bind_method(D_METHOD("is_aboard"), &Expedition::is_aboard);
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
    prompt_label = bottom_label(layer, 22, -122); // 快捷栏在最底下
    notice_label = bottom_label(layer, 26, -84);

    HotbarBar *hotbar = memnew(HotbarBar);
    hotbar->set_name("Hotbar");
    hotbar->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    layer->add_child(hotbar);
    InventoryPanel *inventory = memnew(InventoryPanel);
    inventory->set_name("Backpack");
    inventory->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    inventory->set_visible(false);
    layer->add_child(inventory);
    pack.setup(starting_pack, inventory, hotbar);
    PlacedProps *placed = memnew(PlacedProps);
    placed->set_name("Placed");
    placed->set_as_top_level(true); // 摆出来的东西用全局坐标
    add_child(placed);
    place.setup(placed);
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

void Expedition::set_destination_dock(int p_value) {
    destination_dock = p_value;
    if (map && map->map_ready()) {
        destination_index = map->map_find_dock(destination_dock);
        arrived = false;
        if (map_panel) {
            map_panel->set_destination_dock(destination_dock);
        }
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
    for (const ItemStack &stack : pack.items().get_slots()) {
        const String item = String::utf8(stack.item.c_str());
        if (!stack.empty() && !items.has(item)) {
            items.push_back(item);
        }
    }
    return items;
}

Dictionary Expedition::get_pack_slot(int p_slot) const {
    Dictionary d;
    if (p_slot >= 0 && p_slot < pack.items().size()) {
        d["item"] = String::utf8(pack.items().at(p_slot).item.c_str());
        d["count"] = pack.items().at(p_slot).count;
    }
    return d;
}

Dictionary Expedition::get_pack_cursor_stack() const {
    Dictionary d;
    d["item"] = String::utf8(pack.get_cursor().item.c_str());
    d["count"] = pack.get_cursor().count;
    return d;
}

int Expedition::get_recipe_index(const String &p_id) const {
    const RecipeDef *r = recipes::find(play::to_std(p_id));
    return r ? int(r - recipes::RECIPES) : -1;
}

Array Expedition::get_placed_props() const {
    Array out;
    for (const PlacedProp &p : place.get_placed().get_props()) {
        Dictionary d;
        d["id"] = p.id;
        d["item"] = String::utf8(p.item.c_str());
        d["x"] = p.x;
        d["y"] = p.y;
        d["z"] = p.z;
        d["quarter"] = p.quarter;
        d["used"] = p.contents.used();
        out.push_back(d);
    }
    return out;
}

Dictionary Expedition::get_ghost() const {
    Dictionary d;
    const PlaceCheck &g = place.get_ghost();
    d["on"] = place.is_ghost_on();
    d["ok"] = g.ok;
    d["why"] = String::utf8(g.why.c_str());
    d["x"] = g.x;
    d["y"] = g.y;
    d["z"] = g.z;
    d["quarter"] = g.quarter;
    return d;
}

PackedStringArray Expedition::get_cargo_items() const {
    PackedStringArray items;
    for (const Crate &crate : freight.get_book().get_slots()) {
        items.push_back(String::utf8(crate.item.c_str()));
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
    gather.stand_down();
    place.stand_down();
    if (pack.is_open()) {
        set_pack_open(false);
    }
    refit.close(player);
    build.close(player);
    interior.close(player);
    player->set_input_blocked(map_open);
}

void Expedition::set_pack_open(bool p_open, PackSide p_side, int p_prop) {
    if (p_open) {
        farm_hand.stand_down();
        gather.stand_down();
        place.stand_down();
        pack_side = p_side;
        pack_prop = p_prop;
        pack.open_with(nullptr, String());
        sync_pack_side();
        pack.refresh_panel();
    } else {
        pack.close();
        pack_side = SIDE_NONE;
        pack_prop = -1;
    }
    player->set_input_blocked(p_open || map_open);
    status_label->set_visible(!p_open && !map_open); // 面板自己有标题与说明，底下的提示行别透上来
    prompt_label->set_visible(!p_open && !map_open);
    if (!map_open) {
        Input::get_singleton()->set_mouse_mode(p_open ? Input::MOUSE_MODE_VISIBLE : Input::MOUSE_MODE_CAPTURED); // 开着背包要用鼠标点
    }
}

void Expedition::sync_pack_side() {
    pack.set_stations(place.stations_near(player->get_global_position()));
    if (!pack.is_open()) {
        return;
    }
    if (pack_side == SIDE_HOLD && hold.size() > 0) {
        pack.set_other(&hold, vformat(String(U"船舱（%d 格）"), hold.size()));
    } else if (pack_side == SIDE_PROP) {
        pack.set_other(place.container(pack_prop), place.title_of(pack_prop));
    } else {
        pack.set_other(nullptr, String());
    }
}

void Expedition::sync_hold() {
    const int bay = ship ? ship->get_bay_slots() : 0;
    if (hold.size() != bay) {
        hold.resize(bay); // 改船时已查过放得下（bay_in_use）
    }
}

int Expedition::bay_in_use() const {
    ItemSlots packed = hold;
    packed.compact();
    return packed.used();
}

void Expedition::place_act(bool p_interact, bool p_remove) {
    const PlaceHand::Step step = place.act(pack, p_interact, p_remove, say_fn());
    if (step.open >= 0) {
        set_pack_open(true, place.container(step.open) ? SIDE_PROP : SIDE_NONE, step.open);
    }
}

void Expedition::set_map_open(bool p_open) {
    if (!map_panel || map_open == p_open) {
        return;
    }
    map_open = p_open;
    map_panel->set_visible(map_open);
    status_label->set_visible(!map_open && !pack.is_open()); // 地图自己有标题，底下那行别透上来
    prompt_label->set_visible(!map_open && !pack.is_open());
    player->set_input_blocked(map_open || pack.is_open() || refit.is_open() || build.is_open() || interior.is_open());
    if (ship) {
        ship->set_controlled(!map_open && helm.get_state() == Helm::PILOTING);
    }
    Input::get_singleton()->set_mouse_mode(map_open || pack.is_open() ? Input::MOUSE_MODE_VISIBLE : Input::MOUSE_MODE_CAPTURED);
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
        } else if (pack.is_open()) {
            set_pack_open(false);
        } else {
            input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        }
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    const Ref<InputEventMouseButton> button = p_event;
    if (!map_open && pack.is_open() && (motion.is_valid() || button.is_valid())) {
        pack.on_mouse(p_event, say_fn()); // 背包面板上的点法
        return;
    }
    const bool captured = input->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED;
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
    // 步行时滚轮换快捷栏那一格
    if (captured && button.is_valid() && button->is_pressed() && helm.get_state() == Helm::ON_FOOT && !refit.is_open() &&
            (button->get_button_index() == MOUSE_BUTTON_WHEEL_UP || button->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN)) {
        pack.scroll(button->get_button_index() == MOUSE_BUTTON_WHEEL_UP ? -1 : 1);
        return;
    }
    if (!map_open && button.is_valid() && button->is_pressed() && !captured) {
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

bool Expedition::apply_shift(const std::vector<Vector3 *> &p_extra) {
    MapShift s;
    if (!map || !map->map_shift_begin(s)) {
        return false;
    }
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    // 人与船：位置经真球面换过去，朝向、速度、视线转该点处的那一点点角度（绕竖直轴）；相机挂在它们下面，跟着走
    const Vector3 at = player->get_global_position();
    player->shift(s.point(at), s.yaw(at), s.rotation(at));
    if (ship) {
        const Vector3 o = ship->get_global_position();
        ship->shift(s.point(o), s.yaw(o), s.rotation(o));
    }
    // 摆出来的东西（全局坐标的账）
    place.edit_placed().shift([&s](double &x, double &y, double &z, double &yaw) {
        const Vector3 p = Vector3(real_t(x), real_t(y), real_t(z));
        yaw += s.yaw(p);
        const Vector3 q = s.point(p);
        x = q.x;
        y = q.y;
        z = q.z;
    });
    // 自己记的位置
    if (has_safe_footing) {
        safe_footing = s.point(safe_footing);
    }
    spawn_point = s.point(spawn_point);
    for (Vector3 *p : p_extra) {
        if (p) {
            *p = s.point(*p);
        }
    }
    wild_frame = ~uint64_t(0);
    wild_ok = false;
    // 跑货的村仓与航线只在一个岛群里：接着的单子送不到了，箱子算自家的货
    if (freight.is_active()) {
        freight.void_order();
        say(String(U"换了岛群：这单托运作废，船上的箱子算自家的货"));
    }
    map->map_shift_commit();
    shifts++;
    last_shift_ms = double(Time::get_singleton()->get_ticks_usec() - t0) / 1000.0;
    return true;
}

void Expedition::_physics_process(double p_delta) {
    // 行星计划 P3：地图宿主定下换锚了——这一帧的移动之前把人、船都换到新锚群的平面
    if (map->map_shift_pending()) {
        apply_shift();
    }
    const bool interact = !map_open && Input::get_singleton()->is_action_just_pressed(input_actions::INTERACT);
    notice_time = Math::max(0.0, notice_time - p_delta);
    if (notice_time <= 0.0 && notice_label) {
        notice_label->set_text(String());
    }
    sync_hold();
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
                case Helm::DISEMBARKED:
                    // 野泊落定：从舷门放下舷梯
                    if (ship && helm.get_moor_berth().wild) {
                        const Berth &landed = helm.get_moor_berth();
                        ship->set_gangway(landed.gangway_side, landed.gangway_run, landed.gangway_rise);
                        say(vformat(String(U"野泊：舷梯放在%s"), landed.gangway_side > 0 ? String(U"右舷") : String(U"左舷")));
                    }
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
        const bool aboard = ship && ship->is_aboard(at);
        if (input->is_action_just_pressed(input_actions::INVENTORY)) {
            if (refit.is_open() || build.is_open() || interior.is_open()) {
                close_panels(); // Tab 先合上开着的那个
            } else if (pack.is_open()) {
                set_pack_open(false);
            } else {
                set_pack_open(true, aboard && hold.size() > 0 ? SIDE_HOLD : SIDE_NONE); // 在船上：船舱列在背包上面
            }
        }
        if (!pack.is_open() && !refit.is_open() && !build.is_open() && !interior.is_open()) {
            for (int k = 0; k < PackHand::HOTBAR; k++) {
                if (input->is_action_just_pressed(input_actions::HOTBAR[k])) {
                    pack.select(k); // 1–9：拿起快捷栏那一格
                }
            }
        }
        sync_pack_side();
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
        // 摆放先看一眼（看着哪件、手上能摆的出虚影）；看着木箱 / 工作台 / 窑按 E、手上拿着能摆的按 E、看着摆出来的按 X，
        // 都先归摆放，堆场、村仓往后排（船上不让摆：货位、舵照旧）
        const bool panel_open = refit.is_open() || interior.is_open() || build.is_open() || pack.is_open();
        if (panel_open || freight.is_holding()) {
            place.stand_down(); // 抱着货箱腾不出手
        } else {
            place.look(map, player, pack, aboard);
        }
        const bool remove = input->is_action_just_pressed(input_actions::REMOVE);
        const bool placing_first = !aboard && !panel_open && !freight.is_holding() && place.claims(pack, p_interact, remove);
        if (refit.is_open()) {
            refit.browse(ship, p_interact, say_fn(), bay_in_use()); // 开着改装：E 是加一节
        } else if (interior.is_open()) {
            interior.browse(p_delta, p_interact, say_fn()); // 开着室内编辑：E 是放家具、开门窗……
        } else if (build.is_open()) {
            build.browse(p_delta, p_interact, say_fn()); // 开着布置模式：E 是放一座
        } else if (pack.is_open()) {
            pack.browse(p_interact, say_fn()); // 开着背包：E 是拿放东西、合成，不是上舵、不是干活
        } else if (placing_first) {
            place_act(p_interact, remove); // 这一下 E / X 是摆放的事（摆、收、打开木箱 / 工作台 / 窑）
            farm_hand.stand_down();
            gather.stand_down();
        } else if (freight.step(map, ship, player, pack, p_interact, say_fn())) {
            farm_hand.stand_down(); // 这一下 E 是村仓前 / 货位前 / 堆场的事
            gather.stand_down();
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
        } else if (gather.step(map, player, pack, freight.is_holding(), p_interact, p_delta, say_fn())) {
            farm_hand.stand_down(); // 这一下 E 是采掘面前的事
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
    if (ship) {
        ship->clear_gangway(); // 野泊放下的舷梯收起来
    }
    helm.board();
}

// 野泊：船下这块地落不落得下（这一帧算过就不再算）
bool Expedition::plan_wild(const Vector3 &p_at, double p_yaw, Berth &r_berth, String &r_why) const {
    const LandingRules rules;
    const Vector3 at = p_at;
    double prefer_x = std::numeric_limits<double>::quiet_NaN(), prefer_z = prefer_x;
    const ResourceSpot near = map->map_resource_near(at, WILD_PREFER_M);
    if (near.site >= 0) {
        prefer_x = near.face.x;
        prefer_z = near.face.z;
    }
    const double yaw = p_yaw;
    const WildLanding w = plan_wild_landing(MapLandingGround(map), ship->get_layout_model(), at.x, at.y, at.z, yaw, rules, prefer_x, prefer_z);
    if (!w.ok) {
        r_why = String::utf8(w.why.c_str());
        return false;
    }
    TypedArray<RID> exclude;
    exclude.push_back(ship->get_rid());
    exclude.push_back(player->get_rid());
    const Transform3D pose(Basis(Vector3(0, 1, 0), real_t(yaw)), Vector3(at.x, real_t(w.deck_y), at.z));
    if (Object *hit = ship->landing_obstacle(pose, w.ground_hi, exclude, w.side, w.run, w.rise)) {
        bool house = false;
        for (Node *node = Object::cast_to<Node>(hit); node && !house; node = node->get_parent()) {
            house = Object::cast_to<Settlement>(node) != nullptr;
        }
        r_why = house ? String(U"船底下、舷梯边有房子") : ScatterStream::is_trunk(hit) ? String(U"船底下、舷梯边有树") : String(U"船底下、舷梯边有东西挡着");
        return false;
    }
    r_berth = Berth();
    r_berth.contact = Transform3D(Basis(Vector3(0, 1, 0), real_t(yaw + (w.side > 0 ? 0.0 : Math_PI))), Vector3(real_t(w.door_x), real_t(w.deck_y), real_t(w.door_z)));
    r_berth.wild = true;
    r_berth.gangway_side = w.side;
    r_berth.gangway_run = w.run;
    r_berth.gangway_rise = w.rise;
    return true;
}

void Expedition::refresh_wild() {
    const uint64_t frame = Engine::get_singleton()->get_physics_frames();
    if (frame == wild_frame) {
        return;
    }
    wild_frame = frame;
    wild_ok = false;
    helm.wild_note = String();
    if (!ship || helm.get_state() != Helm::PILOTING) {
        return;
    }
    const Vector3 at = ship->get_global_position();
    double ground = 0.0;
    if (ship->get_speed() > WILD_MAX_SPEED || !map->map_landing_height(at.x, at.z, ground) ||
            at.y - (ground + LandingRules().deck_lift_m) > LandingRules().max_descent_m + WILD_QUIET_ABOVE_M) {
        return; // 还在赶路、船下是云海，或者还高着：不提野泊
    }
    wild_ok = plan_wild(at, ship->get_rotation().y, wild, helm.wild_note);
}

Dictionary Expedition::probe_wild(const Vector3 &p_at, double p_yaw) const {
    Dictionary d;
    Berth berth;
    String why;
    d["ok"] = ship && plan_wild(p_at, p_yaw, berth, why);
    d["why"] = why;
    if (bool(d["ok"])) {
        d["side"] = berth.gangway_side;
        d["run"] = berth.gangway_run;
        d["rise"] = berth.gangway_rise;
        d["door"] = berth.contact.origin; // 舷门的甲板边（y = 落定后的甲板高）
        d["out"] = berth.contact.basis.get_column(0);
    }
    return d;
}

std::vector<Berth> Expedition::berths() {
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
    refresh_wild();
    if (wild_ok) {
        out.push_back(wild);
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
    Vector3 target = has_safe_footing ? safe_footing : spawn_point;
    farm_hand.stand_down();
    // 上一处落脚点在别的群的地上（换锚以后掉下去的）：先换回那一群（落脚点跟着换过去）
    if (map->map_shift_toward(target) && apply_shift()) {
        target = has_safe_footing ? safe_footing : spawn_point;
    }
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
    // 落点在别的群的地上：先换锚（落点跟着换过去）
    if (map->map_shift_toward(target)) {
        apply_shift({ &target });
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
    // 全局只有一个钟：到哪都一样；后面接此地的季节与天气（天与光按人站的那一点取）
    const WorldClock *clock = WorldClock::get_singleton();
    String text = clock ? clock->get_clock_text() : String();
    // 行星上：人所在处的地方时（钟点是参考经度的地方时，D10；这里按人的经度差换算，15° 一小时）
    if (const PlanetView *pv = PlanetView::active_view(); pv && clock && player) {
        const Vector2 ll = pv->latlon_at(player->get_global_position());
        double d = std::fmod(double(ll.y) - pv->get_clock_longitude(), 360.0);
        d = d > 180.0 ? d - 360.0 : (d < -180.0 ? d + 360.0 : d);
        const double hour = std::fmod(clock->get_hour() + d / 15.0 + 24.0, 24.0);
        const int minutes = int(std::floor(hour * 60.0 + 1e-6));
        text += vformat(String(U" · 地方时 %02d:%02d"), (minutes / 60) % 24, minutes % 60);
    }
    Atmosphere *atmosphere = is_inside_tree() ? Object::cast_to<Atmosphere>(get_tree()->get_first_node_in_group(Atmosphere::GROUP)) : nullptr;
    if (atmosphere && player) {
        text += String(U"    ") + atmosphere->hud_text(player->get_global_position());
    }
    return text;
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
                prompt = String(U"左键拿 / 放 · 右键拿一半 · Shift+左键搬过去 · 点配方合成 · Tab 合上");
                break;
            }
            const bool aboard = ship && ship->is_aboard(at);
            if (!aboard && !freight.is_holding() && place.engaged()) {
                prompt = place.prompt(pack); // 看着木箱 / 工作台、手上拿着能摆的：先说这个
                break;
            }
            const String cargo = freight.prompt(map, pack);
            if (!cargo.is_empty()) {
                prompt = cargo; // 村仓 / 货位前、抱着货箱：先说这个
                break;
            }
            if (near_helm || bench) {
                break;
            }
            const String placing = freight.is_holding() ? String() : place.prompt(pack);
            if (!placing.is_empty()) {
                prompt = placing; // 看着摆出来的东西、手上拿着能摆的：摆放的事
                break;
            }
            const String gathering = gather.prompt(pack);
            if (!gathering.is_empty()) {
                prompt = gathering; // 采掘面前：采集的事
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
            // 风大、禁航：掌舵时多一行（船往下风漂由 Ship 按同一份风算）
            if (Atmosphere *atmosphere = Object::cast_to<Atmosphere>(get_tree()->get_first_node_in_group(Atmosphere::GROUP)); atmosphere && ship) {
                const String sailing = atmosphere->sailing_text(ship->get_global_position());
                if (!sailing.is_empty()) {
                    lines.push_back(String(U"风：") + sailing);
                }
            }
            prompt = helm.piloting_prompt(berths());
            break;
        case Helm::MOORING:
            lines.clear();
            lines.push_back(helm.is_mooring_to_yard() ? String(U"入坞中")
                            : helm.get_moor_berth().wild     ? String(U"野泊中")
                            : helm.get_moor_berth().land     ? String(U"落上船台")
                                                             : String(U"靠泊中"));
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
    pack.refresh_bar(); // 采集、收获、摆放都会动快捷栏
}
