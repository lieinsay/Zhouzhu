#include "flight_model.h"

#include <algorithm>
#include <cmath>

namespace {

// 一阶逼近：与帧率无关（dt 拆成几步走，结果一样）
double approach(double p_value, double p_target, double p_tau, double p_dt) {
    return p_target + (p_value - p_target) * std::exp(-p_dt / std::max(1e-6, p_tau));
}

} // namespace

void flight_forward(double p_yaw, double &r_x, double &r_z) {
    r_x = -std::sin(p_yaw);
    r_z = -std::cos(p_yaw);
}

FlightState flight_step(const FlightState &p_state, const FlightInput &p_input, const FlightParams &p_params, double p_dt) {
    FlightState s = p_state;
    const double throttle = std::clamp(p_input.throttle, -1.0, 1.0);
    const double rudder = std::clamp(p_input.rudder, -1.0, 1.0);
    const double lift = std::clamp(p_input.lift, -1.0, 1.0);

    // 舵：右舵 = yaw 减小
    s.yaw_rate = approach(s.yaw_rate, -rudder * p_params.max_yaw_rate, p_params.yaw_tau, p_dt);
    s.yaw += s.yaw_rate * p_dt;

    // 水平速度拆成沿船头与横向两个分量：前者追油门，后者（侧滑）很快衰减
    double fx, fz;
    flight_forward(s.yaw, fx, fz);
    const double rx = -fz, rz = fx; // 右舷
    double v_fwd = s.vx * fx + s.vz * fz;
    double v_side = s.vx * rx + s.vz * rz;
    const double target = throttle >= 0.0 ? throttle * p_params.max_speed : throttle * p_params.reverse_speed;
    v_fwd = approach(v_fwd, target, p_params.accel_tau, p_dt);
    v_side = approach(v_side, 0.0, p_params.side_tau, p_dt);
    s.vx = v_fwd * fx + v_side * rx;
    s.vz = v_fwd * fz + v_side * rz;

    s.vy = approach(s.vy, lift * p_params.max_climb, p_params.climb_tau, p_dt);
    return s;
}

namespace {

const FlightGear GEARS[] = {
    { "harbor", "泊", 1.0, 1.0 },
    { "cruise", "巡", 2.5, 1.5 },
    { "swift", "疾", 4.0, 2.0 },
};
constexpr int GEAR_COUNT = int(sizeof(GEARS) / sizeof(GEARS[0]));

} // namespace

int flight_gears::count() {
    return GEAR_COUNT;
}

const FlightGear &flight_gears::at(int p_gear) {
    return GEARS[std::clamp(p_gear, 0, GEAR_COUNT - 1)];
}

FlightParams apply_gear(const FlightParams &p_params, int p_gear) {
    const FlightGear &g = flight_gears::at(p_gear);
    FlightParams p = p_params;
    p.max_speed *= g.speed_scale;
    p.reverse_speed *= g.speed_scale;
    p.max_climb *= g.climb_scale;
    return p;
}
