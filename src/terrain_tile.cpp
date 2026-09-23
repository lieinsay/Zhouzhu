#include "terrain_tile.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr GroundColor ROCK{ 0.46f, 0.43f, 0.39f };
constexpr GroundColor WET{ 0.25f, 0.40f, 0.38f }; // 溪涧边的湿地皮
constexpr GroundColor GRAVEL{ 0.50f, 0.47f, 0.41f }; // 河床的卵石
constexpr GroundColor MEADOW{ 0.30f, 0.44f, 0.27f }; // 河漫滩的湿草
constexpr GroundColor CLIFF_TOP{ 0.40f, 0.35f, 0.30f };
constexpr GroundColor CLIFF_LOW{ 0.25f, 0.22f, 0.21f };
constexpr GroundColor UNDERSIDE{ 0.20f, 0.18f, 0.18f };

constexpr double COLOR_JITTER_M = 45.0; // 取地表色的位置抖这么多：地表类的边界不再是 100 m 的方格
constexpr uint64_t COLOR_SEED = 0x51EDC0105ull;
constexpr int UNDERSIDE_STRIDE = 8; // 须整除 TileLayout::CELLS

GroundColor lerp(const GroundColor &a, const GroundColor &b, double t) {
    const float k = float(std::clamp(t, 0.0, 1.0));
    return { a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k };
}

// p_sample：这一点的地形取样（河床、河漫滩的权重）；没有河网时按栅格的溪涧格染湿
GroundColor ground_color(const WorldGrid &g, const TerrainSample &p_sample, bool p_rivers, double p_x, double p_z, double p_normal_y) {
    const double jx = p_x + COLOR_JITTER_M * hash_fbm(COLOR_SEED, p_x, p_z, 70.0, 3);
    const double jz = p_z + COLOR_JITTER_M * hash_fbm(COLOR_SEED + 1, p_x, p_z, 70.0, 3);
    const double fc = g.col_of(jx), fr = g.row_of(jz);
    const int c = int(std::floor(fc)), r = int(std::floor(fr));
    const double tc = fc - c, tr = fr - r;
    auto cell = [&](int rr, int cc) {
        return g.index(std::clamp(rr, 0, g.rows - 1), std::clamp(cc, 0, g.cols - 1));
    };
    auto color = [&](int rr, int cc) { return landcover_color(g.landcover[cell(rr, cc)]); };
    auto wet = [&](int rr, int cc) { return g.stream[cell(rr, cc)] ? 1.0 : 0.0; };
    GroundColor out = lerp(lerp(color(r, c), color(r, c + 1), tc), lerp(color(r + 1, c), color(r + 1, c + 1), tc), tr);
    const double w0 = wet(r, c) + (wet(r, c + 1) - wet(r, c)) * tc;
    const double w1 = wet(r + 1, c) + (wet(r + 1, c + 1) - wet(r + 1, c)) * tc;
    if (p_rivers) {
        out = lerp(out, MEADOW, 0.45 * p_sample.bank);
        out = lerp(out, ROCK, (0.86 - p_normal_y) / 0.16); // 约 30° 起露岩，45° 全岩
        out = lerp(out, GRAVEL, p_sample.bed); // 河床不论坡度都是卵石
    } else {
        out = lerp(out, WET, 0.55 * (w0 + (w1 - w0) * tr));
        out = lerp(out, ROCK, (0.86 - p_normal_y) / 0.16);
    }
    const float shade = float(1.0 + 0.07 * hash_fbm(COLOR_SEED + 2, p_x, p_z, 30.0, 3));
    return { out.r * shade, out.g * shade, out.b * shade };
}

struct Builder {
    TileMesh &mesh;
    bool collide;

