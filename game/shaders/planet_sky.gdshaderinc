// 行星计划 P2：整颗行星的画法——天空着色器（sky.gdshader）与云层叠画（planet_overlay.gdshader）都 #include 它（先 include weather.gdshaderinc）。
//
// 画法（docs/PLAN-PLANET.md P2；为什么这样定见 README「行星计划 P2」）：
//   行星本身（海洋球、云带壳、烘进立方体贴图的岛群、夜里的灯）与大气不建网格，逐像素按视线与同心球面求交来画——
//   相机的局部系（画面的世界轴 = 相机处的东、上、南，星心在相机正下方 R + 相机高处）里，视线与海面（−5.6 km）、云带顶（−3.8 km）、
//   贴图岛群那一层（0 m）、天气云层（+2.8 km）几个球面求交，交点经 planet_axes 换到行星系取贴图与气候场。
//   没有深度、没有远近平面，两万公里高也是同一套式子，单精度只用相对相机的量。
//   岛群自己的地面、远处岛群的低模仍是网格，画在前面；高过 30 km 起 P1 的云带顶网格淡出、这里的行星淡入。
// 大气：Hillaire 2020 的两张表（src/planet_sky.* 在 CPU 上算好：透过率、多次散射），沿视线步进单次散射；单位 km。
// 行星云层：+2.8 km 的球壳，图案 = ④ 的降水、风暴、q、uplift × 顺风平移的噪声（两相交替，免得越拉越长）× 带界
//   （赤道永暴带、信风的碎积云、副热带无风带少云、西风带顺风拉长的锋面云、G 定点永暴的漩涡），跟着全局钟动；
//   离锚群 100 km 以内交给岛群自己的云团（weather.gdshaderinc，天与光给的云量），100–400 km 云量渐渐从岛群的换到气候场的。
// 立方体面映射与 src/planet_surface.cpp 的 planet_cube::face_of 逐式相同（改一处要改两处）。

// ---- 参数（PlanetGlobe 每帧设）
uniform float planet_on = 0.0;
uniform mat3 planet_axes = mat3(1.0); // 列 = 相机处的东、上、南（行星系）：行星系方向 = planet_axes * 局部方向
uniform float planet_cam_h = 0.0; // 相机的游戏 y（米，云带顶以上的零点之上）
uniform float planet_datum_r = 6376600.0; // 游戏 y = 0 那一层的半径（米）
uniform vec3 planet_sun = vec3(0.0, 0.0, 1.0); // 行星系
uniform vec3 planet_moon = vec3(0.0, 0.0, -1.0);
uniform float planet_moon_lit = 0.5;
uniform vec4 planet_time = vec4(0.0); // (全局钟的历日, 一年里的份 0..1, 带界这几天往北挪了几度, 0)
uniform vec3 planet_anchor = vec3(0.0, 0.0, 1.0); // 锚群的群心（行星系）
uniform vec4 planet_g = vec4(0.0, 0.0, 1.0, 0.07); // G 定点永暴的中心（行星系）与半径（弧度）
uniform float planet_surface_ready = 0.0; // 行星贴图烘好了
uniform float planet_mask_cos = 2.0; // 相机地面点这个角（余弦）以内的贴图岛群淡掉（那一圈由远处岛群的网格画）
uniform float planet_local_cover = 0.2; // 锚群那一带的云量（天与光按岛群的天气给的）
uniform float planet_pixel = 0.001; // 一个像素多少弧度（取贴图的 mip 用）
uniform float planet_sun_power = 1.0; // 地面、云受的直射（与场景里的方向光同一个量）
uniform float planet_sky_power = 3.14159; // 散射进来的天光乘多少：地面、云按「反照率 × 直射」算（没有 1/π），天光就乘 π 才对得上
uniform float planet_ambient = 1.2; // 地面、云受的天光（多次散射表 × 它）
uniform float planet_flash = 0.0;

