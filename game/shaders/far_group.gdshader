// 远处的岛群（src/far_groups.*，行星计划 P1）：粗版建的低模——顶面、崖壁、倒锥的岛底。
// 顶点在 C++ 里已按真球面弯过、按「群心相对相机」的刚体变换摆好，这里只把远处沿视线压近（planet_squeeze，同地皮）、管颜色：
//   底色是顶点色（sRGB，按地表类，与岛群地皮同一份调色）；CUSTOM0 是季相份额（草、田、灌丛、林，同地面块）；
//   UV.x 是这一点的海拔（米，群平面的 y）、UV.y 是顶面（1）还是崖壁 / 岛底（0）。
// 时令按各群自己的简化版海拔表（实例参数 season_lines，FarGroups 按全局钟的历日与该群的纬度、年均温、温差、直减率算）：
//   x 雪线（以上积雪）、y 枯黄线（以上草枯、灌丛发褐、田休耕）、z 阔叶的秋色 0..1、w 阔叶的叶量 0..1。
// 与地皮的换季同一套颜色（ground.gdshader），只是没有碎斑、没有雨后。
shader_type spatial;
global uniform float planet_cloud_top_y;
render_mode cull_back, specular_schlick_ggx, world_vertex_coords;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"

uniform vec3 straw : source_color = vec3(0.62, 0.55, 0.34);
uniform vec3 shrub_winter : source_color = vec3(0.42, 0.36, 0.26);
uniform vec3 fallow : source_color = vec3(0.46, 0.38, 0.28);
uniform vec3 autumn_leaf : source_color = vec3(0.60, 0.38, 0.17);
uniform vec3 bare_wood : source_color = vec3(0.37, 0.32, 0.27);
uniform vec3 snow_color : source_color = vec3(0.93, 0.95, 0.98);
instance uniform vec4 season_lines = vec4(100000.0, 100000.0, 0.0, 1.0);

varying vec4 v_season;
varying float v_alt;
varying float v_top;
varying float v_up;

void vertex() {
	v_season = CUSTOM0;
	v_alt = UV.x;
	v_top = UV.y;
	v_up = (transpose(mat3(MODEL_MATRIX)) * NORMAL).y; // 群的切平面里的朝上（弯过的倾斜只有零点几度）
	COLOR.rgb = wx_srgb_to_linear(COLOR.rgb);
	if (planet_curve_on > 0.5) {
		VERTEX = planet_squeeze(VERTEX);
	}
}

void fragment() {
	// 岛底的倒锥最深离云带顶还有一千米（浮高计划 F3，far_group_mesh 夹过龙骨）；万一有到云带顶（−5000）以下的一截照旧剪掉——
	// P2 起高空看到的云带是天空着色器画的，网格在它前面，不剪会从云里戳出来
	if (v_alt < planet_cloud_top_y) {
		discard;
	}
	vec3 base = COLOR.rgb;
	float herb = v_season.r;
	float field = v_season.g;
	float shrub = v_season.b;
	float forest = v_season.a;
	float dormant = smoothstep(season_lines.y - 150.0, season_lines.y + 150.0, v_alt);
	base = mix(base, straw, herb * dormant * 0.8);
	base = mix(base, shrub_winter, shrub * dormant * 0.7);
	base = mix(base, fallow, field * dormant * 0.6);
	float broadleaf = clamp(1.3 - (v_alt - 700.0) / 700.0, 0.2, 0.85);
	vec3 canopy = mix(base, autumn_leaf, season_lines.z * 0.85);
	canopy = mix(canopy, bare_wood, clamp(1.0 - season_lines.w, 0.0, 1.0));
	base = mix(base, canopy, forest * broadleaf);
	float snow = smoothstep(season_lines.x - 120.0, season_lines.x + 250.0, v_alt) * v_top * smoothstep(0.42, 0.7, v_up);
	base = mix(base, snow_color, snow);
	ALBEDO = base;
	ROUGHNESS = mix(1.0, 0.8, snow);
	SPECULAR = 0.35;
}
