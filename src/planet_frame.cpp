#include "planet_frame.h"

#include <algorithm>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;

PlanetVec tangent_of(const GroupFrame &p_f, const PlanetVec &p_planet) {
    const PlanetVec d = p_planet - p_f.up * p_f.radius;
    return { d.dot(p_f.east), d.dot(p_f.up), -d.dot(p_f.north) };
}

// 群心切平面坐标（东、上、南）→ 行星系方向
PlanetVec from_tangent_dir(const GroupFrame &p_f, const PlanetVec &p_v) {
    return p_f.east * p_v.x + p_f.up * p_v.y - p_f.north * p_v.z;
}

} // namespace

PlanetBasis PlanetBasis::transposed() const {
    PlanetBasis t;
    t.cols[0] = { cols[0].x, cols[1].x, cols[2].x };
    t.cols[1] = { cols[0].y, cols[1].y, cols[2].y };
    t.cols[2] = { cols[0].z, cols[1].z, cols[2].z };
    return t;
}

PlanetVec planet_unit(double p_lat_deg, double p_lon_deg) {
    const double f = p_lat_deg * DEG, l = p_lon_deg * DEG;
    return { std::cos(f) * std::cos(l), std::cos(f) * std::sin(l), std::sin(f) };
}

void planet_latlon(const PlanetVec &p_unit, double &r_lat_deg, double &r_lon_deg) {
    const PlanetVec u = p_unit.normalized();
    r_lat_deg = std::asin(std::clamp(u.z, -1.0, 1.0)) / DEG;
    r_lon_deg = std::atan2(u.y, u.x) / DEG;
}

double great_circle_m(double p_lat1, double p_lon1, double p_lat2, double p_lon2, double p_radius) {
    const PlanetVec a = planet_unit(p_lat1, p_lon1), b = planet_unit(p_lat2, p_lon2);
    return p_radius * std::atan2(a.cross(b).length(), a.dot(b));
}

double bearing_deg(double p_lat1, double p_lon1, double p_lat2, double p_lon2) {
    const double f1 = p_lat1 * DEG, f2 = p_lat2 * DEG, dl = (p_lon2 - p_lon1) * DEG;
    const double a = std::atan2(std::sin(dl) * std::cos(f2), std::cos(f1) * std::sin(f2) - std::sin(f1) * std::cos(f2) * std::cos(dl)) / DEG;
    return a < 0.0 ? a + 360.0 : a;
}

GroupFrame::GroupFrame(double p_lat_deg, double p_lon_deg, const PlanetShape &p_shape) :
        lat_deg(p_lat_deg), lon_deg(p_lon_deg), radius(p_shape.radius()) {
    const double f = p_lat_deg * DEG, l = p_lon_deg * DEG;
    up = planet_unit(p_lat_deg, p_lon_deg);
    east = { -std::sin(l), std::cos(l), 0.0 };
    north = { -std::sin(f) * std::cos(l), -std::sin(f) * std::sin(l), std::cos(f) };
}

PlanetVec GroupFrame::to_planet(const PlanetVec &p_flat) const {
    const double e = p_flat.x, n = -p_flat.z;
    const double s = std::sqrt(e * e + n * n);
    PlanetVec dir = up;
    if (s > 1e-9) {
        const double th = s / radius;
        dir = up * std::cos(th) + (east * (e / s) + north * (n / s)) * std::sin(th);
    }
    return dir * (radius + p_flat.y);
}

PlanetVec GroupFrame::to_flat(const PlanetVec &p_planet) const {
    const double r = p_planet.length();
    const PlanetVec dir = p_planet * (1.0 / r);
    const PlanetVec h = dir - up * dir.dot(up);
    const double hl = h.length();
    const double th = std::atan2(hl, dir.dot(up));
    double e = 0.0, n = 0.0;
    if (hl > 1e-15) {
        e = th * radius * h.dot(east) / hl;
        n = th * radius * h.dot(north) / hl;
    }
    return { e, r - radius, -n };
}

void GroupFrame::flat_latlon(const PlanetVec &p_flat, double &r_lat_deg, double &r_lon_deg) const {
    planet_latlon(to_planet(p_flat), r_lat_deg, r_lon_deg);
}

PlanetVec GroupFrame::latlon_flat(double p_lat_deg, double p_lon_deg) const {
    PlanetVec f = to_flat(planet_unit(p_lat_deg, p_lon_deg) * radius);
    f.y = 0.0;
    return f;
}

PlanetVec GroupFrame::tangent(const PlanetVec &p_flat) const {
    return tangent_of(*this, to_planet(p_flat));
}

PlanetVec GroupFrame::delta(const PlanetVec &p_flat) const {
    return tangent(p_flat) - p_flat;
}

