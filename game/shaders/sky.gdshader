// 天空（Atmosphere 建、每帧设参数）：昼夜与晨昏的渐变、太阳、月亮（按月相亮着的份额）、星星、云层、阴沉与雨雾、闪电。
// 云层是一张 cloud_height 高的平面，云在上面按云团噪声（weather.gdshaderinc，与 C++ 的 weather_noise 同一式）取：
//   场值高过「按云量取的门槛」的地方有云——与地上判「哪儿在下雨」是同一张场，抬头看见的乌云底下正在下雨；
//   云量按相机那一带的值、再按梯度（cover_grad，每千米变多少）往远处外推，所以锋面过来时远处的天先阴下来。
// 反射与环境光的立方图（AT_CUBEMAP_PASS）里不画云与星，只按阴沉压暗——便宜，环境光也跟着天色走。
// 有行星数据的地图（planet_curve_on，行星计划 P1）：云层是 cloud_height 高的一层球壳（半径 R + cloud_height，球心在相机正下方 R + 相机高处），
// 视线与球壳求交（数值稳的写法）；远处的云随地平线一起弯下去，飞到云层以上往下看，云壳到它自己的地平线为止。
// 行星计划 P2（planet_on，PlanetGlobe 设）：
//   云层离锚群 100 km 以外换成行星云层（planet_sky.gdshaderinc 的 planet_cloud：气候场 × 顺风的噪声 × 带界），近处照旧是岛群自己的云团；
//   相机高过 20 km 起天上交叉淡化成大气散射（Hillaire 的两张表逐像素步进，atmo_blend），高过 30 km 起地平线以下画整颗行星
//   （海、云带壳、贴图岛群、夜里的灯，planet_blend，与 P1 的云带顶网格交叉淡化）；太空里的星星按行星系转、日面与月亮（月相按日月的方向亮）。
//   地面上（两个 blend 都是 0）天色、晨昏、日月星与原来一样。
shader_type sky;
render_mode use_debanding;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/planet_sky.gdshaderinc"

uniform vec3 sun_dir = vec3(0.0, 1.0, 0.0);
uniform vec3 moon_dir = vec3(0.0, -1.0, 0.0);
uniform float moon_lit = 0.0; // 月亮亮着的份额
uniform float cloud_cover = 0.12; // 相机那一带的云量（覆盖率）
uniform vec2 cover_grad = vec2(0.0); // 云量每千米变多少
uniform float overcast = 0.0; // 阴沉 0..1（雨天、风暴）
uniform float rain_haze = 0.0; // 降水的灰（远处看不清）
uniform vec2 drift = vec2(0.0); // 云团漂到哪（格，已对 1024 取模）
uniform float cell_m = 2600.0;
uniform float cloud_height = 2800.0;
uniform float cloud_edge = 0.08;
uniform float flash = 0.0; // 闪电
uniform float star_scale = 1.0;
uniform vec3 day_zenith : source_color = vec3(0.24, 0.43, 0.72);
uniform vec3 day_horizon : source_color = vec3(0.74, 0.83, 0.92);
uniform vec3 dusk_horizon : source_color = vec3(0.98, 0.58, 0.32);
uniform vec3 dusk_zenith : source_color = vec3(0.30, 0.32, 0.52);
uniform vec3 night_zenith : source_color = vec3(0.012, 0.018, 0.045);
uniform vec3 night_horizon : source_color = vec3(0.04, 0.06, 0.10);
uniform vec3 overcast_color : source_color = vec3(0.55, 0.58, 0.62);
uniform vec3 ground_color : source_color = vec3(0.62, 0.66, 0.72);
uniform float planet_blend = 0.0; // 地平线以下画整颗行星（行星计划 P2）
uniform float atmo_blend = 0.0; // 天上换成大气散射（行星计划 P2）

float star_hash(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.x + p.y) * p.z);
}