uniform sampler2DArray planet_surface_color : filter_linear_mipmap, repeat_disable; // 预乘份额的 sRGB 地表色、A = 陆地份额
uniform sampler2DArray planet_surface_extra : filter_linear_mipmap, repeat_disable; // 高（/8000 m，预乘）、灯、林（预乘）、会枯黄的（预乘）
uniform sampler2D atmo_trans_lut : filter_linear, repeat_disable;
uniform sampler2D atmo_ms_lut : filter_linear, repeat_disable;
uniform sampler2D planet_fields : filter_linear, repeat_enable; // 1° 场：降水、风暴、q、uplift
uniform sampler2D planet_fields2 : filter_linear, repeat_enable; // 1° 场：风往东、风往北（m/s）、零点处年均温、年温差（°C）
uniform sampler2D planet_bands : filter_linear, repeat_enable; // 360 × 2：北半球四条带界（赤道永暴、信风、无风、西风的顶）、南半球四条（度，取绝对值）

const float PS_PI = 3.14159265358979;
const float ATMO_BOTTOM = 6371.0; // km：海面
const float ATMO_TOP = 6471.0;
const vec3 ATMO_RAY = vec3(5.802e-3, 13.558e-3, 33.1e-3);
const float ATMO_RAY_H = 8.0;
const float ATMO_MIE_S = 3.996e-3;
const float ATMO_MIE_E = 4.40e-3;
const float ATMO_MIE_H = 1.2;
const float ATMO_MIE_G = 0.8;
const vec3 ATMO_OZONE = vec3(0.650e-3, 1.881e-3, 0.085e-3);
const float PS_SEA_Y = -5600.0; // D13
const float PS_BAND_Y = -3800.0;
const float PS_CLOUD_Y = 2800.0;

// ---- 求交：相机在局部系 (0, r0, 0)（离星心 r0），沿单位方向 d 到半径 rs 的球面
// 近的交点（从外面打进去；在球里面就是出去的那一个）；打不到 −1。c = r0² − rs² 按 (r0 − rs)(r0 + rs) 算，单精度也稳
float ps_hit(float r0, float dy, float rs) {
	float b = r0 * dy;
	float c = (r0 - rs) * (r0 + rs);
	float disc = b * b - c;
	if (disc < 0.0) {
		return -1.0;
	}
	float sq = sqrt(disc);
	if (c > 0.0) {
		return b >= 0.0 ? -1.0 : c / (sq - b);
	}
	return b >= 0.0 ? sq + b : -c / (sq - b);
}

// 出去的那一个交点（相机在球外时是远的交点）；打不到 −1
float ps_hit_far(float r0, float dy, float rs) {
	float b = r0 * dy;
	float c = (r0 - rs) * (r0 + rs);
	float disc = b * b - c;
	if (disc < 0.0) {
		return -1.0;
	}
	float t = sqrt(disc) - b;
	return t > 0.0 ? t : -1.0;
}

// ---- 大气（km）
void atmo_medium(float h, out vec3 ray, out float mie, out vec3 ext) {
	float hh = max(h, 0.0);
	ray = ATMO_RAY * exp(-hh / ATMO_RAY_H);
	float dm = exp(-hh / ATMO_MIE_H);
	mie = ATMO_MIE_S * dm;
	ext = ray + ATMO_MIE_E * dm + ATMO_OZONE * max(0.0, 1.0 - abs(hh - 25.0) / 15.0);
}

vec2 atmo_sub_uv(vec2 unit, vec2 size) {
	return (clamp(unit, 0.0, 1.0) * (size - 1.0) + 0.5) / size;
}

vec2 atmo_trans_uv(float r, float mu) {
	float H = sqrt(ATMO_TOP * ATMO_TOP - ATMO_BOTTOM * ATMO_BOTTOM);
	float rho = sqrt(max(0.0, (r - ATMO_BOTTOM) * (r + ATMO_BOTTOM)));
	float disc = r * r * (mu * mu - 1.0) + ATMO_TOP * ATMO_TOP;
	float d = max(0.0, -r * mu + sqrt(max(0.0, disc)));
	float dmin = ATMO_TOP - r;
	float dmax = rho + H;
	return atmo_sub_uv(vec2((d - dmin) / max(1e-4, dmax - dmin), rho / H), vec2(256.0, 64.0));
}

// 从离星心 r 处朝天顶角余弦 mu 看太阳：透过率（海面挡住的按日面的大小软着过渡）
vec3 atmo_sun_trans(float r, float mu) {
	float q = ATMO_BOTTOM / max(r, ATMO_BOTTOM);
	float horizon = -sqrt(max(0.0, 1.0 - q * q));
	float vis = smoothstep(horizon - 0.005, horizon + 0.005, mu);
	return texture(atmo_trans_lut, atmo_trans_uv(r, max(mu, horizon))).rgb * vis;
}

