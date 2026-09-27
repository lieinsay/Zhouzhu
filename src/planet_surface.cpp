#include "planet_surface.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <unordered_map>

#include "planet_frame.h"
#include "world_grid.h"

namespace {

constexpr double PI = 3.14159265358979323846;

// 各面的法向、右、上（行星系）：与着色器 planet_cube_uv 逐式相同
constexpr double FACE_N[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
constexpr double FACE_U[6][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 1, 0 } };
constexpr double FACE_V[6][3] = { { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };

double dot3(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// 地表类 → 夜里的灯往哪儿撒（可耕地、梯田最多）、林的份额、会枯黄的份额
double light_weight(int p_class) {
    switch (p_class) {
        case 7: // 可耕地
        case 8: // 梯田
            return 1.0;
        case 6: // 草坡
        case 3: // 高山草甸
        case 0:
            return 0.3;
        case 5: // 灌丛
        case 9: // 湿地
            return 0.2;
        case 4: // 林地
            return 0.12;
        default:
            return 0.05;
    }
}

double forest_share(int p_class) {
    return p_class == 4 ? 1.0 : 0.0;
}

double herb_share(int p_class) {
    switch (p_class) {
        case 0:
        case 3:
        case 5:
        case 6:
        case 7:
        case 8:
            return 1.0;
        case 1:
            return 0.5;
        case 9:
            return 0.7;
        default:
            return 0.0;
    }
}

uint64_t mix64(uint64_t p_x) {
    p_x ^= p_x >> 33;
    p_x *= 0xff51afd7ed558ccdull;
    p_x ^= p_x >> 33;
    p_x *= 0xc4ceb9fe1a85ec53ull;
    p_x ^= p_x >> 33;
    return p_x;
}

double hash01(uint64_t p_seed, uint64_t p_k) {
    return double(mix64(p_seed * 0x9E3779B97F4A7C15ull + p_k) >> 11) * (1.0 / 9007199254740992.0);
}

struct Accum {
    double area = 0.0; // 陆地面积（km²）
    double rgb[3] = { 0.0, 0.0, 0.0 }; // × 面积
    double height = 0.0, forest = 0.0, herb = 0.0; // × 面积
    double pop = 0.0; // 人
};

uint8_t to_u8(double p_v) {
    return uint8_t(std::lround(std::clamp(p_v, 0.0, 1.0) * 255.0));
}

} // namespace

int planet_cube::face_of(const double p_dir[3], double &r_u, double &r_v) {
    const double ax = std::abs(p_dir[0]), ay = std::abs(p_dir[1]), az = std::abs(p_dir[2]);
    int face;
    if (ax >= ay && ax >= az) {
        face = p_dir[0] >= 0.0 ? 0 : 1;
    } else if (ay >= az) {
        face = p_dir[1] >= 0.0 ? 2 : 3;
    } else {
        face = p_dir[2] >= 0.0 ? 4 : 5;
    }
    const double dn = std::max(1e-12, dot3(p_dir, FACE_N[face]));
    const double a = dot3(p_dir, FACE_U[face]) / dn, b = dot3(p_dir, FACE_V[face]) / dn;
    r_u = (std::atan(a) * 4.0 / PI + 1.0) * 0.5;
    r_v = (std::atan(b) * 4.0 / PI + 1.0) * 0.5;
    return face;
}

void planet_cube::dir_of(int p_face, double p_u, double p_v, double r_dir[3]) {
    const double a = std::tan((p_u * 2.0 - 1.0) * PI * 0.25), b = std::tan((p_v * 2.0 - 1.0) * PI * 0.25);
    double d[3];
    for (int k = 0; k < 3; k++) {
        d[k] = FACE_N[p_face][k] + a * FACE_U[p_face][k] + b * FACE_V[p_face][k];
    }
    const double l = std::sqrt(dot3(d, d));
    for (int k = 0; k < 3; k++) {
        r_dir[k] = d[k] / l;
    }
}

double planet_cube::texel_solid_angle(int p_size, int p_i, int p_j) {
    const double s = ((p_i + 0.5) / p_size) * 2.0 - 1.0, t = ((p_j + 0.5) / p_size) * 2.0 - 1.0;
    const double a = std::tan(s * PI * 0.25), b = std::tan(t * PI * 0.25);
    const double da = (PI * 0.5) * (1.0 + a * a) / p_size, db = (PI * 0.5) * (1.0 + b * b) / p_size; // dU = 1/size → ds = 2/size → da = π/4·(1+a²)·ds
    return da * db / std::pow(1.0 + a * a + b * b, 1.5);
}

uint32_t surface_group_faces(const PlanetData &p_data, int p_node, const SurfaceBakeRules &p_rules) {
    const PlanetNode &n = p_data.node(p_node);
    const GroupFrame g(n.lat, n.lon, PlanetShape{ p_data.radius_km * 1000.0 });
    double res = 0.0, ox = 0.0, oy = 0.0;
    int rows = 0, cols = 0;
    double x0, x1, z0, z1;
    if (!p_rules.force_stand_ins && p_data.lod_extent(p_node, p_rules.lod_res_m, res, ox, oy, rows, cols)) {
        x0 = ox * 1000.0;
        x1 = x0 + cols * res;
        z0 = -oy * 1000.0;
        z1 = z0 + rows * res;
    } else {
        const double r = 4.0 * std::sqrt(std::max(1.0, n.area_km2) / PI) * 1000.0 + 4.0 * p_rules.lod_res_m;
        x0 = z0 = -r;
        x1 = z1 = r;
    }
    uint32_t mask = 0;
    for (int a = 0; a <= 2; a++) {
        for (int b = 0; b <= 2; b++) {
            const PlanetVec p = g.to_planet(PlanetVec(x0 + (x1 - x0) * 0.5 * a, 0.0, z0 + (z1 - z0) * 0.5 * b));
            const double d[3] = { p.x, p.y, p.z };
            double u, v;
            mask |= 1u << planet_cube::face_of(d, u, v);
        }
    }
    return mask;
}

PlanetLod surface_stand_in(const PlanetData &p_data, int p_node, double p_res_m) {
    // 按行星包的数撒一团：主岛（面积 = 主岛）一块略扁的圆、其余陆地分给几块小岛，围在主岛外一两倍半径处；
    // 边上按哈希抖一抖，地表按台面的年均温与降水挑（冷的高山草甸与裸岩、湿的林、干的草坡，台面周围一圈可耕地）
    const PlanetNode &n = p_data.node(p_node);
    const double main_km2 = std::max(1.0, n.main_area_km2 > 0.0 ? n.main_area_km2 : n.area_km2 * 0.6);
    const double rest_km2 = std::max(0.0, n.area_km2 - main_km2);
    const double r_main = std::sqrt(main_km2 / PI) * 1000.0;
    struct Blob {
        double x, z, r, squash, angle;
    };
    std::vector<Blob> blobs;
    const uint64_t seed = uint64_t(p_node) + 7919;
    blobs.push_back({ 0.0, 0.0, r_main, 0.75 + 0.3 * hash01(seed, 1), PI * hash01(seed, 2) });
    const int small = rest_km2 > 1.0 ? std::clamp(int(rest_km2 / std::max(1.0, main_km2) * 6.0) + 2, 2, 8) : 0;
    for (int k = 0; k < small; k++) {
        const double r = std::sqrt(rest_km2 / small / PI) * 1000.0;
        const double a = 2.0 * PI * (k + hash01(seed, 10 + uint64_t(k))) / small;
        const double d = r_main * (1.4 + 1.2 * hash01(seed, 30 + uint64_t(k))) + r;
        blobs.push_back({ d * std::cos(a), d * std::sin(a), r, 0.7 + 0.3 * hash01(seed, 50 + uint64_t(k)), PI * hash01(seed, 70 + uint64_t(k)) });
    }
    double extent = 0.0;
    for (const Blob &b : blobs) {
        extent = std::max(extent, std::hypot(b.x, b.z) + b.r * 1.3);
    }
    PlanetLod lod;
    lod.node = p_node;
    lod.res_m = p_res_m;
    const int cells = std::max(2, int(std::ceil(2.0 * extent / p_res_m)) + 2);
    lod.rows = lod.cols = cells;
    lod.origin_x_km = -cells * p_res_m * 0.5 / 1000.0;
    lod.origin_y_km = cells * p_res_m * 0.5 / 1000.0;
    const size_t total = size_t(cells) * size_t(cells);
    lod.height.assign(total, 0);
    lod.land.assign(total, 0);
    lod.landcover.assign(total, 0);
    lod.island.assign(total, 0);
    const double top = std::max(50.0, n.height_m);
    int base_class = 6; // 草坡
    if (n.temp_c < -2.0) {
        base_class = 2; // 裸岩
    } else if (n.temp_c < 4.0) {
        base_class = 3; // 高山草甸
    } else if (n.precip > 0.45) {
        base_class = 4; // 林地
    }
    for (int r = 0; r < cells; r++) {
        for (int c = 0; c < cells; c++) {
            double x = 0.0, z = 0.0;
            lod.cell_center(r, c, x, z);
            for (size_t b = 0; b < blobs.size(); b++) {
                const Blob &bl = blobs[b];
                const double dx = x - bl.x, dz = z - bl.z;
                const double ca = std::cos(bl.angle), sa = std::sin(bl.angle);
                const double u = (dx * ca + dz * sa), v = (-dx * sa + dz * ca) / bl.squash;
                const double wobble = 1.0 + 0.18 * (hash01(seed + b, uint64_t(r) * 7919u + uint64_t(c)) - 0.5);
                const double q = std::hypot(u, v) / (bl.r * wobble);
                if (q < 1.0) {
                    const size_t k = lod.index(r, c);
                    lod.land[k] = 255;
                    lod.island[k] = uint8_t(b);
                    lod.height[k] = uint16_t(std::lround(top * (0.4 + 0.9 * (1.0 - q * q)) * (b == 0 ? 1.0 : 0.5)));
                    lod.landcover[k] = uint8_t(q > 0.55 && q < 0.8 && n.temp_c > 6.0 ? 7 : base_class);
                    break;
                }
            }
        }
    }
    return lod;
}

namespace {

uint16_t pack565(const int p_c[3]) {
    const int r = (p_c[0] * 31 + 127) / 255, g = (p_c[1] * 63 + 127) / 255, b = (p_c[2] * 31 + 127) / 255;
    return uint16_t((r << 11) | (g << 5) | b);
}

void unpack565(uint16_t p_c, int r_c[3]) {
    const int r = (p_c >> 11) & 31, g = (p_c >> 5) & 63, b = p_c & 31;
    r_c[0] = (r << 3) | (r >> 2);
    r_c[1] = (g << 2) | (g >> 4);
    r_c[2] = (b << 3) | (b >> 2);
}

void bc3_palettes(const uint8_t p_block[16], int r_alpha[8], int r_color[4][3]) {
    const int a0 = p_block[0], a1 = p_block[1];
    r_alpha[0] = a0;
    r_alpha[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; i++) {
            r_alpha[i + 1] = ((7 - i) * a0 + i * a1) / 7;
        }
    } else {
        for (int i = 1; i <= 4; i++) {
            r_alpha[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        }
        r_alpha[6] = 0;
        r_alpha[7] = 255;
    }
    const uint16_t c0 = uint16_t(p_block[8] | (p_block[9] << 8)), c1 = uint16_t(p_block[10] | (p_block[11] << 8));
    unpack565(c0, r_color[0]);
    unpack565(c1, r_color[1]);
    for (int c = 0; c < 3; c++) {
        r_color[2][c] = (2 * r_color[0][c] + r_color[1][c]) / 3;
        r_color[3][c] = (r_color[0][c] + 2 * r_color[1][c]) / 3;
    }
}

} // namespace

void bc3_encode_block(const uint8_t p_rgba[64], uint8_t r_block[16]) {
    // alpha：最大、最小两端，八级
    int amin = 255, amax = 0;
    for (int k = 0; k < 16; k++) {
        amin = std::min(amin, int(p_rgba[k * 4 + 3]));
        amax = std::max(amax, int(p_rgba[k * 4 + 3]));
    }
    r_block[0] = uint8_t(amax);
    r_block[1] = uint8_t(amin);
    // 颜色：包围盒往里收 1/16（stb_dxt 的做法），两端量化成 565
    int mn[3] = { 255, 255, 255 }, mx[3] = { 0, 0, 0 };
    for (int k = 0; k < 16; k++) {
        for (int c = 0; c < 3; c++) {
            mn[c] = std::min(mn[c], int(p_rgba[k * 4 + c]));
            mx[c] = std::max(mx[c], int(p_rgba[k * 4 + c]));
        }
    }
    for (int c = 0; c < 3; c++) {
        const int inset = (mx[c] - mn[c]) >> 4;
        mn[c] = std::min(255, mn[c] + inset);
        mx[c] = std::max(0, mx[c] - inset);
    }
    const uint16_t c0 = pack565(mx), c1 = pack565(mn);
    r_block[8] = uint8_t(c0 & 0xFF);
    r_block[9] = uint8_t(c0 >> 8);
    r_block[10] = uint8_t(c1 & 0xFF);
    r_block[11] = uint8_t(c1 >> 8);
    int apal[8], cpal[4][3];
    bc3_palettes(r_block, apal, cpal);
    uint64_t abits = 0;
    uint32_t cbits = 0;
    for (int k = 0; k < 16; k++) {
        const int a = p_rgba[k * 4 + 3];
        int best = 0, best_d = 1 << 30;
        for (int j = 0; j < 8 && amax > amin; j++) {
            const int d = std::abs(a - apal[j]);
            if (d < best_d) {
                best_d = d;
                best = j;
            }
        }
        abits |= uint64_t(best) << (3 * k);
        int cbest = 0, cbest_d = 1 << 30;
        for (int j = 0; j < 4 && c0 != c1; j++) {
            int d = 0;
            for (int c = 0; c < 3; c++) {
                const int e = int(p_rgba[k * 4 + c]) - cpal[j][c];
                d += e * e;
            }
            if (d < cbest_d) {
                cbest_d = d;
                cbest = j;
            }
        }
        cbits |= uint32_t(cbest) << (2 * k);
    }
    for (int i = 0; i < 6; i++) {
        r_block[2 + i] = uint8_t((abits >> (8 * i)) & 0xFF);
    }
    for (int i = 0; i < 4; i++) {
        r_block[12 + i] = uint8_t((cbits >> (8 * i)) & 0xFF);
    }
}

void bc3_decode_block(const uint8_t p_block[16], uint8_t r_rgba[64]) {
    int apal[8], cpal[4][3];
    bc3_palettes(p_block, apal, cpal);
    uint64_t abits = 0;
    for (int i = 0; i < 6; i++) {
        abits |= uint64_t(p_block[2 + i]) << (8 * i);
    }
    const uint32_t cbits = uint32_t(p_block[12]) | (uint32_t(p_block[13]) << 8) | (uint32_t(p_block[14]) << 16) | (uint32_t(p_block[15]) << 24);
    for (int k = 0; k < 16; k++) {
        const int ci = int((cbits >> (2 * k)) & 3), ai = int((abits >> (3 * k)) & 7);
        for (int c = 0; c < 3; c++) {
            r_rgba[k * 4 + c] = uint8_t(cpal[ci][c]);
        }
        r_rgba[k * 4 + 3] = uint8_t(apal[ai]);
    }
}

void bc3_encode_image(const uint8_t *p_rgba, int p_width, int p_height, uint8_t *r_out) {
    const int bw = (p_width + 3) / 4, bh = (p_height + 3) / 4;
    uint8_t block[64];
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            for (int y = 0; y < 4; y++) {
                const int sy = std::min(p_height - 1, by * 4 + y);
                for (int x = 0; x < 4; x++) {
                    const int sx = std::min(p_width - 1, bx * 4 + x);
                    std::memcpy(&block[(y * 4 + x) * 4], &p_rgba[(size_t(sy) * size_t(p_width) + size_t(sx)) * 4], 4);
                }
            }
            bc3_encode_block(block, r_out + (size_t(by) * size_t(bw) + size_t(bx)) * 16);
        }
    }
}

