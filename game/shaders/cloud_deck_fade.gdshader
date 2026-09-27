// 云带顶（cloud_deck.gdshader）淡出时用的一份（行星计划 P2）：相机高过 30 km 起 PlanetGlobe 把 P1 的云带顶网格换成这一份、按 fade 渐渐透明，
// 露出天空着色器画的整颗行星（云带壳、缝里的海、贴图岛群）；60 km 以上整张藏起来。颜色与 cloud_deck.gdshader 一样。
shader_type spatial;
render_mode cull_back, specular_schlick_ggx, world_vertex_coords, blend_mix, depth_draw_never;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"

uniform vec4 albedo : source_color = vec4(0.82, 0.86, 0.92, 1.0);
uniform float mottle = 0.07;
uniform float fade = 1.0;

varying vec3 v_flat;

void vertex() {
	v_flat = VERTEX;
	VERTEX = planet_visual(v_flat);
	NORMAL = planet_visual_normal(v_flat, NORMAL);
}

void fragment() {
	float a = wx_value(v_flat.xz / 14000.0 + vec2(311.0, 97.0));
	float b = wx_value(v_flat.xz / 3700.0 + vec2(53.0, 211.0));
	float shade = 1.0 + mottle * ((a - 0.5) * 1.2 + (b - 0.5) * 0.6);
	ALBEDO = albedo.rgb * shade;
	ROUGHNESS = 1.0;
	ALPHA = fade;
}
