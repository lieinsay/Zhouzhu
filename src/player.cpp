#include "player.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/spring_arm3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "input_actions.h"
#include "planet_frame.h"
#include "planet_view.h"

using namespace godot;

namespace {

constexpr double GROUND_ACCEL = 14.0; // 水平速度逼近目标的速率（1/s）
constexpr double AIR_ACCEL = 3.0;
constexpr double TURN_RATE = 12.0; // 身体转向移动方向的速率
constexpr double GOD_ACCEL = 6.0; // god 模式下速度逼近目标的速率（1/s）
constexpr double GOD_SEA_CLEARANCE_M = 2.0; // 有行星数据时 god 往下飞停在海面之上这么高
constexpr double PITCH_MIN = -1.3;
constexpr double PITCH_MAX = 0.6;
constexpr double ROOF_PROBE = 6.0; // 头顶这么高以内有东西挡着，就算在屋里（屋檐下、舱房里）
constexpr double ARM_RATE = 6.0; // 弹簧臂长度逼近目标的速率（1/s）
constexpr uint32_t SHIP_LAYER_BIT = 2; // 船身的碰撞层（ship.tscn 的 collision_layer）：舱顶也算屋顶

} // namespace

void Player::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_controlled", "controlled"), &Player::set_controlled);
    ClassDB::bind_method(D_METHOD("is_controlled"), &Player::is_controlled);
    ClassDB::bind_method(D_METHOD("set_input_blocked", "blocked"), &Player::set_input_blocked);
    ClassDB::bind_method(D_METHOD("is_input_blocked"), &Player::is_input_blocked);
    ClassDB::bind_method(D_METHOD("set_look_yaw", "yaw"), &Player::set_look_yaw);
    ClassDB::bind_method(D_METHOD("set_look_pitch", "pitch"), &Player::set_look_pitch);
    ClassDB::bind_method(D_METHOD("get_look_pitch"), &Player::get_look_pitch);
    ClassDB::bind_method(D_METHOD("get_look_yaw"), &Player::get_look_yaw);
    ClassDB::bind_method(D_METHOD("set_facing_yaw", "yaw"), &Player::set_facing_yaw);
    ClassDB::bind_method(D_METHOD("get_camera"), &Player::get_camera);
    ClassDB::bind_method(D_METHOD("is_on_ladder"), &Player::is_on_ladder);
    ClassDB::bind_method(D_METHOD("is_indoors"), &Player::is_indoors);
    ClassDB::bind_method(D_METHOD("set_indoor_arm", "value"), &Player::set_indoor_arm);
    ClassDB::bind_method(D_METHOD("get_indoor_arm"), &Player::get_indoor_arm);
    ClassDB::bind_method(D_METHOD("set_god_mode", "enabled"), &Player::set_god_mode);
    ClassDB::bind_method(D_METHOD("is_god_mode"), &Player::is_god_mode);
    ClassDB::bind_method(D_METHOD("set_god_speed", "value"), &Player::set_god_speed);
    ClassDB::bind_method(D_METHOD("get_god_speed"), &Player::get_god_speed);
    ClassDB::bind_method(D_METHOD("set_god_boost", "value"), &Player::set_god_boost);
    ClassDB::bind_method(D_METHOD("get_god_boost"), &Player::get_god_boost);
    ClassDB::bind_method(D_METHOD("set_god_scale_height", "value"), &Player::set_god_scale_height);
    ClassDB::bind_method(D_METHOD("get_god_scale_height"), &Player::get_god_scale_height);
    ClassDB::bind_method(D_METHOD("get_god_scale"), &Player::get_god_scale);
    ClassDB::bind_method(D_METHOD("set_carrying", "carrying", "color"), &Player::set_carrying, DEFVAL(Color(0.86f, 0.82f, 0.7f)));
    ClassDB::bind_method(D_METHOD("is_carrying"), &Player::is_carrying);
    ClassDB::bind_method(D_METHOD("set_carry_speed", "value"), &Player::set_carry_speed);
    ClassDB::bind_method(D_METHOD("get_carry_speed"), &Player::get_carry_speed);
    ClassDB::bind_method(D_METHOD("on_sensor_area_entered", "area"), &Player::on_sensor_area_entered);
    ClassDB::bind_method(D_METHOD("on_sensor_area_exited", "area"), &Player::on_sensor_area_exited);

    ClassDB::bind_method(D_METHOD("set_walk_speed", "value"), &Player::set_walk_speed);
    ClassDB::bind_method(D_METHOD("get_walk_speed"), &Player::get_walk_speed);
    ClassDB::bind_method(D_METHOD("set_sprint_speed", "value"), &Player::set_sprint_speed);
    ClassDB::bind_method(D_METHOD("get_sprint_speed"), &Player::get_sprint_speed);
    ClassDB::bind_method(D_METHOD("set_jump_speed", "value"), &Player::set_jump_speed);
    ClassDB::bind_method(D_METHOD("get_jump_speed"), &Player::get_jump_speed);
    ClassDB::bind_method(D_METHOD("set_gravity", "value"), &Player::set_gravity);
    ClassDB::bind_method(D_METHOD("get_gravity"), &Player::get_gravity);
    ClassDB::bind_method(D_METHOD("set_mouse_sensitivity", "value"), &Player::set_mouse_sensitivity);
    ClassDB::bind_method(D_METHOD("get_mouse_sensitivity"), &Player::get_mouse_sensitivity);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "walk_speed", PROPERTY_HINT_RANGE, "0,20,0.1,suffix:m/s"), "set_walk_speed", "get_walk_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sprint_speed", PROPERTY_HINT_RANGE, "0,30,0.1,suffix:m/s"), "set_sprint_speed", "get_sprint_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "jump_speed", PROPERTY_HINT_RANGE, "0,20,0.1,suffix:m/s"), "set_jump_speed", "get_jump_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "carry_speed", PROPERTY_HINT_RANGE, "0,20,0.1,suffix:m/s"), "set_carry_speed", "get_carry_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity", PROPERTY_HINT_RANGE, "0,60,0.1,suffix:m/s²"), "set_gravity", "get_gravity");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "god_speed", PROPERTY_HINT_RANGE, "1,200,1,suffix:m/s"), "set_god_speed", "get_god_speed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "god_boost", PROPERTY_HINT_RANGE, "1,20,0.5"), "set_god_boost", "get_god_boost");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "god_scale_height", PROPERTY_HINT_RANGE, "10,10000,10,suffix:m"), "set_god_scale_height", "get_god_scale_height");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "indoor_arm", PROPERTY_HINT_RANGE, "0.5,10,0.1,suffix:m"), "set_indoor_arm", "get_indoor_arm");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mouse_sensitivity", PROPERTY_HINT_RANGE, "0.0005,0.01,0.0001"), "set_mouse_sensitivity", "get_mouse_sensitivity");
}