vec3 atmo_ms(float r, float mu_s) {
	vec2 unit = vec2(mu_s * 0.5 + 0.5, (r - ATMO_BOTTOM) / (ATMO_TOP - ATMO_BOTTOM));
	return texture(atmo_ms_lut, atmo_sub_uv(unit, vec2(32.0))).rgb;
}

float atmo_phase_r(float c) {
	return 3.0 / (16.0 * PS_PI) * (1.0 + c * c);
}

float atmo_phase_m(float c) {
	float g = ATMO_MIE_G;
	float k = 3.0 / (8.0 * PS_PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + c * c) / pow(max(1e-4, 1.0 + g * g - 2.0 * g * c), 1.5);
}

// 沿视线 d（局部系）从 t0 走到 t1（km），相机在 (0, r0, 0)：进来的光（以太阳 = 1）与透过率
void atmo_march(float r0, vec3 d, float t0, float t1, vec3 sun, int steps, float jitter, out vec3 L, out vec3 T) {
	L = vec3(0.0);
	T = vec3(1.0);
	if (t1 <= t0) {
		return;
	}
	float c = dot(d, sun);
	float pr = atmo_phase_r(c);
	float pm = atmo_phase_m(c);
	float dt = (t1 - t0) / float(steps);
	for (int i = 0; i < steps; i++) {
		float t = t0 + (float(i) + jitter) * dt;
		vec3 p = vec3(0.0, r0, 0.0) + d * t;
		float r = length(p);
		float mu_s = dot(p, sun) / r;
		vec3 ray;
		float mie;
		vec3 ext;
		atmo_medium(r - ATMO_BOTTOM, ray, mie, ext);
		vec3 S = (ray * pr + mie * pm) * atmo_sun_trans(r, mu_s) + (ray + mie) * atmo_ms(r, mu_s);
		vec3 st = exp(-ext * dt);
		L += T * (S - S * st) / max(ext, vec3(1e-7));
		T *= st;
	}
}

// 相机到大气里 [进, 出] 的一段（km）；整段都在大气外返回 false
bool atmo_segment(float r0, vec3 d, float t_stop, out float t0, out float t1) {
	t0 = 0.0;
	t1 = t_stop;
	if (r0 > ATMO_TOP) {
		float te = ps_hit(r0, d.y, ATMO_TOP);
		if (te < 0.0) {
			return false;
		}
		t0 = te;
	}
	float tx = ps_hit_far(r0, d.y, ATMO_TOP);
	if (tx > 0.0) {
		t1 = min(t1, tx);
	}
	return t1 > t0;
}

// ---- 噪声（三维值噪声，行星系的单位向量乘尺度后取）
uint pn_hash3(ivec3 p) {
	uvec3 q = uvec3(p);
	uint h = q.x * 0x8da6b343u ^ q.y * 0xd8163841u ^ q.z * 0xcb1ab31fu;
	h ^= h >> 15u;
	h *= 0x85ebca6bu;
	h ^= h >> 13u;
	h *= 0xc2b2ae35u;
	h ^= h >> 16u;
	return h;
}

float pn_value3(vec3 p) {
	vec3 i = floor(p);
	vec3 f = p - i;
	vec3 u = f * f * (3.0 - 2.0 * f);
	ivec3 c = ivec3(i);
	float n000 = float(pn_hash3(c) & 0xFFFFu) / 65535.0;
	float n100 = float(pn_hash3(c + ivec3(1, 0, 0)) & 0xFFFFu) / 65535.0;
	float n010 = float(pn_hash3(c + ivec3(0, 1, 0)) & 0xFFFFu) / 65535.0;
	float n110 = float(pn_hash3(c + ivec3(1, 1, 0)) & 0xFFFFu) / 65535.0;
	float n001 = float(pn_hash3(c + ivec3(0, 0, 1)) & 0xFFFFu) / 65535.0;
	float n101 = float(pn_hash3(c + ivec3(1, 0, 1)) & 0xFFFFu) / 65535.0;
	float n011 = float(pn_hash3(c + ivec3(0, 1, 1)) & 0xFFFFu) / 65535.0;
	float n111 = float(pn_hash3(c + ivec3(1, 1, 1)) & 0xFFFFu) / 65535.0;
	return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y), mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float pn_fbm3(vec3 p, int octaves) {
	float sum = 0.0;
	float amp = 0.5;
	float norm = 0.0;
	for (int k = 0; k < octaves; k++) {
		sum += amp * pn_value3(p + vec3(19.1, 7.3, 3.7) * float(k));
		norm += amp;
		p = p * 2.03;
		amp *= 0.5;
	}
	return sum / norm;
}

