#include "far_group_mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "planet_frame.h"
#include "world_grid.h"

namespace {

// 同 terrain_tile.cpp 的岩色
constexpr GroundColor ROCK{ 0.46f, 0.43f, 0.39f };
constexpr GroundColor CLIFF_TOP{ 0.40f, 0.35f, 0.30f };
constexpr GroundColor CLIFF_LOW{ 0.25f, 0.22f, 0.21f };
constexpr GroundColor UNDERSIDE{ 0.20f, 0.18f, 0.18f };

constexpr int COVER_CLIFF_EDGE = 1, COVER_ROCK = 2, COVER_ALPINE = 3, COVER_FOREST = 4, COVER_SHRUB = 5, COVER_GRASS = 6, COVER_ARABLE = 7, COVER_TERRACE = 8,
              COVER_WETLAND = 9;

GroundColor mix(const GroundColor &a, const GroundColor &b, double t) {
    const float k = float(std::clamp(t, 0.0, 1.0));
    return { a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k };
}

// 地表类 → 季相份额（同 terrain_tile.cpp 的 ground_color：草、田、灌丛、林）
void class_shares(int p_class, double r_share[4]) {
    r_share[0] = r_share[1] = r_share[2] = r_share[3] = 0.0;
    switch (p_class) {
        case 0:
        case COVER_ALPINE:
        case COVER_GRASS:
            r_share[0] = 1.0;
            break;
        case COVER_CLIFF_EDGE:
            r_share[0] = 0.5;
            break;
        case COVER_WETLAND:
            r_share[0] = 0.7;
            break;
        case COVER_ARABLE:
        case COVER_TERRACE:
            r_share[1] = 1.0;
            break;
        case COVER_SHRUB:
            r_share[2] = 1.0;
            break;
        case COVER_FOREST:
            r_share[3] = 1.0;
            break;
        default:
            break; // 裸岩、河道、湖
    }
}

struct Builder {
    FarGroupMesh &m;
    double radius;

    int vertex(double x, double y, double z, double nx, double ny, double nz, const GroundColor &c, const double p_share[4], bool p_top) {
        // 弯过：群心切平面坐标里的真球面位置 T(q)，法线按该点的局部系转
        const double s = std::sqrt(x * x + z * z);
        double tx = x, ty = y, tz = z;
        if (s > 1e-6) {
            const double th = s / radius, r = radius + y;
            tx = x / s * r * std::sin(th);
            tz = z / s * r * std::sin(th);
            ty = r * std::cos(th) - radius;
        }
        const PlanetVec n = planet_tilt(PlanetVec(x, y, z), PlanetVec(nx, ny, nz).normalized(), radius);
        m.positions.insert(m.positions.end(), { float(tx), float(ty), float(tz) });
        m.normals.insert(m.normals.end(), { float(n.x), float(n.y), float(n.z) });
        m.colors.insert(m.colors.end(), { c.r, c.g, c.b, 1.0f });
        for (int k = 0; k < 4; k++) {
            m.season.push_back(uint8_t(std::lround(std::clamp(p_share ? p_share[k] : 0.0, 0.0, 1.0) * 255.0)));
        }
        m.uvs.insert(m.uvs.end(), { float(y), p_top ? 1.0f : 0.0f });
        const double t[3] = { tx, ty, tz };
        for (int k = 0; k < 3; k++) {
            m.aabb_min[k] = std::min(m.aabb_min[k], t[k]);
            m.aabb_max[k] = std::max(m.aabb_max[k], t[k]);
        }
        return int(m.positions.size() / 3) - 1;
    }
    void triangle(int a, int b, int c) { m.indices.insert(m.indices.end(), { a, b, c }); }
};

} // namespace