// 物理的天与整颗行星（局部方向 dir；相机离星心 r0 米）：hit = 视线打到了行星
vec3 planet_sky_physical(vec3 dir, float r0, float jitter, out float hit) {
	vec3 sun_l = normalize(planet_sun * planet_axes); // 行星系 → 局部：乘 planet_axes 的转置
	vec3 moon_l = normalize(planet_moon * planet_axes);
	vec3 surf;
	float t_hit;
	vec3 hit_n;
	bool on_planet = planet_surface(r0, dir, sun_l, surf, t_hit, hit_n);
	hit = on_planet ? 1.0 : 0.0;
	float r0k = r0 / 1000.0;
	vec3 L = vec3(0.0);
	vec3 T = vec3(1.0);
	float t0;
	float t1;
	if (atmo_segment(r0k, dir, on_planet ? t_hit / 1000.0 : 1e9, t0, t1)) {
		atmo_march(r0k, dir, t0, t1, sun_l, 32, jitter, L, T);
	}
	vec3 col = L * planet_sky_power;
	if (on_planet) {
		return col + T * surf;
	}
	// 太空一侧：星星（按行星系、随自转转）、日面、月亮
	float sky_lum = dot(col, vec3(0.3, 0.5, 0.2));
	vec3 pd = planet_axes * dir;
	float spin = 2.0 * PS_PI * planet_time.x * (1.0 + 1.0 / 336.0);
	vec3 inertial = vec3(pd.x * cos(spin) - pd.y * sin(spin), pd.x * sin(spin) + pd.y * cos(spin), pd.z);
	vec3 cell = floor(inertial * 300.0 * star_scale);
	float st = star_hash(cell);
	float twinkle = 0.75 + 0.25 * sin(TIME * (1.0 + st * 3.0) + st * 40.0);
	float star = step(0.9972, st) * mix(0.4, 1.6, fract(st * 91.7)) * twinkle;
	// 白天（太阳在相机的地平线以上）亮处看不见星星；远到行星只占一小块时照样看得见
	float glare = smoothstep(-0.1, 0.1, sun_l.y) * (1.0 - smoothstep(2.0e6, 1.0e7, planet_cam_h));
	col += vec3(0.9, 0.93, 1.0) * star * T * (1.0 - smoothstep(0.01, 0.12, sky_lum)) * (1.0 - glare);
	float sd = dot(dir, sun_l);
	col += vec3(1.0, 0.96, 0.9) * smoothstep(0.999986, 0.999992, sd) * 40.0 * T;
	col += vec3(1.0, 0.92, 0.8) * (pow(max(sd, 0.0), 6000.0) * 3.0 + pow(max(sd, 0.0), 300.0) * 0.08) * T;
	// 月亮：按日月的方向算哪半边亮（月相），暗的那半边有一点地照
	float md = dot(dir, moon_l);
	float rho = 0.0095; // 月面的角半径（比真的大一点，看得清月相）
	if (md > cos(rho * 1.2)) {
		vec3 off = dir - moon_l * md;
		float q = length(off) / rho;
		if (q < 1.0) {
			vec3 n = normalize(off / rho - moon_l * sqrt(max(0.0, 1.0 - q * q)));
			float lit = max(dot(n, planet_sun * planet_axes), 0.0);
			float edge = 1.0 - smoothstep(0.9, 1.0, q);
			col = mix(col, vec3(0.85, 0.86, 0.9) * (lit * 1.4 + 0.02) * T, edge);
		}
	}
	return col;
}