void Player::_ready() {
    // 扩展类的虚函数在编辑器里也会跑；角色只在游戏里动
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    body = get_node<Node3D>("Body");
    camera_pivot = get_node<Node3D>("CameraPivot");
    camera = get_node<Camera3D>("CameraPivot/SpringArm3D/Camera3D");
    arm = get_node<SpringArm3D>("CameraPivot/SpringArm3D");
    if (arm) {
        outdoor_arm = arm->get_length();
    }
    ERR_FAIL_COND_MSG(!body || !camera_pivot || !camera, "Player: 场景里缺 Body 或 CameraPivot/SpringArm3D/Camera3D");
    Area3D *sensor = get_node<Area3D>("Sensor");
    ERR_FAIL_NULL_MSG(sensor, "Player: 场景里缺 Sensor（Area3D）");
    sensor->connect("area_entered", Callable(this, "on_sensor_area_entered"));
    sensor->connect("area_exited", Callable(this, "on_sensor_area_exited"));
    apply_look();
}

void Player::on_sensor_area_entered(Node3D *p_area) {
    if (p_area && p_area->has_meta("ladder")) {
        ladders.push_back(p_area);
    }
}

void Player::on_sensor_area_exited(Node3D *p_area) {
    ladders.erase(std::remove(ladders.begin(), ladders.end(), p_area), ladders.end());
}

void Player::apply_look() {
    if (camera_pivot) {
        camera_pivot->set_rotation(Vector3(real_t(look_pitch), real_t(look_yaw), 0));
    }
}

void Player::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!controlled || input_blocked || Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        const Vector2 rel = motion->get_relative();
        look_yaw -= rel.x * mouse_sensitivity;
        look_pitch = std::clamp(look_pitch - rel.y * mouse_sensitivity, PITCH_MIN, PITCH_MAX);
        apply_look();
    }
}

