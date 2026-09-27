// 海面（行星计划 P7，D13：y = −5600 m）：SeaSurface 建的一张同心环网格，跟着相机走、半径 300 km，顶点走 planet_curve 弯曲。
// 浪（src/sea_state.* 的 sea_waves，按天与光在相机处取的风——行星天气场）：
//   近处四道 Gerstner 顶点波（主浪按风向、另三道错开几十度，相位由 C++ 按帧累加：风变了浪长跟着变也不乱跳），几百米外渐平；
//   两层可平铺的法线贴图（src/sea_state.* 的 bake_wave_normals）顺风错开平移，远处按 mipmap 自己平下去；
//   白沫：浪尖（Gerstner 折起来的地方）与风起的白头（法线贴图的浪尖通道），风暴里满是；白礁（planet_reef，纯氛围）上是惨白的一片、边上一圈碎浪。
// 光照照引擎的：云底下只有环境光（阴天），亮处多是反光——天空着色器从下往上看的那一段（云底、霾）经辐照图反过来，远处擦着看时按菲涅耳亮。
shader_type spatial;
render_mode world_vertex_coords, cull_back, specular_schlick_ggx;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/planet_sky.gdshaderinc"
#include "res://shaders/planet_sea.gdshaderinc"

uniform vec4 sea_wave = vec4(0.25, 24.0, 0.0, 0.0); // 有效波高（米）、主浪波长（米）、白浪、碎浪
uniform vec2 sea_wind = vec2(1.0, 0.0); // 风往哪吹（锚群平面，单位）
uniform vec4 sea_phase = vec4(0.0); // 四道 Gerstner 波此刻的相位（弧度，C++ 按帧累加 ω·dt）
uniform vec4 sea_scroll = vec4(0.0); // 两层法线贴图的平移（贴图单位，已取模）
uniform sampler2D sea_normals : filter_linear_mipmap, repeat_enable;
uniform vec3 sea_albedo = vec3(0.010, 0.026, 0.034);
uniform vec3 reef_albedo = vec3(0.70, 0.72, 0.66);
uniform vec3 foam_albedo = vec3(0.78, 0.80, 0.82);
uniform float wave_near = 350.0; // Gerstner 顶点波：这么远以内全有
uniform float wave_far = 1400.0; // 到这么远渐平
uniform float detail_far = 9000.0; // 法线贴图与白沫：到这么远渐平

varying vec3 v_flat; // 锚群平面坐标（浪推过之后）
varying vec3 v_rel; // 弯过（没压近）的画面坐标相对相机：换到行星系取白礁
varying vec3 v_normal; // Gerstner 的法线（平面轴）
varying float v_fold; // 浪尖折起来多少（Gerstner 的雅可比：1 平、越小越尖）
varying float v_dist;

const float SEA_G = 9.81;
const vec4 WAVE_ANGLE = vec4(0.0, 0.52, -0.61, 1.05); // 相对风向（弧度）
const vec4 WAVE_LENGTH = vec4(1.0, 0.63, 0.41, 0.27); // 相对主浪
const vec4 WAVE_AMP = vec4(0.52, 0.27, 0.15, 0.09); // 有效波高的份
const float WAVE_STEEP = 0.62;

