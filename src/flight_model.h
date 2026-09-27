#pragma once

#include <cstdint>

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
    double drift_x = 0.0, drift_z = 0.0; // 风把船往哪推（m/s，世界系）：船对空气的速度追油门，对地再加上它
    double bump = 0.0; // 颠簸（行星计划 P7，云带里、风暴里）：这一刻上下乱推的速度（m/s），调用方按 flight_bump × 强度给
};

// 颠簸：随时间平滑起伏、没有规律的一股上下扰动，约 −1..1（两层一维值噪声，零点几到两赫兹）；p_seed 分开几条船。强度由调用方乘
double flight_bump(double p_time_s, uint64_t p_seed = 0);

// 风把船推多快：风速 p_calm_ms 以下不推，往上渐渐推到风速的 p_share（风暴里船往下风漂）
void wind_drift(double p_wind_x, double p_wind_z, double &r_x, double &r_z, double p_calm_ms = 5.0, double p_share = 0.6);

struct FlightState {
    double vx = 0.0, vy = 0.0, vz = 0.0; // 世界系速度（vy 含颠簸）
    double yaw = 0.0;
    double yaw_rate = 0.0;
    double bump = 0.0; // vy 里这一刻颠簸的那一份：升降按舵追，颠簸直接叠上去（不被升降的惯性抹平）
};

// 船头方向（水平单位向量）
void flight_forward(double p_yaw, double &r_x, double &r_z);

FlightState flight_step(const FlightState &p_state, const FlightInput &p_input, const FlightParams &p_params, double p_dt);

// 速度档位：掌舵时一档一档换，档位系数乘到极速与升降极速上（倒车跟着极速走）。
// 头一档就是船本身的船况，泊位附近、村子上空用它；岛与岛之间 5–30 km，换到高档几分钟飞到。
struct FlightGear {
    const char *id;
    const char *name; // 显示名，UTF-8
    double speed_scale;
    double climb_scale;
};

namespace flight_gears {
int count();
const FlightGear &at(int p_gear); // 越界夹到两头
} // namespace flight_gears

FlightParams apply_gear(const FlightParams &p_params, int p_gear);
