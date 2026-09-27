#include "planet_sky.h"

#include <algorithm>
#include <cmath>

#include "planet_frame.h"

namespace {

constexpr double PI = 3.14159265358979323846;

double safe_sqrt(double p_x) {
    return std::sqrt(std::max(0.0, p_x));
}

// 从离星心 r、天顶角余弦 mu 出发，到半径 p_radius 的球面的距离（出去的那一个交点；没有交点返回 −1）
double distance_to_sphere_out(double p_r, double p_mu, double p_radius) {
    const double disc = p_r * p_r * (p_mu * p_mu - 1.0) + p_radius * p_radius;
    if (disc < 0.0) {
        return -1.0;
    }
    return -p_r * p_mu + std::sqrt(disc);
}

// 往下打到半径 p_radius 的球面（近的交点）；打不到返回 −1
double distance_to_sphere_in(double p_r, double p_mu, double p_radius) {
    const double disc = p_r * p_r * (p_mu * p_mu - 1.0) + p_radius * p_radius;
    if (disc < 0.0 || p_mu >= 0.0) {
        return -1.0;
    }
    const double t = -p_r * p_mu - std::sqrt(disc);
    return t >= 0.0 ? t : -1.0;
}

// unit ∈ [0,1] → 格心对齐的纹理坐标的逆：第 k 格的 unit = k / (n − 1)
double unit_of_texel(int p_k, int p_n) {
    return p_n > 1 ? double(p_k) / double(p_n - 1) : 0.0;
}

} // namespace

void AtmosphereModel::medium(double p_h_km, double r_rayleigh[3], double &r_mie, double r_extinct[3]) const {
    const double h = std::max(0.0, p_h_km);
    const double dr = std::exp(-h / rayleigh_h_km);
    const double dm = std::exp(-h / mie_h_km);
    const double dz = std::max(0.0, 1.0 - std::abs(h - ozone_center_km) / ozone_half_km);
    r_mie = mie_scatter * dm;
    for (int k = 0; k < 3; k++) {
        r_rayleigh[k] = rayleigh[k] * dr;
        r_extinct[k] = r_rayleigh[k] + mie_extinct * dm + ozone[k] * dz;
    }
}

void SkyLut::sample(double p_u, double p_v, double r_out[3]) const {
    r_out[0] = r_out[1] = r_out[2] = 0.0;
    if (empty()) {
        return;
    }
    const double x = std::clamp(p_u, 0.0, 1.0) * (width - 1), y = std::clamp(p_v, 0.0, 1.0) * (height - 1);
    const int x0 = std::min(width - 2, int(x)), y0 = std::min(height - 2, int(y));
    const double fx = std::clamp(x - x0, 0.0, 1.0), fy = std::clamp(y - y0, 0.0, 1.0);
    auto at = [&](int xx, int yy, int c) { return double(rgb[(size_t(std::clamp(yy, 0, height - 1)) * size_t(width) + size_t(std::clamp(xx, 0, width - 1))) * 3 + size_t(c)]); };
    for (int c = 0; c < 3; c++) {
        const double a = at(x0, y0, c) + (at(x0 + 1, y0, c) - at(x0, y0, c)) * fx;
        const double b = at(x0, y0 + 1, c) + (at(x0 + 1, y0 + 1, c) - at(x0, y0 + 1, c)) * fx;
        r_out[c] = a + (b - a) * fy;
    }
}

void atmo_transmittance_uv(const AtmosphereModel &p_atmo, double p_r, double p_mu, double &r_u, double &r_v) {
    const double top = p_atmo.top_km, bottom = p_atmo.bottom_km;
    const double H = safe_sqrt(top * top - bottom * bottom);
    const double rho = safe_sqrt(p_r * p_r - bottom * bottom);
    const double d = std::max(0.0, -p_r * p_mu + safe_sqrt(p_r * p_r * (p_mu * p_mu - 1.0) + top * top));
    const double d_min = top - p_r, d_max = rho + H;
    r_u = d_max > d_min ? (d - d_min) / (d_max - d_min) : 0.0;
    r_v = H > 0.0 ? rho / H : 0.0;
}

void atmo_transmittance_r_mu(const AtmosphereModel &p_atmo, double p_u, double p_v, double &r_r, double &r_mu) {
    const double top = p_atmo.top_km, bottom = p_atmo.bottom_km;
    const double H = safe_sqrt(top * top - bottom * bottom);
    const double rho = H * p_v;
    r_r = std::sqrt(rho * rho + bottom * bottom);
    const double d_min = top - r_r, d_max = rho + H;
    const double d = d_min + p_u * (d_max - d_min);
    r_mu = d <= 0.0 ? 1.0 : std::clamp((H * H - rho * rho - d * d) / (2.0 * r_r * d), -1.0, 1.0);
}

