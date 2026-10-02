// Source rivers: Q = q_mean * basin index(day), d proportional to Q^0.40, w to Q^0.26.
// Keep the stage calculation in sync with src/world_river.cpp.
// CUSTOM0: bed, bank depth, negative q_bf/q_mean (legacy: dry threshold), bank half-width.
// CUSTOM1: floodplain, waterfall drop in metres, legacy speed, basin+1.
// CUSTOM2: q_mean, annual SSC mg/L, D50 mm, slope. CUSTOM3: left/right floodplain, downstream x/z.
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_always, specular_schlick_ggx;

#include "res://shaders/weather.gdshaderinc"
#include "res://shaders/planet_curve.gdshaderinc"

uniform float runoff = 1.0;
uniform float runoff_override = -1.0;
uniform sampler2D basin_index : filter_nearest, repeat_disable;
uniform int basin_count = 0;
uniform int index_days = 0;
uniform float calendar_day = 0.0;
uniform float bankfull_ratio = 3.0;
uniform float stage_exp = 0.4;
uniform float flood_max = 3.0;
uniform float floodplain_slope = 0.04;
uniform float bed_flat = 0.55;
uniform float wet_depth = 0.03;
uniform vec3 shallow_color : source_color = vec3(0.26, 0.38, 0.34);
uniform vec3 deep_color : source_color = vec3(0.065, 0.17, 0.19);
uniform vec3 foam_color : source_color = vec3(0.82, 0.85, 0.83);
uniform vec3 ice_color : source_color = vec3(0.80, 0.87, 0.92);
uniform vec3 muddy_color : source_color = vec3(0.42, 0.34, 0.23);
uniform float ice_small_depth = 1.2;

varying float v_depth;
varying float v_half_left;
varying float v_half_right;
varying float v_bank_half;
varying float v_bank_depth;
varying float v_source;
varying float v_lateral;
varying float v_along;
varying float v_fall;
varying float v_speed;
varying float v_ssc;
varying float v_slope;
varying float v_d50;
varying float v_ice;

float source_side_area(float half_w, float bank_half, float depth, float bank_depth) {
	float inner = min(half_w, bank_half);
	float outer = max(0.0, half_w - bank_half);
	float exponent = 0.4 / 0.26;
	return depth * inner - bank_depth * bank_half * pow(inner / bank_half, exponent + 1.0) / (exponent + 1.0)
		+ (depth - bank_depth) * outer - 0.5 * floodplain_slope * outer * outer;
}

void vertex() {
	float bed = CUSTOM0.x;
	float bank_depth = max(CUSTOM0.y, 1e-4);
	float half_bank = max(CUSTOM0.w, 1e-4);
	bool source = CUSTOM0.z < 0.0;
	float local_runoff = runoff_override >= 0.0 ? runoff_override : runoff;
	if (source && CUSTOM1.w > 0.0 && basin_count > 0 && index_days > 0 && runoff_override < 0.0) {
		float day = mod(calendar_day - 0.5, float(index_days));
		int first = int(floor(day));
		int basin = clamp(int(CUSTOM1.w) - 1, 0, basin_count - 1);
		local_runoff = mix(texelFetch(basin_index, ivec2(first, basin), 0).r,
			texelFetch(basin_index, ivec2((first + 1) % index_days, basin), 0).r, fract(day));
	}
	float dry = clamp(CUSTOM0.z, 0.0, 0.999);
	float flow = source ? max(0.0, local_runoff) / -CUSTOM0.z
		: min(max(0.0, local_runoff - dry) / ((1.0 - dry) * bankfull_ratio), flood_max);
	if (source && CUSTOM2.x <= 0.0) flow = 0.0;
	float depth = flow > 0.0 ? bank_depth * pow(flow, stage_exp) : 0.0;
	float flat_half = bed_flat * half_bank;
	float half_w = depth <= 0.0 ? 0.0 : depth <= bank_depth
		? (source ? half_bank * pow(flow, 0.26) : flat_half + (half_bank - flat_half) * sqrt(depth / bank_depth))
		: half_bank + (depth - bank_depth) / floodplain_slope;
	float half_left = min(half_w, half_bank + CUSTOM3.x);
	float half_right = min(half_w, half_bank + CUSTOM3.y);
	VERTEX.y = bed + depth - CUSTOM1.y;
	v_depth = depth;
	v_half_left = half_left;
	v_half_right = half_right;
	v_bank_half = half_bank;
	v_bank_depth = bank_depth;
	v_source = source ? 1.0 : 0.0;
	v_lateral = UV.y;
	v_along = UV.x;
	v_fall = clamp(CUSTOM1.y / 30.0, 0.0, 1.0);
	v_ssc = max(0.0, CUSTOM2.y);
	v_d50 = max(0.0, CUSTOM2.z) * 0.001;
	v_slope = max(0.0, CUSTOM2.w);
	float freeze = bank_depth < ice_small_depth ? 8.0 : 20.0;
	v_ice = smoothstep(freeze - 2.0, freeze + 2.0, wx_elev_row(bed, 0).g) * (1.0 - v_fall);
	float area = (half_left + half_right) * depth;
	if (source) area = max(0.0, source_side_area(half_left, half_bank, depth, bank_depth)
		+ source_side_area(half_right, half_bank, depth, bank_depth));
	v_speed = (source ? (area > 1e-12 ? CUSTOM2.x * max(0.0, local_runoff) / area : 0.0)
		: CUSTOM1.z * (0.5 + flow)) * (1.0 - v_ice);
	// Align ripples with this river, including sloping reaches and falling curtains.
	vec3 across = vec3(CUSTOM3.w, 0.0, -CUSTOM3.z);
	TANGENT = normalize(cross(across, NORMAL));
	BINORMAL = normalize(cross(NORMAL, TANGENT));
	vec3 flat_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	VERTEX += planet_visual(flat_pos) - flat_pos;
	NORMAL = planet_visual_normal(flat_pos, NORMAL);
	TANGENT = planet_visual_normal(flat_pos, TANGENT);
	BINORMAL = planet_visual_normal(flat_pos, BINORMAL);
}

