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
uniform bool surface_enabled = false;
uniform sampler2D surface_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 surface_mean = vec3(1.0);
uniform float surface_tile_metres = 1.0;
uniform bool surface_uv_metres = false;
uniform bool surface_end_enabled = false;
uniform sampler2D surface_end_tex : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform vec3 surface_end_mean = vec3(1.0);
uniform float surface_end_tile_metres = .25;
global uniform float wx_wet;
varying vec3 surface_pos;
varying vec3 surface_n;

void vertex() {
	if (vertex_color) {
		COLOR.rgb = mix(pow((COLOR.rgb + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), COLOR.rgb * (1.0 / 12.92), lessThan(COLOR.rgb, vec3(0.04045)));
	}
	vec3 flat_pos = VERTEX;
	// 在曲率变化之前取得物体局部位置，避免移动对象纹理滑动；米制模型保持实际尺度。
	if (surface_enabled) {
		surface_pos = (inverse(MODEL_MATRIX) * vec4(flat_pos, 1.0)).xyz;
		surface_n = normalize(transpose(mat3(MODEL_MATRIX)) * NORMAL);
	}
	VERTEX = planet_visual(flat_pos);
	NORMAL = planet_visual_normal(flat_pos, NORMAL);
}

void fragment() {
	vec3 base = albedo.rgb;
	if (vertex_color) {
		base *= COLOR.rgb;
	}
	float wet = 0.0;
	if (surface_enabled) {
		vec3 weights = pow(abs(normalize(surface_n)), vec3(4.0));
		weights /= max(dot(weights, vec3(1.0)), 0.0001);
		vec3 p = surface_pos / max(surface_tile_metres, 0.001);
		vec3 detail = texture(surface_tex, p.zy).rgb * weights.x + texture(surface_tex, p.xz).rgb * weights.y + texture(surface_tex, p.xy).rgb * weights.z;
		vec3 ratio=detail/max(surface_mean,vec3(.001));
		if(surface_uv_metres && (UV2.y>8.0 || UV2.y<0.0)) {
			ratio=texture(surface_tex,UV/max(surface_tile_metres,.001)).rgb/max(surface_mean,vec3(.001));
			if(surface_end_enabled && UV2.y<0.0)ratio=texture(surface_end_tex,clamp(UV/max(-UV2,vec2(.001)),vec2(.01),vec2(.99))).rgb/max(surface_end_mean,vec3(.001));
		}
		base *= clamp(ratio, vec3(0.6), vec3(1.4));
		wet = clamp(wx_wet, 0.0, 1.0);
		base *= mix(1.0, 0.88, wet);
	}
	ALBEDO = base;
	ROUGHNESS = roughness * mix(1.0, 0.72, wet);
	METALLIC = metallic;
	SPECULAR = specular;
	EMISSION = emission.rgb * emission_energy;
}
