#include "map_image.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr GroundColor STREAM{ 0.30f, 0.52f, 0.72f };
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
                col = STREAM;
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

void world_to_map(const WorldGrid &g, double p_x, double p_z, double &r_u, double &r_v) {
    r_u = g.col_of(p_x) + 0.5;
    r_v = g.row_of(p_z) + 0.5;
}

void map_to_world(const WorldGrid &g, double p_u, double p_v, double &r_x, double &r_z) {
    r_x = g.x0 + (p_u - 0.5) * g.res;
    r_z = g.z0 + (p_v - 0.5) * g.res;
}