PlanetVec GroupFrame::delta_series(const PlanetVec &p_flat) const {
    // θ = s/R，k = θ²：水平 (R+y)·sinθ / s − 1 ≈ y/R − k/6·(1 + y/R) + k²/120；
    // 竖直 (R+y)(cosθ − 1) ≈ −s²/2R·(1 − k/12 + k²/360) − y·(k/2 − k²/24)（600 km、y 3 km 处差 < 1 mm）
    const double R = radius;
    const double s2 = p_flat.x * p_flat.x + p_flat.z * p_flat.z;
    const double k = s2 / (R * R);
    const double yr = p_flat.y / R;
    const double fh = yr - k / 6.0 * (1.0 + yr) + k * k / 120.0;
    const double dv = -s2 / (2.0 * R) * (1.0 - k / 12.0 + k * k / 360.0) - p_flat.y * (k / 2.0 - k * k / 24.0);
    return { p_flat.x * fh, dv, p_flat.z * fh };
}

PlanetBasis GroupFrame::local_basis(const PlanetVec &p_flat) const {
    const PlanetVec t = tangent(p_flat);
    const PlanetVec up_c = (t + PlanetVec(0.0, radius, 0.0)).normalized(); // 星心在切平面坐标里是 (0, −R, 0)
    const double h = 1.0;
    const PlanetVec jx = (tangent(p_flat + PlanetVec(h, 0.0, 0.0)) - tangent(p_flat - PlanetVec(h, 0.0, 0.0))) * (0.5 / h);
    const PlanetVec east_c = (jx - up_c * jx.dot(up_c)).normalized();
    const PlanetVec south_c = east_c.cross(up_c); // 东 × 上 = 南（x × y = z）
    return { { east_c, up_c, south_c } };
}

double CurveSqueeze::distance(double p_distance) {
    if (p_distance <= KEEP_M) {
        return p_distance;
    }
    // 有理式 x / (1 + x)：一路单调、不会在单精度里压成同一个数（指数式在 480 km 外就全压到 48 km，远处的深度打架）
    const double span = LIMIT_M - KEEP_M, x = (p_distance - KEEP_M) / span;
    return KEEP_M + span * x / (1.0 + x);
}

void planet_squeeze_f(const float p_visual[3], const float p_cam[3], float r_out[3]) {
    // 逐式同 planet_curve.gdshaderinc 的 planet_squeeze
    const float keep = float(CurveSqueeze::KEEP_M), limit = float(CurveSqueeze::LIMIT_M);
    const float rel[3] = { p_visual[0] - p_cam[0], p_visual[1] - p_cam[1], p_visual[2] - p_cam[2] };
    const float d = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
    if (d <= keep) {
        r_out[0] = p_visual[0], r_out[1] = p_visual[1], r_out[2] = p_visual[2];
        return;
    }
    const float span = limit - keep;
    const float x = (d - keep) / span;
    const float d2 = keep + span * x / (1.0f + x);
    for (int k = 0; k < 3; k++) {
        r_out[k] = p_cam[k] + rel[k] * (d2 / d);
    }
}

PlanetVec planet_tilt(const PlanetVec &p_flat, const PlanetVec &p_dir, double p_radius) {
    const double s = std::sqrt(p_flat.x * p_flat.x + p_flat.z * p_flat.z);
    if (s < 1e-6) {
        return p_dir;
    }
    const double th = s / p_radius, c = std::cos(th), sn = std::sin(th);
    const PlanetVec a(p_flat.z / s, 0.0, -p_flat.x / s); // 上 × 离群心的方向：绕它转，上转向离群心的方向
    return p_dir * c + a.cross(p_dir) * sn + a * (a.dot(p_dir) * (1.0 - c));
}

PlanetVec CurveView::visual_dir(const PlanetVec &p_flat, const PlanetVec &p_dir) const {
    return local.apply_t(planet_tilt(p_flat, p_dir, radius));
}

PlanetVec CurveView::flat_of(const GroupFrame &p_frame, const PlanetVec &p_visual) const {
    // visual(p) = p + 小量（近处）；远处按 v − visual(p) 的差一步步修（差随距离平方长，几步就收到毫米）
    PlanetVec p = p_visual;
    for (int k = 0; k < 12; k++) {
        const PlanetVec e = p_visual - visual(p_frame, p);
        p = p + e;
        if (e.length() < 1e-4) {
            break;
        }
    }
    return p;
}

CurveView CurveView::at(const GroupFrame &p_frame, const PlanetVec &p_cam) {
    CurveView v;
    v.cam = p_cam;
    v.local = p_frame.local_basis(p_cam);
    v.delta_cam = p_frame.delta_series(p_cam);
    v.radius = p_frame.radius;
    return v;
}

PlanetVec CurveView::visual(const GroupFrame &p_frame, const PlanetVec &p_flat) const {
    const PlanetVec off = (p_flat - cam) + (p_frame.delta(p_flat) - p_frame.delta(cam));
    return cam + local.apply_t(off);
}

