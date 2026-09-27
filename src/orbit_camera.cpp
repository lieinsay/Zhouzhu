#include "orbit_camera.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "input_actions.h"
#include "map_host.h"
#include "planet_view.h"

using namespace godot;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;
constexpr double SPRINT = 4.0;
constexpr double WHEEL_RATE = 6.0; // 滚轮设的高度按这个速率（1/s，对数里）逼近
constexpr double MAX_HEIGHT = 1.0e8; // 十万公里

Vector3 v3(const PlanetVec &p_v) {
    return Vector3(real_t(p_v.x), real_t(p_v.y), real_t(p_v.z));
}

} // namespace

void OrbitCamera::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_orbit_state", "lat", "lon", "height", "heading", "pitch"), &OrbitCamera::set_orbit_state);
    ClassDB::bind_method(D_METHOD("set_autopilot", "value"), &OrbitCamera::set_autopilot);
    ClassDB::bind_method(D_METHOD("is_autopilot"), &OrbitCamera::is_autopilot);
    ClassDB::bind_method(D_METHOD("get_latlon"), &OrbitCamera::get_latlon);
    ClassDB::bind_method(D_METHOD("get_height"), &OrbitCamera::get_height);
    ClassDB::bind_method(D_METHOD("get_clearance"), &OrbitCamera::get_clearance);
    ClassDB::bind_method(D_METHOD("get_heading"), &OrbitCamera::get_heading);
    ClassDB::bind_method(D_METHOD("get_pitch"), &OrbitCamera::get_pitch);
    ClassDB::bind_method(D_METHOD("get_move_speed"), &OrbitCamera::get_move_speed);
    ClassDB::bind_method(D_METHOD("wheel", "notches"), &OrbitCamera::wheel);
    ADD_SIGNAL(MethodInfo("leave"));
}

void OrbitCamera::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        set_process_input(false);
        return;
    }
    set_process_priority(90); // PlanetView（100）之前摆好
}

PlanetView *OrbitCamera::view() const {
    return Object::cast_to<PlanetView>(ObjectDB::get_instance(view_id));
}

double OrbitCamera::ground_at(const PlanetVec &p_flat) const {
    const PlanetView *pv = view();
    const double g = pv && std::hypot(p_flat.x, p_flat.z) < 400000.0 ? pv->surface_at(p_flat) : NAN;
    return std::isfinite(g) ? g : VerticalDatum::CLOUD_TOP_Y + 10.0;
}

void OrbitCamera::begin_from(Camera3D *p_from, PlanetView *p_view) {
    view_id = p_view ? p_view->get_instance_id() : ObjectID();
    if (!p_view || !p_view->is_active()) {
        return;
    }
    const GroupFrame &anchor = p_view->get_anchor();
    Transform3D xf = p_from ? p_from->get_global_transform() : Transform3D();
    PlanetVec start(xf.origin.x, xf.origin.y, xf.origin.z);
    if (const OrbitCamera *o = Object::cast_to<OrbitCamera>(p_from)) {
        start = o->get_true_flat();
    }
    planet_latlon(anchor.to_planet(start), lat, lon);
    height = start.y;
    const Vector3 f = -xf.basis.get_column(2).normalized(); // 相机朝 −z；世界轴就是相机处的东、上、南
    heading = std::atan2(double(f.x), -double(f.z)) / DEG;
    pitch = std::asin(std::clamp(double(f.y), -1.0, 1.0)) / DEG;
    if (p_from) {
        set_fov(p_from->get_fov());
        set_near(p_from->get_near());
        set_far(p_from->get_far());
        set_cull_mask(p_from->get_cull_mask());
    }
    has_state = true;
    apply();
}

void OrbitCamera::set_orbit_state(double p_lat, double p_lon, double p_height, double p_heading, double p_pitch) {
    if (!view()) {
        view_id = PlanetView::active_view() ? PlanetView::active_view()->get_instance_id() : ObjectID();
    }
    lat = std::clamp(p_lat, -89.9, 89.9);
    lon = p_lon;
    height = p_height;
    heading = p_heading;
    pitch = std::clamp(p_pitch, -90.0, 90.0);
    wheel_clearance = -1.0;
    autopilot = true;
    has_state = true;
    apply();
}

double OrbitCamera::get_move_speed() const {
    return move_rate * (std::max(0.0, height - ground) + 50.0);
}

void OrbitCamera::wheel(int p_notches) {
    const double current = wheel_clearance > 0.0 ? wheel_clearance : std::max(min_clearance, height - ground);
    wheel_clearance = std::clamp(current * std::pow(wheel_step, double(p_notches)), min_clearance, MAX_HEIGHT);
}

