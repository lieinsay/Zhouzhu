// 岛群地上的散布物（树、灌丛、苇、石块……，src/scatter_stream.* 按块挂 MultiMesh）与远处的林子，共用这一份。
// 顶点色（sRGB）× 实例色当反照率——与原来的 StandardMaterial3D（顶点色当反照率、按 sRGB）同色。
// 远近分级：每个实例按它离相机（eye，ScatterStream 每帧设）多远淡入淡出——实例参数 fade = (淡入起, 淡入止, 淡出起, 淡出止)，米：
//   近处的细模淡出、低模淡入，低模淡出、远处的大树冠淡入，都在同一段距离上；淡入与淡出用互补的抖动（4×4 Bayer），交接处不缺也不叠。
//   完全淡出的实例在顶点里缩成一点（不出片元，投影也没有），所以一块里远近不一的实例各自在自己的距离上换，块边上不会整片跳。
shader_type spatial;
render_mode cull_back, specular_schlick_ggx;

uniform vec3 eye = vec3(0.0);
uniform float roughness = 0.95;
instance uniform vec4 fade = vec4(-2.0, -1.0, 1e9, 2e9);

varying float v_in;
varying float v_out;

float bayer4(vec2 p) {
	ivec2 q = ivec2(mod(p, 4.0));
	int i = q.x + q.y * 4;
	int m[16] = int[](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
	return (float(m[i]) + 0.5) / 16.0;
}

void vertex() {
	vec3 origin = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float d = distance(origin, eye);
	v_in = smoothstep(fade.x, fade.y, d);
	v_out = 1.0 - smoothstep(fade.z, fade.w, d);
	if (v_in <= 0.0 || v_out <= 0.0) {
		VERTEX = vec3(0.0);
	}
	// 顶点色 × 实例色是按 sRGB 调的
	COLOR.rgb = mix(pow((COLOR.rgb + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), COLOR.rgb * (1.0 / 12.92), lessThan(COLOR.rgb, vec3(0.04045)));
}

void fragment() {
	float t = bayer4(FRAGCOORD.xy);
	if (t >= v_out || t < 1.0 - v_in) {
		discard;
	}
	ALBEDO = COLOR.rgb;
	ROUGHNESS = roughness;
}
