#pragma once

#include <vector>

#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/gpu_particles3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "almanac.h"
#include "weather.h"

class MapHost;

// 天与光：每个场景一个（world_2051 / main / flat_map 都挂）。按全局钟的历日与相机那一点取天气（地图宿主给的天气模型，
// 没有就用一份没有数据的：常年晴、纬度 latitude），平滑之后驱动——
//   太阳与月亮：方向按历法（纬度、赤纬、时角），太阳的亮度与颜色随高度（晨昏偏暖）、被云遮暗；夜里月光按月相；
//   天空：game/shaders/sky.gdshader（昼夜晨昏、日月星、云层与地上的雨同一份云团噪声、阴沉、雨雾、闪电）；
//   环境光与雾：环境光随天色，雨雪天起雾，云海漫顶的日子按雾顶高度起高度雾（站在高处看得见云海漫上来）；
//   雨雪：跟着相机的两套粒子，按局地降水强度与雨雪份额、随风斜着下；头顶有顶（屋里、舱房里）就停；风暴时打闪；
//   全局着色器参数（weather.gdshaderinc）：海拔表（积雪、冰、物候、作物）、地湿、风、极向与夜色——地皮、散布、河读它换季、盖雪、结冰、摆动。
// 画面按 smoothing（真实秒）跟上天气，瞬移、拨钟、换覆盖都不会一下子跳。
// 有行星数据的地图（场景里有 PlanetView，行星计划 P1）：日月按相机的经纬度算——纬度就是相机那一点的纬度，钟点是参考经度（2051，152.15°E）的地方时（D10），
// 相机处的地方时 = 钟点 + 经度差 / 15°；方向落在相机的局部系（画面的世界轴就是它，远处岛群的受光按真球面对得上）。
// 雾：高过 2.5 km 起按空气变稀减淡（高空往下看不再是一片白），近地面的雾一点不变。
// 开发者覆盖（set_weather_override）改在天气模型上，按 override_fade 渐入渐出；村子、庄稼读同一个模型，所以也跟着变。
class Atmosphere : public godot::Node3D {
    GDCLASS(Atmosphere, godot::Node3D)

    godot::NodePath sun_path = "../Sun";
    godot::NodePath environment_path = "../WorldEnvironment";
    godot::NodePath map_path; // 地图宿主（World / IslandMap / FlatMap）；空或不是宿主 = 没有气候数据
    double latitude = 30.0; // 没有气候数据时的纬度
    double smoothing = 2.5;
    double override_fade = 6.0;
    double cloud_height = 2800.0;
    bool precipitation = true;

    MapHost *map = nullptr;
    WeatherModel fallback;
    godot::DirectionalLight3D *sun = nullptr;
    godot::DirectionalLight3D *moon = nullptr;
    godot::Ref<godot::Environment> env;
    godot::Ref<godot::ShaderMaterial> sky_material;
    godot::GPUParticles3D *rain = nullptr;
    godot::GPUParticles3D *snow = nullptr;
    godot::Ref<godot::StandardMaterial3D> rain_material, snow_material;
    godot::Ref<godot::ImageTexture> elev_texture;
    std::vector<float> elev_values;
    double base_fog = 0.00003, base_ambient = 0.55, base_sun = 1.0;
    godot::Color base_fog_color = godot::Color(0.8f, 0.86f, 0.93f);

    bool override_on = false;
    WeatherKind override_kind = WeatherKind::COUNT;
    double override_temp = 0.0;

    WeatherSample now;
    double sun_day = 0.0; // 算日月用的「相机处的当地历日」（钟的历日 + 经度差 / 360）
    double sun_latitude = 30.0, sun_longitude = 0.0, local_offset_h = 0.0;
    bool on_planet = false;
    double fog_thin = 1.0; // 高处的雾按空气变稀的倍数
    SunState sun_state;
    MoonState moon_state;
    bool have_look = false;
    godot::Vector3 last_eye;
    struct Look {
        double cloud = 0.1, cover = 0.12, precip = 0.0, snow = 0.0, fog = 0.0, storm = 0.0, wet = 0.0, wind_x = 0.0, wind_z = 0.0;
        double overcast = 0.0, haze = 0.0, fog_height = 0.0, grad_x = 0.0, grad_z = 0.0;
    } look;
    double flash = 0.0, flash_timer = 4.0, flicker = 0.0;
    double indoor = 0.0, indoor_timer = 0.0;
    bool indoor_hit = false;
    double gust_phase = 0.0;
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    double rain_ratio = 0.0, snow_ratio = 0.0;