void atmo_transmittance_direct(const AtmosphereModel &p_atmo, double p_r, double p_mu, int p_steps, double r_out[3]) {
    double t_end = distance_to_sphere_out(p_r, p_mu, p_atmo.top_km);
    const double t_ground = distance_to_sphere_in(p_r, p_mu, p_atmo.bottom_km);
    if (t_ground >= 0.0) {
        t_end = t_ground;
    }
    double od[3] = { 0.0, 0.0, 0.0 };
    if (t_end > 0.0) {
        const double dt = t_end / p_steps;
        for (int k = 0; k < p_steps; k++) {
            const double t = (k + 0.5) * dt;
            const double r = std::sqrt(p_r * p_r + t * t + 2.0 * p_r * p_mu * t);
            double ray[3], mie, ext[3];
            p_atmo.medium(r - p_atmo.bottom_km, ray, mie, ext);
            for (int c = 0; c < 3; c++) {
                od[c] += ext[c] * dt;
            }
        }
    }
    for (int c = 0; c < 3; c++) {
        r_out[c] = std::exp(-od[c]);
    }
}

SkyLut atmo_transmittance_lut(const AtmosphereModel &p_atmo, int p_width, int p_height, int p_steps) {
    SkyLut lut;
    lut.width = p_width;
    lut.height = p_height;
    lut.rgb.resize(size_t(p_width) * size_t(p_height) * 3);
    for (int j = 0; j < p_height; j++) {
        for (int i = 0; i < p_width; i++) {
            double r = 0.0, mu = 0.0, t[3];
            atmo_transmittance_r_mu(p_atmo, unit_of_texel(i, p_width), unit_of_texel(j, p_height), r, mu);
            // 表只管看得见大气顶的方向（地平线以上）：参数化到 u = 1 正好是擦着海面的那条
            atmo_transmittance_direct(p_atmo, r, std::max(mu, -safe_sqrt(1.0 - (p_atmo.bottom_km / r) * (p_atmo.bottom_km / r)) + 1e-9), p_steps, t);
            for (int c = 0; c < 3; c++) {
                lut.rgb[(size_t(j) * size_t(p_width) + size_t(i)) * 3 + size_t(c)] = float(t[c]);
            }
        }
    }
    return lut;
}

void atmo_transmittance(const AtmosphereModel &p_atmo, const SkyLut &p_lut, double p_r, double p_mu, double r_out[3]) {
    if (distance_to_sphere_in(p_r, p_mu, p_atmo.bottom_km) >= 0.0) {
        r_out[0] = r_out[1] = r_out[2] = 0.0;
        return;
    }
    double u = 0.0, v = 0.0;
    atmo_transmittance_uv(p_atmo, p_r, p_mu, u, v);
    p_lut.sample(u, v, r_out);
}

