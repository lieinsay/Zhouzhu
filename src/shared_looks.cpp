#include "shared_looks.h"

#include <cmath>

#include "building_mesh.h"
#include "curved_material.h"

using namespace godot;

Ref<Material> SharedLooks::part_material(PartMaterial p_material) {
    Ref<Material> &m = parts[size_t(p_material)];
    if (m.is_null()) {
        float r = 1, g = 1, b = 1, rough = 1;
        part_material_color(p_material, r, g, b, rough);
        m = curved_material::solid(Color(r, g, b), rough);
    }
    return m;
}

Ref<Material> SharedLooks::proxy_material() {
    if (proxy.is_null()) {
        proxy = curved_material::vertex_colored(0.95f); // 顶点色按 sRGB 调的（与细模的材质同色）
    }
    return proxy;
}

Ref<Material> SharedLooks::plain_material(const Color &p_albedo, float p_roughness) {
    const auto q = [](float v) { return uint64_t(std::lround(std::fmin(1.0f, std::fmax(0.0f, v)) * 1023.0f)); };
    const uint64_t key = q(p_albedo.r) | (q(p_albedo.g) << 10) | (q(p_albedo.b) << 20) | (q(p_albedo.a) << 30) | (q(p_roughness) << 40);
    Ref<Material> &m = plain[key];
    if (m.is_null()) {
        m = curved_material::solid(p_albedo, p_roughness);
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
