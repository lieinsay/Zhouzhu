#include "scipy_edt.h"

#include <cmath>

namespace scipy_nd {

namespace {

// scipy/ndimage/src/ni_morphology.c 的 _VoronoiFT（rank = 2，sampling = NULL）：沿一条线（长 p_len）做一维 Voronoi。
// 这条线上第 ii 格的特征是 (fr[ii], fc[ii])（fr < 0 = 没有特征）；d = 这条线的方向（0 = 沿行号，即一列；1 = 沿列号，即一行）；
// coor = 这条线在另一维上的坐标（coor[d] 不用）
void voronoi_ft(int32_t *p_fr, int32_t *p_fc, int64_t p_stride, int64_t p_len, const int64_t p_coor[2], int p_d, std::vector<int64_t> &r_f0,
        std::vector<int64_t> &r_f1, std::vector<int64_t> &r_g) {
    int64_t l = -1;
    for (int64_t ii = 0; ii < p_len; ii++) {
        r_f0[size_t(ii)] = p_fr[ii * p_stride];
        r_f1[size_t(ii)] = p_fc[ii * p_stride];
    }
    auto comp = [&](int64_t p_idx, int p_jj) -> double {
        return double(p_jj == 0 ? r_f0[size_t(p_idx)] : r_f1[size_t(p_idx)]);
    };
    const int other = 1 - p_d;
    for (int64_t ii = 0; ii < p_len; ii++) {
        if (p_fr[ii * p_stride] >= 0) {
            const double fd = comp(ii, p_d);
            double wR = 0.0;
            {
                const double tw = comp(ii, other) - double(p_coor[other]);
                wR += tw * tw;
            }
            while (l >= 1) {
                double a, b, c, uR = 0.0, vR = 0.0, f1;
                const int64_t idx1 = r_g[size_t(l)];
                f1 = comp(idx1, p_d);
                const int64_t idx2 = r_g[size_t(l - 1)];
                a = f1 - comp(idx2, p_d);
                b = fd - f1;
                c = a + b;
                {
                    const double cc = double(p_coor[other]);
                    const double tu = comp(idx2, other) - cc;
                    const double tv = comp(idx1, other) - cc;
                    uR += tu * tu;
                    vR += tv * tv;
                }
                if (c * vR - b * uR - a * wR - a * b * c <= 0.0) {
                    break;
                }
                --l;
            }
            ++l;
            r_g[size_t(l)] = ii;
        }
    }
    const int64_t maxl = l;
    if (maxl >= 0) {
        l = 0;
        for (int64_t ii = 0; ii < p_len; ii++) {
            double delta1 = 0.0, t;
            for (int jj = 0; jj < 2; jj++) {
                t = jj == p_d ? comp(r_g[size_t(l)], jj) - double(ii) : comp(r_g[size_t(l)], jj) - double(p_coor[jj]);
                delta1 += t * t;
            }
            while (l < maxl) {
                double delta2 = 0.0;
                for (int jj = 0; jj < 2; jj++) {
                    t = jj == p_d ? comp(r_g[size_t(l + 1)], jj) - double(ii) : comp(r_g[size_t(l + 1)], jj) - double(p_coor[jj]);
                    delta2 += t * t;
                }
                if (delta1 <= delta2) {
                    break;
                }
                delta1 = delta2;
                ++l;
            }
            const int64_t idx1 = r_g[size_t(l)];
            p_fr[ii * p_stride] = int32_t(r_f0[size_t(idx1)]);
            p_fc[ii * p_stride] = int32_t(r_f1[size_t(idx1)]);
        }
    }
}

} // namespace

void feature_transform(const uint8_t *p_input, int p_h, int p_w, std::vector<int32_t> &r_row, std::vector<int32_t> &r_col) {
    const size_t n = size_t(p_h) * size_t(p_w);
    r_row.assign(n, 0);
    r_col.assign(n, 0);
    const int64_t mx = p_h > p_w ? p_h : p_w;
    std::vector<int64_t> f0(static_cast<size_t>(mx)), f1(static_cast<size_t>(mx)), g(static_cast<size_t>(mx));
    int64_t coor[2] = { 0, 0 };
    // _ComputeFT(d = 1)：先逐列（d = 0）
    for (int64_t jj = 0; jj < p_w; jj++) {
        coor[1] = jj;
        for (int64_t r = 0; r < p_h; r++) {
            const size_t q = size_t(r) * size_t(p_w) + size_t(jj);
            if (p_input[q]) {
                r_row[q] = -1;
            } else {
                r_row[q] = int32_t(r);
                r_col[q] = int32_t(jj);
            }
        }
        voronoi_ft(r_row.data() + jj, r_col.data() + jj, p_w, p_h, coor, 0, f0, f1, g);
    }
    // 再逐行（d = 1）：子空间迭代器按行号走，coor[0] = 行号
    for (int64_t r = 0; r < p_h; r++) {
        coor[0] = r;
        voronoi_ft(r_row.data() + size_t(r) * size_t(p_w), r_col.data() + size_t(r) * size_t(p_w), 1, p_w, coor, 1, f0, f1, g);
    }
}

std::vector<double> distance_edt(const uint8_t *p_input, int p_h, int p_w) {
    std::vector<int32_t> fr, fc;
    feature_transform(p_input, p_h, p_w, fr, fc);
    std::vector<double> out(size_t(p_h) * size_t(p_w));
    for (int r = 0; r < p_h; r++) {
        for (int c = 0; c < p_w; c++) {
            const size_t q = size_t(r) * size_t(p_w) + size_t(c);
            // dt = ft − indices（int32）→ float64 → 平方 → 两维相加 → sqrt
            const double dr = double(fr[q] - r);
            const double dc = double(fc[q] - c);
            out[q] = std::sqrt(dr * dr + dc * dc);
        }
    }
    return out;
}

std::vector<uint8_t> binary_erosion_3x3(const uint8_t *p_mask, int p_h, int p_w) {
    std::vector<uint8_t> out(size_t(p_h) * size_t(p_w), 0);
    for (int r = 0; r < p_h; r++) {
        for (int c = 0; c < p_w; c++) {
            bool all = true;
            for (int dr = -1; dr <= 1 && all; dr++) {
                for (int dc = -1; dc <= 1; dc++) {
                    const int a = r + dr, b = c + dc;
                    if (a < 0 || b < 0 || a >= p_h || b >= p_w || !p_mask[size_t(a) * size_t(p_w) + size_t(b)]) {
                        all = false;
                        break;
                    }
                }
            }
            out[size_t(r) * size_t(p_w) + size_t(c)] = all ? 1 : 0;
        }
    }
    return out;
}

} // namespace scipy_nd
