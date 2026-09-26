#include "shared_looks.h"

#include <cmath>

#include "building_mesh.h"

using namespace godot;

Ref<StandardMaterial3D> SharedLooks::part_material(PartMaterial p_material) {
    Ref<StandardMaterial3D> &m = parts[size_t(p_material)];
    if (m.is_null()) {
        float r = 1, g = 1, b = 1, rough = 1;
        part_material_color(p_material, r, g, b, rough);
        m.instantiate();
        m->set_albedo(Color(r, g, b));
        m->set_roughness(rough);
    }
    return m;
}

Ref<StandardMaterial3D> SharedLooks::proxy_material() {
    if (proxy.is_null()) {
        proxy.instantiate();
        proxy->set_roughness(0.95f);
        proxy->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        proxy->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true); // 顶点色按 sRGB 调的（与细模的材质同色）
    }
    return proxy;
}

Ref<StandardMaterial3D> SharedLooks::plain_material(const Color &p_albedo, float p_roughness) {
    const auto q = [](float v) { return uint64_t(std::lround(std::fmin(1.0f, std::fmax(0.0f, v)) * 1023.0f)); };
    const uint64_t key = q(p_albedo.r) | (q(p_albedo.g) << 10) | (q(p_albedo.b) << 20) | (q(p_albedo.a) << 30) | (q(p_roughness) << 40);
    Ref<StandardMaterial3D> &m = plain[key];
    if (m.is_null()) {
        m.instantiate();
        m->set_albedo(p_albedo);
        m->set_roughness(p_roughness);
    }
    return m;
}

Ref<Mesh> SharedLooks::mesh(const std::string &p_key, const std::function<Ref<Mesh>()> &p_make) {
    Ref<Mesh> &m = meshes[p_key];
    if (m.is_null()) {
        m = p_make();
    }
    return m;
}
