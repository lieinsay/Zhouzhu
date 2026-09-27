#include "flight_model.h"

#include <algorithm>
#include <cmath>

namespace {

// 一阶逼近：与帧率无关（dt 拆成几步走，结果一样）
double approach(double p_value, double p_target, double p_tau, double p_dt) {
    return p_target + (p_value - p_target) * std::exp(-p_dt / std::max(1e-6, p_tau));
}

} // namespace

void wind_drift(double p_wind_x, double p_wind_z, double &r_x, double &r_z, double p_calm_ms, double p_share) {
    const double speed = std::hypot(p_wind_x, p_wind_z);
    const double k = speed > 1e-9 ? std::clamp((speed - p_calm_ms) / 10.0, 0.0, 1.0) * p_share : 0.0;
    r_x = p_wind_x * k;
    r_z = p_wind_z * k;
}

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
    // 风：对地的速度 = 对空气的（追油门、侧滑衰减）+ 风推的那一份
    const double drift_fwd = p_input.drift_x * fx + p_input.drift_z * fz;
    const double drift_side = p_input.drift_x * rx + p_input.drift_z * rz;
    v_fwd = approach(v_fwd, target + drift_fwd, p_params.accel_tau, p_dt);
    v_side = approach(v_side, drift_side, p_params.side_tau, p_dt);
    s.vx = v_fwd * fx + v_side * rx;
    s.vz = v_fwd * fz + v_side * rz;

    // 升降：去掉上一刻的颠簸再追舵，这一刻的颠簸直接叠上去
    const double climb = approach(s.vy - s.bump, lift * p_params.max_climb, p_params.climb_tau, p_dt);
    s.bump = std::isfinite(p_input.bump) ? p_input.bump : 0.0;
    s.vy = climb + s.bump;
    return s;
}

namespace {

// 一维值噪声：整点上的哈希（−1..1），五次平滑插值
double bump_hash(int64_t p_i, uint64_t p_seed) {
    uint64_t h = uint64_t(p_i) * 0x9E3779B97F4A7C15ull ^ (p_seed + 0x632BE59BD9B4E019ull);
    h ^= h >> 31;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 29;
    return double(h >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

double bump_noise(double p_x, uint64_t p_seed) {
    const double i = std::floor(p_x);
    const double f = p_x - i;
    const double u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    const double a = bump_hash(int64_t(i), p_seed), b = bump_hash(int64_t(i) + 1, p_seed);
    return a + (b - a) * u;
}

} // namespace

double flight_bump(double p_time_s, uint64_t p_seed) {
    if (!std::isfinite(p_time_s)) {
        return 0.0;
    }
    return 0.7 * bump_noise(p_time_s * 0.8, p_seed) + 0.3 * bump_noise(p_time_s * 2.1 + 17.3, p_seed ^ 0x5851F42D4C957F2Dull);
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