// ---- 立方体面映射（同 planet_cube::face_of）：返回 (u, v, 面)
vec3 planet_cube_uv(vec3 d) {
	vec3 a = abs(d);
	vec3 n;
	vec3 eu;
	vec3 ev;
	float f;
	if (a.x >= a.y && a.x >= a.z) {
		if (d.x >= 0.0) {
			f = 0.0; n = vec3(1.0, 0.0, 0.0); eu = vec3(0.0, 1.0, 0.0); ev = vec3(0.0, 0.0, 1.0);
		} else {
			f = 1.0; n = vec3(-1.0, 0.0, 0.0); eu = vec3(0.0, -1.0, 0.0); ev = vec3(0.0, 0.0, 1.0);
		}
	} else if (a.y >= a.z) {
		if (d.y >= 0.0) {
			f = 2.0; n = vec3(0.0, 1.0, 0.0); eu = vec3(-1.0, 0.0, 0.0); ev = vec3(0.0, 0.0, 1.0);
		} else {
			f = 3.0; n = vec3(0.0, -1.0, 0.0); eu = vec3(1.0, 0.0, 0.0); ev = vec3(0.0, 0.0, 1.0);
		}
	} else {
		if (d.z >= 0.0) {
			f = 4.0; n = vec3(0.0, 0.0, 1.0); eu = vec3(0.0, 1.0, 0.0); ev = vec3(-1.0, 0.0, 0.0);
		} else {
			f = 5.0; n = vec3(0.0, 0.0, -1.0); eu = vec3(0.0, 1.0, 0.0); ev = vec3(1.0, 0.0, 0.0);
		}
	}
	float dn = max(1e-6, dot(d, n));
	float u = dot(d, eu) / dn;
	float v = dot(d, ev) / dn;
	return vec3((atan(u) * 4.0 / PS_PI + 1.0) * 0.5, (atan(v) * 4.0 / PS_PI + 1.0) * 0.5, f);
}

// ---- 1° 场
vec2 planet_field_uv(vec3 p) {
	float lat = asin(clamp(p.z, -1.0, 1.0));
	float lon = atan(p.y, p.x);
	return vec2(lon / (2.0 * PS_PI) + 0.5, clamp(lat / PS_PI + 0.5, 0.5 / 180.0, 1.0 - 0.5 / 180.0));
}