SkyLut atmo_multiscatter_lut(const AtmosphereModel &p_atmo, const SkyLut &p_transmittance, int p_size, int p_dirs_per_axis, int p_steps) {
    SkyLut lut;
    lut.width = p_size;
    lut.height = p_size;
    lut.rgb.resize(size_t(p_size) * size_t(p_size) * 3);
    const int n_dirs = p_dirs_per_axis * p_dirs_per_axis;
    for (int j = 0; j < p_size; j++) {
        // 离海面的高（格心对齐；最底一格略抬高一点，免得起点就在海面上）
        const double r = p_atmo.bottom_km + std::max(0.01, unit_of_texel(j, p_size) * (p_atmo.top_km - p_atmo.bottom_km) - 0.01);
        for (int i = 0; i < p_size; i++) {
            const double mu_s = unit_of_texel(i, p_size) * 2.0 - 1.0;
            const double sun[3] = { safe_sqrt(1.0 - mu_s * mu_s), mu_s, 0.0 };
            double l_sum[3] = { 0.0, 0.0, 0.0 }, f_sum[3] = { 0.0, 0.0, 0.0 };
            for (int a = 0; a < p_dirs_per_axis; a++) {
                for (int b = 0; b < p_dirs_per_axis; b++) {
                    // 球面上均匀的方向（格心）
                    const double cos_t = 1.0 - 2.0 * (a + 0.5) / p_dirs_per_axis;
                    const double sin_t = safe_sqrt(1.0 - cos_t * cos_t);
                    const double phi = 2.0 * PI * (b + 0.5) / p_dirs_per_axis;
                    const double dir[3] = { sin_t * std::cos(phi), cos_t, sin_t * std::sin(phi) };
                    const double mu = dir[1];
                    double t_end = distance_to_sphere_out(r, mu, p_atmo.top_km);
                    const double t_ground = distance_to_sphere_in(r, mu, p_atmo.bottom_km);
                    const bool ground = t_ground >= 0.0;
                    if (ground) {
                        t_end = t_ground;
                    }
                    if (t_end <= 0.0) {
                        continue;
                    }
                    const double dt = t_end / p_steps;
                    double through[3] = { 1.0, 1.0, 1.0 }, L[3] = { 0.0, 0.0, 0.0 }, F[3] = { 0.0, 0.0, 0.0 };
                    for (int k = 0; k < p_steps; k++) {
                        const double t = (k + 0.3) * dt;
                        const double p[3] = { dir[0] * t, r + dir[1] * t, dir[2] * t };
                        const double pr = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                        const double mu_sun = (p[0] * sun[0] + p[1] * sun[1] + p[2] * sun[2]) / pr;
                        double ray[3], mie, ext[3], ts[3];
                        p_atmo.medium(pr - p_atmo.bottom_km, ray, mie, ext);
                        atmo_transmittance(p_atmo, p_transmittance, pr, mu_sun, ts);
                        for (int c = 0; c < 3; c++) {
                            const double scat = ray[c] + mie;
                            const double st = std::exp(-ext[c] * dt);
                            const double S = scat * ts[c] / (4.0 * PI);
                            L[c] += through[c] * (S - S * st) / std::max(1e-12, ext[c]);
                            F[c] += through[c] * (scat - scat * st) / std::max(1e-12, ext[c]);
                            through[c] *= st;
                        }
                    }
                    if (ground) {
                        const double gp[3] = { dir[0] * t_end, r + dir[1] * t_end, dir[2] * t_end };
                        const double gr = std::sqrt(gp[0] * gp[0] + gp[1] * gp[1] + gp[2] * gp[2]);
                        const double n_dot_l = std::max(0.0, (gp[0] * sun[0] + gp[1] * sun[1] + gp[2] * sun[2]) / gr);
                        double ts[3];
                        atmo_transmittance(p_atmo, p_transmittance, gr, n_dot_l, ts);
                        for (int c = 0; c < 3; c++) {
                            L[c] += through[c] * ts[c] * n_dot_l * p_atmo.ground_albedo / PI;
                        }
                    }
                    for (int c = 0; c < 3; c++) {
                        l_sum[c] += L[c];
                        f_sum[c] += F[c];
                    }
                }
            }
            for (int c = 0; c < 3; c++) {
                // 各向同性：∫ … dω / 4π = 方向上的平均
                const double l2 = l_sum[c] / n_dirs, f_ms = f_sum[c] / n_dirs;
                lut.rgb[(size_t(j) * size_t(p_size) + size_t(i)) * 3 + size_t(c)] = float(l2 / std::max(1e-6, 1.0 - f_ms));
            }
        }
    }
    return lut;
}

void planet_sun_dir(double p_day, double p_clock_lon_deg, const CalendarRules &p_rules, double r_dir[3]) {
    const double hour = (p_day - std::floor(p_day)) * 24.0;
    const double decl = sun_declination_deg(p_day, p_rules);
    const PlanetVec u = planet_unit(decl, p_clock_lon_deg - (hour - 12.0) * 15.0);
    r_dir[0] = u.x, r_dir[1] = u.y, r_dir[2] = u.z;
}

void planet_moon_dir(double p_day, double p_clock_lon_deg, const CalendarRules &p_rules, double r_dir[3]) {
    const double hour = (p_day - std::floor(p_day)) * 24.0;
    double phase = std::fmod((p_day - p_rules.new_moon_day) / std::max(1e-6, p_rules.synodic_days), 1.0);
    if (phase < 0.0) {
        phase += 1.0;
    }
    const double year = double(std::max(1, p_rules.year_days));
    const double decl = p_rules.tilt_deg * std::cos(2.0 * PI * ((day_of_year(p_day, p_rules) - p_rules.solstice_n) / year + phase));
    const PlanetVec u = planet_unit(decl, p_clock_lon_deg - (hour - 12.0) * 15.0 + phase * 360.0);
    r_dir[0] = u.x, r_dir[1] = u.y, r_dir[2] = u.z;
}
