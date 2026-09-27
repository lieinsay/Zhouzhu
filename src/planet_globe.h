#pragma once

#include <memory>
#include <vector>

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include "planet_weather.h"

class Atmosphere;
class PlanetView;
class GroupWorld;
class JobPool;

// 整颗行星（行星计划 P2，docs/PLAN-PLANET.md 第 4.3 节「600 km 到轨道」）：挂在有行星数据的地图里（world_2051.tscn），与 PlanetView、Atmosphere、FarGroups 并列。
// 画法见 game/shaders/planet_sky.gdshaderinc 开头：行星与大气逐像素按视线与同心球面求交来画，不建网格——
//   - 天空着色器（Atmosphere 建的 sky.gdshader）：地平线以下的整颗行星（海、云带壳、贴图岛群、夜里的灯）与大气散射、星空日月；
//   - 云层叠画（本节点建的一张盖满屏幕的片，planet_overlay.gdshader）：相机升过 +2.8 km 以后往下看的天气云层，高空再加一层空气透视。
// 本节点做的：
//   - 后台（World 的任务池，JobKind::PLANET）烘行星贴图的六个面（planet_surface：2048² × 2 张 RGBA8，含 mipmap）、算大气的两张表（planet_sky），
//     做好了在主线程建纹理（Texture2DArray、ImageTexture）；1° 的气候场、风、带界从行星包直接做成小纹理；
//   - 每帧按相机（PlanetView 的双精度位置）设两份材质的参数：相机的局部系到行星系、高、日月（按全局钟在行星系里算，D10）、
//     带界的季节北移、G 定点永暴、锚群的群心、近处由远处岛群网格画的那一圈（贴图岛群在那里淡掉）；
//   - 按高度交叉淡化：20–60 km 天上从原来的天色换成大气散射，30–60 km P1 的云带顶网格淡出、行星淡入；
//   - 相机的地面点离锚群超过 geometry_radius（轨道相机飞远了）：锚群的地面、村子、船、人都藏起来，由行星贴图画它（再飞回来就放回去）；
//   - 行星计划 P5：行星天气场（PlanetView 的 PlanetWeather）的全行星贴图——等经纬 weather_width × weather_height（云量、雨、群的份、风暴），
//     各格的群与权先在后台算一回（静的），之后游戏钟每走 weather_every_h（或拨钟、开发者覆盖变了）后台重烘一回，天空与云层叠画的行星云层读它。
// 没有行星数据（PlanetView 没锚定）就什么都不做，天空着色器的 planet_on 是 0，一切照旧。
class PlanetGlobe : public godot::Node3D {
    GDCLASS(PlanetGlobe, godot::Node3D)

    godot::NodePath planet_path = "../PlanetView";
    godot::NodePath atmosphere_path = "../Atmosphere";
    godot::NodePath map_path = "../World";
    godot::NodePath far_groups_path = "../FarGroups";
    int surface_size = 2048;
    double deck_fade_from = 30000.0, deck_fade_to = 60000.0;
    double atmo_from = 20000.0, atmo_to = 60000.0;
    double geometry_radius = 600000.0;
    double sun_power = 1.0, sky_power = 3.14159;

