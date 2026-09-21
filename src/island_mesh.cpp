#include "island_mesh.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace {

const Color GRASS(0.30f, 0.46f, 0.20f);
const Color GRASS_HIGH(0.47f, 0.52f, 0.30f);
const Color ROCK(0.46f, 0.43f, 0.39f);
const Color EARTH(0.56f, 0.48f, 0.36f); // 停机坪：踩实的土
const Color CLIFF_TOP(0.40f, 0.35f, 0.30f);
const Color CLIFF_LOW(0.25f, 0.22f, 0.21f);
const Color UNDERSIDE(0.20f, 0.18f, 0.18f);

struct Builder {
    IslandMeshData data;

    int add_vertex(const Vector3 &p_pos, const Vector3 &p_normal, const Color &p_color) {
        data.vertices.push_back(p_pos);
        data.normals.push_back(p_normal);
        data.colors.push_back(p_color);
        return int(data.vertices.size()) - 1;
    }

    // a → b → c 须是从正面看的顺时针
    void add_triangle(int p_a, int p_b, int p_c) {
        data.indices.push_back(p_a);
        data.indices.push_back(p_b);
        data.indices.push_back(p_c);
        data.collision_faces.push_back(data.vertices[p_a]);
        data.collision_faces.push_back(data.vertices[p_b]);
        data.collision_faces.push_back(data.vertices[p_c]);
    }
};

Vector3 top_point(const IslandField &f, int i, int j) {
    return Vector3(real_t(f.coord(j)), f.height[f.index(i, j)], real_t(f.coord(i)));
}

Vector3 bottom_point(const IslandField &f, int i, int j) {
    return Vector3(real_t(f.coord(j)), f.bottom[f.index(i, j)], real_t(f.coord(i)));
}

// 中心差分出坡面梯度；邻居不在岛上就用自己
Vector3 surface_normal(const IslandField &f, const std::vector<float> &p_y, int i, int j, bool p_down) {
    auto at = [&](int a, int b) { return f.in_mask(a, b) ? p_y[f.index(a, b)] : p_y[f.index(i, j)]; };
    const real_t span = real_t(2.0 * f.cell_m);
    const real_t dx = (at(i, j + 1) - at(i, j - 1)) / span;
    const real_t dz = (at(i + 1, j) - at(i - 1, j)) / span;
    const Vector3 up = Vector3(-dx, 1.0f, -dz).normalized();
    return p_down ? -up : up;
}

} // namespace

IslandMeshData build_island_mesh(const IslandField &f) {
    Builder b;
    const int n = f.n;
    const real_t relief = real_t(std::max(1.0, f.peak_m - f.rim_m));

    // 顶面与岛底各一套按节点共享的顶点
    std::vector<int> top_id(size_t(n) * n, -1), bottom_id(size_t(n) * n, -1);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (!f.in_mask(i, j)) {
                continue;
            }
            const Vector3 pos = top_point(f, i, j);
            const Vector3 normal = surface_normal(f, f.height, i, j, false);
            const real_t steep = std::clamp((0.86f - normal.y) / 0.16f, 0.0f, 1.0f); // 约 30° 起露岩，45° 全岩
            const real_t high = std::clamp((pos.y - real_t(f.rim_m)) / relief, 0.0f, 1.0f);
            Color color = GRASS.lerp(GRASS_HIGH, high).lerp(ROCK, steep);
            const double d_dock = std::hypot(pos.x - f.dock.x, pos.z - f.dock.z);
            color = color.lerp(EARTH, real_t(std::clamp(1.6 - d_dock / 9.0, 0.0, 1.0)));
            top_id[f.index(i, j)] = b.add_vertex(pos, normal, color);
            bottom_id[f.index(i, j)] = b.add_vertex(bottom_point(f, i, j), surface_normal(f, f.bottom, i, j, true), UNDERSIDE);
        }
    }

    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - 1; j++) {
            if (!f.quad_solid(i, j)) {
                continue;
            }
            const size_t k00 = f.index(i, j), k01 = f.index(i, j + 1), k10 = f.index(i + 1, j), k11 = f.index(i + 1, j + 1);
            // 从上往下看：x 向右、z 向下，(i,j) → (i,j+1) → (i+1,j) 是顺时针
            b.add_triangle(top_id[k00], top_id[k01], top_id[k10]);
            b.add_triangle(top_id[k01], top_id[k11], top_id[k10]);
            b.add_triangle(bottom_id[k00], bottom_id[k10], bottom_id[k01]);
            b.add_triangle(bottom_id[k01], bottom_id[k10], bottom_id[k11]);

            // 崖壁：哪条边的对面没有地面，就沿那条边从顶面垂到岛底
            struct Edge {
                int ai, aj, bi, bj; // 边的两个节点
                int qi, qj; // 对面的格子
                real_t nx, nz; // 朝外的法线
            };
            const Edge edges[4] = {
                { i, j, i, j + 1, i - 1, j, 0, -1 },
                { i + 1, j, i + 1, j + 1, i + 1, j, 0, 1 },
                { i, j, i + 1, j, i, j - 1, -1, 0 },
                { i, j + 1, i + 1, j + 1, i, j + 1, 1, 0 },
            };
            for (const Edge &e : edges) {
                if (f.quad_solid(e.qi, e.qj)) {
                    continue;
                }
                const Vector3 out(e.nx, 0, e.nz);
                const Vector3 pts[4] = { top_point(f, e.ai, e.aj), top_point(f, e.bi, e.bj), bottom_point(f, e.bi, e.bj), bottom_point(f, e.ai, e.aj) };
                const int v0 = b.add_vertex(pts[0], out, CLIFF_TOP);
                const int v1 = b.add_vertex(pts[1], out, CLIFF_TOP);
                const int v2 = b.add_vertex(pts[2], out, CLIFF_LOW);
                const int v3 = b.add_vertex(pts[3], out, CLIFF_LOW);
                // Godot 的面法线 = (a − c) × (a − b)；朝里就换个绕向
                const bool flip = (pts[0] - pts[2]).cross(pts[0] - pts[1]).dot(out) < 0;
                if (flip) {
                    b.add_triangle(v0, v2, v1);
                    b.add_triangle(v0, v3, v2);
                } else {
                    b.add_triangle(v0, v1, v2);
                    b.add_triangle(v0, v2, v3);
                }
            }
        }
    }
    return b.data;
}
