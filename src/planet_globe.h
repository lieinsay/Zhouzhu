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
//   - 相机的地面点离锚群超过 geometry_radius（轨道相机飞远了）：锚群的地面、村子、船、人都藏起来，由行星贴图画它（再飞回来就放回去）。
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

    struct Bake {
        bool started = false;
        int faces_done = 0;
        std::vector<godot::Ref<godot::Image>> color, extra;
        uint64_t start_usec = 0;
        double wall_ms = 0.0, cpu_ms = 0.0, upload_ms = 0.0, lut_ms = 0.0;
        int groups = 0, stand_ins = 0;
        double land_km2 = 0.0;
        bool uploaded = false;
    } bake;
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
    // { surface_ready, luts_ready, bake_wall_ms, bake_cpu_ms, upload_ms, lut_ms, groups, stand_ins, land_km2, blend_planet, blend_atmo, deck_alpha, overlay, anchor_hidden, lateral_km, band_shift }
    godot::Dictionary get_state() const;

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