    PlanetView *view = nullptr;
    Atmosphere *atmosphere = nullptr;
    std::shared_ptr<JobPool> pool; // 场景里共用的（PlanetJobs）；没有就借 map_path 那个岛群的
    godot::ObjectID far_groups_id;
    godot::Ref<godot::ShaderMaterial> sky_mat;
    godot::Ref<godot::ShaderMaterial> overlay_mat;
    godot::Ref<godot::ShaderMaterial> deck_fade_mat;
    godot::MeshInstance3D *overlay = nullptr;
    godot::Ref<godot::Texture2DArray> surface_color, surface_extra;
    godot::Ref<godot::ImageTexture> trans_tex, ms_tex, fields_tex, fields2_tex, bands_tex;
    // 行星天气场的贴图（行星计划 P5）
    int weather_width = 1024, weather_height = 512;
    double weather_every_h = 0.25; // 游戏钟走这么多钟头重烘一回
    std::shared_ptr<const PlanetWeather::BakeGrid> wx_grid;
    std::shared_ptr<PlanetWeather::BakeGrid> wx_grid_building; // 分几段在几条线程上备
    int wx_grid_bands_left = 0;
    uint64_t wx_grid_reach_rev = 0; // 按哪一回的「一群管多远」备的（生存世界粗版来了要重备）
    double wx_grid_started = 0.0;
    bool wx_grid_submitted = false, wx_bake_inflight = false;
    double wx_baked_day = -1e18, wx_bake_day = 0.0;
    uint64_t wx_baked_rev = ~uint64_t(0), wx_bake_rev = 0;
    godot::Ref<godot::ImageTexture> weather_tex;
    std::vector<float> wx_last; // 上回烘的（CPU 这边留一份：测试、工具读某一格）
    double wx_grid_ms = 0.0, wx_bake_ms = 0.0;
    int wx_bakes = 0;
    void update_weather_bake(double p_day);

    struct Bake {
        bool started = false;
        int faces_done = 0;
        std::vector<godot::Ref<godot::Image>> color, extra;
        uint64_t start_usec = 0;
        double wall_ms = 0.0, cpu_ms = 0.0, upload_ms = 0.0, lut_ms = 0.0;
        int groups = 0, stand_ins = 0;
        double land_km2 = 0.0;
        bool uploaded = false;
        bool rebaking = false; // 重烘（生存模式粗版陆续来了）：旧贴图照用，六面都烘好再换
        uint64_t lod_gen = 0; // 按行星包粗版的第几回烘的
        double finished_at = 0.0; // 上回烘好的时刻（秒）
        int rebakes = 0;
    } bake;
    double rebake_every = 20.0; // 行星包的粗版变了，隔这么久（秒）重烘一回贴图
    bool luts_ready = false;
    bool textures_pushed = false;
    std::vector<godot::ObjectID> hidden_nodes;
    bool anchor_hidden = false;
    double blend_planet = 0.0, blend_atmo = 0.0, deck_alpha = 1.0, lateral_m = 0.0, band_shift = 0.0;
    bool overlay_on = false;

    bool setup();
    void build_fields();
    void start_jobs();
    void collect_jobs();
    void build_overlay();
    void push_textures();
    void update_deck();
    void update_anchor_visibility();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void _ready() override;
    void _process(double p_delta) override;

    bool is_surface_ready() const { return bake.uploaded; }
    bool is_ready() const { return bake.uploaded && luts_ready; }
    // { surface_ready, luts_ready, bake_wall_ms, bake_cpu_ms, upload_ms, lut_ms, groups, stand_ins, land_km2, blend_planet, blend_atmo, deck_alpha, overlay, anchor_hidden, lateral_km, band_shift,
    //   weather_ready, weather_bakes, weather_grid_ms, weather_bake_ms, weather_day }
    godot::Dictionary get_state() const;
    // 行星天气场的贴图在某经纬度那一格：(云量, 雨, 群的份, 风暴)；还没烘好是 (−1, −1, −1, −1)
    godot::Color weather_texel(double p_lat, double p_lon) const;

    void set_planet_path(const godot::NodePath &p_value) { planet_path = p_value; }
    godot::NodePath get_planet_path() const { return planet_path; }
    void set_atmosphere_path(const godot::NodePath &p_value) { atmosphere_path = p_value; }
    godot::NodePath get_atmosphere_path() const { return atmosphere_path; }
    void set_map_path(const godot::NodePath &p_value) { map_path = p_value; }
    godot::NodePath get_map_path() const { return map_path; }
    void set_far_groups_path(const godot::NodePath &p_value) { far_groups_path = p_value; }
    godot::NodePath get_far_groups_path() const { return far_groups_path; }
    void set_surface_size(int p_value) { surface_size = p_value; }
    int get_surface_size() const { return surface_size; }
    void set_sun_power(double p_value) { sun_power = p_value; }
    double get_sun_power() const { return sun_power; }
    void set_sky_power(double p_value) { sky_power = p_value; }
    double get_sky_power() const { return sky_power; }
};
