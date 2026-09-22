#include "world_scene.h"

#include <cmath>

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "input_actions.h"

using namespace godot;

namespace {

constexpr double NOTICE_SECONDS = 4.0;
constexpr double CAMERA_FAR_M = 60000.0;
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

} // namespace

void WorldScene::_bind_methods() {
    ClassDB::bind_method(D_METHOD("teleport_to", "position"), &WorldScene::teleport_to);
    ClassDB::bind_method(D_METHOD("is_map_open"), &WorldScene::is_map_open);
    ClassDB::bind_method(D_METHOD("get_spawn_point"), &WorldScene::get_spawn_point);
    ClassDB::bind_method(D_METHOD("get_map"), &WorldScene::get_map);
    ClassDB::bind_method(D_METHOD("on_teleport_requested", "position"), &WorldScene::on_teleport_requested);

    ClassDB::bind_method(D_METHOD("set_world_path", "value"), &WorldScene::set_world_path);
    ClassDB::bind_method(D_METHOD("get_world_path"), &WorldScene::get_world_path);
    ClassDB::bind_method(D_METHOD("set_player_path", "value"), &WorldScene::set_player_path);
    ClassDB::bind_method(D_METHOD("get_player_path"), &WorldScene::get_player_path);
    ClassDB::bind_method(D_METHOD("set_dev_tools", "value"), &WorldScene::set_dev_tools);
    ClassDB::bind_method(D_METHOD("get_dev_tools"), &WorldScene::get_dev_tools);
    ClassDB::bind_method(D_METHOD("set_kill_altitude", "value"), &WorldScene::set_kill_altitude);
    ClassDB::bind_method(D_METHOD("get_kill_altitude"), &WorldScene::get_kill_altitude);

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "World"), "set_world_path", "get_world_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Player"), "set_player_path", "get_player_path");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dev_tools"), "set_dev_tools", "get_dev_tools");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "kill_altitude", PROPERTY_HINT_RANGE, "-500,500,1,suffix:m"), "set_kill_altitude", "get_kill_altitude");
}

void WorldScene::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    input_actions::register_all();
    world = Object::cast_to<World>(get_node_or_null(world_path));
    player = Object::cast_to<Player>(get_node_or_null(player_path));
    // 子节点先于父节点 _ready：World 此刻已经读完数据
    if (!world || !player || !world->is_loaded()) {
        ERR_PRINT("WorldScene: world / player 没接上，或 World 没读到数据");
        set_process(false);
        set_physics_process(false);
        return;
    }
    build_hud();
    if (player->get_camera()) {
        player->get_camera()->set_far(real_t(CAMERA_FAR_M)); // 岛群近百公里宽，远处的岛也要看得见
    }

    // 出生在邑治；没有邑治就在群心附近找地
    const Array villages = world->get_info().get("villages", Array());
    for (int64_t k = 0; k < villages.size(); k++) {
        const Dictionary row = villages[k];
        if (bool(row.get("seat", false))) {
            const Array pos = row.get("pos", Array());
            spawn_point = Vector3(real_t(double(pos[0])), 0, real_t(double(pos[1])));
            break;
        }
    }
    if (!teleport_to(spawn_point)) {
        ERR_PRINT("WorldScene: 出生点附近没有陆地");
    }
    spawn_point = player->get_global_position();
    Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
}

void WorldScene::build_hud() {
    CanvasLayer *layer = memnew(CanvasLayer);
    layer->set_name("Hud");
    add_child(layer);

    status_label = make_label(20);
    status_label->set_position(Vector2(24, 18));
    layer->add_child(status_label);

    notice_label = make_label(26);
    notice_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    notice_label->set_anchors_preset(Control::PRESET_CENTER_BOTTOM);
    notice_label->set_h_grow_direction(Control::GROW_DIRECTION_BOTH);
    notice_label->set_v_grow_direction(Control::GROW_DIRECTION_BEGIN);
    notice_label->set_offset(SIDE_BOTTOM, -56);
    notice_label->set_offset(SIDE_TOP, -56);
    layer->add_child(notice_label);

    map = memnew(WorldMap);
    map->set_name("Map");
    map->set_anchors_preset(Control::PRESET_FULL_RECT);
    map->set_visible(false);
    layer->add_child(map);
    map->setup(world->get_grid(), world->get_info());
    map->set_teleport_enabled(dev_tools);
    map->connect("teleport_requested", Callable(this, "on_teleport_requested"));
}

void WorldScene::set_dev_tools(bool p_value) {
    dev_tools = p_value;
    if (map) {
        map->set_teleport_enabled(dev_tools);
    }
}

void WorldScene::show_notice(const String &p_text) {
    if (notice_label) {
        notice_label->set_text(p_text);
    }
    notice_time = NOTICE_SECONDS;
}