    int vertex(float x, float y, float z, float nx, float ny, float nz, const GroundColor &c) {
        mesh.positions.insert(mesh.positions.end(), { x, y, z });
        mesh.normals.insert(mesh.normals.end(), { nx, ny, nz });
        mesh.colors.insert(mesh.colors.end(), { c.r, c.g, c.b, 1.0f });
        return int(mesh.positions.size() / 3) - 1;
    }
    // a → b → c 须是从正面看的顺时针
    void triangle(int a, int b, int c, bool p_solid) {
        mesh.indices.insert(mesh.indices.end(), { a, b, c });
        if (collide && p_solid) {
            for (int v : { a, b, c }) {
                mesh.collision.insert(mesh.collision.end(), mesh.positions.begin() + v * 3, mesh.positions.begin() + v * 3 + 3);
            }
        }
    }
    // 竖着的四边形 p0 p1（上）p2 p3（下）；按朝外的方向 (ox, oz) 定绕向
    void wall(const float *p0, const float *p1, const float *p2, const float *p3, float ox, float oz,
            const GroundColor &top, const GroundColor &low, bool p_solid) {
        const int v0 = vertex(p0[0], p0[1], p0[2], ox, 0, oz, top);
        const int v1 = vertex(p1[0], p1[1], p1[2], ox, 0, oz, top);
        const int v2 = vertex(p2[0], p2[1], p2[2], ox, 0, oz, low);
        const int v3 = vertex(p3[0], p3[1], p3[2], ox, 0, oz, low);
        // Godot 的面法线 = (a − c) × (a − b)
        const float ax = p0[0] - p2[0], ay = p0[1] - p2[1], az = p0[2] - p2[2];
        const float bx = p0[0] - p1[0], by = p0[1] - p1[1], bz = p0[2] - p1[2];
        const float nx = ay * bz - az * by, nz = ax * by - ay * bx;
        const bool flip = nx * ox + nz * oz < 0;
        if (flip) {
            triangle(v0, v2, v1, p_solid);
            triangle(v0, v3, v2, p_solid);
        } else {
            triangle(v0, v1, v2, p_solid);
            triangle(v0, v2, v3, p_solid);
        }
    }
};

} // namespace

TileLayout::TileLayout(const WorldGrid &p_grid) {
    x0 = p_grid.min_x();
    z0 = p_grid.min_z();
    const double extent = std::max(p_grid.max_x() - x0, p_grid.max_z() - z0);
    max_level = 0;
    while (LEAF_M * double(1 << max_level) < extent) {
        max_level++;
    }
    root_size = LEAF_M * double(1 << max_level);

    rows = p_grid.rows;
    cols = p_grid.cols;
    res = p_grid.res;
    grid_x = p_grid.x0;
    grid_z = p_grid.z0;
    // 双三次会冲出岸距一点，岸线噪声还能把岸推出去 COAST_AMP_M：留一格余量
    const float reach = float(-(WorldTerrain::COAST_AMP_M + res));
    land_sat.assign(size_t(rows + 1) * size_t(cols + 1), 0);
    for (int r = 0; r < rows; r++) {
        int run = 0;
        for (int c = 0; c < cols; c++) {
            run += p_grid.shore[p_grid.index(r, c)] > reach ? 1 : 0;
            land_sat[size_t(r + 1) * size_t(cols + 1) + size_t(c + 1)] = land_sat[size_t(r) * size_t(cols + 1) + size_t(c + 1)] + run;
        }
    }
}

void TileLayout::corner(const TileKey &p_key, double &r_x, double &r_z) const {
    const double s = size(p_key.level);
    r_x = x0 + p_key.ix * s;
    r_z = z0 + p_key.iz * s;
}

bool TileLayout::may_have_land(const TileKey &p_key, double p_margin) const {
    double x, z;
    corner(p_key, x, z);
    const double s = size(p_key.level);
    const int c0 = std::max(0, int(std::floor((x - p_margin - grid_x) / res + 0.5)));
    const int c1 = std::min(cols - 1, int(std::floor((x + s + p_margin - grid_x) / res + 0.5)));
    const int r0 = std::max(0, int(std::floor((z - p_margin - grid_z) / res + 0.5)));
    const int r1 = std::min(rows - 1, int(std::floor((z + s + p_margin - grid_z) / res + 0.5)));
    if (c0 > c1 || r0 > r1) {
        return false;
    }
    auto at = [this](int r, int c) { return land_sat[size_t(r) * size_t(cols + 1) + size_t(c)]; };
    return at(r1 + 1, c1 + 1) - at(r0, c1 + 1) - at(r1 + 1, c0) + at(r0, c0) > 0;
}

TileKey TileLayout::leaf_at(double p_x, double p_z) const {
    const double s = size(max_level);
    const int n = 1 << max_level;
    return { max_level, std::clamp(int(std::floor((p_x - x0) / s)), 0, n - 1), std::clamp(int(std::floor((p_z - z0) / s)), 0, n - 1) };
}

bool TileLayout::contains(const TileKey &p_outer, const TileKey &p_inner) const {
    if (p_inner.level < p_outer.level) {
        return false;
    }
    const int shift = p_inner.level - p_outer.level;
    return (p_inner.ix >> shift) == p_outer.ix && (p_inner.iz >> shift) == p_outer.iz;
}

std::vector<TileKey> TileLayout::select(double p_focus_x, double p_focus_z, double p_split) const {
    std::vector<TileKey> out;
    std::function<void(const TileKey &)> visit = [&](const TileKey &key) {
        if (!may_have_land(key, step(key.level))) {
            return;
        }
        double x, z;
        corner(key, x, z);
        const double s = size(key.level);
        const double dx = std::max({ x - p_focus_x, 0.0, p_focus_x - (x + s) });
        const double dz = std::max({ z - p_focus_z, 0.0, p_focus_z - (z + s) });
        if (key.level < max_level && std::hypot(dx, dz) < s * p_split) {
            for (int k = 0; k < 4; k++) {
                visit({ key.level + 1, key.ix * 2 + (k & 1), key.iz * 2 + (k >> 1) });
            }
        } else {
            out.push_back(key);
        }
    };
    visit({ 0, 0, 0 });
    return out;
}

