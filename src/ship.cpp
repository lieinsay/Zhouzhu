#include "ship.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/prism_mesh.hpp>
#include <godot_cpp/classes/torus_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "input_actions.h"

using namespace godot;

namespace {

constexpr double MOOR_MAX_SPEED = 9.0;
constexpr double MOOR_GAIN = 1.2; // 靠泊速度 = 距离 × 此值 + 下限，越近越慢
constexpr double MOOR_MIN_SPEED = 0.6;
constexpr double MOOR_YAW_RATE = 0.7;
constexpr double BANK_PER_YAW_RATE = 0.3; // 侧倾角 / 偏航角速度
constexpr double TRIM_PER_CLIMB = 0.012; // 纵倾角 / 垂直速度
constexpr double CAMERA_PITCH_MIN = -1.2;
constexpr double CAMERA_PITCH_MAX = 0.5;

const char *GENERATED_META = "ship_generated";

// 几何（米）。每节长 SECTION_LENGTH_M，下面的尺寸都以一节为单位
constexpr real_t DECK_W = 5.0f, DECK_T = 0.6f; // 甲板面在 y = 0
constexpr real_t HULL_W = 3.6f, HULL_H = 2.0f; // 船体吊在甲板下
constexpr real_t RAIL_T = 0.15f, RAIL_H = 1.0f;
constexpr real_t GATE_HALF = 0.9f; // 舷门半宽：z ∈ [−0.9, 0.9] 两舷不设栏
constexpr real_t HELM_AFT = 1.2f; // 舵柱在驾驶区中心之后多少
constexpr real_t SEAT_AFT = 2.1f;

// 从 p_from 转到 p_to 的最短有向角（godot-cpp 的 Math 里没有 angle_difference）
double angle_difference(double p_from, double p_to) {
    const double diff = std::fmod(p_to - p_from, Math_TAU);
    return std::fmod(2.0 * diff, Math_TAU) - diff;
}

Ref<StandardMaterial3D> make_material(const Color &p_albedo, float p_roughness) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_albedo);
    material->set_roughness(p_roughness);
    return material;
}

void adopt(Node *p_parent, Node *p_node, const String &p_name) {
    p_node->set_name(p_name);
    p_node->set_meta(GENERATED_META, true);
    p_parent->add_child(p_node);
}

// 一只盒子：样子挂在 Visual 下，碰撞（可选）挂在船根下
void add_box(Node3D *p_visual, Node *p_body, const String &p_name, const Vector3 &p_size, const Transform3D &p_xf, const Ref<StandardMaterial3D> &p_material, bool p_collide) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(p_size);
    mesh->set_material(p_material);
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_mesh(mesh);
    instance->set_transform(p_xf);
    adopt(p_visual, instance, p_name);
    if (!p_collide) {
        return;
    }
    Ref<BoxShape3D> shape;
    shape.instantiate();
    shape->set_size(p_size);
    CollisionShape3D *collider = memnew(CollisionShape3D);
    collider->set_shape(shape);
    collider->set_transform(p_xf);
    adopt(p_body, collider, p_name + String("Shape"));
}

void add_mesh(Node3D *p_visual, const String &p_name, const Ref<Mesh> &p_mesh, const Transform3D &p_xf) {
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_mesh(p_mesh);
    instance->set_transform(p_xf);
    adopt(p_visual, instance, p_name);
}

Transform3D at(real_t p_x, real_t p_y, real_t p_z) {
    return Transform3D(Basis(), Vector3(p_x, p_y, p_z));
}

} // namespace

