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

GroundColor lith_color(int lith) {
    switch(lith) {
        case 1:return {0.72f,0.71f,0.65f};
        case 2:return {0.57f,0.49f,0.37f};
        case 3:return {0.24f,0.28f,0.28f};
        case 4:return {0.43f,0.51f,0.38f};
        case 5:return PUMICE;
        default:return ROCK;
    }
}

// 源岩性是原生格上的类别。只在格心之间连续重建显示颜色，不能把类别编号当数值插值，
// 也不能沿用地表份额的坐标扭曲；语义查询、资源与原始岩性栅格仍保持原类别。
GroundColor surface_rock_color(const WorldGrid &g, double p_x, double p_z) {
    if (g.lith.empty()) return ROCK;
    const double fc = g.col_of(p_x), fr = g.row_of(p_z);
    const int c = int(std::floor(fc)), r = int(std::floor(fr));
    auto color = [&](int rr, int cc) {
        return lith_color(g.lith[g.index(std::clamp(rr, 0, g.rows - 1), std::clamp(cc, 0, g.cols - 1))]);
    };
    return lerp(lerp(color(r, c), color(r, c + 1), fc - c),
            lerp(color(r + 1, c), color(r + 1, c + 1), fc - c), fr - r);
}

// 一个顶点的季相份额（见 TileMesh::season）
struct GroundSeason {
    uint8_t v[4] = { 0, 0, 0, 0 };
};

constexpr int COVER_CLIFF_EDGE = 1, COVER_ALPINE = 3, COVER_FOREST = 4, COVER_SHRUB = 5, COVER_GRASS = 6, COVER_ARABLE = 7, COVER_TERRACE = 8, COVER_WETLAND = 9;

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
GroundColor ground_color(const WorldTerrain &p_terrain, const TerrainSample &p_sample, double p_x, double p_z, double p_normal_y, double p_footprint, GroundSeason &r_season) {
    const CoverBlend &blend = p_terrain.get_cover();
    const CoverStencil st = blend.stencil(p_x, p_z);
    const CoverMix mix = blend.mix(st, p_x, p_z, p_footprint);
    const GroundColor exposed = surface_rock_color(p_terrain.get_grid(), p_x, p_z);
    // 季相份额：草坡、高山草甸、湿地、崖缘的草、虚空外推的地（当草坡）、有河网时的「河道」滩草都是草；可耕地、梯田是田
    double herb = mix.w[0] + 0.5 * mix.w[COVER_CLIFF_EDGE] + mix.w[COVER_ALPINE] + mix.w[COVER_GRASS] + 0.7 * mix.w[COVER_WETLAND] +
            (p_terrain.has_rivers() ? mix.w[COVER_RIVER] : 0.0);
    double field = mix.w[COVER_ARABLE] + mix.w[COVER_TERRACE];
    double shrub = mix.w[COVER_SHRUB];
    double forest = mix.w[COVER_FOREST];
    GroundColor out{ 0.0f, 0.0f, 0.0f };
    for (int k = 0; k < COVER_CLASSES; k++) {
        if (mix.w[k] > 0.0f) {
            // 有河网时水面由河的网格画：「河道」格（一格 100 m，河槽只占其中几米到几十米）的地皮是滩上的湿草，不是水色
            const GroundColor c = k == 2 ? exposed :
                    k == COVER_RIVER && p_terrain.has_rivers() ? MEADOW : landcover_color(k);
            out = { out.r + mix.w[k] * c.r, out.g + mix.w[k] * c.g, out.b + mix.w[k] * c.b };
        }
    }
    // 约 30° 起露岩、45° 全岩；门槛按细噪声进退（远处退掉）
    const double breakup = ROCK_BREAKUP * (1.0 - cover_ramp(p_footprint, 0.0, 0.75 * ROCK_BREAKUP_FEATURE_M));
    const double rock = (0.86 - p_normal_y) / 0.16 + breakup * hash_fbm(COLOR_SEED + 3, p_x, p_z, ROCK_BREAKUP_FEATURE_M, 2);
    double bare = std::clamp(rock, 0.0, 1.0); // 露岩、河床不换季
    if (p_terrain.has_rivers()) {
        out = lerp(out, MEADOW, 0.45 * p_sample.bank);
        out = lerp(out, exposed, rock);
        out = lerp(out, GRAVEL, p_sample.bed); // 河床不论坡度都是卵石
        herb += 0.45 * p_sample.bank * (1.0 - herb);
        bare = 1.0 - (1.0 - bare) * (1.0 - std::clamp(double(p_sample.bed), 0.0, 1.0));
    } else {
        const WorldGrid &g = p_terrain.get_grid();
        out = lerp(out, WET, 0.55 * blend.smooth(st, [&g](size_t k) { return g.stream[k] ? 1.0 : 0.0; }));
        out = lerp(out, exposed, rock);
    }
    const double keep = 1.0 - bare;
    const double shares[4] = { herb, field, shrub, forest };
    for (int k = 0; k < 4; k++) {
        r_season.v[k] = uint8_t(std::lround(std::clamp(shares[k] * keep, 0.0, 1.0) * 255.0));
    }
    const float shade = float(1.0 + 0.07 * hash_fbm(COLOR_SEED + 2, p_x, p_z, 30.0, 3));
    return { out.r * shade, out.g * shade, out.b * shade };
}