void sky() {
	vec3 dir = normalize(EYEDIR);
	float s = sun_dir.y;
	float day = smoothstep(-0.12, 0.22, s);
	float dusk = smoothstep(-0.22, -0.02, s) * (1.0 - smoothstep(0.02, 0.32, s));
	float toward_sun = pow(max(0.0, dot(normalize(vec2(dir.x, dir.z) + 1e-5), normalize(vec2(sun_dir.x, sun_dir.z) + 1e-5))) * 0.5 + 0.5, 3.0);

	vec3 zenith = mix(night_zenith, day_zenith, day);
	zenith = mix(zenith, dusk_zenith, dusk * 0.5);
	vec3 horizon = mix(night_horizon, day_horizon, day);
	horizon = mix(horizon, dusk_horizon, dusk * toward_sun);
	float up = clamp(dir.y, 0.0, 1.0);
	vec3 col = mix(horizon, zenith, pow(up, 0.55));
	if (dir.y < 0.0) {
		col = mix(horizon, ground_color * mix(0.04, 1.0, day), clamp(-dir.y * 3.0, 0.0, 1.0));
	}

	float clear = 1.0 - overcast;
	// 太阳：日面与光晕（阴天看不见日面）
	float sd = dot(dir, normalize(sun_dir));
	col += vec3(1.0, 0.93, 0.8) * smoothstep(0.99985, 0.99995, sd) * 18.0 * clear * step(-0.02, s);
	col += mix(vec3(1.0, 0.6, 0.35), vec3(1.0, 0.9, 0.75), day) * pow(max(sd, 0.0), 12.0) * 0.35 * clear * smoothstep(-0.15, 0.05, s);

	if (!AT_CUBEMAP_PASS) {
		float dark = 1.0 - day;
		// 星星：夜里、晴的地方
		vec3 cell = floor(dir * 260.0 * star_scale);
		float st = star_hash(cell);
		float star = step(0.9975, st) * (0.5 + 0.5 * sin(TIME * (1.0 + st * 3.0) + st * 40.0));
		col += vec3(0.9, 0.93, 1.0) * star * dark * clear * smoothstep(0.02, 0.2, dir.y) * 1.2;
		// 月亮：日面按亮着的份额，夜里才看得见
		float md = dot(dir, normalize(moon_dir));
		float disk = smoothstep(0.99993, 0.99997, md);
		col += vec3(0.85, 0.88, 0.95) * disk * (0.15 + 2.5 * moon_lit) * clear * smoothstep(-0.02, 0.05, moon_dir.y);
		col += vec3(0.5, 0.55, 0.7) * pow(max(md, 0.0), 40.0) * 0.08 * moon_lit * dark * clear;

		// 云层
		bool below = POSITION.y < cloud_height;
		float t = -1.0;
		if (planet_curve_on > 0.5) {
			// 球壳：t² + 2bt − q = 0，b = r0·dir.y，q = (R + h)² − r0²（r0 = R + 相机高）
			float r0 = planet_curve_radius + POSITION.y;
			float b = r0 * dir.y;
			float q = (cloud_height - POSITION.y) * (2.0 * planet_curve_radius + POSITION.y + cloud_height);
			float disc = b * b + q;
			if (disc >= 0.0) {
				float sq = sqrt(disc);
				if (below) {
					t = b >= 0.0 ? q / (b + sq) : sq - b;
				} else if (b < 0.0) {
					t = -q / (sq - b); // 近的那个交点
				}
			}
		} else if ((below && dir.y > 0.01) || (!below && dir.y < -0.01)) {
			t = (cloud_height - POSITION.y) / dir.y;
		}
		if (t > 0.0) {
			// 岛群自己的云团（近处）与行星云层（离锚群 100 km 外，P2）
			float local_w = 1.0 - smoothstep(30000.0, 90000.0, t);
			float planet_dens = 0.0;
			float planet_thick = 0.0;
			if (planet_on > 0.5) {
				vec3 X = vec3(0.0, planet_datum_r + POSITION.y, 0.0) + dir * t;
				vec3 pn = normalize(planet_axes * X);
				float da = length(pn - planet_anchor) * 6376.6;
				local_w *= 1.0 - smoothstep(70.0, 130.0, da);
				local_w *= 1.0 - smoothstep(cell_m * 0.06, cell_m * 0.25, t * planet_pixel); // 一像素盖过一团云（高空）：换成行星云层
				if (local_w < 0.999) {
					float storm_unused;
					planet_dens = planet_cloud(pn, planet_thick, storm_unused);
				}
			}
			if (local_w > 0.001 || planet_on > 0.5) {
				float dens = 0.0;
				float thick = 0.0;
				if (local_w > 0.001) {
					vec2 p = POSITION.xz + dir.xz * t;
					vec2 q = p / cell_m - drift;
					float field = wx_fbm(q);
					float cov = clamp(cloud_cover + dot(cover_grad, (p - POSITION.xz) / 1000.0), 0.0, 1.0);
					float thr = wx_threshold(cov);
					dens = smoothstep(thr - cloud_edge, thr + cloud_edge, field);
					dens *= 0.8 + 0.4 * wx_value(q * 7.0 + vec2(TIME * 0.004, 0.0));
					dens = clamp(dens, 0.0, 1.0);
					thick = smoothstep(thr, thr + 0.25, field);
				}
				if (planet_on > 0.5) {
					dens = mix(planet_dens, dens, local_w);
					thick = mix(planet_thick, thick, local_w);
				} else {
					dens *= local_w;
				}
				vec3 lit = mix(vec3(0.95, 0.96, 0.98), vec3(1.0, 0.72, 0.5), dusk * toward_sun);
				vec3 base = mix(vec3(0.55, 0.58, 0.63), vec3(0.32, 0.34, 0.38), thick);
				vec3 cloud = mix(base, lit, 0.55 * (1.0 - thick)) * mix(0.05, 1.0, day);
				cloud *= 1.0 - 0.45 * overcast;
				col = mix(col, cloud, dens * 0.95);
			}
		}
		// 降水的灰：远处（近地平线）看不清
		col = mix(col, overcast_color * mix(0.06, 0.8, day), rain_haze * pow(1.0 - abs(dir.y), 3.0));

		// 行星计划 P2：高空的天与整颗行星
		if (planet_on > 0.5 && (planet_blend > 0.001 || atmo_blend > 0.001)) {
			float jitter = fract(52.9829189 * fract(dot(SCREEN_UV * 1000.0, vec2(0.06711056, 0.00583715))));
			float hit;
			vec3 phys = planet_sky_physical(dir, planet_datum_r + planet_cam_h, jitter, hit);
			col = mix(col, phys, hit > 0.5 ? planet_blend : atmo_blend);
		}
	}
	// 阴沉：整片天压成灰
	col = mix(col, overcast_color * mix(0.05, 0.75, day), overcast * 0.85);
	col += vec3(0.8, 0.85, 1.0) * flash * 1.5;
	COLOR = col;
}