void Ship::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_controlled", "controlled"), &Ship::set_controlled);
    ClassDB::bind_method(D_METHOD("is_controlled"), &Ship::is_controlled);
    ClassDB::bind_method(D_METHOD("place_at", "transform"), &Ship::place_at);
    ClassDB::bind_method(D_METHOD("moor_at", "berth"), &Ship::moor_at);
    ClassDB::bind_method(D_METHOD("is_mooring"), &Ship::is_mooring);
    ClassDB::bind_method(D_METHOD("get_speed"), &Ship::get_speed);
    ClassDB::bind_method(D_METHOD("get_helm_position"), &Ship::get_helm_position);
    ClassDB::bind_method(D_METHOD("get_seat_transform"), &Ship::get_seat_transform);
    ClassDB::bind_method(D_METHOD("get_exit_transform"), &Ship::get_exit_transform);
    ClassDB::bind_method(D_METHOD("get_camera"), &Ship::get_camera);
    ClassDB::bind_method(D_METHOD("get_stats"), &Ship::get_stats);
    ClassDB::bind_method(D_METHOD("can_fly"), &Ship::can_fly);
    ClassDB::bind_method(D_METHOD("get_section_count"), &Ship::get_section_count);
    ClassDB::bind_method(D_METHOD("get_section_id", "index"), &Ship::get_section_id);
    ClassDB::bind_method(D_METHOD("get_bow_z"), &Ship::get_bow_z);
    ClassDB::bind_method(D_METHOD("get_stern_z"), &Ship::get_stern_z);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &Ship::get_max_speed);

    ClassDB::bind_method(D_METHOD("set_layout", "ids"), &Ship::set_layout);
    ClassDB::bind_method(D_METHOD("get_layout"), &Ship::get_layout);
    ClassDB::bind_method(D_METHOD("set_half_beam", "value"), &Ship::set_half_beam);
    ClassDB::bind_method(D_METHOD("get_half_beam"), &Ship::get_half_beam);
    ClassDB::bind_method(D_METHOD("set_min_altitude", "value"), &Ship::set_min_altitude);
    ClassDB::bind_method(D_METHOD("get_min_altitude"), &Ship::get_min_altitude);

    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "layout"), "set_layout", "get_layout");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "half_beam", PROPERTY_HINT_RANGE, "0.5,20,0.05,suffix:m"), "set_half_beam", "get_half_beam");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_altitude", PROPERTY_HINT_RANGE, "-100,1000,1,suffix:m"), "set_min_altitude", "get_min_altitude");

    ADD_SIGNAL(MethodInfo("moored"));
    ADD_SIGNAL(MethodInfo("refitted"));
}

void Ship::_ready() {
    visual = get_node<Node3D>("Visual");
    camera_rig = get_node<Node3D>("CameraRig");
    camera = get_node<Camera3D>("CameraRig/Camera3D");
    ERR_FAIL_COND_MSG(!visual || !camera_rig || !camera, "Ship: 场景里缺 Visual / CameraRig/Camera3D");
    rebuild(); // 编辑器里也拼，摆场景时看得见船
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    state.yaw = get_rotation().y;
}

void Ship::ensure_materials() {
    if (mat_wood.is_valid()) {
        return;
    }
    mat_wood = make_material(Color(0.52f, 0.37f, 0.23f), 0.95f);
    mat_hull = make_material(Color(0.33f, 0.22f, 0.15f), 0.9f);
    mat_rail = make_material(Color(0.4f, 0.28f, 0.18f), 0.9f);
    mat_sail = make_material(Color(0.9f, 0.86f, 0.74f), 1.0f);
    mat_sail->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    mat_stone = make_material(Color(0.25f, 0.62f, 0.6f), 0.6f);
    mat_stone->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
    mat_stone->set_emission(Color(0.2f, 0.75f, 0.7f));
    mat_stone->set_emission_energy_multiplier(1.4f);
    mat_crate = make_material(Color(0.58f, 0.44f, 0.26f), 0.95f);
    mat_roof = make_material(Color(0.27f, 0.19f, 0.13f), 0.9f);
}

void Ship::clear_generated() {
    for (Node *parent : { static_cast<Node *>(this), static_cast<Node *>(visual) }) {
        if (!parent) {
            continue;
        }
        const TypedArray<Node> children = parent->get_children();
        for (int64_t k = 0; k < children.size(); k++) {
            Node *child = Object::cast_to<Node>(children[k]);
            if (child && child->has_meta(GENERATED_META)) {
                parent->remove_child(child);
                child->queue_free();
            }
        }
    }
}

