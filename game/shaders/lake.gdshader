// 岛群的湖塘水面（lakes.tsv，复核稿 W1a；src/world_lakes.* 建的平面，跟河面同块挂）。
// 水位与范围来自生成器的年均水账；网格只盖到湿区外不到一步，岸线由深度测试按真实地面切。
// 颜色与河面同一套水中光程（water_optics.gdshaderinc）：浅塘透得见底，深处才显水色；只在贴岸处描边。
// 静水：水纹不顺流，只按世界坐标微动，幅度比河面小；钟用全局 water_time（世界钟暂停时不走）。
// 封冻：按水位的海拔取负积温（weather.gdshaderinc），浅塘静水按溪涧的门槛（ice_index 过 8）。
// 画面按真球面弯（planet_curve.gdshaderinc）。
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_always, specular_schlick_ggx;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/water_optics.gdshaderinc"

uniform vec3 ice_color : source_color = vec3(0.80, 0.87, 0.92);
uniform float ice_freeze = 8.0;
uniform float ripple_speed = 0.35;
uniform float ripple_amp = 0.5;

varying float v_ice;
varying vec2 v_world;

void vertex() {
	vec3 flat_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_world = UV;
	v_ice = smoothstep(ice_freeze - 2.0, ice_freeze + 2.0, wx_elev_row(flat_pos.y, 0).g);
	VERTEX += planet_visual(flat_pos) - flat_pos;
	NORMAL = planet_visual_normal(flat_pos, NORMAL);
	TANGENT = planet_visual_normal(flat_pos, TANGENT);
	BINORMAL = planet_visual_normal(flat_pos, BINORMAL);
}

void fragment() {
	float ripple = water_ripple_fade(v_world.x, v_world.y) * ripple_amp * (1.0 - v_ice);
	NORMAL_MAP = water_ripple_map(v_world.x, v_world.y, water_time * ripple_speed, ripple);
	vec4 trans = water_transmit(SCREEN_UV, VERTEX, INV_PROJECTION_MATRIX, INV_VIEW_MATRIX);
	vec3 albedo;
	vec3 emission;
	water_surface(trans, SCREEN_UV, albedo, emission);
	float keep = 1.0 - water_fresnel(NORMAL, VIEW);
	ALBEDO = mix(albedo * keep, ice_color, v_ice);
	EMISSION = emission * keep * (1.0 - v_ice);
	ALPHA = 1.0;
	ROUGHNESS = mix(0.1, 0.3, v_ice);
	SPECULAR = 0.5;
}