// ---- 行星云层（+2.8 km）：p 是行星系单位向量；thick = 云厚（越厚越白越高），storm = 这里的风暴（夜里打闪）
float planet_cloud(vec3 p, out float thick, out float storm) {
	vec2 uv = planet_field_uv(p);
	vec4 f = texture(planet_fields, uv); // 降水、风暴、q、uplift
	vec4 f2 = texture(planet_fields2, uv); // 风往东、往北、年均温、年温差
	float lat = (uv.y - 0.5) * 180.0;
	float lat_eff = lat - planet_time.z;
	float a = abs(lat_eff);
	vec4 e = lat_eff >= 0.0 ? texture(planet_bands, vec2(uv.x, 0.25)) : texture(planet_bands, vec2(uv.x, 0.75));
	float w_eq = 1.0 - smoothstep(e.x - 1.5, e.x + 1.5, a);
	float w_calm = smoothstep(e.y - 2.0, e.y + 2.0, a) * (1.0 - smoothstep(e.z - 2.0, e.z + 2.0, a));
	float w_west = smoothstep(e.z - 2.0, e.z + 3.0, a) * (1.0 - smoothstep(e.w - 3.0, e.w + 3.0, a));
	float w_trade = smoothstep(e.x - 1.5, e.x + 1.5, a) * (1.0 - smoothstep(e.y - 2.0, e.y + 2.0, a));
	float w_polar = smoothstep(e.w - 3.0, e.w + 3.0, a);

	float cov = 0.10 + 0.50 * f.r + 0.35 * f.a + 0.30 * f.g + 0.25 * (f.b - 0.55);
	cov = mix(cov, 0.9, w_eq * 0.95); // 赤道永暴带：几乎连成一条白带
	cov *= 1.0 - 0.75 * w_calm; // 副热带无风带：下沉、干，天气云少，露出底下的云带
	cov *= 1.0 - 0.2 * w_trade;
	cov += 0.12 * w_polar;

	// 顺风平移：两相交替（周期 3 天），各拿一份随周期换的偏移，免得图案越拉越长、一个周期后又回到原处
	vec3 east = normalize(vec3(-p.y, p.x, 0.0) + vec3(1e-6, 0.0, 0.0));
	vec3 north = cross(p, east);
	vec3 wind = east * f2.r + north * f2.g; // m/s
	float period_h = 72.0;
	float hours = planet_time.x * 24.0;
	float ph0 = fract(hours / period_h);
	float ph1 = fract(hours / period_h + 0.5);
	float cyc0 = floor(hours / period_h);
	float cyc1 = floor(hours / period_h + 0.5);
	// 与岛群的云团同一个漂法：每游戏钟头漂 风速 × 360 m
	vec3 d0 = wind * (ph0 * period_h * 360.0 / 6376600.0);
	vec3 d1 = wind * (ph1 * period_h * 360.0 / 6376600.0);
	vec3 o0 = vec3(fract(cyc0 * 0.6180339) * 97.0, fract(cyc0 * 0.4142136) * 89.0, 0.0);
	vec3 o1 = vec3(fract(cyc1 * 0.6180339) * 97.0 + 41.0, fract(cyc1 * 0.4142136) * 89.0 + 13.0, 7.0);
	float w0 = 1.0 - abs(ph0 * 2.0 - 1.0);
	float scale = 18.0; // 约 350 km 一团
	float n = w0 * pn_fbm3((p - d0) * scale + o0, 5) + (1.0 - w0) * pn_fbm3((p - d1) * scale + o1, 5);
	n = (n - 0.5) * 1.35 + 0.5; // 三维值噪声的 fbm 比二维的窄，拉开一点再按同一张分位表取门槛

	// 西风带：顺风拉长的锋面云（经向压扁的柱面噪声）
	if (w_west > 0.01) {
		float lon = (uv.x - 0.5) * 2.0 * PS_PI;
		float latr = lat * PS_PI / 180.0;
		float west_drift = hours * 360.0 * 12.0 / 6376600.0; // 西风整体往东
		vec3 cyl = vec3(cos(lon - west_drift) * 5.0, sin(lon - west_drift) * 5.0, latr * 38.0);
		float streak = pn_fbm3(cyl + vec3(3.1, 0.0, 0.0), 4);
		n = mix(n, (streak - 0.5) * 1.5 + 0.5, w_west * 0.65);
	}
	// 赤道永暴带：一团团的对流云（一两百公里一簇）
	if (w_eq > 0.01) {
		float conv = pn_fbm3((p - d0 * 0.5) * 48.0 + o0, 4); // 一两百公里一簇的对流云团
		n = mix(n, (conv - 0.5) * 1.8 + 0.62, w_eq * 0.85);
	}
	// 信风带：碎积云
	if (w_trade > 0.01) {
		float cu = pn_fbm3((p - d0) * 160.0 + o1, 2);
		n = mix(n, n * 0.5 + cu * 0.55, w_trade * 0.5);
	}
	// G 定点永暴：两条旋臂的漩涡，中间是眼（北半球逆时针旋进）
	float g_mask = 0.0;
	float gd = length(p - planet_g.xyz); // 弦长 ≈ 弧度
	if (gd < planet_g.w * 5.0) {
		vec3 g = planet_g.xyz;
		vec3 ge = normalize(cross(vec3(0.0, 0.0, 1.0), g));
		vec3 gn = cross(g, ge);
		float th = atan(dot(p, gn), dot(p, ge)) + 0.9 * (pn_fbm3(p * 60.0 + o0, 3) - 0.5); // 旋臂抖一抖，别太规整
		float rr = gd / planet_g.w;
		float spiral = 0.5 + 0.5 * cos(2.0 * (th + 2.6 * log(max(rr, 0.03)) - hours * 0.05));
		spiral = mix(spiral, 1.0, smoothstep(0.9, 0.35, rr) * 0.8); // 近中心连成一片
		g_mask = 1.0 - smoothstep(1.4, 4.6, rr);
		float eye = smoothstep(0.07, 0.16, rr);
		n = mix(n, spiral * 0.75 + n * 0.35, g_mask);
		cov = mix(cov, 0.62, g_mask) * mix(1.0, eye, g_mask);
	}
	// 锚群附近：云量从岛群天气给的渐渐换到气候场的（100 → 400 km）
	float da = length(p - planet_anchor) * 6376.6;
	cov = mix(planet_local_cover, cov, smoothstep(100.0, 400.0, da));
	cov = clamp(cov, 0.0, 0.95);
	float thr = wx_threshold(cov);
	float dens = smoothstep(thr - 0.06, thr + 0.06, n);
	thick = smoothstep(thr, thr + 0.22, n) * (0.6 + 0.4 * max(w_eq, g_mask));
	storm = clamp(max(max(w_eq, g_mask), f.g) * dens, 0.0, 1.0);
	return dens;
}

