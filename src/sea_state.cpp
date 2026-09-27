#include "sea_state.h"

#include <algorithm>
#include <cmath>

#include "planet_data.h"
#include "planet_frame.h"
#include "rng.h"

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double G = 9.81;

double smooth(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

SeaWaves sea_waves(double p_wind_ms, double p_storm, const SeaWaveRules &p_rules) {
    const SeaWaveRules &r = p_rules;
    const double wind = std::isfinite(p_wind_ms) ? std::max(0.0, p_wind_ms) : 0.0;
    const double storm = std::isfinite(p_storm) ? std::clamp(p_storm, 0.0, 1.0) : 0.0;
    const double u = std::max(wind, storm * r.storm_wind_ms);
    SeaWaves w;
    // 充分成长的风浪（Pierson–Moskowitz）：有效波高 0.21 U² / g，谱峰周期 2π U / (0.877 g)
    w.height_m = std::clamp(0.21 * u * u / G, r.calm_height_m, r.max_height_m);
    const double tp = 2.0 * PI * u / (0.877 * G);
    w.length_m = std::clamp(G * tp * tp / (2.0 * PI), r.calm_length_m, r.max_length_m);
    w.period_s = std::sqrt(2.0 * PI * w.length_m / G);
    w.foam = smooth(5.0, 14.0, u);
    w.chop = smooth(12.0, 24.0, u);
    return w;
}

void bake_wave_normals(int p_size, uint32_t p_seed, std::vector<uint8_t> &r_rgba) {
    const int n = std::max(8, p_size);
    struct Wave {
        double kx, kz, amp, phase;
    };
    // 几十道整数波数的余弦：波数 3–40，方向各处都有，振幅 ∝ |k|^−1.6（长的浪高、短的碎）；整数波数所以可平铺
    std::vector<Wave> waves;
    Rng rng(0x5EA5EAull ^ uint64_t(p_seed));
    const int count = 36;
    for (int i = 0; i < count; i++) {
        const double mag = 3.0 * std::pow(40.0 / 3.0, double(i) / double(count - 1));
        const double a = rng.uniform(0.0, 2.0 * PI);
        int kx = int(std::lround(mag * std::cos(a))), kz = int(std::lround(mag * std::sin(a)));
        if (kx == 0 && kz == 0) {
            kx = 1;
        }
        const double k = std::hypot(double(kx), double(kz));
        waves.push_back({ double(kx), double(kz), std::pow(k, -1.6), rng.uniform(0.0, 2.0 * PI) });
    }
    std::vector<double> h(size_t(n) * size_t(n)), gx(h.size()), gz(h.size());
    double h_lo = 1e9, h_hi = -1e9, g_max = 1e-9;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            const double x = double(i) / double(n), z = double(j) / double(n);
            double hv = 0.0, dx = 0.0, dz = 0.0;
            for (const Wave &w : waves) {
                const double t = 2.0 * PI * (w.kx * x + w.kz * z) + w.phase;
                hv += w.amp * std::cos(t);
                const double s = -w.amp * std::sin(t) * 2.0 * PI;
                dx += s * w.kx;
                dz += s * w.kz;
            }
            const size_t k = size_t(j) * size_t(n) + size_t(i);
            h[k] = hv;
            gx[k] = dx;
            gz[k] = dz;
            h_lo = std::min(h_lo, hv);
            h_hi = std::max(h_hi, hv);
            g_max = std::max(g_max, std::max(std::abs(dx), std::abs(dz)));
        }
    }
    r_rgba.assign(size_t(n) * size_t(n) * 4, 0);
    auto byte = [](double p_v) { return uint8_t(std::clamp(int(std::lround(p_v * 255.0)), 0, 255)); };
    for (size_t k = 0; k < h.size(); k++) {
        const double hn = (h[k] - h_lo) / std::max(1e-9, h_hi - h_lo);
        const double slope = std::hypot(gx[k], gz[k]) / g_max;
        r_rgba[k * 4 + 0] = byte(0.5 + 0.5 * gx[k] / g_max);
        r_rgba[k * 4 + 1] = byte(0.5 + 0.5 * gz[k] / g_max);
        r_rgba[k * 4 + 2] = byte(hn);
        r_rgba[k * 4 + 3] = byte(smooth(0.58, 0.92, hn) * (0.6 + 0.4 * smooth(0.1, 0.5, slope))); // 高处（浪头）：白头按它撒
    }
}

