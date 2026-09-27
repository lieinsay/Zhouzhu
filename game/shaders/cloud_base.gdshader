// 云底（行星计划 P7，D13：y = −6200 m）：SeaSurface 建的一张同心环网格（朝下），跟着相机走，顶点走 planet_curve 弯曲。
// 从下往上看是一层压着的灰：几公里的大斑、几百米的团（planet_base_lumps，在行星系里取，换锚不挪），下雨的地方暗一些；
// 云底是光源（透下来的光，天与光按云带的剖面给头顶多亮），不受照：头顶亮、往地平线暗到三分之一（CIE 阴天）；晨昏朝太阳那一侧染暖（日面看不见，只有云底染色）；闪电一亮照亮一片。
// 天空着色器里从下往上看的云底（planet_under_sky）用同一份光、同一个起伏，远处蒙霾接得上。
shader_type spatial;
render_mode world_vertex_coords, cull_back, unshaded;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/planet_sky.gdshaderinc"
#include "res://shaders/planet_sea.gdshaderinc"

uniform vec3 sun_visual = vec3(0.0, 1.0, 0.0); // 太阳（画面的世界轴）
uniform float dusk = 0.0; // 晨昏 0..1
uniform float rain = 0.0; // 此地的降水 0..1
uniform float flash = 0.0; // 闪电

varying vec3 v_flat;
varying vec3 v_rel;

void vertex() {
	v_flat = VERTEX;
	v_rel = planet_curved(v_flat) - planet_curve_cam;
	VERTEX = planet_visual(v_flat);
	NORMAL = planet_visual_normal(v_flat, NORMAL);
}

void fragment() {
	vec3 pn = normalize(planet_axes * (vec3(0.0, planet_datum_r + planet_cam_h, 0.0) + v_rel));
	vec3 dir = normalize(v_rel);
	float lumps = planet_base_lumps(pn);
	float wet = rain * smoothstep(0.45, 0.8, pn_value3(pn * (planet_datum_r / 9000.0) + vec3(7.0, 3.0, 19.0)));
	vec3 col = under_base_color * lumps * planet_overcast_gradient(dir.y) * (1.0 - 0.35 * wet);
	// 晨昏：朝太阳那一侧（水平方向）染暖，离太阳方位越近越浓
	vec2 sun_h = normalize(sun_visual.xz + vec2(1e-5, 0.0));
	float toward = pow(max(0.0, dot(normalize(dir.xz + vec2(1e-5, 0.0)), sun_h)) * 0.5 + 0.5, 4.0);
	col = mix(col, col * vec3(1.9, 1.05, 0.62), dusk * toward * 0.8);
	col += vec3(0.7, 0.75, 0.9) * flash * 0.25;
	ALBEDO = col;
}
