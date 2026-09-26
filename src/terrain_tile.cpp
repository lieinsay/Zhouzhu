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
constexpr GroundColor PUMICE{ 0.52f, 0.49f, 0.44f }; // 浮石的灰白层

constexpr double WALL_ROW_M = 5.0; // 崖壁竖着每这么高分一段，好染岩层（比岩层薄，层才染得出来）
constexpr int WALL_ROWS_MAX = 40;
constexpr double WALL_FADE_M = 160.0; // 崖顶往下这么深从崖顶色渐成崖脚色
constexpr double STRATA_M = 9.0; // 岩层大约这么厚
constexpr uint64_t STRATA_SEED = 0x57A7Aull;

constexpr uint64_t COLOR_SEED = 0x51EDC0105ull;
constexpr int COVER_RIVER = 10; // 地表类：河道
constexpr double ROCK_BREAKUP = 0.35; // 陡坡露岩的门槛按细噪声进退：岩成一块块斑，不是顺着坡度抹开的一片灰
constexpr double ROCK_BREAKUP_FEATURE_M = 14.0;
constexpr int UNDERSIDE_STRIDE = 4; // 须整除 TileLayout::CELLS（岛底垂上千米、带着几十米的岩棱，隔 8 格插值太粗，块与块之间裂缝）

GroundColor lerp(const GroundColor &a, const GroundColor &b, double t) {
    const float k = float(std::clamp(t, 0.0, 1.0));
    return { a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k };
}

// 崖壁与岛底的岩色：上浅下深（p_t：0 崖顶 → 1 崖脚），按绝对高度一层层夹着浮石的灰白层（同一高度在各处是同一层，相邻的崖面对得上）
GroundColor rock_color(double p_y, double p_t) {
    const GroundColor base = lerp(CLIFF_TOP, CLIFF_LOW, p_t);
    const double layer = hash_fbm(STRATA_SEED, p_y / STRATA_M, 0.5, 1.0, 2);
    const GroundColor banded = lerp(base, PUMICE, std::clamp((layer - 0.15) * 1.6, 0.0, 0.55));
    const float shade = float(1.0 + 0.10 * hash_noise(STRATA_SEED + 1, p_y / (0.37 * STRATA_M), 0.25));
    return { banded.r * shade, banded.g * shade, banded.b * shade };
}

// 地皮色按这一点的地表份额混（WorldTerrain::cover，与散布同一份：地皮变色的地方就是树变稀的地方），再按坡度露岩、按河染滩与河床。
// p_sample：这一点的地形取样（河床、河漫滩的权重）；没有河网时按栅格的溪涧格染湿。
// p_footprint：顶点间距——远处的粗块过渡带放宽、细噪声退掉，免得几十米一个顶点时颜色闪成噪点
GroundColor ground_color(const WorldTerrain &p_terrain, const TerrainSample &p_sample, double p_x, double p_z, double p_normal_y, double p_footprint) {
    const CoverBlend &blend = p_terrain.get_cover();
    const CoverStencil st = blend.stencil(p_x, p_z);
    const CoverMix mix = blend.mix(st, p_x, p_z, p_footprint);
    GroundColor out{ 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < COVER_CLASSES; k++) {
        if (mix.w[k] > 0.0f) {
            // 有河网时水面由河的网格画：「河道」格（一格 100 m，河槽只占其中几米到几十米）的地皮是滩上的湿草，不是水色
            const GroundColor c = k == COVER_RIVER && p_terrain.has_rivers() ? MEADOW : landcover_color(k);
            out = { out.r + mix.w[k] * c.r, out.g + mix.w[k] * c.g, out.b + mix.w[k] * c.b };
        }
    }
    // 约 30° 起露岩、45° 全岩；门槛按细噪声进退（远处退掉）
    const double breakup = ROCK_BREAKUP * (1.0 - cover_ramp(p_footprint, 0.0, 0.75 * ROCK_BREAKUP_FEATURE_M));
    const double rock = (0.86 - p_normal_y) / 0.16 + breakup * hash_fbm(COLOR_SEED + 3, p_x, p_z, ROCK_BREAKUP_FEATURE_M, 2);
    if (p_terrain.has_rivers()) {
        out = lerp(out, MEADOW, 0.45 * p_sample.bank);
        out = lerp(out, ROCK, rock);
        out = lerp(out, GRAVEL, p_sample.bed); // 河床不论坡度都是卵石
    } else {
        const WorldGrid &g = p_terrain.get_grid();
        out = lerp(out, WET, 0.55 * blend.smooth(st, [&g](size_t k) { return g.stream[k] ? 1.0 : 0.0; }));
        out = lerp(out, ROCK, rock);
    }
    const float shade = float(1.0 + 0.07 * hash_fbm(COLOR_SEED + 2, p_x, p_z, 30.0, 3));
    return { out.r * shade, out.g * shade, out.b * shade };
}