void OrbitCamera::_input(const Ref<InputEvent> &p_event) {
    if (!is_current() || autopilot) {
        return;
    }
    if (p_event->is_action_pressed("ui_cancel") || p_event->is_action_pressed(input_actions::ORBIT_CAMERA)) {
        get_viewport()->set_input_as_handled();
        emit_signal("leave");
        return;
    }
    const Ref<InputEventMouseButton> button = p_event;
    if (button.is_valid() && button->is_pressed() && (button->get_button_index() == MOUSE_BUTTON_WHEEL_UP || button->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN)) {
        wheel(button->get_button_index() == MOUSE_BUTTON_WHEEL_UP ? 1 : -1);
        get_viewport()->set_input_as_handled();
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid() && Input::get_singleton()->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED) {
        heading += motion->get_relative().x * look_speed;
        pitch = std::clamp(pitch - motion->get_relative().y * look_speed, -90.0, 89.0);
        get_viewport()->set_input_as_handled();
    }
}

void OrbitCamera::_process(double p_delta) {
    if (!has_state) {
        return;
    }
    if (!autopilot && is_current()) {
        Input *input = Input::get_singleton();
        const double boost = input->is_action_pressed(input_actions::SPRINT) ? SPRINT : 1.0;
        // 方向键转视角（没有鼠标时也能用）
        const double turn = 60.0 * p_delta;
        if (input->is_key_pressed(KEY_LEFT)) {
            heading -= turn;
        }
        if (input->is_key_pressed(KEY_RIGHT)) {
            heading += turn;
        }
        if (input->is_key_pressed(KEY_UP)) {
            pitch = std::min(89.0, pitch + turn);
        }
        if (input->is_key_pressed(KEY_DOWN)) {
            pitch = std::max(-90.0, pitch - turn);
        }
        // 平飞：沿大圆，按航向转过的前后左右
        const Vector2 stick = input->get_vector(input_actions::MOVE_LEFT, input_actions::MOVE_RIGHT, input_actions::MOVE_FORWARD, input_actions::MOVE_BACK);
        if (stick.length_squared() > 1e-6) {
            const double dist = get_move_speed() * boost * p_delta * std::min(1.0, double(stick.length()));
            const double bearing = heading + std::atan2(double(stick.x), -double(stick.y)) / DEG;
            const PlanetView *pv = view();
            const double R = pv ? pv->get_radius() : 6376600.0;
            const double d = dist / R, b = bearing * DEG, f1 = lat * DEG, l1 = lon * DEG;
            const double f2 = std::asin(std::sin(f1) * std::cos(d) + std::cos(f1) * std::sin(d) * std::cos(b));
            const double l2 = l1 + std::atan2(std::sin(b) * std::sin(d) * std::cos(f1), std::cos(d) - std::sin(f1) * std::sin(f2));
            lat = std::clamp(f2 / DEG, -89.9, 89.9);
            lon = std::remainder(l2 / DEG, 360.0);
        }
        // 升降：高度按离地多高成倍地变
        const double vertical = input->get_axis(input_actions::FLY_DOWN, input_actions::ASCEND);
        if (std::abs(vertical) > 1e-3) {
            wheel_clearance = -1.0;
            const double clearance = std::max(min_clearance, height - ground) + 20.0;
            height += clearance * (std::exp(vertical * climb_rate * boost * p_delta) - 1.0);
        }
    }
    if (wheel_clearance > 0.0) {
        const double current = std::max(min_clearance, height - ground);
        const double k = 1.0 - std::exp(-WHEEL_RATE * p_delta);
        const double next = std::exp(std::log(current) + (std::log(wheel_clearance) - std::log(current)) * k);
        height = ground + next;
        if (std::abs(std::log(wheel_clearance / next)) < 1e-3) {
            wheel_clearance = -1.0;
        }
    }
    apply();
}

void OrbitCamera::apply() {
    const PlanetView *pv = view();
    if (!pv || !pv->is_active()) {
        return;
    }
    const GroupFrame &anchor = pv->get_anchor();
    PlanetVec dir = planet_unit(lat, lon);
    flat = anchor.to_flat(dir * anchor.radius);
    ground = ground_at(flat);
    height = std::clamp(height, ground + (autopilot ? 0.5 : min_clearance), MAX_HEIGHT);
    flat.y = height;
    const double h = heading * DEG, p = pitch * DEG;
    const Vector3 forward(real_t(std::sin(h) * std::cos(p)), real_t(std::sin(p)), real_t(-std::cos(h) * std::cos(p)));
    const Vector3 up = std::abs(pitch) > 89.0 ? Vector3(real_t(std::sin(h)), 0.0f, real_t(-std::cos(h))) * real_t(pitch < 0.0 ? 1.0 : -1.0) : Vector3(0, 1, 0);
    set_global_transform(Transform3D(Basis::looking_at(forward, up), v3(flat)));
}