// 屋里第三人称：头顶有顶就把相机拉近（弹簧臂顶到墙和天花板本来也会缩，但缩得忽长忽短、还会顺着门洞溜到屋外）
void Player::update_arm(double p_delta) {
    if (!arm || !camera_pivot) {
        return;
    }
    const Vector3 head = camera_pivot->get_global_position();
    Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(head, head + Vector3(0, real_t(ROOF_PROBE), 0), arm->get_collision_mask() | SHIP_LAYER_BIT);
    query->set_exclude(TypedArray<RID>::make(get_rid()));
    indoors = !get_world_3d()->get_direct_space_state()->intersect_ray(query).is_empty();
    const double target = indoors ? indoor_arm : outdoor_arm;
    const double length = arm->get_length();
    arm->set_length(real_t(length + (target - length) * (1.0 - std::exp(-ARM_RATE * p_delta))));
}

void Player::_physics_process(double p_delta) {
    if (!controlled) {
        return;
    }
    update_arm(p_delta);
    Input *input = Input::get_singleton();
    const Vector2 stick = input_blocked ? Vector2() : input->get_vector(input_actions::MOVE_LEFT, input_actions::MOVE_RIGHT, input_actions::MOVE_FORWARD, input_actions::MOVE_BACK);
    const Vector3 wish = Basis(Vector3(0, 1, 0), real_t(look_yaw)).xform(Vector3(stick.x, 0, stick.y));
    const double speed = carrying ? carry_speed : (input->is_action_pressed(input_actions::SPRINT) ? sprint_speed : walk_speed);

    Vector3 velocity = get_velocity();
    if (god_mode) {
        // 飞：三个方向都逼近目标速度，松手就悬停
        const double vertical = input_blocked ? 0.0 : input->get_axis(input_actions::FLY_DOWN, input_actions::ASCEND);
        const double fly = god_speed * (input->is_action_pressed(input_actions::SPRINT) ? god_boost : 1.0) * get_god_scale();
        const Vector3 target = Vector3(wish.x, real_t(vertical), wish.z) * real_t(fly);
        velocity += (target - velocity) * real_t(1.0 - std::exp(-GOD_ACCEL * p_delta));
        set_velocity(velocity);
        move_and_slide();
        if (PlanetView::active_view() != nullptr) {
            const double floor_y = VerticalDatum::SEA_Y + GOD_SEA_CLEARANCE_M;
            Vector3 at = get_global_position();
            if (at.y < floor_y) {
                at.y = real_t(floor_y);
                set_global_position(at);
                velocity.y = std::max(velocity.y, real_t(0));
                set_velocity(velocity);
            }
        }
        if (is_on_floor()) {
            ground_y = get_global_position().y;
        }
        turn_body(wish, p_delta);
        return;
    }
    const bool grounded = is_on_floor();
    if (grounded) {
        ground_y = get_global_position().y;
    }
    // 沿梯子：面朝墙按前是上、背对墙按前是下；站在地上要往下时就当普通走路（走出梯顶的缺口自然会掉进梯子区域）
    double climb = 0.0;
    Vector3 outward;
    if (is_on_ladder()) {
        outward = ladders.back()->get_global_transform().basis.get_column(2);
        const Vector3 look = Basis(Vector3(0, 1, 0), real_t(look_yaw)).xform(Vector3(0, 0, -1));
        const double sign = look.dot(outward) < 0 ? 1.0 : -1.0;
        const double along = input_blocked ? 0.0 : input->get_axis(input_actions::MOVE_BACK, input_actions::MOVE_FORWARD);
        climb = sign * along;
    }
    if (is_on_ladder() && (!grounded || climb > 0.0)) {
        velocity.y = real_t(climb * climb_speed);
        // 水平只留一点，够把人从梯顶挪到甲板上；悬在梯子上时不许离开墙，免得半空滑出梯子掉下去
        Vector3 slide = wish * real_t(0.35 * speed);
        const real_t away = slide.dot(outward);
        if (!grounded && away > 0) {
            slide -= outward * away;
        }
        velocity.x = slide.x;
        velocity.z = slide.z;
    } else {
        const real_t blend = real_t(1.0 - std::exp(-(grounded ? GROUND_ACCEL : AIR_ACCEL) * p_delta));
        velocity.x += (wish.x * real_t(speed) - velocity.x) * blend;
        velocity.z += (wish.z * real_t(speed) - velocity.z) * blend;
        if (grounded) {
            velocity.y = !input_blocked && !carrying && input->is_action_just_pressed(input_actions::JUMP) ? real_t(jump_speed) : 0;
        } else {
            velocity.y -= real_t(gravity * p_delta);
        }
    }
    set_velocity(velocity);
    move_and_slide();
    turn_body(wish, p_delta);
}

