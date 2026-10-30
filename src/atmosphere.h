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
#include "cloud_band.h"
#include "map_host.h"
#include "planet_weather.h"
#include "weather.h"

class MapHost;

// 天与光：每个场景一个（world_seed42 / main / flat_map 都挂）。按全局钟的历日与相机那一点取天气（地图宿主给的天气模型，
// 没有就用一份没有数据的：常年晴、纬度 latitude），平滑之后驱动——
//   太阳与月亮：方向按历法（纬度、赤纬、时角），太阳的亮度与颜色随高度（晨昏偏暖）、被云遮暗；夜里月光按月相；
//   天空：game/shaders/sky.gdshader（昼夜晨昏、日月星、云层与地上的雨同一份云团噪声、阴沉、雨雾、闪电）；
//   环境光与雾：环境光随天色，雨雪天起雾，云海漫顶的日子按雾顶高度起高度雾（站在高处看得见云海漫上来）；
//   雨雪：跟着相机的两套粒子，按局地降水强度与雨雪份额、随风斜着下；头顶有顶（屋里、舱房里）就停；风暴时打闪；
//   全局着色器参数（weather.gdshaderinc）：海拔表（积雪、冰、物候、作物）、地湿、风、极向与夜色——地皮、散布、河读它换季、盖雪、结冰、摆动。
// 画面按 smoothing（真实秒）跟上天气，瞬移、拨钟、换覆盖都不会一下子跳。
// 有行星数据的地图（场景里有 PlanetView，行星计划 P1）：日月按相机的经纬度算——纬度就是相机那一点的纬度，钟点是参考经度（钟的那一群，开局配置 story_start::CLOCK_NODE）的地方时（D10），
// 相机处的地方时 = 钟点 + 经度差 / 15°；方向落在相机的局部系（画面的世界轴就是它，远处岛群的受光按真球面对得上）。
// 雾：高过 2.5 km 起按空气变稀减淡（高空往下看不再是一片白），近地面的雾一点不变。
// 开发者覆盖（set_weather_override）改在天气模型上，按 override_fade 渐入渐出；村子、庄稼读同一个模型，所以也跟着变。
// 行星计划 P7（云带之下，D13）：相机降到云带顶（−5000 m）以下，按 cloud_band_at 的剖面——
//   云带里：浓雾（能见度几十米）、雾色随走进的深度与太阳高度变暗，直射按云带的光学厚度衰减，天被雾盖满；
//   云底下（−6200 m 以下）：阴天式的漫射光（看不见日面、方向光熄了，环境光是云带顶上的三成多），远处是灰蓝的霾，下雨霾浓；
//   天空着色器换成从下往上看（云底、海、霾，under_* 参数）、云海漫顶的高度雾撤掉；晨昏只有云底染色（SeaSurface 的云底按太阳的方位染）。
//   云底、海面的颜色按同一份光给（get_under_look），天空着色器里从下往上看的那一段与网格对得上。气温 y < 0 按 y = 0（天气模型里）。
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
    double above_clouds = 0.0; // 相机升过天气云层多少（0..1，有行星数据时）：天不阴、日光不被遮、不下雨雪
    double space_blend = 0.0; // 天上换成大气散射多少（PlanetGlobe 设）：引擎给天空蒙的雾跟着淡掉
    double base_fog_sky = 0.2;
    // 行星计划 P7：相机在云带的哪儿（没有行星数据时是云带顶上那样：什么都不变）
    CloudBandRules band_rules;
    CloudBandState band;
    double band_fog = 0.06; // 云带里的雾（引擎的指数雾，每米）：50 m 外九成五看不见
    double under_fog = 1.2e-4; // 云底下的霾（引擎的指数雾，每米）：晴天 25 km 外九成五看不见
    double under_rain_fog = 1.2e-3; // 下大雨时再加（2 km 上下就看不清）
    double under_sky_affect = 0.0; // 云底下天空蒙雾的份：天空着色器自己蒙霾（屏幕上的天几乎都被云底、海面网格盖着，主要管反光里的天：留着云底的明暗）
    godot::Color band_light_tint = godot::Color(0.82f, 0.85f, 0.89f); // 云里的雾、云底下的霾（乘此刻透进来的光）
    godot::Color base_albedo = godot::Color(0.86f, 0.89f, 0.94f); // 云底的颜色（乘头顶多亮）
    godot::Color sea_albedo = godot::Color(0.010f, 0.026f, 0.034f); // 海水（漫射的那一份；亮处多是反光）

public:
    struct UnderLook {
        godot::Color fog; // 雾 / 霾的颜色（引擎的雾就是它）
        godot::Color base; // 云底此刻看上去的颜色（反照 × 环境光）
        godot::Color sea; // 海水漫射的那一份
        godot::Color ambient; // 环境光（颜色 × 强度）
        double light = 1.0; // 云里的雾有多亮（0..1，按剖面与昼夜）
        double zenith = 1.0; // 云底下头顶的云底有多亮（0..1）
        double dusk = 0.0; // 晨昏 0..1
        double day = 1.0;
        godot::Vector3 sun_visual = godot::Vector3(0, 1, 0); // 太阳（画面的世界轴）
        double wind_x = 0.0, wind_z = 0.0; // 相机处的风（平滑后，锚群平面轴，m/s）：海浪按它
        double storm = 0.0, precip = 0.0, flash = 0.0;
    };

