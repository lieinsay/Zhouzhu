#pragma once

#include <cstdint>
#include <vector>

class PlanetData;

// 海面（行星计划 P7，docs/PLAN-PLANET.md D13、D14）：浪按当地的风，白礁按行星包。不含 Godot 头。
// 画在 game/shaders/planet_sea.gdshaderinc（SeaSurface 的海面网格、天空着色器从下往上看的那一段都 include 它），这里给数与贴图：
//   - sea_waves：有效波高、主浪波长（充分成长的风浪，Pierson–Moskowitz），白浪、碎浪——风按行星天气场（天与光在相机处取的风），风暴另加；
//   - bake_wave_normals：一张可平铺的海浪法线贴图（几十道整数波数的余弦叠起来），着色器两层按风错开平移、近处再加几道 Gerstner 顶点波；
//   - bake_reef_density：白礁的疏密（等经纬）。老岛沉下去只留下海面上惨白的礁（铁律原则甲：纯氛围，不能落、不能采）——
//     按行星包各群的岛龄与陆地撒：老岛群附近多（离群心 reach_km 上下），别处零星；着色器再按这张图叠几层噪声撒成一片片的。

struct SeaWaves {
    double height_m = 0.0; // 有效波高（浪峰到浪谷，最大的三分之一的平均）
    double length_m = 0.0; // 主浪的波长
    double period_s = 0.0;
    double foam = 0.0; // 白浪（0..1）：五六米每秒起风就起白头
    double chop = 0.0; // 碎浪、飞沫（0..1）：大风、风暴
};

struct SeaWaveRules {
    double calm_height_m = 0.5; // 没风也有的涌
    double calm_length_m = 40.0;
    double max_height_m = 9.0; // 画面上的浪再大也就这么高（船离海面 15 m）
    double max_length_m = 260.0; // 主浪最长（再长近处就看不出是浪）
    double storm_wind_ms = 28.0; // 风暴 1 时按这么大的风算浪
};

// p_wind_ms：此地的风（米每秒）；p_storm：风暴 0..1
SeaWaves sea_waves(double p_wind_ms, double p_storm, const SeaWaveRules &p_rules = SeaWaveRules());

// 海浪法线贴图：p_size²，RGBA8——R、G = 高度场沿 x、z 的斜率（除以最大斜率，0.5 + 0.5·s），B = 高（0..1），A = 浪头（高处，0..1：白头按它撒）。
// 在 [0, 1)² 上可平铺（波数都是整数）；同一个 p_seed 逐位相同
void bake_wave_normals(int p_size, uint32_t p_seed, std::vector<uint8_t> &r_rgba);

struct ReefRules {
    double base = 0.05; // 哪儿都有的零星礁
    double age_from = 0.38, age_to = 0.72; // 岛龄从这里起算「老」，到这里算最老（八千群的岛龄多在 0.2–0.6，0.6 以上的不到 2%）
    double reach_km = 90.0; // 老岛群周围多远还多（高斯的 σ；群与群的间距中位 82 km）
    double gain = 0.9;
    double area_ref_km2 = 400.0; // 陆地到这么大按满份算（小群撒得少一点）
};

// 某经纬度处白礁的疏密（0..1）：base + gain · Σ 老的份 × 陆地的份 × exp(−(d / reach)²)，逐群加（3 σ 以外不算）
double reef_density_at(const PlanetData &p_planet, double p_lat_deg, double p_lon_deg, const ReefRules &p_rules = ReefRules());
// 同一个式子烘成等经纬的一张（p_width × p_height，第 0 行在南、格心对齐，同 planet_sky 的 1° 场），R8
void bake_reef_density(const PlanetData &p_planet, int p_width, int p_height, std::vector<uint8_t> &r_out, const ReefRules &p_rules = ReefRules());