CurveViewF CurveViewF::from(const CurveView &p_view) {
    CurveViewF f;
    f.cam[0] = float(p_view.cam.x), f.cam[1] = float(p_view.cam.y), f.cam[2] = float(p_view.cam.z);
    for (int k = 0; k < 3; k++) {
        f.local[3 * k + 0] = float(p_view.local.cols[k].x);
        f.local[3 * k + 1] = float(p_view.local.cols[k].y);
        f.local[3 * k + 2] = float(p_view.local.cols[k].z);
    }
    f.delta_cam[0] = float(p_view.delta_cam.x), f.delta_cam[1] = float(p_view.delta_cam.y), f.delta_cam[2] = float(p_view.delta_cam.z);
    f.radius = float(p_view.radius);
    return f;
}

void planet_delta_f(const float p_flat[3], float p_radius, float r_out[3]) {
    // 逐式同 planet_curve.gdshaderinc 的 planet_delta
    const float s2 = p_flat[0] * p_flat[0] + p_flat[2] * p_flat[2];
    const float k = s2 / (p_radius * p_radius);
    const float yr = p_flat[1] / p_radius;
    const float fh = yr - k / 6.0f * (1.0f + yr) + k * k / 120.0f;
    const float dv = -s2 / (2.0f * p_radius) * (1.0f - k / 12.0f + k * k / 360.0f) - p_flat[1] * (k * 0.5f - k * k / 24.0f);
    r_out[0] = p_flat[0] * fh;
    r_out[1] = dv;
    r_out[2] = p_flat[2] * fh;
}

void planet_tilt_f(const float p_flat[3], const float p_dir[3], float p_radius, float r_out[3]) {
    // 逐式同 planet_curve.gdshaderinc 的 planet_tilt
    const float s = std::sqrt(p_flat[0] * p_flat[0] + p_flat[2] * p_flat[2]);
    if (s < 1.0f) {
        r_out[0] = p_dir[0], r_out[1] = p_dir[1], r_out[2] = p_dir[2];
        return;
    }
    const float hx = p_flat[0] / s, hz = p_flat[2] / s;
    const float th = s / p_radius;
    const float c = std::cos(th), sn = std::sin(th);
    const float a[3] = { hz, 0.0f, -hx };
    const float axd[3] = { a[1] * p_dir[2] - a[2] * p_dir[1], a[2] * p_dir[0] - a[0] * p_dir[2], a[0] * p_dir[1] - a[1] * p_dir[0] };
    const float ad = a[0] * p_dir[0] + a[1] * p_dir[1] + a[2] * p_dir[2];
    for (int k = 0; k < 3; k++) {
        r_out[k] = p_dir[k] * c + axd[k] * sn + a[k] * ad * (1.0f - c);
    }
}

void CurveViewF::visual_normal(const float p_flat[3], const float p_dir[3], float r_out[3]) const {
    // 逐式同 planet_visual_normal：R_cᵀ · planet_tilt(p, d)
    float t[3];
    planet_tilt_f(p_flat, p_dir, radius, t);
    for (int k = 0; k < 3; k++) {
        r_out[k] = local[3 * k + 0] * t[0] + local[3 * k + 1] * t[1] + local[3 * k + 2] * t[2];
    }
}

void CurveViewF::visual(const float p_flat[3], float r_out[3]) const {
    // 逐式同 planet_curve.gdshaderinc 的 planet_visual：off = (p − c) + (δ(p) − δ(c))；out = c + R_cᵀ · off
    float d[3];
    planet_delta_f(p_flat, radius, d);
    const float off[3] = { (p_flat[0] - cam[0]) + (d[0] - delta_cam[0]), (p_flat[1] - cam[1]) + (d[1] - delta_cam[1]),
        (p_flat[2] - cam[2]) + (d[2] - delta_cam[2]) };
    for (int k = 0; k < 3; k++) {
        r_out[k] = cam[k] + (local[3 * k + 0] * off[0] + local[3 * k + 1] * off[1] + local[3 * k + 2] * off[2]);
    }
}

PlanetVec rebase_point(const GroupFrame &p_a, const GroupFrame &p_b, const PlanetVec &p_flat_a) {
    return p_b.to_flat(p_a.to_planet(p_flat_a));
}

PlanetBasis rebase_rotation(const GroupFrame &p_a, const GroupFrame &p_b, const PlanetVec &p_flat_a) {
    const PlanetVec pb = rebase_point(p_a, p_b, p_flat_a);
    const PlanetBasis la = p_a.local_basis(p_flat_a), lb = p_b.local_basis(pb);
    // 两边的局部系换到行星系：A、B 的列 = 平面 x、y、z 轴在真空间里的方向；Q = Bᵀ·A
    PlanetBasis A, B;
    for (int k = 0; k < 3; k++) {
        A.cols[k] = from_tangent_dir(p_a, la.cols[k]);
        B.cols[k] = from_tangent_dir(p_b, lb.cols[k]);
    }
    return B.transposed() * A;
}