struct Builder {
    TileMesh &mesh;
    bool collide;
    double wall_row = WALL_ROW_M; // 崖壁分段高：粗块（远处）按格距分，不必那么细

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
    // 崖壁：竖着的四边形 p0 p1（上）p2 p3（下），竖着切成每段约 WALL_ROW_M 高；每个顶点按它自己的绝对高度染岩层，
    // 相邻的崖面在同一高度就是同一种色（按段取一个色的话，两列崖顶不一样高，色带就错开成一道道竖条）
    void cliff(const float *p0, const float *p1, const float *p2, const float *p3, float ox, float oz, bool p_solid) {
        const double drop = std::max(p0[1] - p3[1], p1[1] - p2[1]);
        const int rows = std::clamp(int(std::ceil(drop / wall_row)), 1, WALL_ROWS_MAX);
        const double top = 0.5 * (p0[1] + p1[1]);
        auto color = [&](const float *q) { return rock_color(q[1], (top - q[1]) / WALL_FADE_M); };
        float a_hi[3] = { p0[0], p0[1], p0[2] }, b_hi[3] = { p1[0], p1[1], p1[2] };
        for (int k = 1; k <= rows; k++) {
            const float t = float(k) / float(rows);
            const float a_lo[3] = { p0[0] + (p3[0] - p0[0]) * t, p0[1] + (p3[1] - p0[1]) * t, p0[2] + (p3[2] - p0[2]) * t };
            const float b_lo[3] = { p1[0] + (p2[0] - p1[0]) * t, p1[1] + (p2[1] - p1[1]) * t, p1[2] + (p2[2] - p1[2]) * t };
            wall(a_hi, b_hi, b_lo, a_lo, ox, oz, color(a_hi), color(b_hi), color(b_lo), color(a_lo), p_solid);
            std::copy(a_lo, a_lo + 3, a_hi);
            std::copy(b_lo, b_lo + 3, b_hi);
        }
    }
    void wall(const float *p0, const float *p1, const float *p2, const float *p3, float ox, float oz,
            const GroundColor &top, const GroundColor &low, bool p_solid) {
        wall(p0, p1, p2, p3, ox, oz, top, top, low, low, p_solid);
    }
    // 竖着的四边形 p0 p1（上）p2 p3（下），四角各一个色；按朝外的方向 (ox, oz) 定绕向
    void wall(const float *p0, const float *p1, const float *p2, const float *p3, float ox, float oz,
            const GroundColor &c0, const GroundColor &c1, const GroundColor &c2, const GroundColor &c3, bool p_solid) {
        const int v0 = vertex(p0[0], p0[1], p0[2], ox, 0, oz, c0);
        const int v1 = vertex(p1[0], p1[1], p1[2], ox, 0, oz, c1);
        const int v2 = vertex(p2[0], p2[1], p2[2], ox, 0, oz, c2);
        const int v3 = vertex(p3[0], p3[1], p3[2], ox, 0, oz, c3);
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

    Builder b{ mesh, leaf, std::max(WALL_ROW_M, step) };
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
            top_color[k] = ground_color(p_terrain, c, mesh.origin_x + x, mesh.origin_z + z, uy, step);
            top_id[k] = b.vertex(x, float(c.top), z, ux, uy, uz, top_color[k]);
            top_pos[k * 3] = x, top_pos[k * 3 + 1] = float(c.top), top_pos[k * 3 + 2] = z;
            top_normal[k * 3] = ux, top_normal[k * 3 + 1] = uy, top_normal[k * 3 + 2] = uz;

            nx = float((at(i, j + 1).bottom - at(i, j - 1).bottom) / (2.0 * step));
            nz = float((at(i + 1, j).bottom - at(i - 1, j).bottom) / (2.0 * step));
            len = std::sqrt(nx * nx + 1.0f + nz * nz);
            // 岛底：崖脚附近还带着崖壁最下面的岩色，往里渐成岛底的暗色
            const double deep = std::clamp((at(i, j).top - c.bottom) / 400.0, 0.0, 1.0);
            bottom_id[k] = b.vertex(x, float(c.bottom), z, nx / len, -1.0f / len, nz / len, lerp(rock_color(c.bottom, 1.0), UNDERSIDE, deep));
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
    // 画出来的岛底在节点 (i, j) 处多高：隔格铺时是所在那片的四角插值（块边上就是沿边的线性插值）
    auto rendered_bottom = [&](int i, int j) {
        if (bottom_stride == 1) {
            return double(bottom_pos[node(i, j) * 3 + 1]);
        }
        const int i0 = std::min(i / bottom_stride * bottom_stride, N - bottom_stride), j0 = std::min(j / bottom_stride * bottom_stride, N - bottom_stride);
        const double ti = double(i - i0) / bottom_stride, tj = double(j - j0) / bottom_stride;
        auto y = [&](int ii, int jj) { return double(bottom_pos[node(ii, jj) * 3 + 1]); };
        const double a = y(i0, j0) + (y(i0, j0 + bottom_stride) - y(i0, j0)) * tj;
        const double c = y(i0 + bottom_stride, j0) + (y(i0 + bottom_stride, j0 + bottom_stride) - y(i0 + bottom_stride, j0)) * tj;
        return a + (c - a) * ti;
    };

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
                // 岛底也进碰撞：岛体垂下去上千米，船贴着崖脚往里飞会一头扎进岩体
                b.triangle(bottom_id[b00], bottom_id[b10], bottom_id[b01], true);
                b.triangle(bottom_id[b01], bottom_id[b10], bottom_id[b11], true);
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
                    b.cliff(&top_pos[ka * 3], &top_pos[kb * 3], &bottom_pos[kb * 3], &bottom_pos[ka * 3], e.nx, e.nz, true);
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

                    // 岛底也一样：往上（岛里）翘一圈，从下面看不漏天。翘边的底要贴着画出来的岛底（隔格铺时是插值线，不是这一点的真值），
                    // 翘多高按格距放（相邻两块插值的差随跨度长），但不高过这里的岛体厚度，免得从地面上戳出来
                    const float ya = float(rendered_bottom(e.ai, e.aj)), yb = float(rendered_bottom(e.bi, e.bj));
                    const float reach = float(2.0 + step * bottom_stride);
                    const float lift_a = std::max(0.5f, std::min(reach, 0.9f * (top_pos[ka * 3 + 1] - ya)));
                    const float lift_b = std::max(0.5f, std::min(reach, 0.9f * (top_pos[kb * 3 + 1] - yb)));
                    const int ua = b.vertex(bottom_pos[ka * 3], ya, bottom_pos[ka * 3 + 2], 0, -1, 0, UNDERSIDE);
                    const int ub = b.vertex(bottom_pos[kb * 3], yb, bottom_pos[kb * 3 + 2], 0, -1, 0, UNDERSIDE);
                    const int uc = b.vertex(bottom_pos[kb * 3], yb + lift_b, bottom_pos[kb * 3 + 2], 0, -1, 0, UNDERSIDE);
                    const int ud = b.vertex(bottom_pos[ka * 3], ya + lift_a, bottom_pos[ka * 3 + 2], 0, -1, 0, UNDERSIDE);
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
