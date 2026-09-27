// 行星计划 P2：相机在天气云层（+2.8 km）以上时，往下看的云与高空的空气透视——PlanetGlobe 建的一张盖满屏幕的片，最后画（透明、不写深度）。
// 天空着色器画在一切的后面，飞到云层以上往下看的云要盖在地面、远处岛群、行星上面，所以另画一层：
//   逐像素读场景的深度（压近过的距离按 planet_curve_squeeze 还原成真的距离），视线与 +2.8 km 的球壳求交，交点比场景近就画云——
//   离锚群 100 km 以内是岛群自己的云团（同天空着色器，交点换回锚群的平面坐标取 weather.gdshaderinc 的场），以外是行星云层（planet_cloud）；
//   相机高过 20 km 起（atmo_blend）再按大气步进一段：云与地面、远处岛群蒙上一层蓝（天空着色器画的行星自己已经算过，这里只管网格）。
// 混合用普通的 alpha：结果 = 进来的光 + 透过率 × (云 · α + 场景 · (1 − α))，透过率取三色的平均当 alpha（差一点颜色，省一次读屏）。
shader_type spatial;
render_mode unshaded, depth_draw_never, depth_test_disabled, cull_disabled, blend_mix, fog_disabled, shadows_disabled;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/planet_sky.gdshaderinc"

uniform sampler2D depth_tex : hint_depth_texture, filter_nearest;
uniform vec3 sun_dir = vec3(0.0, 1.0, 0.0);
uniform float cloud_cover = 0.12;
uniform vec2 cover_grad = vec2(0.0);
uniform vec2 drift = vec2(0.0);
uniform float cell_m = 2600.0;
uniform float cloud_height = 2800.0;
uniform float cloud_edge = 0.08;
uniform float atmo_blend = 0.0;
uniform float cloud_fade = 1.0; // 刚升过云层时淡入

void vertex() {
	POSITION = vec4(VERTEX.xy * 2.0, 0.5, 1.0);
}

// 压近过的距离还原成真的（planet_squeeze 的逆）
float unsqueeze(float d2) {
	float keep = planet_curve_squeeze.x;
	float limit = planet_curve_squeeze.y;
	if (planet_curve_on < 0.5 || d2 <= keep) {
		return d2;
	}
	float span = limit - keep;
	float y = clamp((d2 - keep) / span, 0.0, 0.999999);
	return keep + span * y / (1.0 - y);
}

