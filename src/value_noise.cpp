#include "value_noise.h"

#include <algorithm>
#include <cmath>

namespace {

double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

LatticeNoise::LatticeNoise(Rng &p_rng, double p_x0, double p_y0, double p_x1, double p_y1, double p_cell) {
    cell = p_cell;
    x0 = p_x0 - 2.0 * cell;
    y0 = p_y0 - 2.0 * cell;
    nx = int(std::ceil((p_x1 - x0) / cell)) + 3;
    ny = int(std::ceil((p_y1 - y0) / cell)) + 3;
    lattice.resize(size_t(nx) * size_t(ny));
    for (double &v : lattice) {
        v = p_rng.uniform(-1.0, 1.0);
    }
}

double LatticeNoise::sample(double p_x, double p_y) const {
    const double fx = (p_x - x0) / cell;
    const double fy = (p_y - y0) / cell;
    const int i0 = std::clamp(int(std::floor(fy)), 0, ny - 2);
    const int j0 = std::clamp(int(std::floor(fx)), 0, nx - 2);
    const double ty = smoothstep(std::clamp(fy - i0, 0.0, 1.0));
    const double tx = smoothstep(std::clamp(fx - j0, 0.0, 1.0));
    const double *row0 = &lattice[size_t(i0) * nx + j0];
    const double *row1 = row0 + nx;
    return row0[0] * (1 - ty) * (1 - tx) + row0[1] * (1 - ty) * tx + row1[0] * ty * (1 - tx) + row1[1] * ty * tx;
}

FractalNoise::FractalNoise(Rng &p_rng, double p_x0, double p_y0, double p_x1, double p_y1, double p_feature,
        int p_octaves, double p_persistence, double p_lacunarity) {
    double amp = 1.0;
    double cell = p_feature;
    total = 0.0;
    const int n = std::max(1, p_octaves);
    layers.reserve(n);
    for (int k = 0; k < n; k++) {
        layers.push_back({ amp, LatticeNoise(p_rng, p_x0, p_y0, p_x1, p_y1, cell) });
        total += amp;
        amp *= p_persistence;
        cell /= p_lacunarity;
    }
}

double FractalNoise::sample(double p_x, double p_y) const {
    double out = 0.0;
    for (const Layer &layer : layers) {
        out += layer.amp * layer.noise.sample(p_x, p_y);
    }
    return out / total;
}
