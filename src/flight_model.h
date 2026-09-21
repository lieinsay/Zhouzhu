#pragma once

// 空艇飞行模型：纯函数，不含 Godot 头。
// 只管「速度与航向怎么随输入变」；碰撞与位置积分是 Ship 的 move_and_slide 的事。
//
// 坐标与 Godot 一致：y 向上，航向 yaw 绕 y 轴，yaw = 0 时船头指向 −z，yaw 增大 = 向左转。

struct FlightParams {
    double max_speed = 32.0; // 前进极速 m/s
    double reverse_speed = 8.0; // 倒车极速
    double accel_tau = 2.5; // 前向速度逼近目标的时间常数（秒）
    double side_tau = 0.6; // 侧滑衰减
    double max_climb = 10.0; // 升降极速
    double climb_tau = 1.0;
    double max_yaw_rate = 0.75; // rad/s
    double yaw_tau = 0.7;
};

struct FlightInput {
    double throttle = 0.0; // −1 倒车 … +1 全速
    double rudder = 0.0; // −1 左 … +1 右
    double lift = 0.0; // −1 降 … +1 升
};

struct FlightState {
    double vx = 0.0, vy = 0.0, vz = 0.0; // 世界系速度
    double yaw = 0.0;
    double yaw_rate = 0.0;
};

// 船头方向（水平单位向量）
void flight_forward(double p_yaw, double &r_x, double &r_z);

FlightState flight_step(const FlightState &p_state, const FlightInput &p_input, const FlightParams &p_params, double p_dt);
