#include "map_image.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "deposit_kinds.h"

namespace {

constexpr GroundColor STREAM{ 0.30f, 0.52f, 0.72f };
constexpr GroundColor TEMPORARY_PATH{ 0.55f, 0.42f, 0.28f };
constexpr GroundColor COAST{ 0.16f, 0.14f, 0.13f };
constexpr double RELIEF_EXAGGERATION = 6.0; // 100 m 一格坡都很缓，山影要夸大才看得出

} // namespace

MapImage build_map_image(const WorldGrid &g) {
    MapImage img;
    img.width = g.cols;
    img.height = g.rows;
    img.rgba.assign(size_t(g.rows) * size_t(g.cols) * 4, 0);
    // 光从西北来
    const double lx = -0.577, ly = 0.577, lz = -0.577;
    auto h = [&](int r, int c) { return double(g.height[g.index(std::clamp(r, 0, g.rows - 1), std::clamp(c, 0, g.cols - 1))]); };
    for (int r = 0; r < g.rows; r++) {
        for (int c = 0; c < g.cols; c++) {
            const size_t k = g.index(r, c);
            if (g.shore[k] <= 0.0f) {
                continue;
            }
            const double dx = (h(r, c + 1) - h(r, c - 1)) / (2.0 * g.res) * RELIEF_EXAGGERATION;
            const double dz = (h(r + 1, c) - h(r - 1, c)) / (2.0 * g.res) * RELIEF_EXAGGERATION;
            const double len = std::sqrt(dx * dx + 1.0 + dz * dz);
            const double lambert = (-dx * lx + ly - dz * lz) / len;
            const float shade = float(std::clamp(0.35 + 0.85 * lambert, 0.3, 1.25));
            GroundColor col = landcover_color(g.landcover[k]);
            if (g.stream[k]) {
                col = g.event_basin_offset > 0 ? TEMPORARY_PATH : STREAM;
            }
            if (g.shore[k] < g.res) {
                col = COAST; // 岸线描一道深边
            }
            uint8_t *px = &img.rgba[k * 4];
            px[0] = uint8_t(std::clamp(col.r * shade, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[1] = uint8_t(std::clamp(col.g * shade, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[2] = uint8_t(std::clamp(col.b * shade, 0.0f, 1.0f) * 255.0f + 0.5f);
            px[3] = 255;
        }
    }
    return img;
}

int dominant_resource(const WorldGrid &g, size_t p_cell, const std::vector<int> &p_patch_kinds, double &r_grade) {
    // 稀的盖常的：先散的三样稀的，再片（浮石、鸟粪、泥炭），再散的三样常的，林木垫底
    struct Field {
        int field;
        const char *key;
        double min;
    };
    static const Field RARE[] = { { WorldGrid::FIELD_ORE, "ore", 0.15 }, { WorldGrid::FIELD_SULFUR, "sulfur", 0.15 }, { WorldGrid::FIELD_PLACER, "placer", 0.15 } };
    static const Field COMMON[] = { { WorldGrid::FIELD_CLAY, "clay", 0.3 }, { WorldGrid::FIELD_GRAVEL, "gravel", 0.3 }, { WorldGrid::FIELD_STONE, "stone", 0.4 } };
    static const char *const PATCHES[] = { "floatstone", "guano", "peat" };
    auto index_of = [](const char *p_key) {
        const DepositKind *kind = deposits::kind(p_key);
        return kind ? int(kind - deposits::KINDS) : -1;
    };
    r_grade = 0.0;
    if (!g.has_resources()) {
        return -1;
    }
    for (const Field &f : RARE) {
        const double v = g.field_cell(f.field, p_cell);
        if (v >= f.min) {
            r_grade = v;
            return index_of(f.key);
        }
    }
    const int patch = g.patch_cell(p_cell);
    const int patch_kind = patch >= 0 && size_t(patch) < p_patch_kinds.size() ? p_patch_kinds[size_t(patch)] : -1;
    for (const char *key : PATCHES) {
        if (patch_kind >= 0 && patch_kind == index_of(key)) {
            r_grade = 1.0;
            return patch_kind;
        }
    }
    for (const Field &f : COMMON) {
        const double v = g.field_cell(f.field, p_cell);
        if (v >= f.min) {
            r_grade = v;
            return index_of(f.key);
        }
    }
    if (patch_kind >= 0 && patch_kind == index_of("timber")) {
        r_grade = 1.0;
        return patch_kind;
    }
    return -1;
}

MapImage build_resource_image(const WorldGrid &g, const std::vector<int> &p_patch_kinds) {
    MapImage img = build_map_image(g);
    for (size_t k = 0; k < g.height.size(); k++) {
        uint8_t *px = &img.rgba[k * 4];
        if (px[3] == 0) {
            continue;
        }
        // 暗灰的山影：资源色压在上面才显眼
        const float lum = (0.3f * px[0] + 0.59f * px[1] + 0.11f * px[2]) / 255.0f;
        float r = 0.2f + 0.45f * lum, gr = r, b = r * 1.05f;
        double grade = 0.0;
        const int kind = dominant_resource(g, k, p_patch_kinds, grade);
        if (kind >= 0) {
            const DepositKind &dk = deposits::KINDS[kind];
            const bool timber = std::string(dk.key) == "timber";
            const float a = timber ? 0.45f : float(std::clamp(0.45 + 0.55 * grade, 0.0, 1.0));
            const float shade = 0.6f + 0.6f * lum;
            r = r + (dk.r * shade - r) * a;
            gr = gr + (dk.g * shade - gr) * a;
            b = b + (dk.b * shade - b) * a;
        }
        px[0] = uint8_t(std::clamp(r, 0.0f, 1.0f) * 255.0f + 0.5f);
        px[1] = uint8_t(std::clamp(gr, 0.0f, 1.0f) * 255.0f + 0.5f);
        px[2] = uint8_t(std::clamp(b, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return img;
}

void world_to_map(const WorldGrid &g, double p_x, double p_z, double &r_u, double &r_v) {
    r_u = g.col_of(p_x) + 0.5;
    r_v = g.row_of(p_z) + 0.5;
}

void map_to_world(const WorldGrid &g, double p_u, double p_v, double &r_x, double &r_z) {
    r_x = g.x0 + (p_u - 0.5) * g.res;
    r_z = g.z0 + (p_v - 0.5) * g.res;
}