void fragment() {
	float depth = texture(depth_tex, SCREEN_UV).r;
	vec4 near_v = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, 1.0, 1.0);
	vec3 dir_v = normalize(near_v.xyz / near_v.w);
	vec3 dir = normalize((INV_VIEW_MATRIX * vec4(dir_v, 0.0)).xyz); // 世界 = 相机的局部系
	float t_scene = 1e12;
	if (depth > 1e-7) { // 反向 Z：0 是远平面（天空）
		vec4 v = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, depth, 1.0);
		t_scene = unsqueeze(length(v.xyz / v.w));
	}
	float r0 = planet_datum_r + planet_cam_h;
	vec3 sun_l = normalize(planet_sun * planet_axes);
	// 云层：往下看的近交点
	float alpha = 0.0;
	vec3 cloud = vec3(0.0);
	float tc = ps_hit(r0, dir.y, planet_datum_r + cloud_height);
	if (planet_cam_h > cloud_height && tc > 0.0 && tc < t_scene) {
		vec3 X = vec3(0.0, r0, 0.0) + dir * tc;
		vec3 up_l = X / length(X);
		vec3 pn = normalize(planet_axes * X);
		float cover_unused;
		float local_w = planet_group_weight(pn, 70.0, 130.0, cover_unused);
		// 岛群的云团一团 2.6 km、最细一层三百来米：一像素盖过一两百米（高空）就只剩一片灰噪点，换成行星云层（锚群附近的云量与它一样）
		local_w *= 1.0 - smoothstep(cell_m * 0.06, cell_m * 0.25, tc * planet_pixel);
		float dens = 0.0;
		float thick = 0.0;
		float storm = 0.0;
		if (local_w > 0.001) {
			// 交点换回锚群的平面坐标：flat ≈ c + δ(c) + R_c · (画面上相对相机的位移)
			vec3 flat_p = planet_curve_cam + planet_curve_dcam + planet_curve_basis * (dir * tc);
			vec2 q = flat_p.xz / cell_m - drift;
			float field = wx_fbm(q);
			float cov = clamp(cloud_cover + dot(cover_grad, (flat_p.xz - planet_curve_cam.xz) / 1000.0), 0.0, 1.0);
			float thr = wx_threshold(cov);
			dens = smoothstep(thr - cloud_edge, thr + cloud_edge, field);
			thick = smoothstep(thr, thr + 0.25, field);
		}
		if (local_w < 0.999) {
			float pt;
			float pd = planet_cloud(pn, pt, storm);
			dens = mix(pd, dens, local_w);
			thick = mix(pt, thick, local_w);
		}
		// 贴着地平线的云按像素糊一点（远处看是一片白）
		alpha = clamp(dens, 0.0, 1.0) * 0.96 * cloud_fade;
		cloud = planet_cloud_color(pn, up_l, dir, sun_l, thick, storm);
	}
	bool sky_pixel = t_scene > 1e11;
	if (sky_pixel && alpha <= 0.002) {
		discard; // 天空着色器画的行星与天已经带着空气透视
	}
	// 空气透视（相机高过 20 km）：到云（在前面的话）或场景为止
	vec3 L = vec3(0.0);
	vec3 T = vec3(1.0);
	if (atmo_blend > 0.001) {
		float stop = alpha > 0.0 ? min(tc, t_scene) : t_scene;
		float t0;
		float t1;
		float r0k = r0 / 1000.0;
		if (atmo_segment(r0k, dir, stop / 1000.0, t0, t1)) {
			float jitter = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));
			atmo_march(r0k, dir, t0, t1, sun_l, 12, jitter, L, T);
		}
		L *= planet_sky_power * atmo_blend;
		T = mix(vec3(1.0), T, atmo_blend);
	}
	// 夜里的灯：高空往下看，锚群与远处岛群的网格上没有灯，按行星贴图的人口给它补上（与贴图岛群一样撒）
	vec3 lights = vec3(0.0);
	if (!sky_pixel && planet_cam_h > 3000.0) {
		vec3 Xs = vec3(0.0, r0, 0.0) + dir * t_scene;
		vec3 up_s = Xs / length(Xs);
		lights = planet_night_lights(normalize(planet_axes * Xs), up_s, sun_l, t_scene * planet_pixel) * smoothstep(30000.0, 120000.0, planet_cam_h); // 近看贴图的人口对不上真的村子：高过一百公里才补
	}
	if (sky_pixel) {
		// 天空的像素（背后是天空着色器画的行星）：云带着它自己那一段空气盖上去
		ALBEDO = L + T * cloud;
		ALPHA = alpha;
	} else {
		// 网格的像素：结果 = L + T·(α·云 + (1 − α)·场景)，alpha 取 A = 1 − T̄(1 − α)
		float t_avg = dot(T, vec3(0.3333));
		float A = clamp(1.0 - t_avg * (1.0 - alpha), 0.0, 1.0);
		vec3 add = L + T * (cloud * alpha + lights * (1.0 - alpha));
		if (dot(lights, vec3(1.0)) > 1e-4) {
			A = max(A, 0.05); // 灯要加上去：alpha 至少留一点（底下的网格夜里本来就暗，压暗 5% 看不出）
		}
		if (A < 0.002) {
			discard;
		}
		ALBEDO = add / A;
		ALPHA = A;
	}
}