// 一节：甲板、船体、两舷的栏（舷门处断开），再按模块摆东西
void Ship::build_section(int p_index) {
    const real_t L = real_t(SECTION_LENGTH_M);
    const real_t z0 = real_t(section_front_z(layout, p_index)), z1 = z0 + L, zc = z0 + 0.5f * L;
    const ModuleKind kind = layout[size_t(p_index)];
    const String prefix = vformat("S%d_", p_index);

    add_box(visual, this, prefix + String("Deck"), Vector3(DECK_W, DECK_T, L), at(0, -0.5f * DECK_T, zc), mat_wood, true);
    add_box(visual, this, prefix + String("Hull"), Vector3(HULL_W, HULL_H, L), at(0, -DECK_T - 0.5f * HULL_H, zc), mat_hull, true);

    // 舷栏：这一节的 [z0, z1] 去掉舷门 [−GATE_HALF, GATE_HALF]，最多剩两段
    const real_t pieces[2][2] = { { z0, std::min(z1, -GATE_HALF) }, { std::max(z0, GATE_HALF), z1 } };
    for (int side = -1; side <= 1; side += 2) {
        for (int k = 0; k < 2; k++) {
            const real_t a = pieces[k][0], b = pieces[k][1];
            if (b - a < 0.05f) {
                continue;
            }
            const String name = prefix + String(side < 0 ? "RailP" : "RailS") + String::num_int64(k);
            add_box(visual, this, name, Vector3(RAIL_T, RAIL_H, b - a), at(side * (0.5f * DECK_W - 0.5f * RAIL_T), 0.5f * RAIL_H, 0.5f * (a + b)), mat_rail, true);
        }
    }

    switch (kind) {
        case ModuleKind::BRIDGE: {
            // 舵柱与舵轮在这一节的后半；舵楼：四根柱撑一片顶
            const real_t zh = zc + HELM_AFT;
            add_box(visual, this, prefix + String("HelmPost"), Vector3(0.4f, 1.0f, 0.4f), at(0, 0.5f, zh), mat_hull, true);
            Ref<TorusMesh> wheel;
            wheel.instantiate();
            wheel->set_inner_radius(0.3f);
            wheel->set_outer_radius(0.4f);
            wheel->set_material(mat_rail);
            add_mesh(visual, prefix + String("Wheel"), wheel, Transform3D(Basis(Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)), Vector3(0, 1.15f, zh + 0.22f)));
            for (int side = -1; side <= 1; side += 2) {
                for (int end = -1; end <= 1; end += 2) {
                    add_box(visual, this, prefix + vformat("Post%d%d", side, end), Vector3(0.12f, 2.4f, 0.12f), at(side * 2.0f, 1.2f, zc + (end < 0 ? -1.4f : 2.2f)), mat_hull, false);
                }
            }
            add_box(visual, this, prefix + String("Roof"), Vector3(DECK_W, 0.12f, 4.2f), at(0, 2.46f, zc + 0.4f), mat_roof, false);
            helm_local = Vector3(0, 1.0f, zh);
            seat_local = Vector3(0, 0, zc + SEAT_AFT);
        } break;

        case ModuleKind::STORAGE: {
            // 两侧各一摞货箱，中间留 1.6 m 走道
            for (int side = -1; side <= 1; side += 2) {
                const real_t x = side * 1.55f;
                add_box(visual, this, prefix + vformat("Crate%dA", side), Vector3(1.4f, 0.9f, 3.0f), at(x, 0.45f, zc), mat_crate, true);
                add_box(visual, this, prefix + vformat("Crate%dB", side), Vector3(1.0f, 0.6f, 2.0f), at(x, 1.2f, zc - 0.3f), mat_crate, true);
            }
        } break;

        case ModuleKind::POWER: {
            // 船底吊一块浮石；甲板上一对侧桅，各挂一面帆
            add_box(visual, this, prefix + String("Floatstone"), Vector3(1.6f, 1.2f, 4.0f), at(0, -DECK_T - HULL_H - 0.6f, zc), mat_stone, false);
            Ref<CylinderMesh> mast;
            mast.instantiate();
            mast->set_top_radius(0.09f);
            mast->set_bottom_radius(0.13f);
            mast->set_height(5.2f);
            mast->set_material(mat_hull);
            Ref<CylinderShape3D> mast_shape;
            mast_shape.instantiate();
            mast_shape->set_radius(0.15f);
            mast_shape->set_height(5.2f);
            for (int side = -1; side <= 1; side += 2) {
                const real_t x = side * 1.9f;
                add_mesh(visual, prefix + vformat("Mast%d", side), mast, at(x, 2.6f, zc));
                CollisionShape3D *collider = memnew(CollisionShape3D);
                collider->set_shape(mast_shape);
                collider->set_transform(at(x, 2.6f, zc));
                adopt(this, collider, prefix + vformat("Mast%dShape", side));
                add_box(visual, this, prefix + vformat("Boom%d", side), Vector3(0.14f, 0.14f, 3.4f), at(x, 1.5f, zc), mat_rail, false);
                add_box(visual, this, prefix + vformat("Sail%d", side), Vector3(0.06f, 3.3f, 3.0f), at(x, 3.25f, zc), mat_sail, false);
            }
        } break;

        case ModuleKind::COUNT:
            break;
    }
}