namespace {

double reef_weight(const PlanetNode &p_node, const ReefRules &p_rules) {
    const double old = smooth(p_rules.age_from, p_rules.age_to, p_node.age);
    const double land = std::sqrt(std::clamp(p_node.area_km2 / p_rules.area_ref_km2, 0.0, 1.0));
    return old * (0.35 + 0.65 * land);
}

} // namespace

double reef_density_at(const PlanetData &p_planet, double p_lat_deg, double p_lon_deg, const ReefRules &p_rules) {
    const double radius_km = p_planet.radius_km;
    const double reach = p_rules.reach_km;
    double sum = 0.0;
    for (int id : p_planet.nodes_within(p_lat_deg, p_lon_deg, 3.0 * reach * 1000.0)) {
        const PlanetNode &n = p_planet.node(id);
        const double w = reef_weight(n, p_rules);
        if (w <= 0.0) {
            continue;
        }
        const double d = great_circle_m(p_lat_deg, p_lon_deg, n.lat, n.lon, radius_km * 1000.0) / 1000.0;
        sum += w * std::exp(-(d / reach) * (d / reach));
    }
    return std::clamp(p_rules.base + p_rules.gain * sum, 0.0, 1.0);
}

void bake_reef_density(const PlanetData &p_planet, int p_width, int p_height, std::vector<uint8_t> &r_out, const ReefRules &p_rules) {
    const int w = std::max(8, p_width), h = std::max(4, p_height);
    std::vector<float> sum(size_t(w) * size_t(h), 0.0f);
    const double radius_km = p_planet.radius_km;
    const double reach = p_rules.reach_km;
    const double span_deg = 3.0 * reach / radius_km * 180.0 / PI; // 3 σ 折成度
    // 每格格心的单位向量（行星系）
    std::vector<PlanetVec> centers(sum.size());
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            centers[size_t(r) * size_t(w) + size_t(c)] = planet_unit(-90.0 + (r + 0.5) * 180.0 / h, -180.0 + (c + 0.5) * 360.0 / w);
        }
    }
    // 逐群往它周围 3 σ 的格子里撒（比逐格找群省得多）
    for (int id = 0; id < p_planet.node_count(); id++) {
        const PlanetNode &n = p_planet.node(id);
        const double wt = reef_weight(n, p_rules);
        if (wt <= 0.0) {
            continue;
        }
        const PlanetVec u = planet_unit(n.lat, n.lon);
        const int r0 = std::max(0, int(std::floor((n.lat - span_deg + 90.0) / 180.0 * h))), r1 = std::min(h - 1, int(std::floor((n.lat + span_deg + 90.0) / 180.0 * h)));
        const double coslat = std::cos(std::min(89.0, std::abs(n.lat) + span_deg) * PI / 180.0);
        const double lon_span = coslat > 1e-3 ? std::min(180.0, span_deg / coslat) : 180.0;
        const int cols = lon_span >= 180.0 ? w : std::min(w, int(std::ceil(2.0 * lon_span / 360.0 * w)) + 2);
        const int c0 = int(std::floor((n.lon - lon_span + 180.0) / 360.0 * w));
        for (int r = r0; r <= r1; r++) {
            for (int k = 0; k < cols; k++) {
                const int c = ((c0 + k) % w + w) % w;
                const size_t idx = size_t(r) * size_t(w) + size_t(c);
                const double cosang = std::clamp(u.dot(centers[idx]), -1.0, 1.0);
                const double d = std::acos(cosang) * radius_km;
                if (d > 3.0 * reach) {
                    continue;
                }
                sum[idx] += float(wt * std::exp(-(d / reach) * (d / reach)));
            }
        }
    }
    r_out.assign(sum.size(), 0);
    for (size_t k = 0; k < sum.size(); k++) {
        const double v = std::clamp(p_rules.base + p_rules.gain * double(sum[k]), 0.0, 1.0);
        r_out[k] = uint8_t(std::clamp(int(std::lround(v * 255.0)), 0, 255));
    }
}
