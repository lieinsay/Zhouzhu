// 天气的共用部分：地皮、散布、河、天空都 #include 它。
//
// 1. 云团噪声——与 src/weather_noise.cpp 同一个式子（哈希常数、五次平滑、四层偏移、按 1024 格循环、覆盖率 → 门槛的分位表），改一处要改两处。
//    天上的云用它，C++ 按它判地上哪儿在下雨，所以抬头看见的乌云底下正在下雨。
// 2. 全局参数（project.godot 的 [shader_globals]，Atmosphere 每帧设）：
//    wx_elev        海拔表：宽 = 分箱数，两行，每箱 RGBA（float）——第 0 行 积雪水当量 mm、负积温、绿度、叶；第 1 行 秋色、春芽、作物季相、日均温 °C
//    wx_elev_range  分箱覆盖的海拔 (最低, 最高)
//    wx_wet         地湿 0..1（相机那一带）
//    wx_wind        风 (往 x 吹 m/s, 往 z 吹 m/s, 阵风 0..1, 风暴 0..1)
//    wx_misc        (极向：南半球 +1（背阴坡朝 +z）北半球 −1, 夜色 0..1, 0, 0)

global uniform sampler2D wx_elev;
global uniform vec2 wx_elev_range;
global uniform float wx_wet;
global uniform vec4 wx_wind;
global uniform vec4 wx_misc;

uint wx_hash(ivec2 p) {
	uvec2 q = uvec2(p & ivec2(1023));
	uint h = (q.x * 0x27d4eb2du) ^ (q.y * 0x165667b1u + 0x9e3779b9u);
	h ^= h >> 15u;
	h *= 0x85ebca6bu;
	h ^= h >> 13u;
	h *= 0xc2b2ae35u;
	h ^= h >> 16u;
	return h;
}

float wx_rand(ivec2 p) {
	return float(wx_hash(p) & 0xFFFFFFu) / 16777215.0;
}

float wx_value(vec2 p) {
	vec2 i = floor(p);
	vec2 f = p - i;
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	ivec2 c = ivec2(i);
	float a = wx_rand(c);
	float b = wx_rand(c + ivec2(1, 0));
	float cc = wx_rand(c + ivec2(0, 1));
	float d = wx_rand(c + ivec2(1, 1));
	return mix(mix(a, b, u.x), mix(cc, d, u.x), u.y);
}

float wx_fbm(vec2 p) {
	float sum = 0.0;
	float amp = 0.5;
	float norm = 0.0;
	for (int k = 0; k < 4; k++) {
		sum += amp * wx_value(p + vec2(37.0, 17.0) * float(k));
		norm += amp;
		p *= 2.0;
		amp *= 0.5;
	}
	return sum / norm;
}

// 覆盖率 → 门槛（同 weather_noise.cpp 的 QUANTILES）
float wx_threshold(float coverage) {
	const float Q[17] = float[](1.01, 0.7148, 0.6666, 0.6312, 0.6011, 0.5739, 0.5484, 0.5239, 0.4998, 0.4758, 0.4513, 0.4258, 0.3987, 0.3687, 0.3332, 0.2851, -0.01);
	float c = clamp(coverage, 0.0, 1.0) * 16.0;
	int k = min(15, int(c));
	return mix(Q[k], Q[k + 1], c - float(k));
}

// 海拔表的一行，按海拔在两箱之间插值；表还没设（1 × 1 的默认图）时给「暖季、没雪」
vec4 wx_elev_row(float h, int row) {
	ivec2 size = textureSize(wx_elev, 0);
	if (size.x < 2 || size.y < 2) {
		return row == 0 ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.5, 18.0);
	}
	float span = max(1.0, wx_elev_range.y - wx_elev_range.x);
	float u = clamp((h - wx_elev_range.x) / span, 0.0, 1.0) * float(size.x - 1);
	int i = min(size.x - 2, int(floor(u)));
	return mix(texelFetch(wx_elev, ivec2(i, row), 0), texelFetch(wx_elev, ivec2(i + 1, row), 0), u - float(i));
}

vec3 wx_srgb_to_linear(vec3 c) {
	return mix(pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), c * (1.0 / 12.92), lessThan(c, vec3(0.04045)));
}

// 便宜的二维值噪声（地皮、散布上的碎斑用；不必与 C++ 对得上）
float wx_blotch(vec2 p) {
	return wx_value(p);
}