TileMesh build_tile_mesh(const WorldTerrain &p_terrain, const TileLayout &p_layout, const TileKey &p_key) {
    constexpr int N = TileLayout::CELLS;
    constexpr int W = N + 3; // 节点 -1 … N+1：外面多一圈，好判断块边上的崖与算法线
    TileMesh mesh;
    mesh.key = p_key;
    p_layout.corner(p_key, mesh.origin_x, mesh.origin_z);
    const double step = p_layout.step(p_key.level);
    const bool leaf = p_key.level == p_layout.get_max_level();
    const WorldGrid &grid = p_terrain.get_grid();

    std::vector<TerrainSample> s(size_t(W) * W);
    auto at = [&](int i, int j) -> TerrainSample & { return s[size_t(i + 1) * W + size_t(j + 1)]; };
    for (int i = -1; i <= N + 1; i++) {
        for (int j = -1; j <= N + 1; j++) {
            at(i, j) = p_terrain.sample(mesh.origin_x + j * step, mesh.origin_z + i * step);
        }
    }
    auto solid = [&](int qi, int qj) {
        return at(qi, qj).land && at(qi, qj + 1).land && at(qi + 1, qj).land && at(qi + 1, qj + 1).land;
    };

    Builder b{ mesh, leaf };
    std::vector<int> top_id(size_t(N + 1) * (N + 1), -1), bottom_id(size_t(N + 1) * (N + 1), -1);
    std::vector<float> top_pos(size_t(N + 1) * (N + 1) * 3), bottom_pos(size_t(N + 1) * (N + 1) * 3);
    std::vector<GroundColor> top_color(size_t(N + 1) * (N + 1));
    std::vector<float> top_normal(size_t(N + 1) * (N + 1) * 3);
    auto node = [&](int i, int j) { return size_t(i) * (N + 1) + size_t(j); };

    for (int i = 0; i <= N; i++) {
        for (int j = 0; j <= N; j++) {
            const TerrainSample &c = at(i, j);
            if (!c.land) {
                continue;
            }
            const size_t k = node(i, j);
            const float x = float(j * step), z = float(i * step);
            // 中心差分；外圈邻居就算不是陆地，顶面高度也有定义，所以块与块的法线接得上
            float nx = float(-(at(i, j + 1).top - at(i, j - 1).top) / (2.0 * step));
            float nz = float(-(at(i + 1, j).top - at(i - 1, j).top) / (2.0 * step));
            float len = std::sqrt(nx * nx + 1.0f + nz * nz);
            const float ux = nx / len, uy = 1.0f / len, uz = nz / len;
            top_color[k] = ground_color(grid, c, p_terrain.has_rivers(), mesh.origin_x + x, mesh.origin_z + z, uy);
            top_id[k] = b.vertex(x, float(c.top), z, ux, uy, uz, top_color[k]);
            top_pos[k * 3] = x, top_pos[k * 3 + 1] = float(c.top), top_pos[k * 3 + 2] = z;
            top_normal[k * 3] = ux, top_normal[k * 3 + 1] = uy, top_normal[k * 3 + 2] = uz;

            nx = float((at(i, j + 1).bottom - at(i, j - 1).bottom) / (2.0 * step));
            nz = float((at(i + 1, j).bottom - at(i - 1, j).bottom) / (2.0 * step));
            len = std::sqrt(nx * nx + 1.0f + nz * nz);
            bottom_id[k] = b.vertex(x, float(c.bottom), z, nx / len, -1.0f / len, nz / len, UNDERSIDE);
            bottom_pos[k * 3] = x, bottom_pos[k * 3 + 1] = float(c.bottom), bottom_pos[k * 3 + 2] = z;
        }
    }

    // 整块都是陆地（没有崖）时岛底只从下面远远看得见：隔 UNDERSIDE_STRIDE 格铺一片就够
    bool interior = true;
    for (int i = -1; i <= N && interior; i++) {
        for (int j = -1; j <= N && interior; j++) {
            interior = solid(i, j);
        }
    }
    const int bottom_stride = interior ? UNDERSIDE_STRIDE : 1;

    // 裙边垂多深：比相邻两级精度之差大就行
    const float skirt = float(2.0 + step);
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (!solid(i, j)) {
                continue;
            }
            mesh.land_quads++;
            const size_t k00 = node(i, j), k01 = node(i, j + 1), k10 = node(i + 1, j), k11 = node(i + 1, j + 1);
            // 从上往下看：x 向右、z 向下，(i,j) → (i,j+1) → (i+1,j) 是顺时针
            b.triangle(top_id[k00], top_id[k01], top_id[k10], true);
            b.triangle(top_id[k01], top_id[k11], top_id[k10], true);
            if (i % bottom_stride == 0 && j % bottom_stride == 0) {
                const int d = bottom_stride;
                const size_t b00 = node(i, j), b01 = node(i, j + d), b10 = node(i + d, j), b11 = node(i + d, j + d);
                b.triangle(bottom_id[b00], bottom_id[b10], bottom_id[b01], false);
                b.triangle(bottom_id[b01], bottom_id[b10], bottom_id[b11], false);
            }

            struct Edge {
                int ai, aj, bi, bj; // 边的两个节点
                int qi, qj; // 对面的格子
                float nx, nz; // 朝外
            };
            const Edge edges[4] = {
                { i, j, i, j + 1, i - 1, j, 0, -1 },
                { i + 1, j, i + 1, j + 1, i + 1, j, 0, 1 },
                { i, j, i + 1, j, i, j - 1, -1, 0 },
                { i, j + 1, i + 1, j + 1, i, j + 1, 1, 0 },
            };
            for (const Edge &e : edges) {
                const size_t ka = node(e.ai, e.aj), kb = node(e.bi, e.bj);
                const bool on_border = e.qi < 0 || e.qj < 0 || e.qi >= N || e.qj >= N;
                if (!solid(e.qi, e.qj)) {
                    // 崖壁：从顶面垂到岛底
                    b.wall(&top_pos[ka * 3], &top_pos[kb * 3], &bottom_pos[kb * 3], &bottom_pos[ka * 3], e.nx, e.nz, CLIFF_TOP, CLIFF_LOW, true);
                } else if (on_border) {
                    // 裙边：邻块精度不同，两边的面对不齐，垂一圈同色的面把缝遮住（两面都画，从哪边看都有）
                    const float a_low[3] = { top_pos[ka * 3], top_pos[ka * 3 + 1] - skirt, top_pos[ka * 3 + 2] };
                    const float b_low[3] = { top_pos[kb * 3], top_pos[kb * 3 + 1] - skirt, top_pos[kb * 3 + 2] };
                    const int va = b.vertex(top_pos[ka * 3], top_pos[ka * 3 + 1], top_pos[ka * 3 + 2], top_normal[ka * 3], top_normal[ka * 3 + 1], top_normal[ka * 3 + 2], top_color[ka]);
                    const int vb = b.vertex(top_pos[kb * 3], top_pos[kb * 3 + 1], top_pos[kb * 3 + 2], top_normal[kb * 3], top_normal[kb * 3 + 1], top_normal[kb * 3 + 2], top_color[kb]);
                    const int vc = b.vertex(b_low[0], b_low[1], b_low[2], top_normal[kb * 3], top_normal[kb * 3 + 1], top_normal[kb * 3 + 2], top_color[kb]);
                    const int vd = b.vertex(a_low[0], a_low[1], a_low[2], top_normal[ka * 3], top_normal[ka * 3 + 1], top_normal[ka * 3 + 2], top_color[ka]);
                    b.triangle(va, vb, vc, false);
                    b.triangle(va, vc, vd, false);
                    b.triangle(va, vc, vb, false);
                    b.triangle(va, vd, vc, false);

                    // 岛底也一样：往上（岛里）翘一圈，从下面看不漏天
                    const float lift = float(2.0 + step * bottom_stride * 0.5);
                    const int ua = b.vertex(bottom_pos[ka * 3], bottom_pos[ka * 3 + 1], bottom_pos[ka * 3 + 2], 0, -1, 0, UNDERSIDE);
                    const int ub = b.vertex(bottom_pos[kb * 3], bottom_pos[kb * 3 + 1], bottom_pos[kb * 3 + 2], 0, -1, 0, UNDERSIDE);
                    const int uc = b.vertex(bottom_pos[kb * 3], bottom_pos[kb * 3 + 1] + lift, bottom_pos[kb * 3 + 2], 0, -1, 0, UNDERSIDE);
                    const int ud = b.vertex(bottom_pos[ka * 3], bottom_pos[ka * 3 + 1] + lift, bottom_pos[ka * 3 + 2], 0, -1, 0, UNDERSIDE);
                    b.triangle(ua, ub, uc, false);
                    b.triangle(ua, uc, ud, false);
                    b.triangle(ua, uc, ub, false);
                    b.triangle(ua, ud, uc, false);
                }
            }
        }
    }
    return mesh;
}