vec2 sea_rot(vec2 v, float a) {
	float c = cos(a);
	float s = sin(a);
	return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

void vertex() {
	vec3 flat_p = VERTEX;
	v_dist = length(flat_p.xz - planet_curve_cam.xz);
	float near = 1.0 - smoothstep(wave_near, wave_far, v_dist);
	vec2 wd = normalize(sea_wind + vec2(1e-5, 0.0));
	vec3 disp = vec3(0.0);
	vec3 n = vec3(0.0, 1.0, 0.0);
	float fold = 1.0;
	if (near > 0.001) {
		for (int i = 0; i < 4; i++) {
			vec2 d = sea_rot(wd, WAVE_ANGLE[i]);
			float lam = sea_wave.y * WAVE_LENGTH[i];
			float k = 6.2831853 / lam;
			float a = 0.5 * sea_wave.x * WAVE_AMP[i] * near;
			float q = WAVE_STEEP / max(1e-4, k * a * 4.0);
			q = min(q, 1.0);
			float ph = k * dot(d, flat_p.xz) - sea_phase[i];
			float c = cos(ph);
			float s = sin(ph);
			disp.xz += q * a * d * c;
			disp.y += a * s;
			n.x -= d.x * k * a * c;
			n.z -= d.y * k * a * c;
			n.y -= q * k * a * s;
			fold -= q * k * a * s;
		}
	}
	flat_p += disp;
	v_flat = flat_p;
	v_normal = normalize(n);
	v_fold = fold;
	v_rel = planet_curved(flat_p) - planet_curve_cam;
	VERTEX = planet_visual(flat_p);
	NORMAL = planet_visual_normal(flat_p, v_normal);
}

void fragment() {
	vec2 p = v_flat.xz;
	vec2 wd = normalize(sea_wind + vec2(1e-5, 0.0));
	float detail = 1.0 - smoothstep(detail_far * 0.25, detail_far, v_dist);
	// 两层法线贴图：一层顺风（38 m 一铺），一层斜着（13 m 一铺）；斜率按浪的大小给
	vec2 uv1 = sea_rot(p, -atan(wd.y, wd.x)) / 38.0 + sea_scroll.xy;
	vec2 uv2 = sea_rot(p, 0.9 - atan(wd.y, wd.x)) / 13.0 + sea_scroll.zw;
	vec4 t1 = texture(sea_normals, uv1);
	vec4 t2 = texture(sea_normals, uv2);
	float strength = (0.16 + 0.22 * sea_wave.z + 0.25 * sea_wave.w) * detail;
	vec2 g1 = sea_rot((t1.rg - 0.5) * 2.0, atan(wd.y, wd.x));
	vec2 g2 = sea_rot((t2.rg - 0.5) * 2.0, atan(wd.y, wd.x) - 0.9);
	vec2 g = (g1 * 0.65 + g2 * 0.45) * strength;
	vec3 n = normalize(v_normal + vec3(-g.x, 0.0, -g.y));
	// 白礁（纯氛围）：行星系里取，远了按像素平均
	vec3 pn = normalize(planet_axes * (vec3(0.0, planet_datum_r + planet_cam_h, 0.0) + v_rel));
	float slant = max(0.05, abs(normalize(v_rel).y));
	float reef = planet_reef(pn, v_dist * planet_pixel / slant);
	float reef_edge = reef * (1.0 - reef) * 4.0; // 礁边一圈碎浪
	// 白沫：Gerstner 浪头折起来的地方（风起了才有）、风起的白头（贴图的浪头通道，两层叠着才白），风暴里顺风拉成一道道；礁边一圈碎浪
	float crest = smoothstep(0.64, 0.42, v_fold) * sea_wave.z;
	float streak = texture(sea_normals, vec2(uv1.x * 0.22, uv1.y * 1.7) + vec2(0.37, 0.61)).a;
	float heads = mix(t1.a * t2.a, streak * (0.5 + 0.5 * t1.a), 0.6 * sea_wave.w);
	float caps = smoothstep(0.62 - 0.4 * sea_wave.z - 0.15 * sea_wave.w, 0.9 - 0.3 * sea_wave.w, heads) * (0.55 * sea_wave.z + 0.45 * sea_wave.w);
	float foam = clamp(crest + caps + reef_edge * 0.5, 0.0, 1.0) * detail;
	vec3 albedo = mix(sea_albedo, reef_albedo, reef);
	albedo = mix(albedo, foam_albedo, foam);
	ALBEDO = albedo;
	ROUGHNESS = mix(0.05 + 0.12 * sea_wave.w, 0.85, max(reef * 0.85, foam));
	SPECULAR = mix(0.5, 0.25, reef);
	METALLIC = 0.0;
	NORMAL = normalize((VIEW_MATRIX * vec4(planet_visual_normal(v_flat, n), 0.0)).xyz);
}
