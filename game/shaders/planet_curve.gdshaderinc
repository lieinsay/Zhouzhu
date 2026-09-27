// 画面按真球面弯曲（docs/PLAN-PLANET.md 第 4.2 节；行星计划 P1 起接上）。要弯的着色器 #include 它，按坐标空间二选一：
//   render_mode world_vertex_coords 的（地皮、弯曲标准材质）：vertex() 末尾
//       vec3 flat = VERTEX; VERTEX = planet_visual(flat); NORMAL = planet_visual_normal(flat, NORMAL);
//   模型空间的（散布、河）：vertex() 末尾
//       planet_curve_model(MODEL_MATRIX, VERTEX, NORMAL);
//
// 与 src/planet_frame.cpp 的 planet_delta_f / planet_tilt_f / CurveViewF::visual / visual_normal 逐式相同，改一处要改两处（单测拿 C++ 单精度版对照双精度）。
//   δ(p)：锚群平面坐标 p（x 东、y 上、z 南，米）在真球面上相对群心切平面的偏差（方位等距投影，展开到 θ⁴，θ = 离群心 / R）
//   visual(p) = c + R_cᵀ · ((p − c) + (δ(p) − δ(c)))：相机局部的真球面位置——近处 ≈ p（与物理一致），100 km 外降约 784 m
//   法线：R_cᵀ · R_p · n，R_p 是群心到 p 沿大圆的平行移动（绕水平轴转 θ）；近处 ≈ n
//   远处往相机压（planet_squeeze，同 C++ 的 planet_squeeze_f）：离相机 10 km 以内不动，往外沿视线平滑地压到 26 km 以内——
//   画面上的方向与深度的先后不变，相机的远平面（60 km）不用放大；引擎的雾按压过的距离算，远处的雾不再变浓（空气透视）。
// 全局参数（project.godot 的 [shader_globals]，PlanetView 每帧用双精度算好）：
//   planet_curve_on      0 = 不弯（没有行星数据的地图；visual(p) = p），1 = 弯
//   planet_curve_cam     c：相机的平面坐标
//   planet_curve_basis   R_c：列 = 相机处的东、上、南（在锚群的切平面坐标里）
//   planet_curve_dcam    δ(c)
//   planet_curve_radius  R（米，海面半径 + 5,600）
//   planet_curve_squeeze 远处往相机压的 (keep, limit)：30 km 以下是 (10 km, 26 km)，相机再高按高度放大（P2，同 C++ 的 CurveSqueeze::at_height）

global uniform float planet_curve_on;
global uniform vec3 planet_curve_cam;
global uniform mat3 planet_curve_basis;
global uniform vec3 planet_curve_dcam;
global uniform float planet_curve_radius;
global uniform vec2 planet_curve_squeeze;

vec3 planet_delta(vec3 p) {
	float s2 = p.x * p.x + p.z * p.z;
	float k = s2 / (planet_curve_radius * planet_curve_radius);
	float yr = p.y / planet_curve_radius;
	float fh = yr - k / 6.0 * (1.0 + yr) + k * k / 120.0;
	float dv = -s2 / (2.0 * planet_curve_radius) * (1.0 - k / 12.0 + k * k / 360.0) - p.y * (k * 0.5 - k * k / 24.0);
	return vec3(p.x * fh, dv, p.z * fh);
}

// 平面坐标 → 相机局部的真球面位置（同 C++ 的 CurveViewF::visual）
vec3 planet_curved(vec3 p) {
	vec3 off = (p - planet_curve_cam) + (planet_delta(p) - planet_curve_dcam);
	return planet_curve_cam + off * planet_curve_basis; // 行向量 × 矩阵 = R_cᵀ · off
}

// 远处沿视线往相机压（keep、limit 是全局参数 planet_curve_squeeze）
vec3 planet_squeeze(vec3 v) {
	float keep = planet_curve_squeeze.x;
	float limit = planet_curve_squeeze.y;
	vec3 rel = v - planet_curve_cam;
	float d = length(rel);
	if (d <= keep) {
		return v;
	}
	float span = limit - keep;
	float x = (d - keep) / span;
	float d2 = keep + span * x / (1.0 + x);
	return planet_curve_cam + rel * (d2 / d);
}

// 平面坐标 → 画面坐标（世界空间）：弯过、再把远处压近
vec3 planet_visual(vec3 p) {
	if (planet_curve_on < 0.5) {
		return p;
	}
	return planet_squeeze(planet_curved(p));
}

// 方向从平面轴转到 p 处的局部系：沿群心到 p 的大圆平行移动（绕 上 × 离群心的方向 转 θ）
vec3 planet_tilt(vec3 p, vec3 d) {
	float s = sqrt(p.x * p.x + p.z * p.z);
	if (s < 1.0) {
		return d;
	}
	float hx = p.x / s;
	float hz = p.z / s;
	float th = s / planet_curve_radius;
	float c = cos(th);
	float sn = sin(th);
	vec3 a = vec3(hz, 0.0, -hx);
	return d * c + cross(a, d) * sn + a * dot(a, d) * (1.0 - c);
}

// p 处的法线（切线）在画面上：R_cᵀ · R_p · d
vec3 planet_visual_normal(vec3 p, vec3 d) {
	if (planet_curve_on < 0.5) {
		return d;
	}
	return planet_tilt(p, d) * planet_curve_basis;
}

// 只转、不计该点的倾斜（旧写法；整体朝向用）
vec3 planet_visual_dir(vec3 d) {
	if (planet_curve_on < 0.5) {
		return d;
	}
	return d * planet_curve_basis;
}

// 模型空间的顶点与法线就地弯过（MODEL_MATRIX 含转动、平移与缩放；法线的转动很小，按模型轴近似就够）
void planet_curve_model(mat4 model, inout vec3 v, inout vec3 n) {
	if (planet_curve_on < 0.5) {
		return;
	}
	mat3 m = mat3(model);
	mat3 inv = inverse(m);
	vec3 w = (model * vec4(v, 1.0)).xyz;
	v += inv * (planet_visual(w) - w);
	n = normalize(inv * planet_visual_normal(w, m * n));
}
