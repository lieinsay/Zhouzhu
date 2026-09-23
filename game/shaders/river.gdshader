// 岛群的河的水面（src/river_mesh.* 建的带子，World 挂上、每帧设 runoff）。
// 水位按径流指数在顶点里算——与 src/world_river.cpp 的 river_stage 同一个公式，改一处要改两处：
//   流量 / 平岸流量 = min(max(0, r − 门槛) / ((1 − 门槛) · bankfull_ratio), flood_max)
//   水深 = 平岸深 · (流量 / 平岸流量)^stage_exp；水位 = 河床 + 水深
//   水面半宽 = 平岸半宽 · √(水深 / 平岸深)（抛物线河床）；没过平岸就按滩面横坡往外漫，最多漫到滩边
// 片元超出此刻水面半宽的丢掉：水面随水情变宽变窄，远处（地面块粗、河道凿不出来）也不会漫出河谷。
// 远处的水面抬高一点（far_lift），免得被粗地面块盖住；近处不抬。
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_always, specular_schlick_ggx;

uniform float runoff = 1.0;
uniform float bankfull_ratio = 3.0;
uniform float stage_exp = 0.4;
uniform float flood_max = 3.0;
uniform float floodplain_slope = 0.04;
uniform float wet_depth = 0.03;
uniform float fall_length = 45.0;
uniform float far_lift_start = 150.0;
uniform float far_lift = 0.0025;
uniform vec3 shallow_color : source_color = vec3(0.34, 0.50, 0.47);
uniform vec3 deep_color : source_color = vec3(0.10, 0.23, 0.29);
uniform vec3 foam_color : source_color = vec3(0.86, 0.89, 0.87);

varying float v_depth;
varying float v_half;
varying float v_lateral;
varying float v_along;
varying float v_fall;
varying float v_speed;

void vertex() {
	float bed = CUSTOM0.x;
	float bank_depth = max(CUSTOM0.y, 1e-4);
	float dry = clamp(CUSTOM0.z, 0.0, 0.999);
	float half_bank = CUSTOM0.w;
	float floodplain = CUSTOM1.x;
	float flow = min(max(0.0, runoff - dry) / ((1.0 - dry) * bankfull_ratio), flood_max);
	float depth = flow > 0.0 ? bank_depth * pow(flow, stage_exp) : 0.0;
	float half_w = depth <= bank_depth ? half_bank * sqrt(depth / bank_depth)
									   : min(half_bank + (depth - bank_depth) / floodplain_slope, half_bank + floodplain);
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	float lift = max(0.0, length(world.xz - CAMERA_POSITION_WORLD.xz) - far_lift_start) * far_lift;
	VERTEX.y = bed + depth + lift - CUSTOM1.y * fall_length;
	v_depth = depth;
	v_half = half_w;
	v_lateral = UV.y;
	v_along = UV.x;
	v_fall = CUSTOM1.y;
	v_speed = CUSTOM1.z * (0.5 + flow);
}

void fragment() {
	if (v_depth < wet_depth || abs(v_lateral) > v_half) {
		discard;
	}
	float t = TIME * v_speed;
	float w1 = sin(v_along * 0.9 - t * 2.6 + sin(v_lateral * 1.3 + v_along * 0.21) * 1.6);
	float w2 = sin(v_along * 2.3 + v_lateral * 1.7 - t * 4.1);
	float w3 = sin(v_along * 0.37 - v_lateral * 0.8 - t * 1.3);
	NORMAL_MAP = normalize(vec3(0.5 + 0.07 * w1 + 0.04 * w3, 0.5 + 0.06 * w2, 1.0));
	float deep = clamp(v_depth / 1.2, 0.0, 1.0);
	float edge = 1.0 - smoothstep(0.0, 0.5 + 0.3 * v_half, v_half - abs(v_lateral));
	vec3 col = mix(shallow_color, deep_color, deep);
	col = mix(col, foam_color, clamp(edge * 0.35 + v_fall * 0.7 + max(0.0, w2) * 0.06 * v_speed, 0.0, 1.0));
	ALBEDO = col;
	ALPHA = mix(0.6, 0.9, deep) * (1.0 - 0.5 * edge) * (1.0 - v_fall * v_fall);
	ROUGHNESS = 0.06;
	SPECULAR = 0.5;
}
