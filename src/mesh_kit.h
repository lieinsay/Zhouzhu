#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "building_mesh.h"

// 拼网格的公用件（从 building_mesh.cpp 挪出来，建筑的细模 / 简模与搭建件共用）：盒子、凸多边形、它们的碰撞三角面。不含 Godot 头。
// 约定同 Godot：正面顺时针；盒子先绕自己的 x 转 pitch、再绕 y 转 yaw、再平移（Basis(Y, yaw) * Basis(X, pitch)）。
namespace mesh_kit {

struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

inline V3 operator+(const V3 &a, const V3 &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline V3 operator-(const V3 &a, const V3 &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline V3 operator*(const V3 &a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline V3 cross(const V3 &a, const V3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline double dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct Xform {
    double m[3][3];
    V3 o;

    Xform(double p_yaw, double p_pitch, const V3 &p_origin) :
            o(p_origin) {
        const double cy = std::cos(p_yaw), sy = std::sin(p_yaw), cp = std::cos(p_pitch), sp = std::sin(p_pitch);
        const double r[3][3] = { { cy, sy * sp, sy * cp }, { 0.0, cp, -sp }, { -sy, cy * sp, cy * cp } };
        std::copy(&r[0][0], &r[0][0] + 9, &m[0][0]);
    }
    V3 rotate(const V3 &v) const {
        return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
    }
    V3 xform(const V3 &v) const { return rotate(v) + o; }
};

struct Rgba {
    float r = 1, g = 1, b = 1, a = 1;
};

// 盒子的六个面：法线 n 与面上的两根轴 u、w（u × w = n）；四角按 u、w 逆时针排
inline const V3 FACE_N[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
inline const V3 FACE_U[6] = { { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
inline const V3 FACE_W[6] = { { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } };
inline const double SU[4] = { -1, 1, 1, -1 }, SW[4] = { -1, -1, 1, 1 };

inline V3 corner(const V3 &p_half, int p_face, double p_su, double p_sw) {
    const V3 c = FACE_N[p_face] + FACE_U[p_face] * p_su + FACE_W[p_face] * p_sw;
    return { c.x * p_half.x, c.y * p_half.y, c.z * p_half.z };
}

inline void push3(std::vector<float> &r_out, const V3 &p_v) {
    r_out.push_back(float(p_v.x));
    r_out.push_back(float(p_v.y));
    r_out.push_back(float(p_v.z));
}

// 一个平面多边形（凸），p_points 顺序随意绕，按 p_normal 摆正：从外面看逆时针的点，三角形反着连（Godot 的正面顺时针）
inline void polygon(MeshPart &r_part, std::vector<V3> p_points, const V3 &p_normal, const Rgba *p_color) {
    if (p_points.size() < 3) {
        return;
    }
    if (dot(cross(p_points[1] - p_points[0], p_points[2] - p_points[0]), p_normal) < 0.0) {
        std::reverse(p_points.begin(), p_points.end());
    }
    const int base = r_part.vertex_count();
    const double len = std::sqrt(dot(p_normal, p_normal));
    const V3 n = len > 0.0 ? p_normal * (1.0 / len) : V3{ 0, 1, 0 };
    for (const V3 &p : p_points) {
        push3(r_part.positions, p);
        push3(r_part.normals, n);
        if (p_color) {
            r_part.colors.insert(r_part.colors.end(), { p_color->r, p_color->g, p_color->b, p_color->a });
        }
    }
    for (int k = 1; k + 1 < int(p_points.size()); k++) {
        r_part.indices.insert(r_part.indices.end(), { base, base + k + 1, base + k });
    }
}

// 同一个多边形的碰撞三角面（每三个点一个面，两面都挡由上层定）
inline void polygon_faces(std::vector<float> &r_faces, const std::vector<V3> &p_points) {
    for (int k = 1; k + 1 < int(p_points.size()); k++) {
        push3(r_faces, p_points[0]);
        push3(r_faces, p_points[size_t(k + 1)]);
        push3(r_faces, p_points[size_t(k)]);
    }
}

inline void append_box(MeshPart &r_part, const Xform &p_xf, const V3 &p_size, const Rgba *p_color = nullptr) {
    const V3 half = p_size * 0.5;
    for (int f = 0; f < 6; f++) {
        std::vector<V3> pts;
        for (int k = 0; k < 4; k++) {
            pts.push_back(p_xf.xform(corner(half, f, SU[k], SW[k])));
        }
        polygon(r_part, pts, p_xf.rotate(FACE_N[f]), p_color);
    }
}

inline void append_box_faces(std::vector<float> &r_faces, const Xform &p_xf, const V3 &p_size) {
    const V3 half = p_size * 0.5;
    for (int f = 0; f < 6; f++) {
        V3 c[4];
        for (int k = 0; k < 4; k++) {
            c[k] = p_xf.xform(corner(half, f, SU[k], SW[k]));
        }
        for (int i : { 0, 2, 1, 0, 3, 2 }) {
            push3(r_faces, c[i]);
        }
    }
}

// 竖着的一块板（厚 p_thick）：p_points 是板的中面上的凸多边形（在一个竖直平面里），p_normal 是板的法线（水平）；六个面按多边形拉出厚度
inline void append_slab(MeshPart &r_part, std::vector<float> *r_faces, const std::vector<V3> &p_points, const V3 &p_normal, double p_thick, const Rgba *p_color = nullptr) {
    const double len = std::sqrt(dot(p_normal, p_normal));
    if (p_points.size() < 3 || len <= 0.0) {
        return;
    }
    const V3 n = p_normal * (1.0 / len), off = n * (0.5 * p_thick);
    std::vector<V3> front, back;
    for (const V3 &p : p_points) {
        front.push_back(p + off);
        back.push_back(p - off);
    }
    polygon(r_part, front, n, p_color);
    polygon(r_part, back, n * -1.0, p_color);
    for (size_t k = 0; k < p_points.size(); k++) {
        const size_t k1 = (k + 1) % p_points.size();
        const V3 edge = p_points[k1] - p_points[k];
        V3 side = cross(edge, n);
        // 朝外：离多边形的中心远的那边
        V3 center{};
        for (const V3 &p : p_points) {
            center = center + p;
        }
        center = center * (1.0 / double(p_points.size()));
        if (dot(side, (p_points[k] - center)) < 0.0) {
            side = side * -1.0;
        }
        polygon(r_part, { front[k], front[k1], back[k1], back[k] }, side, p_color);
    }
    if (r_faces) {
        polygon_faces(*r_faces, front);
        polygon_faces(*r_faces, back);
        for (size_t k = 0; k < p_points.size(); k++) {
            const size_t k1 = (k + 1) % p_points.size();
            polygon_faces(*r_faces, { front[k], front[k1], back[k1], back[k] });
        }
    }
}

} // namespace mesh_kit
