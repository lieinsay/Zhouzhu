#include "voyage.h"

#include <cmath>
#include <initializer_list>

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "input_actions.h"

using namespace godot;

namespace {

constexpr double RECALL_DISTANCE = 60.0; // 坠落回位时，船离得比这远就把它召回最近的码头
constexpr double NOTICE_SECONDS = 2.5;

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

} // namespace

void Voyage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_state"), &Voyage::get_state);
    ClassDB::bind_method(D_METHOD("has_arrived"), &Voyage::has_arrived);

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

    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "origin_island", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Island"), "set_origin_island_path", "get_origin_island_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "destination_island", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Island"), "set_destination_island_path", "get_destination_island_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "ship", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Ship"), "set_ship_path", "get_ship_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Player"), "set_player_path", "get_player_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helm_range", PROPERTY_HINT_RANGE, "0.5,10,0.1,suffix:m"), "set_helm_range", "get_helm_range");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_range", PROPERTY_HINT_RANGE, "5,200,1,suffix:m"), "set_dock_range", "get_dock_range");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "leave_helm_speed", PROPERTY_HINT_RANGE, "0,30,0.5,suffix:m/s"), "set_leave_helm_speed", "get_leave_helm_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "kill_altitude", PROPERTY_HINT_RANGE, "-500,500,1,suffix:m"), "set_kill_altitude", "get_kill_altitude");

    ADD_SIGNAL(MethodInfo("arrived"));

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

    // 子节点先于父节点 _ready，岛此刻已经生成完，泊位与出生点都有了
    ship->place_at(origin->get_berth_transform(ship->get_half_beam()));
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
}

void Voyage::_unhandled_input(const Ref<InputEvent> &p_event) {
    Input *input = Input::get_singleton();
    if (p_event->is_action_pressed("ui_cancel")) {
        input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
        return;
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
            if (interact && player->get_global_position().distance_to(ship->get_helm_position()) <= helm_range) {
                board();
                break;
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
            Transform3D berth;
            if (island_in_dock_range(berth)) {
                ship->moor_at(berth);
                ship->set_controlled(false);
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
    player->set_velocity(Vector3());
    player->set_global_position(target + Vector3(0, 0.5f, 0));
    // 人回来了船却漂在远处，这一局就死了：把船召回最近的码头
    if (ship->get_global_position().distance_to(target) > RECALL_DISTANCE) {
        ship->place_at(nearest_island(target)->get_berth_transform(ship->get_half_beam()));
    }
    notice = String(U"坠入云海，回到了上一个落脚点");
    notice_time = NOTICE_SECONDS;
}

Island *Voyage::island_in_dock_range(Transform3D &r_berth) const {
    Island *best = nullptr;
    double best_distance = dock_range;
    for (Island *island : { origin, destination }) {
        const Transform3D berth = island->get_berth_transform(ship->get_half_beam());
        const double distance = ship->get_global_position().distance_to(berth.origin);
        if (distance <= best_distance) {
            best = island;
            best_distance = distance;
            r_berth = berth;
        }
    }
    return best;
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
    const Transform3D target_berth = target->get_berth_transform(ship->get_half_beam());

    switch (state) {
        case ON_FOOT: {
            status = arrived ? String(U"已踏上对岸的岛") : String(U"目标：登上空艇，飞到对岸的岛上去");
            const bool near_helm = player->get_global_position().distance_to(ship->get_helm_position()) <= helm_range;
            prompt = near_helm ? String(U"按 E 掌舵") : String(U"WASD 移动 · 空格 跳 · Shift 疾跑");
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

            Transform3D berth;
            if (island_in_dock_range(berth)) {
                prompt = ship_position.distance_to(berth.origin) < 0.5 ? String(U"按 E 离舵") : String(U"按 E 靠泊");
            } else if (ship->get_speed() <= leave_helm_speed) {
                prompt = String(U"W/S 进退 · A/D 转舵 · 空格 升 · Shift 降 · E 离舵");
            } else {
                prompt = String(U"W/S 进退 · A/D 转舵 · 空格 升 · Shift 降");
            }
        } break;

        case MOORING: {
            status = String(U"靠泊中");
            prompt = String(U"……");
        } break;
    }
    if (notice_time > 0.0) {
        prompt = notice;
    }
    status_label->set_text(status);
    prompt_label->set_text(prompt);
}
