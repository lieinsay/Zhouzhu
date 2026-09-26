// 画面按真球面弯曲（docs/PLAN-PLANET.md 第 4.2 节）：要弯的着色器在 vertex() 里 #include 它，
//     VERTEX = (inverse(MODEL_MATRIX) * vec4(planet_visual((MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz), 1.0)).xyz;
// 这类写法按各着色器的坐标空间接（P1 做）。
//
// 与 src/planet_frame.cpp 的 planet_delta_f / CurveViewF::visual 逐式相同，改一处要改两处（单测拿 C++ 单精度版对照双精度）。
//   δ(p)：锚群平面坐标 p（x 东、y 上、z 南，米）在真球面上相对群心切平面的偏差（方位等距投影，展开到 θ⁴，θ = 离群心 / R）
//   visual(p) = c + R_cᵀ · ((p − c) + (δ(p) − δ(c)))：相机局部的真球面位置——近处 ≈ p（与物理一致），100 km 外降约 784 m
// 全局参数（project.godot 的 [shader_globals]，每帧由 CPU 用双精度算好）：
//   planet_curve_on      0 = 不弯（没有行星数据的地图；visual(p) = p），1 = 弯
//   planet_curve_cam     c：相机的平面坐标
//   planet_curve_basis   R_c：列 = 相机处的东、上、南（在锚群的切平面坐标里）
//   planet_curve_dcam    δ(c)
//   planet_curve_radius  R（米，海面半径 + 5,600）

global uniform float planet_curve_on;
global uniform vec3 planet_curve_cam;
global uniform mat3 planet_curve_basis;
global uniform vec3 planet_curve_dcam;
global uniform float planet_curve_radius;

vec3 planet_delta(vec3 p) {
	float s2 = p.x * p.x + p.z * p.z;
	float k = s2 / (planet_curve_radius * planet_curve_radius);
	float yr = p.y / planet_curve_radius;
	float fh = yr - k / 6.0 * (1.0 + yr) + k * k / 120.0;
	float dv = -s2 / (2.0 * planet_curve_radius) * (1.0 - k / 12.0 + k * k / 360.0) - p.y * (k * 0.5 - k * k / 24.0);
	return vec3(p.x * fh, dv, p.z * fh);
}

// 平面坐标 → 画面坐标（世界空间）
vec3 planet_visual(vec3 p) {
	if (planet_curve_on < 0.5) {
		return p;
	}
	vec3 off = (p - planet_curve_cam) + (planet_delta(p) - planet_curve_dcam);
	return planet_curve_cam + off * planet_curve_basis; // 行向量 × 矩阵 = R_cᵀ · off
}

// 方向（法线、切线）：只转、不计投影的细微形变
vec3 planet_visual_dir(vec3 d) {
	if (planet_curve_on < 0.5) {
		return d;
	}
	return d * planet_curve_basis;
}
