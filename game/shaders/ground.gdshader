// 岛群的地皮（src/terrain_tile.* 建的地面块，World 挂上）。
// 底色是顶点色（sRGB，按地表份额混好的「盛夏」样子），再按时令换季、按天气盖雪、雨后变深：
//   CUSTOM0（RGBA8）是这一点的季相份额：草、田、灌丛、林（露岩、河床、崖壁、岛底是 0）；
//   时令从海拔表（weather.gdshaderinc 的 wx_elev，Atmosphere 每帧设）按这一点的海拔取：绿度、叶、秋色、春芽、作物季相、积雪。
//   草：不绿时枯黄；灌丛：冬天发褐；田：休耕的土色 → 青苗 → 黄熟；林：高处多针叶（不换季），阔叶的秋色、光秃。
//   雪：积雪水当量按碎斑进退（雪线不是一条等高线），陡坡挂不住、背阴坡（南半球朝南）多留一点。
//   雨后：底色压暗、糙度降下来（湿地皮反光）。
// 画面按真球面弯（planet_curve.gdshaderinc，行星计划 P1）：时令、积雪按没弯过的位置（海拔、坡度）取，只有画出来的位置与法线弯。
shader_type spatial;
render_mode cull_back, specular_schlick_ggx, world_vertex_coords;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"

uniform vec3 straw : source_color = vec3(0.62, 0.55, 0.34);
uniform vec3 shrub_winter : source_color = vec3(0.42, 0.36, 0.26);
uniform vec3 fallow : source_color = vec3(0.46, 0.38, 0.28);
uniform vec3 sprout : source_color = vec3(0.34, 0.50, 0.21);
uniform vec3 ripe : source_color = vec3(0.74, 0.63, 0.31);
uniform vec3 autumn_leaf : source_color = vec3(0.60, 0.38, 0.17);
uniform vec3 bare_wood : source_color = vec3(0.37, 0.32, 0.27);
uniform vec3 spring_leaf : source_color = vec3(0.40, 0.58, 0.24);
uniform vec3 snow_color : source_color = vec3(0.93, 0.95, 0.98);

varying vec3 v_world;
varying vec3 v_up; // 世界法线
varying vec4 v_season;

void vertex() {
	// world_vertex_coords：VERTEX、NORMAL 已是世界（平面）坐标
	v_world = VERTEX;
	v_up = normalize(NORMAL);
	v_season = CUSTOM0;
	COLOR.rgb = wx_srgb_to_linear(COLOR.rgb);
	VERTEX = planet_visual(v_world);
	NORMAL = planet_visual_normal(v_world, NORMAL);
}

void fragment() {
	vec3 base = COLOR.rgb;
	vec4 e0 = wx_elev_row(v_world.y, 0); // 积雪 mm、负积温、绿、叶
	vec4 e1 = wx_elev_row(v_world.y, 1); // 秋色、春芽、作物、日均温
	float herb = v_season.r, field = v_season.g, shrub = v_season.b, forest = v_season.a;
	float blotch = wx_blotch(v_world.xz / 23.0);
	float dormant = clamp(1.0 - e0.b, 0.0, 1.0);

	// 草：不绿时枯黄（碎斑：有的地方先黄）
	base = mix(base, straw, herb * clamp(dormant * (0.75 + 0.5 * blotch), 0.0, 0.9));
	// 春芽：一片嫩绿
	base = mix(base, spring_leaf, (herb + forest * 0.5) * e1.g * 0.35);
	// 灌丛：冬天发褐
	base = mix(base, shrub_winter, shrub * dormant * 0.7);
	// 田：作物季相（0 休耕 → 青苗 → 黄熟 → 收了又是土）
	float c = e1.b;
	vec3 crop = c < 0.06 ? fallow : (c < 0.6 ? mix(fallow, sprout, smoothstep(0.06, 0.3, c)) : mix(sprout, ripe, smoothstep(0.6, 0.95, c)));
	base = mix(base, crop, field * 0.75);
	// 林：阔叶的份额随海拔减（千米以上针叶渐多），阔叶秋色、落光了露出枝干的褐灰
	float broadleaf = clamp(1.3 - (v_world.y - 700.0) / 700.0, 0.2, 0.85) * (0.8 + 0.4 * blotch);
	vec3 canopy = mix(base, autumn_leaf, e1.r * 0.85);
	canopy = mix(canopy, bare_wood, clamp(1.0 - e0.a, 0.0, 1.0));
	base = mix(base, canopy, forest * broadleaf);

	// 雪：积雪按碎斑进退、陡坡挂不住、背阴坡多留；河床、崖壁（法线水平）自然没有
	float snow_mm = e0.r * (0.55 + 0.9 * wx_blotch(v_world.xz / 61.0 + vec2(17.0, 5.0))) * (1.0 + 0.5 * v_up.z * wx_misc.x);
	float cover = smoothstep(0.8, 9.0, snow_mm) * smoothstep(0.42, 0.7, v_up.y); // 五十度上下还挂得住
	base = mix(base, snow_color, cover);

	// 雨后：压暗、反光
	float wet = wx_wet * (1.0 - cover);
	base *= 1.0 - 0.32 * wet;
	ALBEDO = base;
	ROUGHNESS = mix(mix(1.0, 0.5, wet), 0.75, cover);
	SPECULAR = mix(0.5, 0.6, wet);
}
