// 水体光学（河、湖共用；复核稿 W0）：颜色由视线在水里实际走过的路程决定，不由河心深度或流量指数决定。
//   透射 = exp(−每米衰减 × 水中路程)（逐通道）；透得过的部分直接是屏幕上已画好的河床/湖底，透不过的部分是水本身散射的颜色。
//   水中路程 = 不透明几何（深度缓冲）到相机的距离 − 水面到相机的距离：浅滩薄、深槽厚，靠同一份几何自然分出深浅。
//   精绘日式只对透射率做单调分阶（深的不会比浅的显得浅），不另加没有物理来源的颜色信号。
//   描边只画在水面真正贴着岸/床的地方（垂直水深趋近 0），宽约两个像素，不再给所有水边常驻白边；
//   按像素量宽度，是因为缓岸上水深涨得慢，按固定水深量会在平的塘岸上铺出十几米宽的白带。
// 每米衰减与散射色是「淡水」色类的轻量预设（复核稿 X8：色类附吸收色与清度级），不是实测水质。
uniform sampler2D water_screen : hint_screen_texture, filter_linear;
uniform sampler2D water_depth : hint_depth_texture, filter_nearest;
uniform vec3 water_absorb = vec3(0.42, 0.15, 0.10);
uniform vec3 water_scatter : source_color = vec3(0.09, 0.24, 0.27);
uniform float water_bands = 5.0;
uniform float water_band_mix = 0.5;
uniform vec3 water_contact_color : source_color = vec3(0.84, 0.89, 0.86);
uniform float water_contact_m = 0.15; // 只在垂直水深不到这么深的地方描边
global uniform float water_time;

// 不透明几何在视空间的位置（Forward+ / Vulkan：NDC 的 z 就是深度缓冲原值，反向 Z）
vec3 water_bed_view(vec2 p_uv, mat4 p_inv_projection) {
	float raw = texture(water_depth, p_uv).r;
	vec4 view = p_inv_projection * vec4(p_uv * 2.0 - 1.0, raw, 1.0);
	return view.xyz / view.w;
}

// 输出：rgb = 透射率（已分阶），a = 垂直水深（米）
vec4 water_transmit(vec2 p_uv, vec3 p_vertex, mat4 p_inv_projection, mat4 p_inv_view) {
	vec3 bed = water_bed_view(p_uv, p_inv_projection);
	float path = max(length(bed) - length(p_vertex), 0.0);
	float vertical = max((p_inv_view * vec4(p_vertex, 1.0)).y - (p_inv_view * vec4(bed, 1.0)).y, 0.0);
	vec3 trans = exp(-water_absorb * path);
	float t = max((trans.r + trans.g + trans.b) / 3.0, 1e-4);
	float banded = mix(t, floor(t * water_bands + 0.5) / water_bands, water_band_mix);
	return vec4(trans * (banded / t), vertical);
}

// 把透射结果写成表面：透过来的河床走 EMISSION（屏幕颜色已含光照），水自身散射走 ALBEDO（受场景光照）
void water_surface(vec4 p_trans, vec2 p_uv, out vec3 r_albedo, out vec3 r_emission) {
	float t = (p_trans.r + p_trans.g + p_trans.b) / 3.0;
	float px = fwidth(p_trans.a);
	float contact = (1.0 - smoothstep(px, 2.5 * px + 1e-4, p_trans.a)) * (1.0 - smoothstep(0.3 * water_contact_m, water_contact_m, p_trans.a));
	vec3 bed = textureLod(water_screen, p_uv, 0.0).rgb;
	r_emission = bed * p_trans.rgb * (1.0 - contact);
	r_albedo = mix(water_scatter * (1.0 - t), water_contact_color, contact * 0.8);
}

// 切线空间的水纹（NORMAL_MAP）：x 顺着切线（河是顺水方向），y 横着；p_phase 已乘过流速。
// 几组斜交的波错开方向，免得排成一格一格的横纹；p_amp 由调用方按像素覆盖的米数压低（远处波长小于像素就不画，免得摩尔纹）。
vec3 water_ripple_map(float p_along, float p_lateral, float p_phase, float p_amp) {
	float w1 = sin(p_along * 0.9 - p_phase * 2.6 + sin(p_lateral * 1.3 + p_along * 0.21) * 1.6);
	float w2 = sin(p_along * 1.7 + p_lateral * 1.1 - p_phase * 4.1);
	float w3 = sin(p_along * 0.6 - p_lateral * 1.9 - p_phase * 1.9);
	float w4 = sin(p_along * 3.1 - p_lateral * 0.7 - p_phase * 5.3);
	return vec3(0.5 + p_amp * (0.035 * w1 + 0.025 * w2 + 0.02 * w4), 0.5 + p_amp * (0.03 * w3 + 0.02 * w2), 1.0); // 引擎只取 xy（z 自己补），不能整体归一化
}

// 波纹按像素覆盖的米数淡出：一个像素超过约 1.5 m 时波长（2–7 m）已画不清
float water_ripple_fade(float p_along, float p_lateral) {
	return clamp(1.0 - 0.65 * max(fwidth(p_along), fwidth(p_lateral)), 0.0, 1.0);
}

// 菲涅耳：反射交给引擎的高光（SPECULAR 0.5 ≈ F0 0.04），透过来的与水自身散射按 1 − F 减掉，免得掠射处又亮又透
float water_fresnel(vec3 p_normal, vec3 p_view) {
	float c = clamp(dot(p_normal, p_view), 0.0, 1.0);
	return 0.04 + 0.96 * pow(1.0 - c, 5.0);
}