void Ship::rebuild() {
    ERR_FAIL_NULL(visual);
    ensure_materials();
    clear_generated();
    stats = layout_stats(layout);
    params.max_speed = stats.max_speed;
    params.reverse_speed = 0.25 * stats.max_speed;
    params.max_climb = stats.max_climb;
    params.max_yaw_rate = stats.max_yaw_rate;
    helm_local = Vector3(0, 1, 0);
    seat_local = Vector3(0, 0, 0);
    exit_local = Vector3(0, 0, 0); // 出口就是舷门

    for (int k = 0; k < int(layout.size()); k++) {
        build_section(k);
    }

    // 船头楔与首尾栏
    const real_t zf = real_t(layout_bow_z(layout)), zs = real_t(layout_stern_z(layout));
    const Basis lay_flat(Vector3(1, 0, 0), Vector3(0, 0, 1), Vector3(0, -1, 0)); // 三棱柱躺倒：高 → z
    Ref<PrismMesh> bow;
    bow.instantiate();
    bow->set_size(Vector3(DECK_W, 3.2f, DECK_T));
    bow->set_material(mat_wood);
    add_mesh(visual, "Bow", bow, Transform3D(lay_flat, Vector3(0, -0.5f * DECK_T, zf - 1.6f)));
    Ref<PrismMesh> bow_hull;
    bow_hull.instantiate();
    bow_hull->set_size(Vector3(HULL_W, 3.2f, HULL_H));
    bow_hull->set_material(mat_hull);
    add_mesh(visual, "BowHull", bow_hull, Transform3D(lay_flat, Vector3(0, -DECK_T - 0.5f * HULL_H, zf - 0.1f)));
    add_box(visual, this, "RailBow", Vector3(DECK_W, RAIL_H, RAIL_T), at(0, 0.5f * RAIL_H, zf + 0.5f * RAIL_T), mat_rail, true);
    add_box(visual, this, "RailStern", Vector3(DECK_W, RAIL_H, RAIL_T), at(0, 0.5f * RAIL_H, zs - 0.5f * RAIL_T), mat_rail, true);

    // 追尾相机退到船尾之后：船越长，退得越远
    if (camera) {
        camera->set_position(Vector3(0, 0, std::max(20.0f, zs + 13.0f)));
    }

    emit_signal("refitted");
}

void Ship::set_layout(const PackedStringArray &p_ids) {
    std::vector<std::string> ids;
    for (int64_t k = 0; k < p_ids.size(); k++) {
        ids.emplace_back(String(p_ids[k]).utf8().get_data());
    }
    bool ok = false;
    const ShipLayout next = layout_from_ids(ids, &ok);
    if (!ok) {
        ERR_PRINT("Ship: layout 里有认不出的模块 id");
    }
    const std::string problem = layout_problem(next);
    if (!problem.empty()) {
        ERR_PRINT(String("Ship: layout 不合法——") + String::utf8(problem.c_str()));
        return;
    }
    layout = next;
    if (visual) {
        rebuild();
    }
}

PackedStringArray Ship::get_layout() const {
    PackedStringArray ids;
    for (const std::string &id : layout_ids(layout)) {
        ids.push_back(String::utf8(id.c_str()));
    }
    return ids;
}

bool Ship::apply_layout(const ShipLayout &p_layout) {
    if (!layout_problem(p_layout).empty()) {
        return false;
    }
    layout = p_layout;
    if (visual) {
        rebuild();
    }
    return true;
}

Dictionary Ship::get_stats() const {
    Dictionary d;
    d["sections"] = stats.sections;
    d["mass"] = stats.mass;
    d["thrust"] = stats.thrust;
    d["hold"] = stats.hold;
    d["max_speed"] = stats.max_speed;
    d["max_climb"] = stats.max_climb;
    d["max_yaw_rate"] = stats.max_yaw_rate;
    d["can_fly"] = stats.can_fly();
    return d;
}

String Ship::get_section_id(int p_index) const {
    if (p_index < 0 || p_index >= int(layout.size())) {
        return String();
    }
    return String(modules::def(layout[size_t(p_index)]).id);
}

void Ship::_physics_process(double p_delta) {
    if (mooring) {
        step_mooring(p_delta);
    } else {
        step_flight(p_delta);
    }
    if (visual) {
        const real_t blend = real_t(1.0 - std::exp(-4.0 * p_delta));
        Vector3 tilt = visual->get_rotation();
        tilt.z += (real_t(state.yaw_rate * BANK_PER_YAW_RATE) - tilt.z) * blend; // 左转（角速度为正）向左倾
        tilt.x += (real_t(state.vy * TRIM_PER_CLIMB) - tilt.x) * blend;
        visual->set_rotation(tilt);
    }
}