// ---- 云带壳（−3.8 km）：稳定的一层，有疏有密；缝多在风暴带、少在副热带无风带。返回 1 = 云、0 = 缝
float planet_band_cover(vec3 p, out float shade) {
	vec2 uv = planet_field_uv(p);
	vec4 f = texture(planet_fields, uv);
	float gap = 0.012 + 0.30 * f.g * f.g + 0.06 * f.a + 0.04 * f.r; // 风暴带缝多、副热带无风带（风暴与降水都少）几乎连成一片
	float slow = planet_time.x * 0.02; // 云带几乎不动
	float n = pn_fbm3(p * 55.0 + vec3(slow, 0.0, 0.0), 4);
	n = (n - 0.5) * 1.35 + 0.5;
	float thr = wx_threshold(1.0 - clamp(gap, 0.0, 0.6)); // 场值高过门槛的是云（占 1 − gap），低的是缝
	shade = 0.86 + 0.14 * pn_fbm3(p * 700.0, 2);
	return smoothstep(thr - 0.06, thr + 0.02, n);
}

vec3 ps_srgb(vec3 c) {
	return mix(pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), c * (1.0 / 12.92), lessThan(c, vec3(0.04045)));
}

// ---- 从局部系方向 d（相机在 (0, r0, 0)，米）看行星的表面：打到了返回 true；color 是这一点反出来的光（还没乘大气），t_hit 米、hit_n 是交点的行星系单位向量
bool planet_surface(float r0, vec3 d, vec3 sun_l, out vec3 color, out float t_hit, out vec3 hit_n) {
	float R = planet_datum_r;
	float t_band = ps_hit(r0, d.y, R + PS_BAND_Y);
	color = vec3(0.0);
	t_hit = -1.0;
	hit_n = vec3(0.0, 0.0, 1.0);
	if (t_band < 0.0 && r0 > R + PS_BAND_Y) {
		return false;
	}
	// 贴图里的岛群（y = 0 那一层）
	float t_isl = ps_hit(r0, d.y, R);
	vec3 island = vec3(0.0);
	float isl_a = 0.0;
	float isl_light = 0.0;
	if (t_isl > 0.0 && planet_surface_ready > 0.5) {
		vec3 q = vec3(0.0, r0, 0.0) + d * t_isl;
		vec3 pn = normalize(planet_axes * q);
		// 相机地面点附近由远处岛群的网格画：那一圈淡掉
		float mask = smoothstep(planet_mask_cos, planet_mask_cos - 0.0006, dot(pn, planet_axes[1]));
		vec3 cu = planet_cube_uv(pn);
		float incid = max(0.12, abs(dot(q / length(q), d)));
		// 这一像素在地上多大 ÷ 贴图一像素多大（面宽 2048 时约 4.9 km）→ mip 级（按视线自己算：跨面的地方导数会跳）
		float lod = log2(max(1.0, t_isl * planet_pixel / incid * float(textureSize(planet_surface_color, 0).x) / (4890.0 * 2048.0)));
		vec4 c = textureLod(planet_surface_color, cu, lod);
		vec4 x = textureLod(planet_surface_extra, cu, lod);
		// 远处一像素盖好几个贴图像素：小岛的份额被平均得很淡，按 mip 级把份额往上提（远看岛群的分布要看得出来）
		isl_a = pow(c.a, mix(1.0, 0.55, clamp(lod / 3.0, 0.0, 1.0))) * mask;
		if (isl_a > 0.002) {
			float inv = 1.0 / max(c.a, 1e-3);
			vec3 base = ps_srgb(clamp(c.rgb * inv, 0.0, 1.0));
			float h = x.r * inv * 8000.0;
			float forest = x.b * inv;
			float herb = x.a * inv;
			vec4 f2 = texture(planet_fields2, planet_field_uv(pn));
			float south = pn.z < 0.0 ? 1.0 : 0.0;
			float warm = (126.0 + 45.0 + south * 168.0) / 336.0;
			float temp = f2.b + 0.5 * f2.a * cos(2.0 * PS_PI * (planet_time.y - warm)) - 6.0 * h / 1000.0;
			float dormant = smoothstep(7.0, 2.0, temp);
			base = mix(base, vec3(0.36, 0.30, 0.16), herb * dormant * 0.7);
			base = mix(base, vec3(0.17, 0.12, 0.07), forest * dormant * 0.35);
			float snow = smoothstep(-0.5, -4.0, temp);
			base = mix(base, vec3(0.85, 0.87, 0.90), snow * 0.9);
			island = base;
			isl_light = x.g * mask;
		}
	}
	// 云带壳（−3.8 km）与缝里的海
	vec3 band_col = vec3(0.0);
	vec3 bn = vec3(0.0, 0.0, 1.0);
	if (t_band > 0.0) {
		vec3 q = vec3(0.0, r0, 0.0) + d * t_band;
		bn = normalize(planet_axes * q);
		float shade;
		float cover = planet_band_cover(bn, shade);
		vec3 up_l = q / length(q);
		float mu_s = dot(up_l, sun_l);
		float rk = (R + PS_BAND_Y) / 1000.0;
		vec3 sun_t = atmo_sun_trans(rk, mu_s) * planet_sun_power;
		vec3 amb = atmo_ms(rk, mu_s) * planet_ambient + vec3(0.004, 0.005, 0.008) * (0.2 + planet_moon_lit);
		// 天气云的影子（顺着太阳往上找 +2.8 km 那一层）
		float shadow = 0.0;
		if (mu_s > 0.02) {
			vec3 sun_p = planet_axes * sun_l;
			vec3 toward = normalize(sun_p - bn * dot(sun_p, bn));
			float along = (PS_CLOUD_Y - PS_BAND_Y) / R * sqrt(max(0.0, 1.0 - mu_s * mu_s)) / max(0.08, mu_s);
			float th;
			float st;
			shadow = planet_cloud(normalize(bn + toward * along), th, st) * 0.55;
		}
		vec3 lit = sun_t * max(mu_s, 0.0) * (1.0 - shadow) + amb;
		vec3 cloud = vec3(0.52, 0.56, 0.62) * shade * lit; // 云带比天气云暗、偏灰蓝：从上往下看分得出两层
		// 海：深蓝，日光的反光（从缝里看得见），远处按菲涅耳反天光；海面上的白礁（纯氛围）
		vec3 ocean = vec3(0.004, 0.013, 0.028) * lit;
		vec3 refl = reflect(d, up_l);
		float rs = max(dot(refl, sun_l), 0.0);
		float fres = 0.02 + 0.98 * pow(1.0 - max(0.0, dot(-d, up_l)), 5.0);
		// 日光的反光：一小片亮斑（浪把它散开一点），按菲涅耳；远处擦着看时反一点天光
		float glint = (pow(rs, 900.0) * 6.0 + pow(rs, 80.0) * 0.25) * fres;
		ocean += glint * sun_t + fres * vec3(0.06, 0.09, 0.14) * max(mu_s + 0.1, 0.0);
		// 白礁（纯氛围）：一片片的，只在有些地方
		float reef = smoothstep(0.72, 0.8, pn_fbm3(bn * 260.0 + vec3(5.0), 3)) * smoothstep(0.52, 0.68, pn_value3(bn * 22.0 + vec3(9.0))) * 0.7;
		ocean = mix(ocean, vec3(0.62, 0.64, 0.6) * lit, reef);
		band_col = mix(ocean, cloud, cover);
	}
	// 岛群的光照：朝上的面，受直射与天光；夜里的灯
	vec3 isl_col = vec3(0.0);
	if (isl_a > 0.002) {
		vec3 q = vec3(0.0, r0, 0.0) + d * t_isl;
		vec3 up_l = q / length(q);
		float mu_s = dot(up_l, sun_l);
		float rk = R / 1000.0;
		vec3 lit = atmo_sun_trans(rk, mu_s) * planet_sun_power * max(mu_s, 0.0) + atmo_ms(rk, mu_s) * planet_ambient + vec3(0.003, 0.004, 0.006) * (0.2 + planet_moon_lit);
		float night = smoothstep(0.05, -0.12, mu_s);
		// 夜里的灯：按人口的密度，撒成一点一点（村、镇）；远处一像素盖几公里时取平均
		vec3 qn = normalize(planet_axes * (vec3(0.0, r0, 0.0) + d * t_isl));
		float foot = t_isl * planet_pixel;
		float sparkle = mix(smoothstep(0.5, 0.8, pn_value3(qn * 2600.0)) * smoothstep(0.72, 0.9, pn_value3(qn * 21000.0)) * 6.0, 0.55, smoothstep(300.0, 3000.0, foot));
		isl_col = island * lit + vec3(1.0, 0.7, 0.36) * isl_light * isl_light * sparkle * night * 2.6;
	}
	if (t_band > 0.0) {
		color = mix(band_col, isl_col, isl_a);
		t_hit = isl_a > 0.5 && t_isl > 0.0 ? t_isl : t_band;
		hit_n = bn;
		return true;
	}
	return false;
}

