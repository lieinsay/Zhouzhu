#pragma once

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "sea_state.h"

class Atmosphere;
class PlanetView;

// 云带之下的两张面（行星计划 P7，docs/PLAN-PLANET.md D13、D14、第五节 P7）：PlanetGlobe 建、每帧推一下——
//   - 云底（y = −5000 m）：朝下的同心环网格（半径 1,000 km），game/shaders/cloud_base.gdshader——一层压着的灰，只受环境光，晨昏朝太阳那一侧染暖；
//   - 海面（y = −5600 m）：近密远疏的同心环网格（半径 300 km，最里一圈 1 m），game/shaders/sea.gdshader——
//     浪按天与光在相机处取的风（src/sea_state.* 的 sea_waves：行星天气场的风，风暴另加），Gerstner 的相位按帧累加（风变了浪长跟着变也不乱跳），
//     两层法线贴图（bake_wave_normals）顺风平移，白礁按行星包（PlanetGlobe 烘的 planet_reefs）。
// 两张都跟着相机的平面坐标走（海面按 2 m 取整，免得浪「游」），顶点走 planet_curve 弯曲；相机在云带顶以上 60 m 起藏起来（上面有不透明的云带顶挡着）。
// 行星的参数（相机局部系到行星系、相机高、一像素多大、白礁的贴图）由 PlanetGlobe 与天空着色器一起设（get_materials）。
class SeaSurface : public godot::Node3D {
    GDCLASS(SeaSurface, godot::Node3D)

    godot::MeshInstance3D *sea = nullptr;
    godot::MeshInstance3D *base = nullptr;
    godot::Ref<godot::ShaderMaterial> sea_mat, base_mat;
    godot::Ref<godot::ImageTexture> normals;
    bool built = false;
    bool shown = false;
    double show_below = 60.0; // 相机在云带顶以上这么多以内才画
    double snap_m = 2.0;
    double phase[4] = { 0.0, 0.0, 0.0, 0.0 };
    double scroll[4] = { 0.0, 0.0, 0.0, 0.0 };
    SeaWaves waves;
    SeaWaveRules wave_rules;
    double wind_x = 1.0, wind_z = 0.0, wind_ms = 0.0, storm = 0.0;

    void build_meshes();

protected:
    static void _bind_methods();

public:
    // 建网格、材质、法线贴图（读不了着色器返回 false，报错一次）
    bool build();
    // 每帧（PlanetGlobe 的 _process 末尾）：摆到相机下、藏 / 显、按天与光的风推浪、给云底此刻的光
    void update(const PlanetView *p_view, const Atmosphere *p_atmosphere, double p_delta);
    bool is_built() const { return built; }
    bool is_shown() const { return shown; }
    // 两份材质（PlanetGlobe 往上设行星的参数）；没建好是空的
    godot::Ref<godot::ShaderMaterial> get_sea_material() const { return sea_mat; }
    godot::Ref<godot::ShaderMaterial> get_base_material() const { return base_mat; }
    // { shown, wave_height, wave_length, wave_period, foam, chop, wind_ms, storm }
    godot::Dictionary get_state() const;
};
