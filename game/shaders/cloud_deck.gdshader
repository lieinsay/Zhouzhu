// 云带顶（行星计划 P1，D13：y = −3800 m）：PlanetView 建的一张同心环网格，跟着相机走、半径到 1,000 km，
// 顶点按真球面弯（planet_curve.gdshaderinc），所以远处的云海顺着地平线弯下去、岛群从地平线上冒出来。
// 颜色与原来的平板云海（world_2051.tscn 的 CloudSea）一样，另按平面坐标加两层很淡的大斑（几公里到十几公里），高处往下看有起伏、看得出远近。
// P2 换成行星球的云带壳。
shader_type spatial;
render_mode cull_back, specular_schlick_ggx, world_vertex_coords;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"

uniform vec4 albedo : source_color = vec4(0.82, 0.86, 0.92, 1.0);
uniform float mottle = 0.07;

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
}
