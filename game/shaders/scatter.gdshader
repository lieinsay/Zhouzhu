// 岛群地上的散布物（树、灌丛、苇、石块……，src/scatter_stream.* 按块挂 MultiMesh）与远处的林子，共用这一份。
// 顶点色（sRGB）× 实例色当反照率——与原来的 StandardMaterial3D（顶点色当反照率、按 sRGB）同色。
// 远近分级：每个实例按它离相机（eye，ScatterStream 每帧设）多远淡入淡出——实例参数 fade = (淡入起, 淡入止, 淡出起, 淡出止)，米：
//   近处的细模淡出、低模淡入，低模淡出、远处的大树冠淡入，都在同一段距离上；淡入与淡出用互补的抖动（4×4 Bayer），交接处不缺也不叠。
//   完全淡出的实例在顶点里缩成一点（不出片元，投影也没有），所以一块里远近不一的实例各自在自己的距离上换，块边上不会整片跳。
// 时令与天气（weather.gdshaderinc 的海拔表，按这一株根部的海拔取）：
//   顶点色的 alpha 标出叶子（0.5）与别的（1）；实例参数 season = (阔叶里落叶树的份额, 草本的枯黄, 挂雪, 随风摆)。
//   落叶树：入冷季秋色，叶子按叶量抖动着掉光（剩枝干），回暖春芽嫩绿；哪几棵是落叶的按根的位置哈希定。针叶冬天暗一点。
//   灌丛、苇：不绿时枯黄。朝上的面按积雪盖白。叶子与苇随风摆（顶点里按离根的高度挪），风暴时摆得大。
// 画面按真球面弯（planet_curve.gdshaderinc，行星计划 P1）：淡入淡出按没弯过的距离算，顶点最后才弯。
shader_type spatial;
render_mode cull_back, specular_schlick_ggx;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/scene_detail.gdshaderinc"

uniform vec3 eye = vec3(0.0);
uniform float roughness = 0.95;
uniform vec3 autumn_color : source_color = vec3(0.78, 0.45, 0.16);
uniform vec3 autumn_red : source_color = vec3(0.66, 0.24, 0.12);
uniform vec3 straw_color : source_color = vec3(0.64, 0.56, 0.34);
uniform vec3 spring_color : source_color = vec3(0.52, 0.70, 0.30);
uniform vec3 snow_color : source_color = vec3(0.93, 0.95, 0.98);
instance uniform vec4 fade = vec4(-2.0, -1.0, 1e9, 2e9);
instance uniform vec4 season = vec4(0.0, 0.0, 1.0, 0.0);

varying float v_in;
varying float v_out;
varying float v_foliage;
varying float v_leaf; // 这一棵此刻的叶量（落叶树；别的 1）
varying vec3 v_tint; // 季相染色后的颜色（线性）
varying float v_snow; // 积雪水当量 mm（根部海拔）
varying float v_seed;
varying vec3 detail_position;
varying vec3 detail_normal;
varying float detail_semantic;
varying float detail_distance;

float bayer4(vec2 p) {
	ivec2 q = ivec2(mod(p, 4.0));
	int i = q.x + q.y * 4;
	int m[16] = int[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
	return (float(m[i]) + 0.5) / 16.0;
}

void vertex() {
	vec3 origin = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float d = distance(origin, eye);
	detail_position=VERTEX*vec3(length(MODEL_MATRIX[0].xyz),length(MODEL_MATRIX[1].xyz),length(MODEL_MATRIX[2].xyz));
	detail_normal=NORMAL;detail_semantic=COLOR.a;detail_distance=d;
	v_in = smoothstep(fade.x, fade.y, d);
	v_out = 1.0 - smoothstep(fade.z, fade.w, d);
	if (v_in <= 0.0 || v_out <= 0.0) {
		VERTEX = vec3(0.0);
	}
	v_foliage = COLOR.a < 0.75 ? 1.0 : 0.0;
	v_seed = fract(sin(dot(wx_group_pos(origin).xz, vec2(12.9898, 78.233))) * 43758.5453); // 这一群的平面坐标：换锚前后树种不变
	vec4 e0 = wx_elev_row(origin.y, 0); // 积雪、负积温、绿、叶
	vec4 e1 = wx_elev_row(origin.y, 1); // 秋、春芽、作物、日均温
	v_snow = e0.r;
	// 顶点色 × 实例色是按 sRGB 调的
	vec3 col = wx_srgb_to_linear(COLOR.rgb);
	bool deciduous = v_seed < season.x;
	v_leaf = 1.0;
	if (v_foliage > 0.5) {
		if (deciduous) {
			vec3 fall = mix(autumn_color, autumn_red, step(0.7, fract(v_seed * 7.13)));
			col = mix(col, fall, clamp(e1.r * (0.8 + 0.4 * fract(v_seed * 3.1)), 0.0, 1.0));
			col = mix(col, spring_color, e1.g * 0.6);
			v_leaf = e0.a;
		} else if (season.y <= 0.0) {
			col *= mix(0.8, 1.0, e0.b); // 常绿的冬天暗一点
		}
		col = mix(col, straw_color, season.y * clamp(1.0 - e0.b, 0.0, 1.0) * 0.85);
		// 随风摆：离根越高挪得越多
		float lift = max(0.0, VERTEX.y);
		float gust = 0.35 + 0.65 * wx_wind.z;
		float sway = season.w * lift * 0.012 * gust * sin(TIME * (1.3 + 0.6 * wx_wind.w) + v_seed * 6.2831);
		vec3 push = vec3(wx_wind.x, 0.0, wx_wind.y) * 0.06 * lift * season.w * (0.3 + 0.7 * wx_wind.w);
		vec3 local_push = (inverse(mat3(MODEL_MATRIX)) * (push + vec3(wx_wind.x, 0.0, wx_wind.y) * sway * 0.1));
		VERTEX += local_push;
	}
	v_tint = col;
	planet_curve_model(MODEL_MATRIX, VERTEX, NORMAL);
}

void fragment() {
	float t = bayer4(FRAGCOORD.xy);
	if (t >= v_out || t < 1.0 - v_in) {
		discard;
	}
	// 落叶：叶子按叶量抖动着掉（剩枝干）
	if (v_foliage > 0.5 && v_leaf < 0.999 && fract(t + v_seed * 0.37) >= v_leaf) {
		discard;
	}
	vec3 col = v_tint*scene_detail(detail_position,detail_normal,detail_semantic,detail_distance);
	col*=mix(1.0,.88,clamp(wx_wet,0.0,1.0));
	vec3 up = (INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz;
	float cover = smoothstep(0.8, 9.0, v_snow * (0.7 + 0.6 * v_seed)) * smoothstep(0.25, 0.75, up.y) * season.z;
	col = mix(col, snow_color, cover);
	ALBEDO = col;
	ROUGHNESS = mix(roughness, 0.75, cover);
}
