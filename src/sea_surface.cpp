#include "sea_surface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "atmosphere.h"
#include "planet_frame.h"
#include "planet_view.h"

using namespace godot;

namespace {

constexpr const char *SEA_SHADER = "res://shaders/sea.gdshader";
constexpr const char *BASE_SHADER = "res://shaders/cloud_base.gdshader";
constexpr double PI = 3.14159265358979323846;
constexpr double G = 9.81;
constexpr int SEA_SEGMENTS = 160;
constexpr double SEA_RADIUS = 300000.0;
constexpr int BASE_SEGMENTS = 96;
constexpr double BASE_RADIUS = 1000000.0;
constexpr double PLANET_R = PLANET_DEFAULT_R; // 包围盒往下放多少用（弯曲下沉 d² / 2R）
// 四道 Gerstner 波的波长（相对主浪）：与 sea.gdshader 的 WAVE_LENGTH 同
constexpr double WAVE_LENGTH[4] = { 1.0, 0.63, 0.41, 0.27 };
constexpr int NORMAL_SIZE = 256;
constexpr uint32_t NORMAL_SEED = 7;

// 同心环：中心一点，往外一圈圈到 radii.back()。p_down：从下往上看（绕向反过来、法线朝下）
Ref<ArrayMesh> ring_mesh(const std::vector<double> &p_radii, int p_segments, bool p_down) {
    PackedVector3Array vertices, normals;
    PackedInt32Array indices;
    const Vector3 n = p_down ? Vector3(0, -1, 0) : Vector3(0, 1, 0);
    vertices.push_back(Vector3());
    normals.push_back(n);
    for (double r : p_radii) {
        for (int k = 0; k < p_segments; k++) {
            const double a = 2.0 * PI * k / p_segments;
            vertices.push_back(Vector3(real_t(r * std::cos(a)), 0.0f, real_t(r * std::sin(a))));
            normals.push_back(n);
        }
    }
    auto ring = [p_segments](int i, int k) { return 1 + i * p_segments + (k % p_segments); };
    auto tri = [&](int a, int b, int c) {
        indices.push_back(a);
        indices.push_back(p_down ? c : b);
        indices.push_back(p_down ? b : c);
    };
    for (int k = 0; k < p_segments; k++) {
        tri(0, ring(0, k), ring(0, k + 1)); // 从上往下看（x 右、z 下）顺时针，同云带顶
    }
    for (int i = 0; i + 1 < int(p_radii.size()); i++) {
        for (int k = 0; k < p_segments; k++) {
            tri(ring(i, k), ring(i + 1, k), ring(i + 1, k + 1));
            tri(ring(i, k), ring(i + 1, k + 1), ring(i, k + 1));
        }
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_INDEX] = indices;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    // 顶点在着色器里按真球面下弯、远处往相机压：包围盒往下放足，永远画
    const double outer = p_radii.back();
    const real_t R = real_t(outer * 1.05);
    const real_t drop = real_t(outer * outer / (2.0 * PLANET_R) * 1.2 + 2000.0);
    mesh->set_custom_aabb(AABB(Vector3(-R, -drop, -R), Vector3(2 * R, drop + 200.0f, 2 * R)));
    return mesh;
}

MeshInstance3D *mesh_node(const char *p_name, const Ref<ArrayMesh> &p_mesh, const Ref<Material> &p_material) {
    p_mesh->surface_set_material(0, p_material);
    MeshInstance3D *node = memnew(MeshInstance3D);
    node->set_name(p_name);
    node->set_mesh(p_mesh);
    node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    node->set_as_top_level(true);
    node->set_visible(false);
    return node;
}

} // namespace

void SeaSurface::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_state"), &SeaSurface::get_state);
    ClassDB::bind_method(D_METHOD("is_shown"), &SeaSurface::is_shown);
}

bool SeaSurface::build() {
    if (built) {
        return true;
    }
    const Ref<Shader> sea_shader = ResourceLoader::get_singleton()->load(SEA_SHADER);
    const Ref<Shader> base_shader = ResourceLoader::get_singleton()->load(BASE_SHADER);
    if (sea_shader.is_null() || base_shader.is_null()) {
        ERR_PRINT(String(U"SeaSurface：读不了海面 / 云底的着色器（") + SEA_SHADER + String(U"、") + BASE_SHADER + String(U"），降到云带以下看不见海"));
        return false;
    }
    sea_mat.instantiate();
    sea_mat->set_shader(sea_shader);
    base_mat.instantiate();
    base_mat->set_shader(base_shader);
    // 海浪的法线贴图（可平铺，带 mipmap：远处自己平下去）
    std::vector<uint8_t> rgba;
    bake_wave_normals(NORMAL_SIZE, NORMAL_SEED, rgba);
    PackedByteArray bytes;
    bytes.resize(int64_t(rgba.size()));
    std::memcpy(bytes.ptrw(), rgba.data(), rgba.size());
    Ref<Image> image = Image::create_from_data(NORMAL_SIZE, NORMAL_SIZE, false, Image::FORMAT_RGBA8, bytes);
    image->generate_mipmaps();
    normals = ImageTexture::create_from_image(image);
    sea_mat->set_shader_parameter("sea_normals", normals);
    build_meshes();
    built = true;
    return true;
}

