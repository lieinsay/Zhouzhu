#pragma once

// 行星与岛群的坐标（docs/PLAN-PLANET.md 第 4.2 节）。不含 Godot 头；全用双精度，另给一份与着色器
// game/shaders/planet_curve.gdshaderinc 逐式相同的单精度版（visual_f），单测拿它对照双精度。
//
// 行星系（P）：原点星心，轴同 skyisle-gen 的 sphere.py——x 指 (0°N, 0°E)、y 指 (0°N, 90°E)、z 指北极；经度东正；米。
// 岛群平面系（G）：就是游戏坐标——x 东、y 上（云带顶以上的米）、z 南；原点 = 节点经纬度在基准球上的点。
// 平面 → 球面用以群心为中心的方位等距投影（AEQD）：平面上离群心 s、方位 α 的点 → 球面上离群心大圆距离 s、方位 α 的点，高度沿径向。
// 基准球半径 R = 海面半径 + 5,600 m（游戏 y = 0 在海面之上 5.6 km，见 VerticalDatum）。
//
// 画面弯曲：一个群（锚）的平面坐标 p 在「相机局部的真球面」里的位置
//     visual(p) = c + R_cᵀ · ((p − c) + (δ(p) − δ(c)))
// c 是相机的平面坐标，δ(p) = T(p) − p 是 AEQD 相对群心切平面的偏差（竖直降 ≈ s²/2R），R_c 是相机处的局部系
// （在锚的切平面坐标里：上 = 径向，东 / 南 = 平面 x / z 方向在球面上的像、正交化）。近处 visual(p) ≈ p，远处按真球面下弯。

#include <cmath>

// 垂直结构（D13）：游戏 y（云带顶以上的米，生成器的高度零点）
struct VerticalDatum {
    static constexpr double CLOUD_TOP_Y = -3800.0; // 云带顶（看得见的云海面）
    static constexpr double CLOUD_BASE_Y = -5000.0; // 云带底
    static constexpr double SEA_Y = -5600.0; // 海面
};

struct PlanetVec {
    double x = 0.0, y = 0.0, z = 0.0;

    PlanetVec() = default;
    PlanetVec(double p_x, double p_y, double p_z) :
            x(p_x), y(p_y), z(p_z) {}
    PlanetVec operator+(const PlanetVec &p_o) const { return { x + p_o.x, y + p_o.y, z + p_o.z }; }
    PlanetVec operator-(const PlanetVec &p_o) const { return { x - p_o.x, y - p_o.y, z - p_o.z }; }
    PlanetVec operator*(double p_s) const { return { x * p_s, y * p_s, z * p_s }; }
    double dot(const PlanetVec &p_o) const { return x * p_o.x + y * p_o.y + z * p_o.z; }
    PlanetVec cross(const PlanetVec &p_o) const { return { y * p_o.z - z * p_o.y, z * p_o.x - x * p_o.z, x * p_o.y - y * p_o.x }; }
    double length() const { return std::sqrt(dot(*this)); }
    PlanetVec normalized() const {
        const double l = length();
        return l > 0.0 ? *this * (1.0 / l) : *this;
    }
};

// 3×3（列 = 基向量）
struct PlanetBasis {
    PlanetVec cols[3];

    PlanetVec apply(const PlanetVec &p_v) const { return cols[0] * p_v.x + cols[1] * p_v.y + cols[2] * p_v.z; }
    // 转置作用：各列与 v 的点积
    PlanetVec apply_t(const PlanetVec &p_v) const { return { cols[0].dot(p_v), cols[1].dot(p_v), cols[2].dot(p_v) }; }
    PlanetBasis operator*(const PlanetBasis &p_o) const { return { { apply(p_o.cols[0]), apply(p_o.cols[1]), apply(p_o.cols[2]) } }; }
    PlanetBasis transposed() const;
};

struct PlanetShape {
    double sea_radius_m = 6371000.0; // 生成器的行星半径 = 海面
    double datum_above_sea_m = -VerticalDatum::SEA_Y; // 游戏 y = 0 在海面之上多少

    double radius() const { return sea_radius_m + datum_above_sea_m; } // 弯曲与投影统一用的 R
};

// 经纬度（度）↔ 单位向量（同 sphere.py）
PlanetVec planet_unit(double p_lat_deg, double p_lon_deg);
void planet_latlon(const PlanetVec &p_unit, double &r_lat_deg, double &r_lon_deg);
// 大圆距离（米，按 p_radius）与从 1 到 2 的初始方位（度，北起顺时针）
double great_circle_m(double p_lat1, double p_lon1, double p_lat2, double p_lon2, double p_radius);
double bearing_deg(double p_lat1, double p_lon1, double p_lat2, double p_lon2);

