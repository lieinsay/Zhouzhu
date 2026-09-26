#include "cover_blend.h"

#include <cmath>

#include "value_noise.h"

namespace {

constexpr uint64_t STREAM_WARP_X = 0xC0F3A1ull;
constexpr uint64_t STREAM_WARP_Z = 0xC0F3B2ull;
constexpr uint64_t STREAM_BEND_X = 0xBE3D1ull;
constexpr uint64_t STREAM_BEND_Z = 0xBE3D2ull;
constexpr uint64_t STREAM_BREAKUP = 0xB7EAC0ull;
constexpr int BEND_OCTAVES = 2;
constexpr int WARP_OCTAVES = 3;
constexpr int BREAKUP_OCTAVES = 2;
constexpr double LOGISTIC_10_90 = 2.1972245773362196; // ln 9：logistic 从一成到九成，对数几率走 ±ln 9

uint64_t mix64(uint64_t p_a, uint64_t p_b) {
    uint64_t z = p_a ^ (p_b + 0x9E3779B97F4A7C15ull + (p_a << 6) + (p_a >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

double smooth01(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// 均匀三次 B 样条：t ∈ [0, 1) 时四个控制点（t 左边两个、右边两个）的权重，和为 1、处处非负，
// 插值出来 C2 连续（比双线性圆，不会有 Catmull-Rom 那样的过冲，份额一直在 [0, 1] 里）
void bspline(double t, double r_w[4]) {
    const double u = 1.0 - t, t2 = t * t, t3 = t2 * t;
    r_w[0] = u * u * u / 6.0;
    r_w[1] = (3.0 * t3 - 6.0 * t2 + 4.0) / 6.0;
    r_w[2] = (-3.0 * t3 + 3.0 * t2 + 3.0 * t + 1.0) / 6.0;
    r_w[3] = t3 / 6.0;
}

} // namespace

double cover_ramp(double p_value, double p_lo, double p_hi) {
    return p_hi == p_lo ? (p_value >= p_hi ? 1.0 : 0.0) : smooth01((p_value - p_lo) / (p_hi - p_lo));
}

CoverBlend::CoverBlend(const WorldGrid &p_grid, uint64_t p_seed, const CoverRules &p_rules) :
        grid(&p_grid), seed(mix64(p_seed, 0xC0FE12ull)), rules(p_rules) {}

CoverStencil CoverBlend::stencil(double p_x, double p_z) const {
    CoverStencil s;
    const WorldGrid &g = *grid;
    s.wx = p_x + rules.bend_m * hash_fbm(seed ^ STREAM_BEND_X, p_x, p_z, rules.bend_feature_m, BEND_OCTAVES) +
            rules.warp_m * hash_fbm(seed ^ STREAM_WARP_X, p_x, p_z, rules.warp_feature_m, WARP_OCTAVES);
    s.wz = p_z + rules.bend_m * hash_fbm(seed ^ STREAM_BEND_Z, p_x, p_z, rules.bend_feature_m, BEND_OCTAVES) +
            rules.warp_m * hash_fbm(seed ^ STREAM_WARP_Z, p_x, p_z, rules.warp_feature_m, WARP_OCTAVES);
    const double fc = g.col_of(s.wx), fr = g.row_of(s.wz);
    const int c1 = int(std::floor(fc)), r1 = int(std::floor(fr));
    double wc[4], wr[4];
    bspline(fc - c1, wc);
    bspline(fr - r1, wr);
    for (int i = 0; i < 4; i++) {
        const int r = std::clamp(r1 - 1 + i, 0, g.rows - 1);
        for (int j = 0; j < 4; j++) {
            const int c = std::clamp(c1 - 1 + j, 0, g.cols - 1);
            s.cell[i * 4 + j] = g.index(r, c);
            s.weight[i * 4 + j] = float(wr[i] * wc[j]);
            const double dx = s.wx - (g.x0 + c * g.res), dz = s.wz - (g.z0 + r * g.res);
            s.dist2[i * 4 + j] = float(dx * dx + dz * dz);
        }
    }
    s.nearest = g.nearest_cell(s.wx, s.wz);
    return s;
}

CoverMix CoverBlend::mix(const CoverStencil &p_stencil, double p_x, double p_z, double p_footprint) const {
    CoverMix m;
    m.cell = p_stencil.nearest;
    blend(p_stencil, p_x, p_z, p_footprint, STREAM_COVER, COVER_CLASSES, [this](size_t k) { return grid->landcover[k]; }, m.w);
    return m;
}

void CoverBlend::resolve(const CoverStencil &p_stencil, double p_x, double p_z, double p_footprint, uint64_t p_stream, int p_n, const int *p_labels, float *r_out) const {
    std::fill(r_out, r_out + p_n, 0.0f);
    bool uniform = true;
    for (int k = 1; k < 16; k++) {
        uniform = uniform && p_labels[k] == p_labels[0];
    }
    if (uniform) { // 4×4 格全是一类：绝大多数点走这条
        r_out[p_labels[0]] = 1.0f;
        return;
    }
    // 取样越稀越换成 B 样条的平滑份额：顶点隔半格以上全换
    const double far = smooth01(p_footprint / (0.5 * grid->res));
    if (far < 1.0) {
        // 软最近格：相邻两格格心隔 res，过交界线 u 米对数几率差 2·res·u / τ；过渡带宽 W（一成到九成）⇔ τ = W·res / ln 9
        const double tau = std::max(1.0, rules.transition_m) * grid->res / LOGISTIC_10_90;
        const double breakup = rules.breakup_m * (1.0 - smooth01(p_footprint / (0.75 * rules.breakup_feature_m)));
        float d_min = p_stencil.dist2[0];
        for (int k = 1; k < 16; k++) {
            d_min = std::min(d_min, p_stencil.dist2[k]);
        }
        double sum[MAX_LABELS] = {};
        bool present[MAX_LABELS] = {};
        for (int k = 0; k < 16; k++) {
            sum[p_labels[k]] += std::exp(-double(p_stencil.dist2[k] - d_min) / tau);
            present[p_labels[k]] = true;
        }
        double total = 0.0;
        for (int l = 0; l < p_n; l++) {
            if (!present[l]) {
                continue;
            }
            if (breakup > 0.0) {
                // 对数份额加 n·breakup·2·res/τ：两类的噪声差 Δn 把交界线挪 Δn·breakup 米
                sum[l] *= std::exp(breakup * 2.0 * grid->res / tau *
                        hash_fbm(mix64(seed ^ STREAM_BREAKUP, p_stream * 64 + uint64_t(l)), p_x, p_z, rules.breakup_feature_m, BREAKUP_OCTAVES));
            }
            total += sum[l];
        }
        for (int l = 0; l < p_n; l++) {
            r_out[l] = float((1.0 - far) * sum[l] / total);
        }
    }
    if (far > 0.0) {
        for (int k = 0; k < 16; k++) {
            r_out[p_labels[k]] += float(far * p_stencil.weight[k]);
        }
    }
}
