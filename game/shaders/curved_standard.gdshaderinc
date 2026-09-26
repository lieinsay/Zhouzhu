// 弯曲标准材质（行星计划 P1）：代替 StandardMaterial3D 里用到的那几样——素色 / 顶点色当反照率（sRGB）、糙度、自发光，
// 顶点按真球面弯（planet_curve.gdshaderinc；没有行星数据的地图 planet_curve_on = 0，与原来的标准材质一样）。
// 村子的简模细模（SharedLooks）、码头、资源点、船、船坞、农田用它；C++ 里经 src/curved_material.* 建。
// curved_standard.gdshader（单面）与 curved_standard_two_sided.gdshader（双面，帆）各 #include 一次，只差 render_mode。

#include "res://shaders/planet_curve.gdshaderinc"

uniform vec4 albedo : source_color = vec4(1.0);
uniform float roughness : hint_range(0.0, 1.0) = 1.0;
uniform float metallic : hint_range(0.0, 1.0) = 0.0;
uniform float specular : hint_range(0.0, 1.0) = 0.5;
uniform bool vertex_color = false; // 顶点色（按 sRGB 调的）乘进反照率
uniform vec4 emission : source_color = vec4(0.0, 0.0, 0.0, 1.0);
uniform float emission_energy = 0.0;

void vertex() {
	if (vertex_color) {
		COLOR.rgb = mix(pow((COLOR.rgb + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), COLOR.rgb * (1.0 / 12.92), lessThan(COLOR.rgb, vec3(0.04045)));
	}
	vec3 flat_pos = VERTEX;
	VERTEX = planet_visual(flat_pos);
	NORMAL = planet_visual_normal(flat_pos, NORMAL);
}

void fragment() {
	vec3 base = albedo.rgb;
	if (vertex_color) {
		base *= COLOR.rgb;
	}
	ALBEDO = base;
	ROUGHNESS = roughness;
	METALLIC = metallic;
	SPECULAR = specular;
	EMISSION = emission.rgb * emission_energy;
}