void bake_surface_face(const PlanetData &p_data, int p_face, const SurfaceBakeRules &p_rules, SurfaceFace &r_face, SurfaceBakeStats *r_stats) {
    const auto t0 = std::chrono::steady_clock::now();
    const int N = p_rules.size;
    const double R_km = p_data.radius_km + 5.6; // 游戏 y = 0（D13）；像素的面积按这一层算
    const PlanetShape shape{ p_data.radius_km * 1000.0 };
    std::unordered_map<uint32_t, Accum> acc;
    acc.reserve(1 << 17);
    SurfaceBakeStats stats;
    const std::vector<double> *pop_col = p_data.column("s09.pop");
    for (int node = 0; node < p_data.node_count(); node++) {
        if (!(surface_group_faces(p_data, node, p_rules) & (1u << p_face))) {
            continue;
        }
        PlanetLod lod = p_rules.force_stand_ins ? PlanetLod() : p_data.lod(node, p_rules.lod_res_m);
        const bool stand_in = lod.empty();
        if (stand_in) {
            lod = surface_stand_in(p_data, node, p_rules.lod_res_m);
        }
        const PlanetNode &n = p_data.node(node);
        const GroupFrame g(n.lat, n.lon, shape);
        const double cell_km2 = (lod.res_m / 1000.0) * (lod.res_m / 1000.0);
        // 人口按地表类的权撒到陆地格上
        double weight_sum = 0.0;
        for (size_t k = 0; k < lod.land.size(); k++) {
            if (lod.land[k] > 0) {
                weight_sum += light_weight(lod.landcover[k]) * lod.land[k] / 255.0;
            }
        }
        const double pop = pop_col && size_t(node) < pop_col->size() ? std::max(0.0, (*pop_col)[size_t(node)]) : std::max(0.0, n.pop);
        bool any = false;
        for (int r = 0; r < lod.rows; r++) {
            for (int c = 0; c < lod.cols; c++) {
                const size_t k = lod.index(r, c);
                if (lod.land[k] == 0) {
                    continue;
                }
                double x = 0.0, z = 0.0;
                lod.cell_center(r, c, x, z);
                const PlanetVec p = g.to_planet(PlanetVec(x, 0.0, z));
                const double d[3] = { p.x, p.y, p.z };
                double u = 0.0, v = 0.0;
                if (planet_cube::face_of(d, u, v) != p_face) {
                    continue;
                }
                any = true;
                const double frac = lod.land[k] / 255.0;
                const double area = cell_km2 * frac;
                const int lc = lod.landcover[k];
                const GroundColor col = landcover_color(lc);
                const double cell_pop = weight_sum > 0.0 ? pop * light_weight(lc) * frac / weight_sum : 0.0;
                // 按双线性的权分给最近的四个像素（面积守恒，海岸不起锯齿）
                const double fx = u * N - 0.5, fy = v * N - 0.5;
                const int i0 = int(std::floor(fx)), j0 = int(std::floor(fy));
                const double tx = fx - i0, ty = fy - j0;
                for (int q = 0; q < 4; q++) {
                    const int i = i0 + (q & 1), j = j0 + (q >> 1);
                    if (i < 0 || j < 0 || i >= N || j >= N) {
                        continue;
                    }
                    const double w = ((q & 1) ? tx : 1.0 - tx) * ((q >> 1) ? ty : 1.0 - ty);
                    if (w <= 0.0) {
                        continue;
                    }
                    Accum &a = acc[uint32_t(j) * uint32_t(N) + uint32_t(i)];
                    const double wa = w * area;
                    a.area += wa;
                    a.rgb[0] += wa * col.r;
                    a.rgb[1] += wa * col.g;
                    a.rgb[2] += wa * col.b;
                    a.height += wa * lod.height[k];
                    a.forest += wa * forest_share(lc);
                    a.herb += wa * herb_share(lc);
                    a.pop += w * cell_pop;
                }
            }
        }
        if (any) {
            stats.groups++;
            stats.stand_ins += stand_in ? 1 : 0;
        }
    }
    r_face.color.assign(size_t(N) * size_t(N) * 4, 0);
    r_face.extra.assign(size_t(N) * size_t(N) * 4, 0);
    for (const auto &it : acc) {
        const Accum &a = it.second;
        if (a.area <= 0.0) {
            continue;
        }
        const int i = int(it.first % uint32_t(N)), j = int(it.first / uint32_t(N));
        const double texel_km2 = planet_cube::texel_solid_angle(N, i, j) * R_km * R_km;
        const double cover = std::min(1.0, a.area / texel_km2);
        // 预乘份额：双线性与 mipmap 在海岸上不会拖出黑边，着色器除回来
        uint8_t *c = &r_face.color[size_t(it.first) * 4];
        c[0] = to_u8(a.rgb[0] / a.area * cover);
        c[1] = to_u8(a.rgb[1] / a.area * cover);
        c[2] = to_u8(a.rgb[2] / a.area * cover);
        c[3] = to_u8(cover);
        uint8_t *e = &r_face.extra[size_t(it.first) * 4];
        e[0] = to_u8(a.height / a.area / p_rules.height_scale_m * cover);
        const double density = a.pop / texel_km2;
        e[1] = to_u8(std::sqrt(std::min(1.0, density / p_rules.light_full_per_km2)));
        e[2] = to_u8(a.forest / a.area * cover);
        e[3] = to_u8(a.herb / a.area * cover);
        stats.land_texels++;
        stats.land_km2 += cover * texel_km2;
        stats.lit_texels += e[1] > 8 ? 1 : 0;
    }
    stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (r_stats) {
        *r_stats = stats;
    }
}
