#pragma once

#include <cmath>
#include <vector>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/spring_arm3d.hpp>

// 玩家角色：第三人称，相对相机朝向的 WASD、跳、疾跑。
// 根节点自己从不旋转——转的是相机支点（鼠标）和身体模型（朝移动方向）。
// 它不知道飞船的存在：入座时由上层 set_controlled(false) 停掉输入与物理，再直接摆位姿。
// set_input_blocked(true) 只挡输入（开着背包时 W/S 是在选东西，不是在走路），物理照常：重力、落地都还在。
// 爬梯：Sensor（Area3D）碰到带 "ladder" 元数据的区域时，前 / 后变成沿梯子上 / 下（面朝墙按前是上，背对墙按前是下），
// 水平速度放慢，不受重力。梯子是谁的（船）它不知道。
// 屋里：头顶 6 m 内有东西（天花板、舱顶）就把相机的弹簧臂缓缓收到 indoor_arm，出来再放回场景里配的长度——还是第三人称，只是近些。
// 弹簧臂撞地面、房子（第 1 层）也撞船（第 2 层）：船舱是空心的，相机穿进舱墙就只看得见墙；头顶那一探也看船，舱里算屋里。
// 抱着货箱（上层 set_carrying）：身前画一只箱子，只能慢走（carry_speed），不能疾跑、不能跳；箱子是谁的、装的什么它不知道。
// god 模式（开发者工具开的，它不知道是谁）：不受重力，按相机的水平朝向飞，空格升、Ctrl 降、Shift 加速；碰撞照旧，降到地上就站住。
// 速度随离地高度放大：× (1 + 离上一次站着的地面多高（往上往下都算） / god_scale_height)——300 m 起一倍、50 km 时一百六十多倍，
// 不按 Shift 升到 50 km 约 51 s、从 50 km 降回来也差不多（行星计划 P1）；贴着地面还是 god_speed。
// 有行星数据的地图里往下飞穿得过云带，停在海面之上 GOD_SEA_CLEARANCE_M（海面只是画出来的，没有碰撞）；飞在岛下面不算坠落（Expedition 管）。
//
// 场景（player.tscn）里须有：Body（Node3D，身体模型）、CameraPivot/SpringArm3D/Camera3D、Sensor（Area3D）。
class SceneCharacter;
class Player : public godot::CharacterBody3D {
    GDCLASS(Player, godot::CharacterBody3D)

    double walk_speed = 5.0;
    double sprint_speed = 8.5;
    double jump_speed = 7.5;
    double gravity = 22.0;
    double climb_speed = 2.5;
    double carry_speed = 4.0; // 抱着货箱时
    double mouse_sensitivity = 0.0025;
    double god_speed = 30.0;
    double god_boost = 5.0; // 按住 Shift 时速度乘这么多
    double god_scale_height = 300.0; // 离地每高这么多，god 的速度多一倍
    double ground_y = NAN; // 上一次站在地上时脚下的高（god 的高度倍数按它算）；还没站过是 NAN
    double indoor_arm = 2.6; // 屋里相机离人多远
    double outdoor_arm = 5.0; // 屋外：开局从场景里的 SpringArm3D 读
    bool indoors = false;

    bool controlled = true;
    bool input_blocked = false;
    bool god_mode = false;
    bool carrying = false;
    double look_yaw = 0.0;
    double look_pitch = -0.25;
    uint32_t saved_layer = 0;
    uint32_t saved_mask = 0;

    godot::Node3D *body = nullptr;
    SceneCharacter *avatar = nullptr;
    godot::Node3D *camera_pivot = nullptr;
    godot::Camera3D *camera = nullptr;
    godot::SpringArm3D *arm = nullptr;
    godot::Node3D *crate = nullptr; // 抱着的箱子（Body 下，用时才建）
    std::vector<godot::Node3D *> ladders; // 眼下碰着的梯子区域（区域的 +Z 是离墙的方向）

    void apply_look();
    void update_arm(double p_delta);
    void turn_body(const godot::Vector3 &p_wish, double p_delta); // 身体模型转向移动方向
    void on_sensor_area_entered(godot::Node3D *p_area);
    void on_sensor_area_exited(godot::Node3D *p_area);

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _physics_process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    void set_controlled(bool p_controlled);
    bool is_controlled() const { return controlled; }
    void set_input_blocked(bool p_blocked) { input_blocked = p_blocked; }
    bool is_input_blocked() const { return input_blocked; }
    bool is_on_ladder() const { return !ladders.empty(); }
    bool is_indoors() const { return indoors; }
    void set_indoor_arm(double p_value) { indoor_arm = p_value; }
    double get_indoor_arm() const { return indoor_arm; }
    void set_god_mode(bool p_enabled);
    // 抱起 / 放下货箱；p_color 是箱盖的颜色（货物）
    void set_carrying(bool p_carrying, const godot::Color &p_color = godot::Color(0.86f, 0.82f, 0.7f));
    bool is_carrying() const { return carrying; }
    void set_carry_speed(double p_value) { carry_speed = p_value; }
    double get_carry_speed() const { return carry_speed; }
    bool is_god_mode() const { return god_mode; }
    void set_look_yaw(double p_yaw);
    double get_look_yaw() const { return look_yaw; }
    void set_look_pitch(double p_pitch); // 测试、截图摆视角（准星的射线跟着相机）
    double get_look_pitch() const { return look_pitch; }
    void set_facing_yaw(double p_yaw); // 身体模型朝向
    // 换锚（行星计划 P3）：视线与身体模型一起转 p_dyaw（弧度），速度乘 p_rotation
    void shift(const godot::Vector3 &p_position, double p_dyaw, const godot::Basis &p_rotation);
    godot::Camera3D *get_camera() const { return camera; }

    void set_walk_speed(double p_value) { walk_speed = p_value; }
    double get_walk_speed() const { return walk_speed; }
    void set_sprint_speed(double p_value) { sprint_speed = p_value; }
    double get_sprint_speed() const { return sprint_speed; }
    void set_jump_speed(double p_value) { jump_speed = p_value; }
    double get_jump_speed() const { return jump_speed; }
    void set_gravity(double p_value) { gravity = p_value; }
    double get_gravity() const { return gravity; }
    void set_mouse_sensitivity(double p_value) { mouse_sensitivity = p_value; }
    void set_god_speed(double p_value) { god_speed = p_value; }
    double get_god_speed() const { return god_speed; }
    void set_god_boost(double p_value) { god_boost = p_value; }
    double get_god_boost() const { return god_boost; }
    void set_god_scale_height(double p_value) { god_scale_height = p_value; }
    double get_god_scale_height() const { return god_scale_height; }
    double get_god_scale() const; // 此刻 god 的速度倍数（离地越高越大）
    double get_mouse_sensitivity() const { return mouse_sensitivity; }
};
