// 岛群的河的水面（src/river_mesh.* 建的带子，World 挂上、每帧设 runoff）。
// rivers.tsv源包按CUSTOM2中的均值宽深、basin逐日指数和溪涧开闭算水面；下列平岸公式仅供旧包兼容。
// 水位按径流指数在顶点里算——与 src/world_river.cpp 的 river_stage 同一个公式，改一处要改两处：
//   流量 / 平岸流量 = min(max(0, r − 门槛) / ((1 − 门槛) · bankfull_ratio), flood_max)
//   水深 = 平岸深 · (流量 / 平岸流量)^stage_exp；水位 = 河床 + 水深
//   水面半宽 = 平底半宽 + 岸坡宽 · √(水深 / 平岸深)（平底 = bed_flat · 平岸半宽，岸坡是 u²）；没过平岸就按滩面横坡往外漫，最多漫到滩边
// 片元超出此刻水面半宽的丢掉：水面随水情变宽变窄，远处（地面块粗、河道凿不出来）也不会漫出河谷。
// 远处粗地面块盖住窄河时就不显示（深度测试自然挡掉），不把水面抬到岸上（复核稿 W0 第4条）。
// 颜色按水中光程（water_optics.gdshaderinc）：透过来的是河床本身，深槽自然变暗；只在水面贴岸/床处描边。
// 时令（weather.gdshaderinc 的海拔表，按河床的海拔取负积温）：溪涧（平岸深不到 ice_small_depth）过 8、河过 20 就封冻——
// 冰面不起波纹、不透。水纹用全局 water_time（Atmosphere 累计，世界钟暂停时不走）。
// 画面按真球面弯（planet_curve.gdshaderinc，行星计划 P1）：水位算好之后再弯（块只平移，模型轴就是世界轴）。
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_always, specular_schlick_ggx;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/water_optics.gdshaderinc"

uniform float runoff = 1.0;
uniform bool source_rivers = false;
uniform float basin_runoff[256];
uniform float stream_threshold = 0.3;
uniform float station_width_exp = 0.26;
uniform float station_depth_exp = 0.40;
uniform float bankfull_ratio = 3.0;
uniform float stage_exp = 0.4;
uniform float flood_max = 3.0;
uniform float floodplain_slope = 0.04;
uniform float bed_flat = 0.55;
uniform float wet_depth = 0.03;
uniform float fall_length = 150.0;
uniform vec3 foam_color : source_color = vec3(0.86, 0.89, 0.87);
uniform vec3 ice_color : source_color = vec3(0.80, 0.87, 0.92);
uniform float ice_small_depth = 1.2;

varying float v_depth;
varying float v_half;
varying float v_lateral;
varying float v_along;
varying float v_fall;
varying float v_speed;
varying float v_ice;

void vertex() {
	float bed = CUSTOM0.x;
	float bank_depth = max(CUSTOM0.y, 1e-4);
	float dry = clamp(CUSTOM0.z, 0.0, 0.999);
	float half_bank = CUSTOM0.w;
	float floodplain = CUSTOM1.x;
	float flow = min(max(0.0, runoff - dry) / ((1.0 - dry) * bankfull_ratio), flood_max);
	float depth = flow > 0.0 ? bank_depth * pow(flow, stage_exp) : 0.0;
	float flat_half = bed_flat * half_bank;
	float half_w = depth <= 0.0 ? 0.0
			: depth <= bank_depth ? flat_half + (half_bank - flat_half) * sqrt(depth / bank_depth)
								  : min(half_bank + (depth - bank_depth) / floodplain_slope, half_bank + floodplain);
	if (source_rivers) {
		float wet = CUSTOM2.w > 0.5 ? round(4.0 * smoothstep(0.75, 1.25, CUSTOM0.z / max(stream_threshold, 1e-6))) / 4.0 : 1.0;
		flow = max(0.0, basin_runoff[clamp(int(CUSTOM2.z), 0, 255)]) * wet;
		depth = CUSTOM2.y * pow(flow, station_depth_exp);
		half_w = 0.5 * CUSTOM2.x * pow(flow, station_width_exp);
	}
	VERTEX.y = bed + depth - CUSTOM1.y * fall_length;
	v_depth = depth;
	v_half = half_w;
	v_lateral = UV.y;
	v_along = UV.x;
	v_fall = CUSTOM1.y;
	float ice_index = wx_elev_row(bed, 0).g;
	float freeze = bank_depth < ice_small_depth ? 8.0 : 20.0;
	v_ice = smoothstep(freeze - 2.0, freeze + 2.0, ice_index) * (1.0 - CUSTOM1.y);
	v_speed = CUSTOM1.z * (0.5 + flow) * (1.0 - v_ice);
	vec3 flat_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	VERTEX += planet_visual(flat_pos) - flat_pos;
	NORMAL = planet_visual_normal(flat_pos, NORMAL);
	TANGENT = planet_visual_normal(flat_pos, TANGENT);
	BINORMAL = planet_visual_normal(flat_pos, BINORMAL);
}

void fragment() {
	if (v_depth <= (source_rivers ? 0.0 : wet_depth) || abs(v_lateral) > v_half) {
		discard;
	}
	float ripple = water_ripple_fade(v_along, v_lateral) * (1.0 - v_ice);
	NORMAL_MAP = water_ripple_map(v_along, v_lateral, water_time * v_speed, ripple);
	vec4 trans = water_transmit(SCREEN_UV, VERTEX, INV_PROJECTION_MATRIX, INV_VIEW_MATRIX);
	vec3 albedo;
	vec3 emission;
	water_surface(trans, SCREEN_UV, albedo, emission);
	float keep = 1.0 - water_fresnel(NORMAL, VIEW);
	// 跌水帘越往下越白、越散。平水面上不再按正弦条纹加白：陡段碎浪要等 W2 按坡降与流态判定
	float foam = clamp(v_fall * 0.7, 0.0, 1.0);
	albedo = mix(albedo * keep, foam_color, foam);
	emission *= keep * (1.0 - foam);
	ALBEDO = mix(albedo, ice_color, v_ice);
	EMISSION = emission * (1.0 - v_ice);
	ALPHA = mix(1.0 - v_fall * v_fall, 1.0, v_ice);
	ROUGHNESS = mix(0.1, 0.3, v_ice);
	SPECULAR = 0.5;
}
