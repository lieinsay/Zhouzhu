// 天空（Atmosphere 建、每帧设参数）：昼夜与晨昏的渐变、太阳、月亮（按月相亮着的份额）、星星、云层、阴沉与雨雾、闪电。
// 云层是一张 cloud_height 高的平面，云在上面按云团噪声（weather.gdshaderinc，与 C++ 的 weather_noise 同一式）取：
//   场值高过「按云量取的门槛」的地方有云——与地上判「哪儿在下雨」是同一张场，抬头看见的乌云底下正在下雨；
//   云量按相机那一带的值、再按梯度（cover_grad，每千米变多少）往远处外推，所以锋面过来时远处的天先阴下来。
// 反射与环境光的立方图（AT_CUBEMAP_PASS）里不画云与星，只按阴沉压暗——便宜，环境光也跟着天色走。
shader_type sky;
render_mode use_debanding;

#include "res://shaders/weather.gdshaderinc"

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

float star_hash(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.x + p.y) * p.z);
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
		if ((below && dir.y > 0.01) || (!below && dir.y < -0.01)) {
			float t = (cloud_height - POSITION.y) / dir.y;
			if (t < 90000.0) {
				vec2 p = POSITION.xz + dir.xz * t;
				vec2 q = p / cell_m - drift;
				float field = wx_fbm(q);
				float cov = clamp(cloud_cover + dot(cover_grad, (p - POSITION.xz) / 1000.0), 0.0, 1.0);
				float thr = wx_threshold(cov);
				float dens = smoothstep(thr - cloud_edge, thr + cloud_edge, field);
				dens *= 0.8 + 0.4 * wx_value(q * 7.0 + vec2(TIME * 0.004, 0.0));
				dens = clamp(dens, 0.0, 1.0) * (1.0 - smoothstep(30000.0, 90000.0, t));
				float thick = smoothstep(thr, thr + 0.25, field);
				vec3 lit = mix(vec3(0.95, 0.96, 0.98), vec3(1.0, 0.72, 0.5), dusk * toward_sun);
				vec3 base = mix(vec3(0.55, 0.58, 0.63), vec3(0.32, 0.34, 0.38), thick);
				vec3 cloud = mix(base, lit, 0.55 * (1.0 - thick)) * mix(0.05, 1.0, day);
				cloud *= 1.0 - 0.45 * overcast;
				col = mix(col, cloud, dens * 0.95);
			}
		}
		// 降水的灰：远处（近地平线）看不清
		col = mix(col, overcast_color * mix(0.06, 0.8, day), rain_haze * pow(1.0 - abs(dir.y), 3.0));
	}
	// 阴沉：整片天压成灰
	col = mix(col, overcast_color * mix(0.05, 0.75, day), overcast * 0.85);
	col += vec3(0.8, 0.85, 1.0) * flash * 1.5;
	COLOR = col;
}