void Player::turn_body(const Vector3 &p_wish, double p_delta) {
    if (body && p_wish.length_squared() > 0.01f) {
        const double target = std::atan2(-p_wish.x, -p_wish.z); // 模型正面朝 −z
        const double current = body->get_rotation().y;
        body->set_rotation(Vector3(0, real_t(Math::lerp_angle(current, target, 1.0 - std::exp(-TURN_RATE * p_delta))), 0));
    }
}

void Player::set_carrying(bool p_carrying, const Color &p_color) {
    carrying = p_carrying;
    if (!body) {
        return;
    }
    if (!crate) {
        crate = memnew(Node3D);
        crate->set_name("Crate");
        body->add_child(crate);
        crate->set_position(Vector3(0, 1.05f, -0.55f)); // 身前、齐胸
        Ref<StandardMaterial3D> wood;
        wood.instantiate();
        wood->set_albedo(Color(0.58f, 0.44f, 0.26f));
        wood->set_roughness(0.95f);
        Ref<BoxMesh> box;
        box.instantiate();
        box->set_size(Vector3(0.8f, 0.6f, 0.7f));
        box->set_material(wood);
        MeshInstance3D *mesh = memnew(MeshInstance3D);
        mesh->set_name("Box");
        mesh->set_mesh(box);
        crate->add_child(mesh);
        Ref<BoxMesh> lid;
        lid.instantiate();
        lid->set_size(Vector3(0.7f, 0.04f, 0.6f));
        MeshInstance3D *lid_mesh = memnew(MeshInstance3D);
        lid_mesh->set_name("Lid");
        lid_mesh->set_mesh(lid);
        lid_mesh->set_position(Vector3(0, 0.32f, 0));
        crate->add_child(lid_mesh);
    }
    MeshInstance3D *lid_mesh = Object::cast_to<MeshInstance3D>(crate->get_node_or_null("Lid"));
    if (lid_mesh) {
        Ref<StandardMaterial3D> paint;
        paint.instantiate();
        paint->set_albedo(p_color);
        paint->set_roughness(0.9f);
        lid_mesh->set_material_override(paint);
    }
    crate->set_visible(carrying);
}

double Player::get_god_scale() const {
    if (std::isnan(ground_y) || !is_inside_tree()) {
        return 1.0;
    }
    return 1.0 + std::abs(double(get_global_position().y) - ground_y) / std::max(1.0, god_scale_height); // 往下飞向海面也一样放大
}

void Player::set_god_mode(bool p_enabled) {
    god_mode = p_enabled;
    if (god_mode && is_inside_tree() && (std::isnan(ground_y) || is_on_floor())) {
        ground_y = get_global_position().y; // 从这儿起飞
    }
    set_velocity(Vector3()); // 关掉时从零开始落，开的时候原地悬停
}

void Player::set_controlled(bool p_controlled) {
    if (controlled == p_controlled) {
        return;
    }
    controlled = p_controlled;
    if (controlled) {
        set_collision_layer(saved_layer);
        set_collision_mask(saved_mask);
    } else {
        // 入座：不再参与碰撞，位姿交给上层摆
        saved_layer = get_collision_layer();
        saved_mask = get_collision_mask();
        set_collision_layer(0);
        set_collision_mask(0);
    }
    set_velocity(Vector3());
}

void Player::set_look_yaw(double p_yaw) {
    look_yaw = p_yaw;
    apply_look();
}

void Player::set_look_pitch(double p_pitch) {
    look_pitch = std::clamp(p_pitch, PITCH_MIN, PITCH_MAX);
    apply_look();
}

void Player::shift(const Vector3 &p_position, double p_dyaw, const Basis &p_rotation) {
    set_global_position(p_position);
    set_velocity(p_rotation.xform(get_velocity()));
    look_yaw += p_dyaw;
    apply_look();
    if (body) {
        body->set_rotation(body->get_rotation() + Vector3(0, real_t(p_dyaw), 0));
    }
}

void Player::set_facing_yaw(double p_yaw) {
    if (body) {
        body->set_rotation(Vector3(0, real_t(p_yaw), 0));
    }
}