void WorldScene::set_map_open(bool p_open) {
    if (map_open == p_open) {
        return;
    }
    map_open = p_open;
    map->set_visible(map_open);
    status_label->set_visible(!map_open); // 地图自己有标题，底下那行别透上来
    player->set_input_blocked(map_open);
    Input::get_singleton()->set_mouse_mode(map_open ? Input::MOUSE_MODE_VISIBLE : Input::MOUSE_MODE_CAPTURED);
    if (map_open) {
        const Vector3 at = player->get_global_position();
        map->look_at_world(at.x, at.z, map_viewed ? 0.0 : MAP_FIRST_VIEW_M); // 之后再开沿用上次的缩放
        map_viewed = true;
        map->set_player(at, player->get_look_yaw());
    }
}

bool WorldScene::nearest_land(double p_x, double p_z, Vector3 &r_point) const {
    double y = world->get_surface_height(p_x, p_z);
    if (std::isfinite(y)) {
        r_point = Vector3(real_t(p_x), real_t(y), real_t(p_z));
        return true;
    }
    for (double r = LAND_RING_STEP_M; r <= land_search_m; r += LAND_RING_STEP_M) {
        const int count = std::max(12, int(Math_TAU * r / LAND_RING_STEP_M));
        for (int k = 0; k < count; k++) {
            const double a = Math_TAU * k / count;
            const double dx = std::cos(a), dz = std::sin(a);
            y = world->get_surface_height(p_x + r * dx, p_z + r * dz);
            if (!std::isfinite(y)) {
                continue;
            }
            // 再往里挪一点，别正落在崖边上
            const double in_x = p_x + (r + INLAND_NUDGE_M) * dx, in_z = p_z + (r + INLAND_NUDGE_M) * dz;
            const double in_y = world->get_surface_height(in_x, in_z);
            if (std::isfinite(in_y)) {
                r_point = Vector3(real_t(in_x), real_t(in_y), real_t(in_z));
            } else {
                r_point = Vector3(real_t(p_x + r * dx), real_t(y), real_t(p_z + r * dz));
            }
            return true;
        }
    }
    return false;
}

bool WorldScene::teleport_to(const Vector3 &p_position) {
    Vector3 target;
    if (!world || !player || !nearest_land(p_position.x, p_position.z, target)) {
        return false;
    }
    world->prepare_at(target);
    player->set_global_position(target + Vector3(0, 0.3f, 0));
    player->set_velocity(Vector3());
    safe_footing = target;
    has_safe_footing = true;
    return true;
}

void WorldScene::on_teleport_requested(const Vector3 &p_position) {
    if (!dev_tools) {
        return;
    }
    if (!teleport_to(p_position)) {
        show_notice(vformat(String(U"那里是云海，%d km 内没有能落脚的地"), int(land_search_m / 1000.0)));
        return;
    }
    const Vector3 at = player->get_global_position();
    show_notice(vformat(String(U"瞬移到岛 #%d（东 %s km，南 %s km）"), world->get_island_at(at.x, at.z), String::num(at.x / 1000.0, 2), String::num(at.z / 1000.0, 2)));
    set_map_open(false);
}

void WorldScene::_unhandled_input(const Ref<InputEvent> &p_event) {
    Input *input = Input::get_singleton();
    if (p_event->is_action_pressed(input_actions::MAP)) {
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
    const Ref<InputEventMouseButton> click = p_event;
    if (!map_open && click.is_valid() && click->is_pressed() && input->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        input->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

void WorldScene::_physics_process(double /* p_delta */) {
    const Vector3 at = player->get_global_position();
    if (at.y < kill_altitude) {
        teleport_to(has_safe_footing ? safe_footing : spawn_point);
        show_notice(String(U"坠入云海……回到上一处站稳的地方"));
        return;
    }
    if (player->is_on_floor() && world->is_ready_at(at)) {
        safe_footing = at;
        has_safe_footing = true;
    }
}

void WorldScene::_process(double p_delta) {
    const Vector3 at = player->get_global_position();
    world->set_focus(at);
    if (map_open) {
        map->set_player(at, player->get_look_yaw());
    }
    if (notice_time > 0.0) {
        notice_time -= p_delta;
        if (notice_time <= 0.0) {
            notice_label->set_text(String());
        }
    }
    update_hud();
}

void WorldScene::update_hud() {
    const Vector3 at = player->get_global_position();
    const int pending = world->get_pending_count();
    String line = vformat(String(U"岛 #%d · 东 %s km  南 %s km · 高 %d m · M 地图"), world->get_island_at(at.x, at.z),
            String::num(at.x / 1000.0, 2), String::num(at.z / 1000.0, 2), int(std::lround(at.y)));
    if (pending > 0) {
        line += vformat(String(U" · 地面加载中 %d"), pending);
    }
    status_label->set_text(line);
}