    double random01();

    WeatherModel *model();
    double calendar_day() const;
    void build();
    void update_override(double p_delta);
    void update_look(const godot::Vector3 &p_eye, double p_delta);
    void update_lights();
    void update_environment();
    void update_globals(double p_day);
    void update_particles(const godot::Vector3 &p_eye, double p_delta);
    void update_lightning(double p_delta);
    void check_indoor(const godot::Vector3 &p_eye, double p_delta);

protected:
    static void _bind_methods();

public:
    static constexpr const char *GROUP = "atmosphere";

    void _ready() override;
    void _process(double p_delta) override;
    void _physics_process(double p_delta) override; // 头顶有没有顶（射线要在物理帧里打）

    // 此刻相机那一点的天气（平滑前）与天上：见 weather_view::to_dictionary，另加
    // { sun_altitude, sun_azimuth, sunrise, sunset, day_length, moon_phase, moon_lit, snow_line, ice_line, season, season_name, date, night, override }
    godot::Dictionary get_weather() const;
    // 某一点此刻的天气（世界坐标）
    godot::Dictionary weather_at(const godot::Vector3 &p_global);
    // HUD 用的一行：「暖季一 · 小雨 18°C · 东南风 3 m/s」（某一点）
    godot::String hud_text(const godot::Vector3 &p_global);
    // 某一点此刻的风（m/s，往 x / z 吹）：掌舵时船按它往下风漂（flight_model 的 wind_drift）
    godot::Vector2 wind_at(const godot::Vector3 &p_global);
    // 掌舵时的提醒：风大（船往下风漂）、这一天禁航；平常是空的
    godot::String sailing_text(const godot::Vector3 &p_global);
    // 画面此刻跟到哪（平滑后）：{ cloud, cover, precip, snow, fog, storm, wet, wind_x, wind_z, overcast, indoor, flash, rain_ratio, snow_ratio }
    godot::Dictionary get_look() const;
    double get_sun_altitude() const { return sun_state.altitude_deg; }
    bool is_night() const { return sun_state.altitude_deg < -6.0; }

    // 开发者覆盖：p_kind 是天气的 id（"clear" / "rain" / …；空 = 天气照自动），p_temp_offset 是气温偏移（°C）
    void set_weather_override(const godot::String &p_kind, double p_temp_offset);
    void clear_weather_override();
    // { active, kind（id，空 = 自动）, kind_name, temp_offset, blend, fresh_snow_mm }
    godot::Dictionary get_override() const;
    // 画面立刻跟上（不平滑，测试与瞬移后用）
    void snap();

    const WeatherModel *get_model_const();

    void set_sun_path(const godot::NodePath &p_value) { sun_path = p_value; }
    godot::NodePath get_sun_path() const { return sun_path; }
    void set_environment_path(const godot::NodePath &p_value) { environment_path = p_value; }
    godot::NodePath get_environment_path() const { return environment_path; }
    void set_map_path(const godot::NodePath &p_value) { map_path = p_value; }
    godot::NodePath get_map_path() const { return map_path; }
    void set_latitude(double p_value);
    double get_latitude() const { return latitude; }
    void set_smoothing(double p_value) { smoothing = p_value; }
    double get_smoothing() const { return smoothing; }
    void set_override_fade(double p_value) { override_fade = p_value; }
    double get_override_fade() const { return override_fade; }
    void set_cloud_height(double p_value) { cloud_height = p_value; }
    double get_cloud_height() const { return cloud_height; }
    void set_precipitation(bool p_value) { precipitation = p_value; }
    bool get_precipitation() const { return precipitation; }
};