private:
    UnderLook under_look;
    double env_fog_density = 0.0, env_sky_affect = 0.0, env_ambient = 0.0; // 这一帧设给引擎的（测试录）
    godot::Color env_fog_color;
    SunState sun_state;
    MoonState moon_state;
    bool have_look = false;
    godot::Vector3 last_eye;
    // 相机在真球面上的位置（行星系，有 PlanetView 时）：换锚时世界坐标一下子挪上百公里，真位置不动——按它判「一帧挪太远」，换锚那一帧照旧平滑
    double last_true[3] = { 0.0, 0.0, 0.0 };
    bool have_true = false;
    struct Look {
        double cloud = 0.1, cover = 0.12, precip = 0.0, snow = 0.0, fog = 0.0, storm = 0.0, wet = 0.0, wind_x = 0.0, wind_z = 0.0;
        double overcast = 0.0, haze = 0.0, fog_height = 0.0, grad_x = 0.0, grad_z = 0.0;
        // 雨雪粒子跟的降水与雪的份额：比天色跟得快（drop_smoothing），HUD 说停了，雨丝一两秒内就停（行星计划 P4 查出来的：原来跟 precip，
        // 大雨在 rain_ratio 上是饱和的，天气停了还要下五六秒）
        double drops = 0.0, drops_snow = 0.0;
    } look;
    double drop_smoothing = 0.5; // 真实秒
    double last_day = 0.0; // 上一帧的历日：一帧里钟挪了 CLOCK_JUMP_H 以上（拨钟、测试跳钟）画面直接跟上
    bool have_day = false;
    double flash = 0.0, flash_timer = 4.0, flicker = 0.0;
    double indoor = 0.0, indoor_timer = 0.0;
    bool indoor_hit = false;
    double gust_phase = 0.0;
    double water_time = 0.0; // 水纹的钟（全局着色器参数 water_time）：按真实秒走，世界钟暂停时停
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    double rain_ratio = 0.0, snow_ratio = 0.0;

    double random01();

    WeatherModel *model();
    // 某一点的天气：地图宿主的天气模型（行星上是锚群的，它挂着行星天气场：群与群之间由场按距离与风向过渡，行星计划 P5）；p_h 是高
    WeatherSample sample_mix(double p_day, const godot::Vector3 &p_global, double p_h);
    std::vector<WeatherModel *> models_buf;
    std::vector<int> override_nodes;
    // 近处的云团（行星计划 P5）：相机附近管得着的几群（≤ 4）各自平面上的 fbm，天空、云层叠画、云带顶按它们的权混（全局参数 wx_cloud_g0..3）；
    // 候选变了渐入渐出（cloud_slot_fade 秒），瞬移、拨钟直接跟上
    struct CloudSlot {
        int node = -1;
        double fade = 0.0;
        bool want = false;
    };
    CloudSlot cloud_slots[4];
    std::vector<PlanetCloudCandidate> cloud_cands;
    double cloud_slot_fade = 2.0;
    bool cloud_slots_on = false;
    bool last_jump = false;
    void update_cloud_slots(double p_day, double p_delta);
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
    // 掌舵时的提醒：风大（船往下风漂）、这一天禁航、赤道永暴带 / G 圈里风暴禁航；平常是空的
    godot::String sailing_text(const godot::Vector3 &p_global);
    // 行星计划 P5：赤道永暴带、G 定点永暴圈里（背景接手的地方）把船往外推多快（m/s，全局 x / z）；别处是 0
    godot::Vector2 sail_push_at(const godot::Vector3 &p_global);
    // 行星天气场在某一点（全局）：{ ready, nodes, weights, bg, group_share, zone, zone_kind（0 没有 1 赤道永暴带 2 G）, push_x, push_z,
    //   cloud_cover, coverage, temp_c, wind_x, wind_z, storm, day_kind, front, lat, lon }；没有行星数据 ready 为假
    godot::Dictionary planet_weather_at(const godot::Vector3 &p_global);
    // 同上，按经纬度问（风是那一点的东、南）
    godot::Dictionary planet_weather_latlon(double p_lat, double p_lon);
    // 近处云团的几个槽：[{ node, fade }]
    godot::Array get_cloud_slots() const;
    // 画面此刻跟到哪（平滑后）：{ cloud, cover, precip, snow, fog, storm, wet, wind_x, wind_z, overcast, indoor, flash, rain_ratio, snow_ratio }，
    // 另有这一帧设给引擎的 { sun_energy, moon_energy, ambient_energy, fog_density, fog_sky_affect, fog_r, fog_g, fog_b } 与云带（行星计划 P7）
    // { band_inside, band_under, band_below, band_depth, band_sun, band_diffuse, band_light, sea_height }
    godot::Dictionary get_look() const;
    // 行星计划 P7：船在 p_global 处颠得多厉害（0..1.5）：云带里 1，云底下一点，风暴里（云带里、云底下）再加；云带顶上 0
    double turbulence_at(const godot::Vector3 &p_global);
    // 相机在云带的哪儿（cloud_band_at 的剖面）与此刻云底、海、霾的光（SeaSurface 用）
    const CloudBandState &get_band() const { return band; }
    const UnderLook &get_under_look() const { return under_look; }
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
    // 天空着色器的材质（PlanetGlobe 往上面设行星的参数；没有天空时是空的）
    godot::Ref<godot::ShaderMaterial> get_sky_material() const { return sky_material; }
    // 月亮亮着的份额、相机升过云层多少（行星计划 P2 的行星画法用）
    double get_moon_lit() const { return moon_state.illuminated; }
    double get_above_clouds() const { return above_clouds; }
    void set_space_blend(double p_value) { space_blend = p_value; }

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