// ---- 夜里的灯（行星系单位向量 pn，这一点的局部上 up_l；foot = 一像素在地上多大，米）：人口按贴图撒成一点一点
vec3 planet_night_lights(vec3 pn, vec3 up_l, vec3 sun_l, float foot) {
	if (planet_surface_ready < 0.5) {
		return vec3(0.0);
	}
	float night = smoothstep(0.05, -0.12, dot(up_l, sun_l));
	if (night <= 0.0) {
		return vec3(0.0);
	}
	vec3 cu = planet_cube_uv(pn);
	float lod = log2(max(1.0, foot * float(textureSize(planet_surface_extra, 0).x) / (4890.0 * 2048.0)));
	float light = textureLod(planet_surface_extra, cu, lod).g;
	float sparkle = mix(smoothstep(0.5, 0.8, pn_value3(pn * 2600.0)) * smoothstep(0.72, 0.9, pn_value3(pn * 21000.0)) * 6.0, 0.55, smoothstep(300.0, 3000.0, foot));
	return vec3(1.0, 0.7, 0.36) * light * light * sparkle * night * 2.6;
}

// ---- 天气云层（+2.8 km）的颜色：p 行星系，up_l 该点的局部上，相机方向 d
vec3 planet_cloud_color(vec3 p, vec3 up_l, vec3 d, vec3 sun_l, float thick, float storm) {
	float mu_s = dot(up_l, sun_l);
	float rk = (planet_datum_r + PS_CLOUD_Y) / 1000.0;
	vec3 sun_t = atmo_sun_trans(rk, mu_s) * planet_sun_power;
	vec3 amb = atmo_ms(rk, mu_s) * planet_ambient * 1.2 + vec3(0.004, 0.005, 0.008) * (0.2 + planet_moon_lit);
	float diffuse = max(mu_s, 0.0) * (0.55 + 0.45 * thick) + 0.25 * max(mu_s + 0.2, 0.0) * (1.0 - thick);
	vec3 col = vec3(0.92, 0.93, 0.95) * (sun_t * diffuse + amb * (0.8 + 0.4 * thick));
	// 夜里风暴带打闪
	float night = smoothstep(0.05, -0.15, mu_s);
	if (storm > 0.2 && night > 0.0) {
		vec3 cell = floor(p * 900.0);
		float tick = floor(planet_time.x * 24.0 * 60.0 * 3.0);
		float flash = step(0.9985, float(pn_hash3(ivec3(cell) + ivec3(int(tick), 0, 0)) & 0xFFFFu) / 65535.0);
		col += vec3(0.6, 0.7, 1.0) * flash * storm * night * 3.0;
	}
	return col;
}