// 一个岛群的平面系
struct GroupFrame {
    double lat_deg = 0.0, lon_deg = 0.0;
    double radius = 6376600.0; // R
    PlanetVec up, east, north; // 群心处（行星系）

    GroupFrame() = default;
    GroupFrame(double p_lat_deg, double p_lon_deg, const PlanetShape &p_shape = PlanetShape());

    // 平面坐标（x 东、y 上、z 南，米）→ 行星系位置（米，相对星心）
    PlanetVec to_planet(const PlanetVec &p_flat) const;
    // 行星系位置 → 平面坐标（AEQD 的逆）
    PlanetVec to_flat(const PlanetVec &p_planet) const;
    // 平面坐标 → 经纬度
    void flat_latlon(const PlanetVec &p_flat, double &r_lat_deg, double &r_lon_deg) const;
    // 经纬度 → 平面上的水平坐标（y = 0）
    PlanetVec latlon_flat(double p_lat_deg, double p_lon_deg) const;

    // T(p)：真球面位置在群心切平面坐标里（原点 = 群心在基准球上的点，轴 = 东、上、南）
    PlanetVec tangent(const PlanetVec &p_flat) const;
    // δ(p) = T(p) − p：精确式（双精度）
    PlanetVec delta(const PlanetVec &p_flat) const;
    // δ(p) 的展开式（到 θ⁴，θ = s/R）：单精度与着色器用的就是它
    PlanetVec delta_series(const PlanetVec &p_flat) const;
    // 该点的局部系（在群心切平面坐标里；列 = 东、上、南）：上 = 径向，东 / 南 = 平面 x / z 方向在球面上的像、正交化
    PlanetBasis local_basis(const PlanetVec &p_flat) const;
};

// 方向（法线、切线）从平面轴转到该点的局部系：沿群心到该点的大圆平行移动，即绕水平轴（上 × 离群心的方向）转 θ = 离群心 / R。
// 与 local_basis(p).apply(d) 只差 AEQD 在方位向的形变（θ² 量级），着色器 planet_tilt 同式（单精度版 planet_tilt_f）
PlanetVec planet_tilt(const PlanetVec &p_flat, const PlanetVec &p_dir, double p_radius);

// 相机局部的画面弯曲（锚 = p_frame，相机平面坐标 p_cam）
struct CurveView {
    PlanetVec cam; // c
    PlanetBasis local; // R_c：列 = 相机处的东、上、南（锚切平面坐标）；visual 里乘它的转置
    PlanetVec delta_cam; // δ(c)（展开式，与着色器一致）
    double radius = 6376600.0;

    static CurveView at(const GroupFrame &p_frame, const PlanetVec &p_cam);
    // 双精度：用精确的 δ
    PlanetVec visual(const GroupFrame &p_frame, const PlanetVec &p_flat) const;
    // 该点处的方向（法线）在画面上：R_cᵀ · R_p · d（近处 ≈ d）
    PlanetVec visual_dir(const PlanetVec &p_flat, const PlanetVec &p_dir) const;
    // 画面坐标 → 平面坐标（visual 的逆，迭代到毫米；拾取用）
    PlanetVec flat_of(const GroupFrame &p_frame, const PlanetVec &p_visual) const;
};

// 与着色器逐式相同的单精度版
struct CurveViewF {
    float cam[3];
    float local[9]; // R_c 按列：local[3·k + i] = 第 k 列第 i 个分量（着色器里的 mat3 也是按列）
    float delta_cam[3];
    float radius;

    static CurveViewF from(const CurveView &p_view);
    void visual(const float p_flat[3], float r_out[3]) const;
    // 逐式同着色器的 planet_visual_normal：R_cᵀ · planet_tilt(p, d)
    void visual_normal(const float p_flat[3], const float p_dir[3], float r_out[3]) const;
};
// δ 的展开式（单精度，逐式同着色器的 planet_delta）
void planet_delta_f(const float p_flat[3], float p_radius, float r_out[3]);
// 逐式同着色器的 planet_tilt
void planet_tilt_f(const float p_flat[3], const float p_dir[3], float p_radius, float r_out[3]);

// 换锚：把群 a 平面系里的一点换到群 b 平面系（经真球面，精确）
PlanetVec rebase_point(const GroupFrame &p_a, const GroupFrame &p_b, const PlanetVec &p_flat_a);
// 该点处从 a 的平面轴到 b 的平面轴的转动（朝向、速度都乘它）：两边的局部系在真空间里对齐
PlanetBasis rebase_rotation(const GroupFrame &p_a, const GroupFrame &p_b, const PlanetVec &p_flat_a);