struct Builder {
    TileMesh &mesh;
    bool collide;
    double wall_row = WALL_ROW_M; // 崖壁分段高：粗块（远处）按格距分，不必那么细

    int vertex(float x, float y, float z, float nx, float ny, float nz, const GroundColor &c, const GroundSeason &s = GroundSeason()) {
        mesh.positions.insert(mesh.positions.end(), { x, y, z });
        mesh.normals.insert(mesh.normals.end(), { nx, ny, nz });
        mesh.colors.insert(mesh.colors.end(), { c.r, c.g, c.b, 1.0f });
        mesh.season.insert(mesh.season.end(), s.v, s.v + 4);
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

EditGrid TileLayout::edit_grid() const {
    EditGrid g;
    g.x0 = x0;
    g.z0 = z0;
    return g;
}

TileKey TileLayout::leaf_of_chunk(int p_ci, int p_cj) const {
    const int per_leaf = int(std::lround(LEAF_M / (EditGrid::CHUNK * EditGrid::CELL_M))); // 一个叶块每边几块
    const int n = 1 << max_level;
    auto floor_div = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    return { max_level, std::clamp(floor_div(p_cj, per_leaf), 0, n - 1), std::clamp(floor_div(p_ci, per_leaf), 0, n - 1) };
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

TileMesh build_tile_mesh(const WorldTerrain &p_terrain, const TileLayout &p_layout, const TileKey &p_key, const std::vector<int64_t> &p_holes) {
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
    // 改过的块里的粗格：顶面与裙边留给补丁画（build_edit_patch）
    const int per_chunk = int(std::lround(EditGrid::CHUNK * EditGrid::CELL_M / step));
    auto holed = [&](int qi, int qj) {
        if (!leaf || p_holes.empty() || qi < 0 || qj < 0 || qi >= N || qj >= N) {
            return false;
        }
        const int64_t chunk = EditGrid::chunk_key(p_key.iz * (N / per_chunk) + qi / per_chunk, p_key.ix * (N / per_chunk) + qj / per_chunk);
        return std::binary_search(p_holes.begin(), p_holes.end(), chunk);
    };

    Builder b{ mesh, leaf, std::max(WALL_ROW_M, step) };
    std::vector<int> top_id(size_t(N + 1) * (N + 1), -1), bottom_id(size_t(N + 1) * (N + 1), -1);
    std::vector<float> top_pos(size_t(N + 1) * (N + 1) * 3), bottom_pos(size_t(N + 1) * (N + 1) * 3);
    std::vector<GroundColor> top_color(size_t(N + 1) * (N + 1));
    std::vector<GroundSeason> top_season(size_t(N + 1) * (N + 1));
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
            top_color[k] = ground_color(p_terrain, c, mesh.origin_x + x, mesh.origin_z + z, uy, step, top_season[k]);
            top_id[k] = b.vertex(x, float(c.top), z, ux, uy, uz, top_color[k], top_season[k]);
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

    // Clip boundary triangles against the continuous coast field; the same edge roots
    // feed the top, underside and collision wall, so there is no staircase or open seam.
    auto clipped_triangle = [&](size_t a, size_t c, size_t d) {
        struct V { double x, z, shore; };
        auto point = [&](size_t k) { double x=mesh.origin_x+double(k%(N+1))*step, z=mesh.origin_z+double(k/(N+1))*step; return V{x,z,p_terrain.sample(x,z).shore}; };
        std::vector<V> src = {point(a),point(c),point(d)}, poly, roots;
        for (size_t k=0;k<3;k++) {
            const V &u=src[k], &v=src[(k+1)%3];
            if (u.shore>0) poly.push_back(u);
            if ((u.shore>0)!=(v.shore>0)) {
                V lo=u,hi=v;
                for (int it=0;it<24;it++) {
                    V mid{(lo.x+hi.x)*0.5,(lo.z+hi.z)*0.5,0}; mid.shore=p_terrain.sample(mid.x,mid.z).shore;
                    if ((mid.shore>0)==(lo.shore>0)) lo=mid; else hi=mid;
                }
                V root{(lo.x+hi.x)*0.5,(lo.z+hi.z)*0.5,0}; poly.push_back(root); roots.push_back(root);
            }
        }
        if (poly.size()<3) return;
        std::vector<int> top,low;
        for (const V &v:poly) {
            const auto q=p_terrain.sample(v.x,v.z);
            const double e=std::max(0.5,step*0.5);
            const float nx=float(-(p_terrain.sample(v.x+e,v.z).top-p_terrain.sample(v.x-e,v.z).top)/(2*e));
            const float nz=float(-(p_terrain.sample(v.x,v.z+e).top-p_terrain.sample(v.x,v.z-e).top)/(2*e));
            const float inv=1/std::sqrt(1+nx*nx+nz*nz);
            GroundSeason season; const auto color=ground_color(p_terrain,q,v.x,v.z,inv,step,season);
            top.push_back(b.vertex(float(v.x-mesh.origin_x),float(q.top),float(v.z-mesh.origin_z),nx*inv,inv,nz*inv,color,season));
            low.push_back(b.vertex(float(v.x-mesh.origin_x),float(q.bottom),float(v.z-mesh.origin_z),0,-1,0,UNDERSIDE));
        }
        for (size_t k=1;k+1<poly.size();k++) { b.triangle(top[0],top[k],top[k+1],true); b.triangle(low[0],low[k+1],low[k],true); }
        if (roots.size()==2) {
            const auto u=p_terrain.sample(roots[0].x,roots[0].z),v=p_terrain.sample(roots[1].x,roots[1].z);
            float p0[3]={float(roots[0].x-mesh.origin_x),float(u.top),float(roots[0].z-mesh.origin_z)};
            float p1[3]={float(roots[1].x-mesh.origin_x),float(v.top),float(roots[1].z-mesh.origin_z)};
            float p2[3]={p1[0],float(v.bottom),p1[2]},p3[3]={p0[0],float(u.bottom),p0[2]};
            double x=(roots[0].x+roots[1].x)*0.5,z=(roots[0].z+roots[1].z)*0.5;
            double nx=-(p_terrain.sample(x+1,z).shore-p_terrain.sample(x-1,z).shore),nz=-(p_terrain.sample(x,z+1).shore-p_terrain.sample(x,z-1).shore);
            double len=std::hypot(nx,nz); if(len>0) b.cliff(p0,p1,p2,p3,float(nx/len),float(nz/len),true);
        }
    };

    // 裙边垂多深：比相邻两级精度之差大就行
    const float skirt = float(2.0 + step);
    // 近处网格沿源河心与河岸切分；亚格细河无需放宽，也不会被一张跨河三角形盖住。
    struct RiverCut { RiverVertex a,b; };
    std::vector<RiverCut> river_cuts;
    if(p_terrain.get_grid().source_rivers && p_terrain.has_rivers() && step<=32) {
        p_terrain.get_rivers().segments_in(mesh.origin_x,mesh.origin_z,mesh.origin_x+N*step,mesh.origin_z+N*step,
            [&](int si,int vi) { const auto &v=p_terrain.get_rivers().get_stems()[size_t(si)].v; river_cuts.push_back({v[size_t(vi)],v[size_t(vi)+1]}); });
    }
    auto river_quad=[&](int i,int j) {
        struct P { double x,z; };
        const double x=mesh.origin_x+j*step,z=mesh.origin_z+i*step;
        std::vector<std::vector<P>> polygons{{{x,z},{x+step,z},{x+step,z+step},{x,z+step}}};
        bool changed=false;
        for(const auto &cut:river_cuts) {
            const auto &a=cut.a,&c=cut.b;
            const double margin=std::max(a.half(),c.half());
            if(std::max(a.x,c.x)+margin<x || std::min(a.x,c.x)-margin>x+step || std::max(a.z,c.z)+margin<z || std::min(a.z,c.z)-margin>z+step) continue;
            const double dx=c.x-a.x,dz=c.z-a.z,len=std::hypot(dx,dz);
            if(len<1e-6) continue;
            for(double offset:{-1.0,-0.5,0.0,0.5,1.0}) {
                auto side=[&](const P &p) {
                    const double t=((p.x-a.x)*dx+(p.z-a.z)*dz)/(len*len);
                    return ((p.z-a.z)*dx-(p.x-a.x)*dz)/len-offset*(a.half()+t*(c.half()-a.half()));
                };
                std::vector<std::vector<P>> next;
                for(const auto &poly:polygons) {
                    std::vector<P> lo,hi;
                    for(size_t k=0;k<poly.size();++k) {
                        const P p=poly[k],q=poly[(k+1)%poly.size()]; const double fp=side(p),fq=side(q);
                        if(fp<=1e-9) lo.push_back(p); if(fp>=-1e-9) hi.push_back(p);
                        if((fp<0 && fq>0)||(fp>0 && fq<0)) {
                            const double t=fp/(fp-fq); P r{p.x+t*(q.x-p.x),p.z+t*(q.z-p.z)}; lo.push_back(r); hi.push_back(r);
                        }
                    }
                    if(lo.size()>=3 && hi.size()>=3) { next.push_back(std::move(lo)); next.push_back(std::move(hi)); changed=true; }
                    else next.push_back(poly);
                }
                polygons=std::move(next);
            }
        }
        if(!changed) return false;
        for(const auto &poly:polygons) {
            std::vector<int> ids;
            for(const auto &p:poly) {
                const auto sample=p_terrain.sample(p.x,p.z); GroundSeason season;
                // 切分点仍按真实地面求坡度。窄槽的差分不能跨出整条河，
                // 否则厘米河的两岸会被两米网格平均成朝上的法线。
                double e=step;
                RiverHit nearby;
                const auto &rivers=p_terrain.get_rivers();
                if(rivers.nearest(p.x,p.z,rivers.max_reach(),nearby) && nearby.at.half()>0 && nearby.distance<=nearby.at.half()+step) {
                    e=std::max(1e-4,std::min(e,nearby.at.half()*0.25)); // 0.1 mm 仅防数值差分退化，不改河槽。
                }
                const float nx=float(-(p_terrain.sample(p.x+e,p.z).top-p_terrain.sample(p.x-e,p.z).top)/(2*e));
                const float nz=float(-(p_terrain.sample(p.x,p.z+e).top-p_terrain.sample(p.x,p.z-e).top)/(2*e));
                const float inv=1/std::sqrt(1+nx*nx+nz*nz);
                const GroundColor color=ground_color(p_terrain,sample,p.x,p.z,inv,step,season);
                ids.push_back(b.vertex(float(p.x-mesh.origin_x),float(sample.top),float(p.z-mesh.origin_z),nx*inv,inv,nz*inv,color,season));
            }
            for(size_t k=1;k+1<ids.size();++k) b.triangle(ids[0],ids[k],ids[k+1],true);
        }
        return true;
    };
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (!solid(i, j)) {
                if (p_terrain.get_grid().source_landscape && !holed(i, j)) {
                    const size_t a=node(i,j), c=node(i,j+1), d=node(i+1,j), e=node(i+1,j+1);
                    clipped_triangle(a,c,d); clipped_triangle(c,e,d);
                }
                continue;
            }
            mesh.land_quads++;
            const size_t k00 = node(i, j), k01 = node(i, j + 1), k10 = node(i + 1, j), k11 = node(i + 1, j + 1);
            const bool hole = holed(i, j);
            // 从上往下看：x 向右、z 向下，(i,j) → (i,j+1) → (i+1,j) 是顺时针
            if (!hole) {
                if(!river_quad(i,j)) {
                    b.triangle(top_id[k00], top_id[k01], top_id[k10], true);
                    b.triangle(top_id[k01], top_id[k11], top_id[k10], true);
                }
            }
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
                if (!solid(e.qi, e.qj) && !p_terrain.get_grid().source_landscape) {
                    // 崖壁：从顶面垂到岛底
                    b.cliff(&top_pos[ka * 3], &top_pos[kb * 3], &bottom_pos[kb * 3], &bottom_pos[ka * 3], e.nx, e.nz, true);
                } else if (on_border && !hole) {
                    // 裙边：邻块精度不同，两边的面对不齐，垂一圈同色的面把缝遮住（两面都画，从哪边看都有）
                    const float a_low[3] = { top_pos[ka * 3], top_pos[ka * 3 + 1] - skirt, top_pos[ka * 3 + 2] };
                    const float b_low[3] = { top_pos[kb * 3], top_pos[kb * 3 + 1] - skirt, top_pos[kb * 3 + 2] };
                    const int va = b.vertex(top_pos[ka * 3], top_pos[ka * 3 + 1], top_pos[ka * 3 + 2], top_normal[ka * 3], top_normal[ka * 3 + 1], top_normal[ka * 3 + 2], top_color[ka], top_season[ka]);
                    const int vb = b.vertex(top_pos[kb * 3], top_pos[kb * 3 + 1], top_pos[kb * 3 + 2], top_normal[kb * 3], top_normal[kb * 3 + 1], top_normal[kb * 3 + 2], top_color[kb], top_season[kb]);
                    const int vc = b.vertex(b_low[0], b_low[1], b_low[2], top_normal[kb * 3], top_normal[kb * 3 + 1], top_normal[kb * 3 + 2], top_color[kb], top_season[kb]);
                    const int vd = b.vertex(a_low[0], a_low[1], a_low[2], top_normal[ka * 3], top_normal[ka * 3 + 1], top_normal[ka * 3 + 2], top_color[ka], top_season[ka]);
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

std::vector<int64_t> tile_edit_chunks(const TileLayout &p_layout, const TerrainEdits &p_edits, const TileKey &p_key) {
    std::vector<int64_t> out;
    if (p_key.level != p_layout.get_max_level() || p_edits.empty()) {
        return out;
    }
    double x = 0.0, z = 0.0;
    p_layout.corner(p_key, x, z);
    const double s = p_layout.size(p_key.level);
    // 往里缩半格：块边正好落在叶块边上，别把隔壁叶块的块也算进来
    out = p_edits.chunks_in(x + 0.5, z + 0.5, x + s - 0.5, z + s - 0.5);
    std::sort(out.begin(), out.end());
    return out;
}

double soil_depth_at(const WorldTerrain &p_terrain, double p_x, double p_z) {
    // 地表类：0 虚空 1 崖缘 2 裸岩 3 高山草甸 4 林地 5 灌丛 6 草坡 7 可耕地 8 梯田 9 湿地 10 河道 11 湖
    static const double DEPTH[COVER_CLASSES] = { 0.0, 0.5, 0.0, 1.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.5, 1.0, 0.0 };
    const CoverMix mix = p_terrain.cover(p_x, p_z);
    double d = 0.0;
    for (int k = 0; k < COVER_CLASSES; k++) {
        d += mix.w[k] * DEPTH[k];
    }
    return std::floor(d / EditGrid::STEP_M + 0.5) * EditGrid::STEP_M;
}

namespace {

constexpr GroundColor DUG_SOIL{ 0.44f, 0.34f, 0.24f }; // 挖开的土（潮）
constexpr GroundColor DUG_STONE{ 0.54f, 0.52f, 0.49f }; // 挖到土层以下：石
constexpr GroundColor RAISED{ 0.52f, 0.41f, 0.29f }; // 垒上来的土（夯过）
constexpr GroundColor WALL_SOIL{ 0.47f, 0.37f, 0.27f };
constexpr GroundColor WALL_STONE{ 0.57f, 0.55f, 0.52f };
constexpr GroundColor WALL_RAISED{ 0.51f, 0.41f, 0.29f };
constexpr uint64_t BAND_SEED = 0xEA27B5ull;
constexpr float SKIRT_INNER_M = 0.3f; // 块里没改过的格挨着粗格垂这么深（只遮 T 形接缝）
constexpr float TILE_EDGE_INSET_M = 0.01f; // 坎在叶块边上时往低的那格让这么多（邻块的裙边在那条线上）

GroundColor shade(const GroundColor &p_c, double p_k) {
    const float k = float(p_k);
    return { p_c.r * k, p_c.g * k, p_c.b * k };
}

} // namespace

TileMesh build_edit_patch(const WorldTerrain &p_terrain, const TileLayout &p_layout, const TerrainEdits &p_edits, int p_ci, int p_cj) {
    constexpr int C = EditGrid::CHUNK;
    constexpr int N = TileLayout::CELLS;
    TileMesh mesh;
    const TileKey key = p_layout.leaf_of_chunk(p_ci, p_cj);
    mesh.key = key;
    p_layout.corner(key, mesh.origin_x, mesh.origin_z);
    const double step = p_layout.step(key.level);
    const int per = int(std::lround(C * EditGrid::CELL_M / step)); // 一块每边几个粗格（8）
    const int fine = int(std::lround(step / EditGrid::CELL_M)); // 一个粗格每边几格（2）
    const int q0i = p_ci * per - key.iz * N, q0j = p_cj * per - key.ix * N; // 这块的第一个粗格在叶块里的行列
    const int gi0 = p_ci * C, gj0 = p_cj * C; // 这块的第一格（全局格号）

    // 粗格的节点：与叶块逐位同样取样、同样算法线与地皮色（外面多一圈好算法线）
    const int W = per + 3;
    std::vector<TerrainSample> s(size_t(W) * W);
    auto at = [&](int di, int dj) -> TerrainSample & { return s[size_t(di + 1) * W + size_t(dj + 1)]; };
    for (int di = -1; di <= per + 1; di++) {
        for (int dj = -1; dj <= per + 1; dj++) {
            at(di, dj) = p_terrain.sample(mesh.origin_x + (q0j + dj) * step, mesh.origin_z + (q0i + di) * step);
        }
    }
    struct Node {
        double top = 0.0;
        double n[3] = { 0.0, 1.0, 0.0 };
        double c[3] = { 0.0, 0.0, 0.0 };
        double season[4] = { 0.0, 0.0, 0.0, 0.0 };
    };
    std::vector<Node> nodes(size_t(per + 1) * (per + 1));
    auto node = [&](int di, int dj) -> Node & { return nodes[size_t(di) * (per + 1) + size_t(dj)]; };
    for (int di = 0; di <= per; di++) {
        for (int dj = 0; dj <= per; dj++) {
            const TerrainSample &c = at(di, dj);
            Node &nd = node(di, dj);
            nd.top = c.top;
            const int i = q0i + di, j = q0j + dj;
            const float x = float(j * step), z = float(i * step);
            const float nx = float(-(at(di, dj + 1).top - at(di, dj - 1).top) / (2.0 * step));
            const float nz = float(-(at(di + 1, dj).top - at(di - 1, dj).top) / (2.0 * step));
            const float len = std::sqrt(nx * nx + 1.0f + nz * nz);
            const float ux = nx / len, uy = 1.0f / len, uz = nz / len;
            nd.n[0] = ux, nd.n[1] = uy, nd.n[2] = uz;
            if (!c.land) {
                continue;
            }
            GroundSeason season;
            const GroundColor col = ground_color(p_terrain, c, mesh.origin_x + x, mesh.origin_z + z, uy, step, season);
            nd.c[0] = col.r, nd.c[1] = col.g, nd.c[2] = col.b;
            for (int k = 0; k < 4; k++) {
                nd.season[k] = season.v[k];
            }
        }
    }
    auto solid = [&](int qi, int qj) {
        return at(qi, qj).land && at(qi, qj + 1).land && at(qi + 1, qj).land && at(qi + 1, qj + 1).land;
    };
    // 粗格 (qi, qj) 里的点 (u, v)（u 沿 x、v 沿 z，0..1）：按叶块的两个三角形插值（对角线 (0,1)–(1,0)）
    struct Attr {
        double y = 0.0, n[3] = { 0.0, 1.0, 0.0 }, c[3] = { 0.0, 0.0, 0.0 }, season[4] = { 0.0, 0.0, 0.0, 0.0 };
    };
    auto interp = [&](int qi, int qj, double u, double v) {
        const Node &a00 = node(qi, qj), &a01 = node(qi, qj + 1), &a10 = node(qi + 1, qj), &a11 = node(qi + 1, qj + 1);
        double w00, w01, w10, w11;
        if (u + v <= 1.0) {
            w00 = 1.0 - u - v, w01 = u, w10 = v, w11 = 0.0;
        } else {
            w00 = 0.0, w01 = 1.0 - v, w10 = 1.0 - u, w11 = u + v - 1.0;
        }
        Attr out;
        out.y = w00 * a00.top + w01 * a01.top + w10 * a10.top + w11 * a11.top;
        for (int k = 0; k < 3; k++) {
            out.n[k] = w00 * a00.n[k] + w01 * a01.n[k] + w10 * a10.n[k] + w11 * a11.n[k];
            out.c[k] = w00 * a00.c[k] + w01 * a01.c[k] + w10 * a10.c[k] + w11 * a11.c[k];
        }
        for (int k = 0; k < 4; k++) {
            out.season[k] = w00 * a00.season[k] + w01 * a01.season[k] + w10 * a10.season[k] + w11 * a11.season[k];
        }
        const double len = std::sqrt(out.n[0] * out.n[0] + out.n[1] * out.n[1] + out.n[2] * out.n[2]);
        for (int k = 0; k < 3; k++) {
            out.n[k] = len > 0.0 ? out.n[k] / len : (k == 1 ? 1.0 : 0.0);
        }
        return out;
    };
    // 块里第 (fi, fj) 格的角 (ci, cj ∈ {0, 1})
    auto corner_attr = [&](int fi, int fj, int ci, int cj) {
        return interp(fi / fine, fj / fine, double(fj % fine + cj) / fine, double(fi % fine + ci) / fine);
    };
    auto local_x = [&](int fj) { return (q0j + fj / fine) * step + double(fj % fine) * EditGrid::CELL_M; }; // 格的西边（叶块局部坐标）
    auto local_z = [&](int fi) { return (q0i + fi / fine) * step + double(fi % fine) * EditGrid::CELL_M; };
    std::vector<double> soil(size_t(C) * C, 0.0);
    auto soil_of = [&](int fi, int fj) -> double & { return soil[size_t(fi) * C + size_t(fj)]; };
    for (int fi = 0; fi < C; fi++) {
        for (int fj = 0; fj < C; fj++) {
            soil_of(fi, fj) = soil_depth_at(p_terrain, mesh.origin_x + local_x(fj) + 0.5, mesh.origin_z + local_z(fi) + 0.5);
        }
    }
    auto cell_solid = [&](int fi, int fj) { return solid(fi / fine, fj / fine); };
    auto season_of = [](const Attr &p_a) {
        GroundSeason se;
        for (int k = 0; k < 4; k++) {
            se.v[k] = uint8_t(std::lround(std::clamp(p_a.season[k], 0.0, 255.0)));
        }
        return se;
    };
    auto color_of = [](const Attr &p_a) { return GroundColor{ float(p_a.c[0]), float(p_a.c[1]), float(p_a.c[2]) }; };

    Builder b{ mesh, true };
    auto push = [&](double x, double y, double z, const double *n, const GroundColor &c, const GroundSeason &se) {
        return b.vertex(float(x), float(y), float(z), float(n[0]), float(n[1]), float(n[2]), c, se);
    };
    const double H = EditGrid::STEP_M;

    // 顶面：每格自己四个顶点（格与格级数不同，角上不共用）
    for (int fi = 0; fi < C; fi++) {
        for (int fj = 0; fj < C; fj++) {
            if (!cell_solid(fi, fj)) {
                continue;
            }
            mesh.land_quads++;
            const int d = p_edits.step_at(gi0 + fi, gj0 + fj);
            int id[2][2];
            for (int ci = 0; ci < 2; ci++) {
                for (int cj = 0; cj < 2; cj++) {
                    const Attr a = corner_attr(fi, fj, ci, cj);
                    GroundColor c = color_of(a);
                    GroundSeason se;
                    if (d == 0) {
                        se = season_of(a);
                    } else if (d > 0) {
                        c = RAISED;
                    } else {
                        c = -d * H >= soil_of(fi, fj) - 1e-9 ? DUG_STONE : DUG_SOIL;
                    }
                    const double x = local_x(fj) + cj * EditGrid::CELL_M, z = local_z(fi) + ci * EditGrid::CELL_M;
                    id[ci][cj] = push(x, a.y + d * H, z, a.n, c, se);
                }
            }
            b.triangle(id[0][0], id[0][1], id[1][0], true);
            b.triangle(id[0][1], id[1][1], id[1][0], true);
        }
    }

    // 坎与裙边：每格看四个邻格
    struct Dir {
        int di, dj;
    };
    const Dir dirs[4] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (int fi = 0; fi < C; fi++) {
        for (int fj = 0; fj < C; fj++) {
            if (!cell_solid(fi, fj)) {
                continue;
            }
            const int da = p_edits.step_at(gi0 + fi, gj0 + fj);
            for (const Dir &dir : dirs) {
                const int ni = fi + dir.di, nj = fj + dir.dj;
                const bool inside = ni >= 0 && nj >= 0 && ni < C && nj < C;
                const int db = p_edits.step_at(gi0 + ni, gj0 + nj);
                const bool other_active = !inside && p_edits.chunk_active(EditGrid::chunk_of(gi0 + ni), EditGrid::chunk_of(gj0 + nj));
                // 两格共有的那条边的两个端点（这一格的角）
                int c0i, c0j, c1i, c1j;
                if (dir.di != 0) {
                    c0i = c1i = dir.di > 0 ? 1 : 0;
                    c0j = 0, c1j = 1;
                } else {
                    c0j = c1j = dir.dj > 0 ? 1 : 0;
                    c0i = 0, c1i = 1;
                }
                const double x0 = local_x(fj) + c0j * EditGrid::CELL_M, z0 = local_z(fi) + c0i * EditGrid::CELL_M;
                const double x1 = local_x(fj) + c1j * EditGrid::CELL_M, z1 = local_z(fi) + c1i * EditGrid::CELL_M;
                const Attr a0 = corner_attr(fi, fj, c0i, c0j), a1 = corner_attr(fi, fj, c1i, c1j);
                const double n0 = a0.y, n1 = a1.y;
                if (da == db) {
                    // 裙边：没改过的格挨着粗格（隔壁块没改动，由叶块画）——遮住 T 形接缝；在叶块边上垂得同叶块的裙边一样深
                    if (da == 0 && !inside && !other_active) {
                        const int qi = q0i + fi / fine, qj = q0j + fj / fine;
                        const bool tile_edge = (dir.di < 0 && qi == 0) || (dir.di > 0 && qi == N - 1) || (dir.dj < 0 && qj == 0) || (dir.dj > 0 && qj == N - 1);
                        const double depth = tile_edge ? 2.0 + step : SKIRT_INNER_M;
                        const GroundColor g0 = color_of(a0), g1 = color_of(a1);
                        const GroundSeason s0 = season_of(a0), s1 = season_of(a1);
                        const int va = push(x0, n0, z0, a0.n, g0, s0), vb = push(x1, n1, z1, a1.n, g1, s1);
                        const int vc = push(x1, n1 - depth, z1, a1.n, g1, s1), vd = push(x0, n0 - depth, z0, a0.n, g0, s0);
                        b.triangle(va, vb, vc, false);
                        b.triangle(va, vc, vd, false);
                        b.triangle(va, vc, vb, false);
                        b.triangle(va, vd, vc, false);
                    }
                    continue;
                }
                // 坎归谁出：同一块里、隔壁块也有改动时归高的那格；隔壁块没改动（叶块画的）就归这一块
                if ((inside || other_active) && da < db) {
                    continue;
                }
                const bool a_high = da > db;
                const int lo = std::min(da, db), hi = std::max(da, db);
                // 朝外 = 朝低的那格
                const float ox = float(a_high ? dir.dj : -dir.dj), oz = float(a_high ? dir.di : -dir.di);
                // 坎后面是高的那格的土：土层按它算
                const double soil_hi = a_high ? soil_of(fi, fj)
                                              : inside ? soil_of(ni, nj)
                                                       : soil_depth_at(p_terrain, mesh.origin_x + local_x(fj) + 0.5 + dir.dj, mesh.origin_z + local_z(fi) + 0.5 + dir.di);
                // 在叶块边上：往低的那格让一点
                double sx = 0.0, sz = 0.0;
                const bool on_tile_edge = (dir.dj != 0 && (std::abs(x0) < 1e-6 || std::abs(x0 - N * step) < 1e-6)) ||
                        (dir.di != 0 && (std::abs(z0) < 1e-6 || std::abs(z0 - N * step) < 1e-6));
                if (on_tile_edge) {
                    sx = ox * TILE_EDGE_INSET_M;
                    sz = oz * TILE_EDGE_INSET_M;
                }
                for (int level = lo; level < hi; level++) {
                    GroundColor c;
                    if (level >= 0) {
                        c = WALL_RAISED;
                    } else {
                        c = -(level + 1) * H >= soil_hi - 1e-9 ? WALL_STONE : WALL_SOIL;
                    }
                    c = shade(c, 1.0 + 0.05 * hash_noise(BAND_SEED, level * 1.7, 0.0));
                    const float p0[3] = { float(x0 + sx), float(n0 + (level + 1) * H), float(z0 + sz) };
                    const float p1[3] = { float(x1 + sx), float(n1 + (level + 1) * H), float(z1 + sz) };
                    const float p2[3] = { float(x1 + sx), float(n1 + level * H), float(z1 + sz) };
                    const float p3[3] = { float(x0 + sx), float(n0 + level * H), float(z0 + sz) };
                    b.wall(p0, p1, p2, p3, ox, oz, c, c, true);
                }
            }
        }
    }
    return mesh;
}