void SeaSurface::build_meshes() {
    // 海面：近处一米一圈、往外越来越疏（500 m 内每圈 ×1.06，20 km 内 ×1.1，再往外 ×1.2），到 300 km
    std::vector<double> sea_radii;
    for (double r = 1.0; r < SEA_RADIUS; r *= r < 500.0 ? 1.06 : (r < 20000.0 ? 1.1 : 1.2)) {
        sea_radii.push_back(r);
    }
    sea_radii.push_back(SEA_RADIUS);
    // 云底：同云带顶（最里 60 m，每圈 ×1.16），朝下
    std::vector<double> base_radii;
    for (double r = 60.0; r < BASE_RADIUS; r *= 1.16) {
        base_radii.push_back(r);
    }
    base_radii.push_back(BASE_RADIUS);
    sea = mesh_node("SeaSurface", ring_mesh(sea_radii, SEA_SEGMENTS, false), sea_mat);
    base = mesh_node("CloudBase", ring_mesh(base_radii, BASE_SEGMENTS, true), base_mat);
    add_child(sea);
    add_child(base);
}

void SeaSurface::update(const PlanetView *p_view, const Atmosphere *p_atmosphere, double p_delta) {
    if (!built || !p_view || !p_atmosphere) {
        return;
    }
    const PlanetVec cam = p_view->camera_flat();
    const bool want = std::isfinite(cam.y) && cam.y < VerticalDatum::CLOUD_TOP_Y + show_below;
    if (want != shown) {
        sea->set_visible(want);
        base->set_visible(want);
        shown = want;
    }
    // 浪照样按帧推（藏着也推：降下来那一刻相位是连着的）
    const Atmosphere::UnderLook &u = p_atmosphere->get_under_look();
    wind_ms = std::hypot(u.wind_x, u.wind_z);
    if (wind_ms > 0.05) {
        wind_x = u.wind_x / wind_ms;
        wind_z = u.wind_z / wind_ms;
    }
    storm = u.storm;
    waves = sea_waves(wind_ms, storm, wave_rules);
    const double dt = std::isfinite(p_delta) ? std::clamp(p_delta, 0.0, 0.5) : 0.0;
    for (int i = 0; i < 4; i++) {
        const double k = 2.0 * PI / (waves.length_m * WAVE_LENGTH[i]);
        phase[i] = std::fmod(phase[i] + std::sqrt(G * k) * dt, 2.0 * PI);
    }
    // 两层法线贴图顺风平移（贴图单位：一层 38 m、一层 13 m 一铺）
    const double s1 = (1.2 + 0.06 * wind_ms) * dt / 38.0, s2 = (0.8 + 0.04 * wind_ms) * dt / 13.0;
    scroll[0] = std::fmod(scroll[0] - s1, 1.0);
    scroll[1] = std::fmod(scroll[1] + 0.13 * s1, 1.0);
    scroll[2] = std::fmod(scroll[2] - s2, 1.0);
    scroll[3] = std::fmod(scroll[3] - 0.21 * s2, 1.0);
    if (!shown) {
        return;
    }
    const double sx = std::round(cam.x / snap_m) * snap_m, sz = std::round(cam.z / snap_m) * snap_m;
    sea->set_global_position(Vector3(real_t(sx), real_t(VerticalDatum::SEA_Y), real_t(sz)));
    base->set_global_position(Vector3(real_t(cam.x), real_t(VerticalDatum::CLOUD_BASE_Y), real_t(cam.z)));
    sea_mat->set_shader_parameter("sea_wave", Vector4(real_t(waves.height_m), real_t(waves.length_m), real_t(waves.foam), real_t(waves.chop)));
    sea_mat->set_shader_parameter("sea_wind", Vector2(real_t(wind_x), real_t(wind_z)));
    sea_mat->set_shader_parameter("sea_phase", Vector4(real_t(phase[0]), real_t(phase[1]), real_t(phase[2]), real_t(phase[3])));
    sea_mat->set_shader_parameter("sea_scroll", Vector4(real_t(scroll[0]), real_t(scroll[1]), real_t(scroll[2]), real_t(scroll[3])));
    base_mat->set_shader_parameter("sun_visual", u.sun_visual);
    base_mat->set_shader_parameter("dusk", u.dusk);
    base_mat->set_shader_parameter("under_base_color", u.base);
    base_mat->set_shader_parameter("rain", std::clamp(u.precip * 1.3, 0.0, 1.0));
    base_mat->set_shader_parameter("flash", u.flash);
}

Dictionary SeaSurface::get_state() const {
    Dictionary d;
    d["shown"] = shown;
    d["wave_height"] = waves.height_m;
    d["wave_length"] = waves.length_m;
    d["wave_period"] = waves.period_s;
    d["foam"] = waves.foam;
    d["chop"] = waves.chop;
    d["wind_ms"] = wind_ms;
    d["storm"] = storm;
    return d;
}