void Ship::step_flight(double p_delta) {
    FlightInput in;
    if (controlled) {
        Input *input = Input::get_singleton();
        in.throttle = input->get_axis(input_actions::MOVE_BACK, input_actions::MOVE_FORWARD);
        in.rudder = input->get_axis(input_actions::MOVE_LEFT, input_actions::MOVE_RIGHT);
        in.lift = input->get_axis(input_actions::DESCEND, input_actions::ASCEND);
    }
    state = flight_step(state, in, params, p_delta);

    // 别沉进云海
    const double altitude = get_global_position().y;
    if (altitude < min_altitude) {
        state.vy = std::max(state.vy, 2.0 * (min_altitude - altitude));
    }

    // 没人掌舵又已停稳：彻底不动，甲板上的人脚下才是死的
    if (!controlled && get_speed() < 0.01 && std::abs(state.yaw_rate) < 0.001) {
        state.vx = state.vy = state.vz = state.yaw_rate = 0.0;
        return;
    }

    set_rotation(Vector3(0, real_t(state.yaw), 0));
    set_velocity(Vector3(real_t(state.vx), real_t(state.vy), real_t(state.vz)));
    move_and_slide();
    // 撞上东西后的实际速度写回模型，否则它会顶着岛一直「加速」
    const Vector3 actual = get_velocity();
    state.vx = actual.x;
    state.vy = actual.y;
    state.vz = actual.z;
}

void Ship::step_mooring(double p_delta) {
    Vector3 position = get_global_position();
    const Vector3 to_berth = berth_position - position;
    const double distance = to_berth.length();
    const double step = std::min(MOOR_MAX_SPEED, MOOR_GAIN * distance + MOOR_MIN_SPEED) * p_delta;
    const bool there = distance <= step;
    position = there ? berth_position : position + to_berth * real_t(step / distance);

    const double yaw_error = angle_difference(state.yaw, berth_yaw);
    const double yaw_step = MOOR_YAW_RATE * p_delta;
    const bool aligned = std::abs(yaw_error) <= yaw_step;
    state.yaw = aligned ? berth_yaw : state.yaw + (yaw_error > 0 ? yaw_step : -yaw_step);
    state.yaw_rate = aligned ? 0.0 : (yaw_error > 0 ? MOOR_YAW_RATE : -MOOR_YAW_RATE);

    const Vector3 moved = position - get_global_position();
    state.vx = moved.x / p_delta;
    state.vy = moved.y / p_delta;
    state.vz = moved.z / p_delta;
    set_global_position(position);
    set_rotation(Vector3(0, real_t(state.yaw), 0));

    if (there && aligned) {
        mooring = false;
        state.vx = state.vy = state.vz = state.yaw_rate = 0.0;
        emit_signal("moored");
    }
}

void Ship::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!controlled || !camera_rig || Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        const Vector2 rel = motion->get_relative();
        camera_yaw -= rel.x * mouse_sensitivity;
        camera_pitch = std::clamp(camera_pitch - rel.y * mouse_sensitivity, CAMERA_PITCH_MIN, CAMERA_PITCH_MAX);
        camera_rig->set_rotation(Vector3(real_t(camera_pitch), real_t(camera_yaw), 0));
    }
}

void Ship::set_controlled(bool p_controlled) {
    controlled = p_controlled;
    if (controlled && camera_rig) {
        // 上舵时相机回到正后方
        camera_yaw = 0.0;
        camera_pitch = -0.3;
        camera_rig->set_rotation(Vector3(real_t(camera_pitch), 0, 0));
    }
}

void Ship::place_at(const Transform3D &p_transform) {
    mooring = false;
    state = FlightState();
    state.yaw = p_transform.basis.get_euler().y;
    set_global_position(p_transform.origin);
    set_rotation(Vector3(0, real_t(state.yaw), 0));
    set_velocity(Vector3());
}

void Ship::moor_at(const Transform3D &p_berth) {
    berth_position = p_berth.origin;
    // 两舷都有舷门，船头朝哪边都能靠：取转得少的那个
    const double yaw = p_berth.basis.get_euler().y;
    berth_yaw = std::abs(angle_difference(state.yaw, yaw)) <= Math_PI / 2 ? yaw : yaw + Math_PI;
    mooring = true;
}

double Ship::get_speed() const {
    return std::sqrt(state.vx * state.vx + state.vy * state.vy + state.vz * state.vz);
}

Vector3 Ship::get_helm_position() const {
    return to_global(helm_local);
}

Transform3D Ship::get_seat_transform() const {
    return get_global_transform() * Transform3D(Basis(), seat_local);
}

Transform3D Ship::get_exit_transform() const {
    return get_global_transform() * Transform3D(Basis(), exit_local);
}
