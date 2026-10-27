#include "curved_material.h"
#include "art_surface.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/texture2d.hpp>

using namespace godot;

namespace curved_material {

namespace {

constexpr const char *ONE_SIDED = "res://shaders/curved_standard.gdshader";
constexpr const char *TWO_SIDED = "res://shaders/curved_standard_two_sided.gdshader";

// ResourceLoader 自己按路径缓存着色器：一样的路径拿到同一份
Ref<Shader> shader(bool p_two_sided) {
    return ResourceLoader::get_singleton()->load(p_two_sided ? TWO_SIDED : ONE_SIDED);
}

Ref<Material> make(const Color &p_albedo, float p_roughness, bool p_two_sided, bool p_vertex_color, const Color &p_emission, float p_energy) {
    const Ref<Shader> s = shader(p_two_sided);
    if (s.is_valid()) {
        Ref<ShaderMaterial> m;
        m.instantiate();
        m->set_shader(s);
        m->set_shader_parameter("albedo", p_albedo);
        m->set_shader_parameter("roughness", p_roughness);
        if (p_vertex_color) {
            m->set_shader_parameter("vertex_color", true);
        }
        if (p_energy > 0.0f) {
            m->set_shader_parameter("emission", p_emission);
            m->set_shader_parameter("emission_energy", p_energy);
        }
        return m;
    }
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_albedo(p_albedo);
    m->set_roughness(p_roughness);
    if (p_two_sided) {
        m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    }
    if (p_vertex_color) {
        m->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        m->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
    }
    if (p_energy > 0.0f) {
        m->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
        m->set_emission(p_emission);
        m->set_emission_energy_multiplier(p_energy);
    }
    return m;
}

} // namespace

Ref<Material> solid(const Color &p_albedo, float p_roughness, bool p_two_sided) {
    return make(p_albedo, p_roughness, p_two_sided, false, Color(), 0.0f);
}

Ref<Material> surface(const Color &p_albedo, float p_roughness, const String &p_asset, float p_metallic, bool p_two_sided) {
    Ref<Material> result = make(p_albedo, p_roughness, p_two_sided, false, Color(), 0.0f);
    Ref<ShaderMaterial> material = result;
    if (material.is_null()) return result;
    material->set_meta("art_asset",p_asset);
    ArtSurface::configure(material,"surface",p_asset);
    material->set_shader_parameter("surface_uv_metres",true);
    if(p_asset==String("material.wood")) ArtSurface::configure(material,"surface_end","material.wood_endgrain");
    material->set_shader_parameter("metallic", p_metallic);
    return result;
}

Ref<Material> vertex_colored(float p_roughness) {
    return make(Color(1, 1, 1), p_roughness, false, true, Color(), 0.0f);
}

Ref<Material> glowing(const Color &p_albedo, float p_roughness, const Color &p_emission, float p_energy) {
    return make(p_albedo, p_roughness, false, false, p_emission, p_energy);
}

} // namespace curved_material
