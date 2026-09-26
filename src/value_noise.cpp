#include "value_noise.h"

#include <algorithm>
#include <cmath>

namespace {

double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

uint64_t mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double lattice(uint64_t p_seed, int64_t p_i, int64_t p_j) {
    const uint64_t h = mix(p_seed ^ mix(uint64_t(p_i) * 0x9E3779B97F4A7C15ull ^ mix(uint64_t(p_j) + 0x632BE59BD9B4E019ull)));
    return double(h >> 11) * (2.0 / 9007199254740992.0) - 1.0; // [-1, 1)
}

double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
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

double hash_noise(uint64_t p_seed, double p_x, double p_z) {
    const double fx = std::floor(p_x), fz = std::floor(p_z);
    const int64_t i = int64_t(fx), j = int64_t(fz);
    const double tx = fade(p_x - fx), tz = fade(p_z - fz);
    const double a = lattice(p_seed, i, j), b = lattice(p_seed, i + 1, j);
    const double c = lattice(p_seed, i, j + 1), d = lattice(p_seed, i + 1, j + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

double hash_fbm(uint64_t p_seed, double p_x, double p_z, double p_feature, int p_octaves, double p_persistence) {
    double sum = 0.0, norm = 0.0, amp = 1.0, freq = 1.0 / p_feature;
    for (int k = 0; k < p_octaves; k++) {
        // 每层错开一点再转个角度，免得各层格点对齐成网纹
        const double c = 0.8, s = 0.6;
        const double x = p_x * freq, z = p_z * freq;
        const double rx = k % 2 ? c * x - s * z : x, rz = k % 2 ? s * x + c * z : z;
        sum += amp * hash_noise(mix(p_seed + uint64_t(k)), rx + 17.3 * k, rz - 9.1 * k);
        norm += amp;
        amp *= p_persistence;
        freq *= 2.0;
    }
    return sum / norm;
}
