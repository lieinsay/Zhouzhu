#include "scatter_meshes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "crops.h"

using namespace godot;

namespace scatter_meshes {

namespace {

constexpr double TAU = 6.283185307179586;

// 平涂三角面：每个面三个独立顶点、面法线
struct Builder {
    PackedVector3Array v, n;
    PackedColorArray c;

    // 正面顺时针（Godot 约定）：从外面看 a → b → c 顺时针
    void tri(const Vector3 &a, const Vector3 &b, const Vector3 &p_c, const Color &col) {
        Vector3 normal = (p_c - a).cross(b - a);
        if (normal.length_squared() < 1e-12f) {
            return;
        }
        normal.normalize();
        v.push_back(a);
        v.push_back(b);
        v.push_back(p_c);
        for (int k = 0; k < 3; k++) {
            n.push_back(normal);
            c.push_back(col);
        }
    }

    // 一段锥台（侧面，外法线朝外）：底 y0 半径 r0，顶 y1 半径 r1，sides 面；r1 = 0 是尖锥；cap 封顶
    void frustum(const Vector3 &base, double y0, double r0, double y1, double r1, int sides, const Color &side, bool cap = false, const Color &top = Color()) {
        for (int k = 0; k < sides; k++) {
            const double a0 = TAU * k / sides, a1 = TAU * (k + 1) / sides;
            const Vector3 b0 = base + Vector3(real_t(r0 * std::cos(a0)), real_t(y0), real_t(r0 * std::sin(a0)));
            const Vector3 b1 = base + Vector3(real_t(r0 * std::cos(a1)), real_t(y0), real_t(r0 * std::sin(a1)));
            const Vector3 t0 = base + Vector3(real_t(r1 * std::cos(a0)), real_t(y1), real_t(r1 * std::sin(a0)));
            const Vector3 t1 = base + Vector3(real_t(r1 * std::cos(a1)), real_t(y1), real_t(r1 * std::sin(a1)));
            tri(b0, b1, t0, side);
            if (r1 > 1e-6) {
                tri(b1, t1, t0, side);
                if (cap) {
                    tri(base + Vector3(0, real_t(y1), 0), t0, t1, top);
                }
            }
        }
    }

