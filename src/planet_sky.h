#pragma once

#include <vector>

#include "almanac.h"

// 行星计划 P2：从地面到太空的大气、行星系里的日月。不含 Godot 头。
//
// 大气照 Hillaire 2020（「A Scalable and Production Ready Sky and Atmosphere Rendering Technique」）的做法：
//   两张只跟大气参数有关的表，启动时 CPU 算一次——
//     透过率表（256 × 64）：离星心 r、天顶角余弦 mu 处往外到大气顶的透过率（Bruneton 的参数化，着色器 atmo_transmittance_uv 同式）；
//     多次散射表（32 × 32）：高 h、太阳天顶角余弦 mu_s 处，二次及以上散射折成的各向同性源项 Ψ_ms（论文 §5.5：
//       64 个方向上积单次散射 L 与「散射一次的传递」f_ms，Ψ = L₂ / (1 − f_ms)，含地面反照）；
//   着色器（game/shaders/planet_sky.gdshaderinc）沿视线步进单次散射，每步查透过率表看得见太阳多少、查多次散射表加多次散射——
//   所以地面、高空、太空、晨昏线、大气的边都是同一套式子。不做 Hillaire 的天空视图表 / 空气透视体素（Godot 4.4 的天空着色器没有计算通道；
//   RTX 4080 上逐像素步进 24 步不到半毫秒），也不做 Bruneton 的四维散射表（要几十兆的三维纹理、CPU 算一分钟，换大气参数就得重算）。
// 单位：km（海面半径 6371、大气顶再高 100 km），系数都是每 km。
// 表的取样按「格心对齐」：unit ∈ [0,1] ↔ 纹理坐标 0.5/N + unit·(N − 1)/N（着色器 atmo_sub_uv 同式）。

struct AtmosphereModel {
    double bottom_km = 6371.0; // 海面（生成器的行星半径；游戏 y = 0 在它之上 6.8 km，D13）
    double top_km = 6471.0;
    double rayleigh[3] = { 5.802e-3, 13.558e-3, 33.1e-3 };
    double rayleigh_h_km = 8.0;
    double mie_scatter = 3.996e-3;
    double mie_extinct = 4.40e-3;
    double mie_h_km = 1.2;
    double mie_g = 0.8;
    double ozone[3] = { 0.650e-3, 1.881e-3, 0.085e-3 }; // 只吸收
    double ozone_center_km = 25.0, ozone_half_km = 15.0; // 帐篷形：中心 25 km、两边各 15 km 降到 0
    double ground_albedo = 0.45; // 多次散射按地面反照算：往下看大半是白的云带

    // 离海面 p_h_km 处：Rayleigh 散射（rgb）、Mie 散射、消光（rgb）
    void medium(double p_h_km, double r_rayleigh[3], double &r_mie, double r_extinct[3]) const;
};

struct SkyLut {
    int width = 0, height = 0;
    std::vector<float> rgb; // 行主序，每格 3 个 float（第 0 行 = v 最小）

    bool empty() const { return width == 0; }
    // 双线性（按格心对齐的 unit 坐标，夹边）
    void sample(double p_u, double p_v, double r_out[3]) const;
};

// 透过率表：unit (u, v) ↔ (r km, mu)
void atmo_transmittance_uv(const AtmosphereModel &p_atmo, double p_r, double p_mu, double &r_u, double &r_v);
void atmo_transmittance_r_mu(const AtmosphereModel &p_atmo, double p_u, double p_v, double &r_r, double &r_mu);
// 从 (r, mu) 沿直线到大气顶（或打到海面为止）的光学厚度积分成的透过率
void atmo_transmittance_direct(const AtmosphereModel &p_atmo, double p_r, double p_mu, int p_steps, double r_out[3]);
SkyLut atmo_transmittance_lut(const AtmosphereModel &p_atmo, int p_width = 256, int p_height = 64, int p_steps = 40);
// 查表：看得见大气顶的方向给透过率，打到海面（地平线以下）给 0
void atmo_transmittance(const AtmosphereModel &p_atmo, const SkyLut &p_lut, double p_r, double p_mu, double r_out[3]);
// 多次散射表：unit u = mu_s·0.5 + 0.5、v = (r − 底) / (顶 − 底)
SkyLut atmo_multiscatter_lut(const AtmosphereModel &p_atmo, const SkyLut &p_transmittance, int p_size = 32, int p_dirs_per_axis = 8, int p_steps = 20);

// 行星系（同 sphere.py：x → 0°N 0°E、y → 0°N 90°E、z → 北极）里指向太阳 / 月亮的单位向量。
// p_day 是全局钟的历日：钟点是参考经度 p_clock_lon_deg 的地方时（D10），赤纬、月相、月亮落后太阳多少同 almanac 的 sun_at / moon_at——
// 在经纬度 (φ, λ) 处换到当地的东、上、南，就是 sun_at(p_day + (λ − 参考经度) / 360, 纬度 φ) 的方向。
void planet_sun_dir(double p_day, double p_clock_lon_deg, const CalendarRules &p_rules, double r_dir[3]);
void planet_moon_dir(double p_day, double p_clock_lon_deg, const CalendarRules &p_rules, double r_dir[3]);
