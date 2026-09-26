#include "weather_noise.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int OCTAVES = 4;
constexpr double OCTAVE_SHIFT_X = 37.0, OCTAVE_SHIFT_Y = 17.0; // 各层错开，免得格点叠在一起

// fbm ≥ 门槛 的面积份额 = 覆盖率：覆盖率 0、1/16、…、1 各一个门槛（在 1024 × 1024 格上每格 4 × 4 取样量出来的，单测对照）
constexpr double QUANTILES[WX_QUANTILES] = {
    1.01, 0.7148, 0.6666, 0.6312, 0.6011, 0.5739, 0.5484, 0.5239, 0.4998, 0.4758, 0.4513, 0.4258, 0.3987, 0.3687, 0.3332, 0.2851, -0.01,
};

} // namespace

uint32_t wx_hash(int p_x, int p_y) {
    const uint32_t x = uint32_t(p_x) & uint32_t(WX_PERIOD - 1);
    const uint32_t y = uint32_t(p_y) & uint32_t(WX_PERIOD - 1);
    uint32_t h = (x * 0x27d4eb2du) ^ (y * 0x165667b1u + 0x9e3779b9u);
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

double wx_rand(int p_x, int p_y) {
    return double(wx_hash(p_x, p_y) & 0xFFFFFFu) / 16777215.0;
}

double wx_value(double p_x, double p_y) {
    const double ix = std::floor(p_x), iy = std::floor(p_y);
    const double fx = p_x - ix, fy = p_y - iy;
    const double ux = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0);
    const double uy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0);
    const int cx = int(ix), cy = int(iy);
    const double a = wx_rand(cx, cy), b = wx_rand(cx + 1, cy), c = wx_rand(cx, cy + 1), d = wx_rand(cx + 1, cy + 1);
    const double top = a + (b - a) * ux, bottom = c + (d - c) * ux;
    return top + (bottom - top) * uy;
}

double wx_fbm(double p_x, double p_y) {
    double sum = 0.0, amp = 0.5, norm = 0.0, x = p_x, y = p_y;
    for (int k = 0; k < OCTAVES; k++) {
        sum += amp * wx_value(x + OCTAVE_SHIFT_X * k, y + OCTAVE_SHIFT_Y * k);
        norm += amp;
        x *= 2.0;
        y *= 2.0;
        amp *= 0.5;
    }
    return sum / norm;
}

double wx_threshold(double p_coverage) {
    const double c = std::clamp(p_coverage, 0.0, 1.0) * (WX_QUANTILES - 1);
    const int k = std::min(WX_QUANTILES - 2, int(c));
    const double t = c - k;
    return QUANTILES[k] + (QUANTILES[k + 1] - QUANTILES[k]) * t;
}

const double *wx_quantile_table() {
    return QUANTILES;
}