    // 一团：二十面体（p_subdiv 细分一次成 80 面）按 (rx, ry, rz) 拉伸，顶点按 p_seed 抖 p_jitter 倍；颜色上浅下深
    void blob(const Vector3 &center, const Vector3 &radius, int p_subdiv, double p_jitter, uint32_t p_seed, const Color &col, double p_shade = 0.18) {
        const double t = (1.0 + std::sqrt(5.0)) / 2.0;
        std::vector<Vector3> pts = {
            Vector3(-1, real_t(t), 0), Vector3(1, real_t(t), 0), Vector3(-1, real_t(-t), 0), Vector3(1, real_t(-t), 0),
            Vector3(0, -1, real_t(t)), Vector3(0, 1, real_t(t)), Vector3(0, -1, real_t(-t)), Vector3(0, 1, real_t(-t)),
            Vector3(real_t(t), 0, -1), Vector3(real_t(t), 0, 1), Vector3(real_t(-t), 0, -1), Vector3(real_t(-t), 0, 1),
        };
        for (Vector3 &p : pts) {
            p.normalize();
        }
        std::vector<std::array<int, 3>> faces = {
            { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
            { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 },
        };
        for (int s = 0; s < p_subdiv; s++) {
            std::map<std::pair<int, int>, int> mids;
            auto mid = [&](int a, int b) {
                const auto key = std::make_pair(std::min(a, b), std::max(a, b));
                const auto found = mids.find(key);
                if (found != mids.end()) {
                    return found->second;
                }
                pts.push_back((pts[size_t(a)] + pts[size_t(b)]).normalized());
                mids[key] = int(pts.size()) - 1;
                return int(pts.size()) - 1;
            };
            std::vector<std::array<int, 3>> next;
            for (const auto &f : faces) {
                const int ab = mid(f[0], f[1]), bc = mid(f[1], f[2]), ca = mid(f[2], f[0]);
                next.push_back({ f[0], ab, ca });
                next.push_back({ f[1], bc, ab });
                next.push_back({ f[2], ca, bc });
                next.push_back({ ab, bc, ca });
            }
            faces.swap(next);
        }
        uint32_t h = p_seed * 2654435761u + 1u;
        for (Vector3 &p : pts) {
            h ^= h << 13;
            h ^= h >> 17;
            h ^= h << 5;
            const double j = 1.0 + p_jitter * ((h % 1000) / 500.0 - 1.0);
            p = Vector3(real_t(p.x * radius.x * j), real_t(p.y * radius.y * j), real_t(p.z * radius.z * j)) + center;
        }
        for (const auto &f : faces) {
            const Vector3 &a = pts[size_t(f[0])], &b = pts[size_t(f[1])], &cc = pts[size_t(f[2])];
            const double up = ((a.y + b.y + cc.y) / 3.0 - center.y) / std::max(0.01f, radius.y); // −1 底 … 1 顶
            const Color shade = col.darkened(float(p_shade * (1.0 - up) * 0.5));
            tri(a, cc, b, shade); // 二十面体的面是逆时针的：反过来
        }
    }

    Ref<ArrayMesh> build(const Ref<StandardMaterial3D> &p_material) const {
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        if (v.is_empty()) {
            return mesh;
        }
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = v;
        arrays[Mesh::ARRAY_NORMAL] = n;
        arrays[Mesh::ARRAY_COLOR] = c;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_material(0, p_material);
        return mesh;
    }
};

const Color BARK(0.36f, 0.27f, 0.18f);
const Color LEAF(0.27f, 0.45f, 0.2f);
const Color NEEDLE(0.17f, 0.34f, 0.2f);

Ref<ArrayMesh> make(ScatterLook p_look, const Ref<StandardMaterial3D> &p_material) {
    Builder b;
    switch (p_look) {
        case ScatterLook::BROADLEAF: {
            b.frustum(Vector3(), -0.3, 0.32, 3.4, 0.2, 6, BARK);
            b.blob(Vector3(0, 4.9f, 0), Vector3(2.5f, 2.0f, 2.5f), 1, 0.12, 11, LEAF);
            b.blob(Vector3(1.0f, 5.9f, 0.5f), Vector3(1.8f, 1.5f, 1.8f), 0, 0.1, 12, LEAF.lightened(0.06f));
            b.blob(Vector3(-0.9f, 5.6f, -0.6f), Vector3(1.9f, 1.6f, 1.9f), 0, 0.1, 13, LEAF.darkened(0.05f));
        } break;
        case ScatterLook::CONIFER: {
            b.frustum(Vector3(), -0.3, 0.3, 2.4, 0.18, 6, BARK);
            b.frustum(Vector3(), 1.6, 2.3, 5.4, 0.0, 8, NEEDLE);
            b.frustum(Vector3(), 3.6, 1.8, 7.2, 0.0, 8, NEEDLE.lightened(0.05f));
            b.frustum(Vector3(), 5.6, 1.2, 9.0, 0.0, 8, NEEDLE.lightened(0.1f));
        } break;
        case ScatterLook::SHRUB: {
            const Color leaf(0.33f, 0.44f, 0.2f);
            b.blob(Vector3(0, 0.55f, 0), Vector3(1.0f, 0.75f, 1.0f), 0, 0.15, 21, leaf);
            b.blob(Vector3(0.5f, 0.45f, 0.35f), Vector3(0.7f, 0.55f, 0.7f), 0, 0.15, 22, leaf.lightened(0.07f));
            b.frustum(Vector3(), -0.1, 0.08, 0.4, 0.05, 4, BARK);
        } break;
        case ScatterLook::REEDS: {
            // 一簇细秆：三棱的细锥，高低不一
            for (int k = 0; k < 12; k++) {
                const double a = TAU * k / 12.0 + 0.4 * std::sin(k * 1.7), r = 0.25 + 0.55 * std::fmod(k * 0.618, 1.0);
                const double h = 1.5 + 0.9 * std::fmod(k * 0.382 + 0.2, 1.0);
                const Color stalk = Color(0.62f, 0.6f, 0.36f).lerp(Color(0.4f, 0.52f, 0.28f), float(std::fmod(k * 0.27, 1.0)));
                b.frustum(Vector3(real_t(r * std::cos(a)), 0, real_t(r * std::sin(a))), -0.1, 0.05, h, 0.0, 3, stalk);
            }
        } break;
        case ScatterLook::BOULDER:
            b.blob(Vector3(0, 0.25f, 0), Vector3(0.85f, 0.6f, 0.75f), 1, 0.18, 31, Color(0.58f, 0.57f, 0.55f), 0.3);
            break;
        case ScatterLook::FLOATSTONE:
            b.blob(Vector3(0, 0.45f, 0), Vector3(0.75f, 0.9f, 0.7f), 0, 0.22, 41, Color(0.84f, 0.8f, 0.94f), 0.25);
            b.blob(Vector3(0.6f, 0.2f, 0.3f), Vector3(0.45f, 0.4f, 0.4f), 0, 0.2, 42, Color(0.78f, 0.72f, 0.9f), 0.25);
            break;
        case ScatterLook::CLAY_BANK:
            b.blob(Vector3(0, -0.1f, 0), Vector3(1.3f, 0.45f, 1.1f), 1, 0.12, 51, Color(0.68f, 0.41f, 0.27f), 0.25);
            break;
        case ScatterLook::GRAVEL_HEAP:
            b.frustum(Vector3(), -0.1, 1.1, 0.55, 0.25, 9, Color(0.76f, 0.72f, 0.63f), true, Color(0.8f, 0.77f, 0.68f));
            b.blob(Vector3(0.9f, 0.05f, 0.4f), Vector3(0.25f, 0.18f, 0.22f), 0, 0.2, 61, Color(0.6f, 0.58f, 0.55f));
            b.blob(Vector3(-0.7f, 0.05f, -0.6f), Vector3(0.2f, 0.15f, 0.2f), 0, 0.2, 62, Color(0.66f, 0.63f, 0.58f));
            break;
        case ScatterLook::PLACER:
            b.frustum(Vector3(), -0.05, 1.0, 0.06, 0.85, 10, Color(0.8f, 0.72f, 0.5f), true, Color(0.88f, 0.78f, 0.46f));
            for (int k = 0; k < 5; k++) {
                const double a = TAU * k / 5.0 + 0.3;
                b.blob(Vector3(real_t(0.5 * std::cos(a)), 0.08f, real_t(0.5 * std::sin(a))), Vector3(0.06f, 0.03f, 0.06f), 0, 0.0, 70 + k, Color(1.0f, 0.86f, 0.3f), 0.0);
            }
            break;
        case ScatterLook::ORE_ROCK: {
            // 灰石头上带几块矿色（顶点色白的那几块乘实例色，灰的那几块乘出来暗）
            b.blob(Vector3(0, 0.2f, 0), Vector3(0.7f, 0.5f, 0.6f), 1, 0.2, 81, Color(0.62f, 0.62f, 0.62f), 0.3);
            b.blob(Vector3(0.35f, 0.4f, 0.15f), Vector3(0.3f, 0.22f, 0.28f), 0, 0.2, 82, Color(1, 1, 1), 0.1);
            b.blob(Vector3(-0.3f, 0.35f, -0.2f), Vector3(0.25f, 0.2f, 0.25f), 0, 0.2, 83, Color(1, 1, 1), 0.1);
        } break;
        case ScatterLook::CRUST:
            // 白的，颜色全靠实例色（硫磺黄、鸟粪白）
            b.frustum(Vector3(), -0.05, 1.0, 0.05, 0.9, 7, Color(0.9f, 0.9f, 0.9f), true, Color(1, 1, 1));
            b.frustum(Vector3(0.7f, 0, 0.4f), -0.05, 0.5, 0.08, 0.4, 6, Color(0.9f, 0.9f, 0.9f), true, Color(1, 1, 1));
            break;
    }
    return b.build(p_material);
}

Ref<ArrayMesh> make_stump(bool p_tree, const Ref<StandardMaterial3D> &p_material) {
    Builder b;
    if (p_tree) {
        b.frustum(Vector3(), -0.3, 0.36, 0.45, 0.32, 6, BARK, true, Color(0.72f, 0.58f, 0.4f));
    } else {
        for (int k = 0; k < 4; k++) {
            const double a = TAU * k / 4.0;
            b.frustum(Vector3(real_t(0.3 * std::cos(a)), 0, real_t(0.3 * std::sin(a))), -0.05, 0.05, 0.2, 0.04, 4, BARK, true, Color(0.6f, 0.5f, 0.36f));
        }
    }
    return b.build(p_material);
}

uint64_t hash64(uint64_t p_x) {
    p_x ^= p_x >> 33;
    p_x *= 0xFF51AFD7ED558CCDull;
    p_x ^= p_x >> 33;
    p_x *= 0xC4CEB9FE1A85EC53ull;
    return p_x ^ (p_x >> 33);
}

} // namespace

} // namespace scatter_meshes

Ref<StandardMaterial3D> ScatterMeshes::material() {
    if (shared.is_null()) {
        shared.instantiate();
        shared->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        shared->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
        shared->set_roughness(0.95f);
    }
    return shared;
}

Ref<ArrayMesh> ScatterMeshes::mesh(ScatterLook p_look) {
    Ref<ArrayMesh> &m = up[int(p_look)];
    if (m.is_null()) {
        m = scatter_meshes::make(p_look, material());
    }
    return m;
}

Ref<ArrayMesh> ScatterMeshes::felled_mesh(ScatterLook p_look) {
    const bool tree = p_look == ScatterLook::BROADLEAF || p_look == ScatterLook::CONIFER;
    if (!tree && p_look != ScatterLook::SHRUB && p_look != ScatterLook::REEDS) {
        return Ref<ArrayMesh>();
    }
    Ref<ArrayMesh> &m = felled[tree ? 0 : 1];
    if (m.is_null()) {
        m = scatter_meshes::make_stump(tree, material());
    }
    return m;
}

namespace scatter_meshes {

Color instance_color(ScatterLook p_look, uint8_t p_sub, uint64_t p_key) {
    const uint64_t h = hash64(p_key);
    const float jitter = float((h % 1000) / 1000.0 - 0.5); // ±0.5
    if (p_look == ScatterLook::ORE_ROCK || p_look == ScatterLook::CRUST) {
        float r = 1, g = 1, b = 1;
        catalog::item_color(deposits::scatter_product(p_look, p_sub), r, g, b);
        return Color(r, g, b).lightened(0.08f * jitter + 0.04f);
    }
    // 深浅抖一成多；树冠另带一点黄绿 / 蓝绿
    const float tint = float(((h >> 20) % 1000) / 1000.0 - 0.5);
    const float base = 0.88f + 0.24f * jitter;
    return Color(base, std::min(1.0f, base + 0.06f * tint), std::min(1.0f, base - 0.06f * tint));
}

} // namespace scatter_meshes