FarGroupMesh build_far_group_mesh(const PlanetLod &p_lod, double p_radius, const FarGroupRules &p_rules) {
    FarGroupMesh out;
    out.node = p_lod.node;
    out.res_m = p_lod.res_m;
    for (int k = 0; k < 3; k++) {
        out.aabb_min[k] = std::numeric_limits<double>::max();
        out.aabb_max[k] = -std::numeric_limits<double>::max();
    }
    if (p_lod.empty()) {
        return out;
    }
    const int rows = p_lod.rows, cols = p_lod.cols;
    const double res = p_lod.res_m;
    const int threshold = int(std::lround(p_rules.solid_land * 255.0));
    auto solid = [&](int r, int c) { return r >= 0 && c >= 0 && r < rows && c < cols && p_lod.land[p_lod.index(r, c)] >= threshold; };
    const int CR = rows + 1, CC = cols + 1; // 角的格子
    auto corner = [&](int r, int c) { return size_t(r) * size_t(CC) + size_t(c); };
    // 角 (r, c) 的平面坐标：栅格左上角 + c·格距（东）、+ r·格距（往南）
    auto corner_x = [&](int c) { return (p_lod.origin_x_km * 1000.0) + c * res; };
    auto corner_z = [&](int r) { return -(p_lod.origin_y_km * 1000.0) + r * res; };

    const size_t NC = size_t(CR) * size_t(CC);
    std::vector<uint8_t> kind(NC, 0); // 0 不是陆地 1 岸上（挨着虚空）2 里面
    std::vector<int> isl(NC, -1);
    std::vector<double> top(NC, 0.0);
    for (int r = 0; r < CR; r++) {
        for (int c = 0; c < CC; c++) {
            int n_solid = 0, first = -1;
            double sum = 0.0;
            for (int k = 0; k < 4; k++) {
                const int rr = r - 1 + (k >> 1), cc = c - 1 + (k & 1);
                if (solid(rr, cc)) {
                    n_solid++;
                    sum += p_lod.height[p_lod.index(rr, cc)];
                    if (first < 0) {
                        first = p_lod.island[p_lod.index(rr, cc)];
                    }
                }
            }
            const size_t k = corner(r, c);
            if (n_solid == 0) {
                continue;
            }
            kind[k] = n_solid == 4 ? 2 : 1;
            isl[k] = first;
            top[k] = sum / n_solid;
        }
    }
    // 离岸多远（角到最近的岸上角，3-4 倒角距离，米）
    const double INF = 1e18;
    std::vector<double> dist(NC, INF);
    for (size_t k = 0; k < NC; k++) {
        if (kind[k] == 1) {
            dist[k] = 0.0;
        }
    }
    const double d1 = res, d2 = res * 1.41421356;
    for (int r = 0; r < CR; r++) {
        for (int c = 0; c < CC; c++) {
            double &d = dist[corner(r, c)];
            if (c > 0) d = std::min(d, dist[corner(r, c - 1)] + d1);
            if (r > 0) {
                d = std::min(d, dist[corner(r - 1, c)] + d1);
                if (c > 0) d = std::min(d, dist[corner(r - 1, c - 1)] + d2);
                if (c + 1 < CC) d = std::min(d, dist[corner(r - 1, c + 1)] + d2);
            }
        }
    }
    for (int r = CR - 1; r >= 0; r--) {
        for (int c = CC - 1; c >= 0; c--) {
            double &d = dist[corner(r, c)];
            if (c + 1 < CC) d = std::min(d, dist[corner(r, c + 1)] + d1);
            if (r + 1 < CR) {
                d = std::min(d, dist[corner(r + 1, c)] + d1);
                if (c + 1 < CC) d = std::min(d, dist[corner(r + 1, c + 1)] + d2);
                if (c > 0) d = std::min(d, dist[corner(r + 1, c - 1)] + d2);
            }
        }
    }
    // 各岛的最深岸距 → 岛体（同 island_body）
    const int n_isl = int(p_lod.islands.size());
    std::vector<double> max_shore(size_t(std::max(1, n_isl)), 1.0);
    for (size_t k = 0; k < NC; k++) {
        if (kind[k] && isl[k] >= 0 && isl[k] < n_isl) {
            max_shore[size_t(isl[k])] = std::max(max_shore[size_t(isl[k])], dist[k]);
        }
    }
    struct Body {
        double rim = 0.0, wall = 0.0, keel = 0.0, shore = 1.0;
    };
    std::vector<Body> bodies(size_t(std::max(1, n_isl)));
    for (int i = 0; i < n_isl; i++) {
        Body &b = bodies[size_t(i)];
        const PlanetLodIsland &li = p_lod.islands[size_t(i)];
        b.rim = li.rim_m;
        b.shore = std::max(1.0, max_shore[size_t(i)]);
        b.keel = p_rules.keel_k * std::pow(b.shore, p_rules.keel_exp); // 同 island_body：粗版的 keel_m 是崖脚高程、随浮高平移，不用
        b.wall = std::max(p_rules.cliff_m, std::clamp(p_rules.wall_share * b.keel, p_rules.wall_min_m, p_rules.wall_max_m));
        // 浮高计划 F3（同 island_body）：岛底连岩棱不低于 bottom_min_y，超了收短龙骨、崖壁不动
        b.keel = std::min(b.keel, std::max(0.0, (b.rim - b.wall - p_rules.underside_amp_m - p_rules.bottom_min_y) / (1.0 + p_rules.rib_share)));
    }
    auto body_of = [&](size_t k) -> const Body & { return bodies[size_t(std::clamp(isl[k], 0, std::max(0, n_isl - 1)))]; };
    std::vector<double> bottom(NC, 0.0);
    for (size_t k = 0; k < NC; k++) {
        if (!kind[k]) {
            continue;
        }
        const Body &b = body_of(k);
        if (kind[k] == 1 && n_isl > 0) {
            top[k] = b.rim; // 岸上：岸缘高
        }
        const double inward = std::clamp(dist[k] / b.shore, 0.0, 1.0);
        bottom[k] = std::min(b.rim - b.wall - b.keel * std::sqrt(inward), top[k] - 2.0);
    }

    Builder bld{ out, p_radius };
    std::vector<int> top_id(NC, -1), bottom_id(NC, -1);
    auto h_at = [&](const std::vector<double> &p_h, int r, int c, size_t p_self) {
        if (r < 0 || c < 0 || r >= CR || c >= CC || !kind[corner(r, c)]) {
            return p_h[p_self];
        }
        return p_h[corner(r, c)];
    };
    double peak = -1e18;
    for (int r = 0; r < CR; r++) {
        for (int c = 0; c < CC; c++) {
            const size_t k = corner(r, c);
            if (!kind[k]) {
                continue;
            }
            const double x = corner_x(c), z = corner_z(r);
            // 顶面
            const double gx = (h_at(top, r, c + 1, k) - h_at(top, r, c - 1, k)) / (2.0 * res);
            const double gz = (h_at(top, r + 1, c, k) - h_at(top, r - 1, c, k)) / (2.0 * res);
            const double nl = std::sqrt(gx * gx + 1.0 + gz * gz);
            const double ny = 1.0 / nl;
            GroundColor col{ 0.0f, 0.0f, 0.0f };
            double share[4] = { 0.0, 0.0, 0.0, 0.0 };
            int n = 0;
            for (int q = 0; q < 4; q++) {
                const int rr = r - 1 + (q >> 1), cc = c - 1 + (q & 1);
                if (!solid(rr, cc)) {
                    continue;
                }
                const int lc = p_lod.landcover[p_lod.index(rr, cc)];
                const GroundColor pc = landcover_color(lc);
                col = { col.r + pc.r, col.g + pc.g, col.b + pc.b };
                double s[4];
                class_shares(lc, s);
                for (int j = 0; j < 4; j++) {
                    share[j] += s[j];
                }
                n++;
            }
            const float inv = 1.0f / float(std::max(1, n));
            col = { col.r * inv, col.g * inv, col.b * inv };
            const double rock = std::clamp((p_rules.rock_slope - ny) / 0.16, 0.0, 1.0);
            col = mix(col, ROCK, rock);
            for (int j = 0; j < 4; j++) {
                share[j] = share[j] / std::max(1, n) * (1.0 - rock);
            }
            top_id[k] = bld.vertex(x, top[k], z, -gx / nl, ny, -gz / nl, col, share, true);
            if (top[k] > peak) {
                peak = top[k];
                out.peak_x = x, out.peak_y = top[k], out.peak_z = z;
            }
            // 岛底（朝下）：崖脚处是崖色的暗端，往里越深越暗
            const double bx = (h_at(bottom, r, c + 1, k) - h_at(bottom, r, c - 1, k)) / (2.0 * res);
            const double bz = (h_at(bottom, r + 1, c, k) - h_at(bottom, r - 1, c, k)) / (2.0 * res);
            const double deep = std::clamp((body_of(k).rim - body_of(k).wall - bottom[k]) / 600.0, 0.0, 1.0);
            bottom_id[k] = bld.vertex(x, bottom[k], z, bx, -1.0, bz, mix(CLIFF_LOW, UNDERSIDE, deep), nullptr, false);
        }
    }
    // 顶面、岛底：每个实地格两片
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            if (!solid(r, c)) {
                continue;
            }
            out.solid_cells++;
            const size_t k00 = corner(r, c), k01 = corner(r, c + 1), k10 = corner(r + 1, c), k11 = corner(r + 1, c + 1);
            // 从上往下看：x 向右、z 向下，(r,c) → (r,c+1) → (r+1,c) 是顺时针
            bld.triangle(top_id[k00], top_id[k01], top_id[k10]);
            bld.triangle(top_id[k01], top_id[k11], top_id[k10]);
            bld.triangle(bottom_id[k00], bottom_id[k10], bottom_id[k01]);
            bld.triangle(bottom_id[k01], bottom_id[k10], bottom_id[k11]);
            // 崖壁：挨着虚空的边从顶面垂到崖脚（岸上角的岛底就是崖脚）
            struct Edge {
                int ar, ac, br, bc, qr, qc;
                double ox, oz;
            };
            const Edge edges[4] = {
                { r, c, r, c + 1, r - 1, c, 0.0, -1.0 },
                { r + 1, c, r + 1, c + 1, r + 1, c, 0.0, 1.0 },
                { r, c, r + 1, c, r, c - 1, -1.0, 0.0 },
                { r, c + 1, r + 1, c + 1, r, c + 1, 1.0, 0.0 },
            };
            for (const Edge &e : edges) {
                if (solid(e.qr, e.qc)) {
                    continue;
                }
                const size_t ka = corner(e.ar, e.ac), kb = corner(e.br, e.bc);
                const double ax = corner_x(e.ac), az = corner_z(e.ar), bx = corner_x(e.bc), bz = corner_z(e.br);
                const int v0 = bld.vertex(ax, top[ka], az, e.ox, 0.0, e.oz, CLIFF_TOP, nullptr, false);
                const int v1 = bld.vertex(bx, top[kb], bz, e.ox, 0.0, e.oz, CLIFF_TOP, nullptr, false);
                const int v2 = bld.vertex(bx, bottom[kb], bz, e.ox, 0.0, e.oz, CLIFF_LOW, nullptr, false);
                const int v3 = bld.vertex(ax, bottom[ka], az, e.ox, 0.0, e.oz, CLIFF_LOW, nullptr, false);
                // Godot 的面法线 = (a − c) × (a − b)：朝外就照 v0 v1 v2，否则反过来
                const double px = ax - bx, py = top[ka] - bottom[kb], pz = az - bz; // a − c
                const double qx = ax - bx, qy = top[ka] - top[kb], qz = az - bz; // a − b
                const double fx = py * qz - pz * qy, fz = px * qy - py * qx;
                if (fx * e.ox + fz * e.oz < 0.0) {
                    bld.triangle(v0, v2, v1);
                    bld.triangle(v0, v3, v2);
                } else {
                    bld.triangle(v0, v1, v2);
                    bld.triangle(v0, v2, v3);
                }
            }
        }
    }
    if (out.empty()) {
        for (int k = 0; k < 3; k++) {
            out.aabb_min[k] = out.aabb_max[k] = 0.0;
        }
    }
    return out;
}