void fragment() {
	float half_w = v_lateral >= 0.0 ? v_half_left : v_half_right;
	// The source already supplies discharge. Its shallow brooks are not dry rivers.
	if ((v_source > 0.5 ? v_depth <= 0.0 : v_depth < wet_depth) || abs(v_lateral) > half_w) discard;
	// Local optical depth fades at the waterline instead of painting the entire strip opaque.
	float side = abs(v_lateral);
	float u = clamp(side / v_bank_half, 0.0, 1.0);
	float legacy_u = clamp((u - bed_flat) / (1.0 - bed_flat), 0.0, 1.0);
	float bed_rise = side <= v_bank_half
		? v_bank_depth * (v_source > 0.5 ? pow(u, 0.4 / 0.26) : legacy_u * legacy_u)
		: v_bank_depth + (side - v_bank_half) * floodplain_slope;
	float local_depth = mix(max(0.0, v_depth - bed_rise), v_depth, v_fall);
	// Pattern translation is metres at the computed m/s. SSC stays annual; no invented storm multiplier.
	float along = v_along - TIME * v_speed;
	float w1 = sin(along * 1.3 + sin(v_lateral * 0.7) * 0.6);
	float w2 = sin(along * 3.1 + v_lateral * 1.7);
	float turbulence = clamp(smoothstep(0.015, 0.10, v_slope) + v_fall, 0.0, 1.0);
	float grain = clamp(v_d50 / max(0.05, local_depth), 0.0, 1.0);
	float ripple = mix(0.015, 0.08, turbulence) * (1.0 - v_ice);
	NORMAL_MAP = vec3(0.5 + ripple * w1, 0.5 + ripple * w2, 1.0);
	float deep = 1.0 - exp(-local_depth * 0.8);
	float suspended = 1.0 - exp(-v_ssc * local_depth * 0.003);
	vec3 col = mix(mix(shallow_color, deep_color, deep), muddy_color, suspended * 0.85);
	float foam = smoothstep(0.7, 1.0, w2) * turbulence * mix(0.08, 0.25, grain);
	col = mix(col, foam_color, clamp(foam + v_fall * 0.65, 0.0, 0.9));
	ALBEDO = mix(col, ice_color, v_ice);
	// Even a centimetre of water reflects at grazing angles. The old 8 cm fade
	// made whole 2--4 cm brooks almost transparent, rather than just their banks.
	float edge = smoothstep(0.0, 0.004, local_depth);
	float fresnel = 0.02 + 0.98 * pow(1.0 - clamp(dot(normalize(NORMAL), normalize(VIEW)), 0.0, 1.0), 5.0);
	float opacity = mix(mix(0.35, 0.93, max(deep, suspended)), 1.0, fresnel);
	ALPHA = mix(opacity * edge, 0.96 * edge, v_ice);
	ROUGHNESS = mix(mix(0.09, 0.25, turbulence), 0.3, v_ice);
	SPECULAR = 0.5;
}
