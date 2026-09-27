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
// 3. 每群一份（行星计划 P3，材质参数；GroupWorld 设在这一群的地皮、水面、散布材质上）：一个场景里有好几个岛群时，各群按自己的天气模型换季、盖雪；
//    没设的（天空、云带顶、甲乙两岛、平板岛）照旧用上面的全局参数。
//    wx_group_elev  这一群的海拔表（格式同 wx_elev）
//    wx_group       (分箱最低, 最高, 极向, 1 = 用这一群的)
//    wx_group_inv   世界坐标 → 这一群的平面坐标（邻群摆了刚体变换，锚群是单位阵）：碎斑、树种这些按位置的哈希按它取，换锚前后不变

global uniform sampler2D wx_elev;
global uniform vec2 wx_elev_range;
global uniform float wx_wet;
global uniform vec4 wx_wind;
global uniform vec4 wx_misc;
// 4. 近处的云团（行星计划 P5，Atmosphere 按相机处的行星天气场设；与 src/planet_weather.* 的云团场同式）：
//    相机附近管得着的几群（≤ 4），各自平面上一张随它自己的风漂的 fbm，按「管辖差」的权混、方差不变——
//    群自己的陆地上就是它自己的云团（与地上判雨的 WeatherModel 同一张），群与群之间渐变；没有公共平面，换锚不跳、离多远都不歪。
//    wx_cloud_on = 0（没有行星数据的地图）：照旧只有这一张图自己的一张（天空给的 drift）。
//    每槽一个 mat4（列）：[0] = (a, b, c, d) / 一格——锚群平面 p 到那一群平面 q 的线性部分（q.x = a·p.x + b·p.z，q.z = c·p.x + d·p.z，在相机处线性化）；
//      [1] = (偏移 x, 偏移 z（格，已减云团漂移、对 1024 取模），群心 x, 群心 z（锚群平面，米））；[2] = (渐入渐出 0..1, 偏移 x, z（米，不减漂移）, 0)；
//    另有两个 vec4：那一群往八个方位（它的平面里北起顺时针，扇区心 0°、45°……）管多远（米）——wx_cloud_r<k>a 是 0–3、wx_cloud_r<k>b 是 4–7。
//    wx_cloud_shape = (② 的风向 x, z（锚群平面，单位）, 顺风方向的压缩, 过渡宽 blend（米）)，wx_cloud_shape2 = (过渡随距离变宽的系数, 权的门槛, 0, 0)
global uniform float wx_cloud_on;
global uniform mat4 wx_cloud_g0;
global uniform mat4 wx_cloud_g1;
global uniform mat4 wx_cloud_g2;
global uniform mat4 wx_cloud_g3;
global uniform vec4 wx_cloud_r0a;
global uniform vec4 wx_cloud_r0b;
global uniform vec4 wx_cloud_r1a;
global uniform vec4 wx_cloud_r1b;
global uniform vec4 wx_cloud_r2a;
global uniform vec4 wx_cloud_r2b;
global uniform vec4 wx_cloud_r3a;
global uniform vec4 wx_cloud_r3b;
global uniform vec4 wx_cloud_shape;
global uniform vec4 wx_cloud_shape2;

uniform sampler2D wx_group_elev : filter_nearest;
uniform vec4 wx_group = vec4(0.0, 2100.0, -1.0, 0.0);
uniform mat4 wx_group_inv = mat4(1.0);

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

// 海拔表的一行，按海拔在两箱之间插值；表还没设（1 × 1 的默认图）时给「暖季、没雪」。材质设了这一群的表（wx_group.w = 1）就用它的
vec4 wx_elev_row(float h, int row) {
	bool own = wx_group.w > 0.5;
	ivec2 size = own ? textureSize(wx_group_elev, 0) : textureSize(wx_elev, 0);
	if (size.x < 2 || size.y < 2) {
		return row == 0 ? vec4(0.0, 0.0, 1.0, 1.0) : vec4(0.0, 0.0, 0.5, 18.0);
	}
	vec2 range = own ? wx_group.xy : wx_elev_range;
	float span = max(1.0, range.y - range.x);
	float u = clamp((h - range.x) / span, 0.0, 1.0) * float(size.x - 1);
	int i = min(size.x - 2, int(floor(u)));
	vec4 a;
	vec4 b;
	if (own) {
		a = texelFetch(wx_group_elev, ivec2(i, row), 0);
		b = texelFetch(wx_group_elev, ivec2(i + 1, row), 0);
	} else {
		a = texelFetch(wx_elev, ivec2(i, row), 0);
		b = texelFetch(wx_elev, ivec2(i + 1, row), 0);
	}
	return mix(a, b, u - float(i));
}

// 锚群平面上的一点 p 在某槽那一群的云团噪声里取哪（格）
vec2 wx_cloud_slot_q(mat4 g, vec2 p) {
	return vec2(g[0].x * p.x + g[0].y * p.y, g[0].z * p.x + g[0].w * p.y) + g[1].xy;
}

// 某槽那一群往（它的平面里）方位 bearing（弧度，北起顺时针）管多远（米）：八个扇区之间线性插值
float wx_cloud_reach(vec4 ra, vec4 rb, float bearing) {
	float r[8] = float[](ra.x, ra.y, ra.z, ra.w, rb.x, rb.y, rb.z, rb.w);
	float u = bearing / (3.14159265358979 * 0.25);
	float f = floor(u);
	int s0 = int(mod(f, 8.0));
	int s1 = (s0 + 1) % 8;
	return mix(r[s0], r[s1], u - f);
}

// 锚群平面上的一点 p：各槽的权（与 planet_weather.cpp 的 weights_at 同式：管辖差 s = 顺风压过的距离 − 那一群往这一点的方位管多远，权 ∝ exp(−(s − s_min) / B)，
// B = blend + 系数 × max(0, s_min)，不到门槛的丢掉；再乘各槽的渐入渐出）。返回权和（0 = 一个都不管）
float wx_cloud_weights(vec2 p, out vec4 w) {
	mat4 g[4] = mat4[](wx_cloud_g0, wx_cloud_g1, wx_cloud_g2, wx_cloud_g3);
	vec4 ra[4] = vec4[](wx_cloud_r0a, wx_cloud_r1a, wx_cloud_r2a, wx_cloud_r3a);
	vec4 rb[4] = vec4[](wx_cloud_r0b, wx_cloud_r1b, wx_cloud_r2b, wx_cloud_r3b);
	vec2 dir = wx_cloud_shape.xy;
	float sq = max(1.0, wx_cloud_shape.z);
	vec4 s = vec4(1e12);
	float s_min = 1e12;
	for (int k = 0; k < 4; k++) {
		if (g[k][2].x <= 0.0) {
			continue;
		}
		vec2 d = p - g[k][1].zw;
		float along = dot(d, dir);
		float across = d.x * dir.y - d.y * dir.x;
		// 那一群平面里的方位：线性部分只转不缩（除以一格的倍数不影响方向）
		vec2 dg = vec2(g[k][0].x * d.x + g[k][0].y * d.y, g[k][0].z * d.x + g[k][0].w * d.y);
		float bearing = atan(dg.x, -dg.y);
		s[k] = sqrt((along / sq) * (along / sq) + across * across) - wx_cloud_reach(ra[k], rb[k], bearing < 0.0 ? bearing + 6.28318530718 : bearing);
		s_min = min(s_min, s[k]);
	}
	float blend = max(1.0, wx_cloud_shape.w + wx_cloud_shape2.x * max(0.0, s_min));
	float sum = 0.0;
	for (int k = 0; k < 4; k++) {
		w[k] = 0.0;
		if (s[k] > 1e11) {
			continue;
		}
		float wk = exp(-(s[k] - s_min) / blend);
		if (wk < wx_cloud_shape2.y) {
			continue;
		}
		w[k] = wk * g[k][2].x;
		sum += w[k];
	}
	return sum;
}

// 近处的云团场：锚群平面上的一点 p（米）；没有行星数据时是这一张图自己的（p / 一格 − legacy_drift）
float wx_cloud_field(vec2 p, vec2 legacy_drift, float cell) {
	if (wx_cloud_on < 0.5) {
		return wx_fbm(p / cell - legacy_drift);
	}
	vec4 w;
	float sum = wx_cloud_weights(p, w);
	if (sum <= 1e-6) {
		return wx_fbm(p / cell - legacy_drift);
	}
	mat4 g[4] = mat4[](wx_cloud_g0, wx_cloud_g1, wx_cloud_g2, wx_cloud_g3);
	float num = 0.0;
	float sqsum = 0.0;
	float one = 0.5;
	int n = 0;
	for (int k = 0; k < 4; k++) {
		if (w[k] <= 0.0) {
			continue;
		}
		float f = wx_fbm(wx_cloud_slot_q(g[k], p));
		float wk = w[k] / sum;
		one = f;
		num += wk * (f - 0.5);
		sqsum += wk * wk;
		n++;
	}
	return n == 1 ? one : 0.5 + num / sqrt(sqsum);
}

// 云团的细纹（天空给云团加一点起伏）：几群各自的格坐标上取、按权混；没有行星数据时按这一张图自己的
float wx_cloud_shimmer(vec2 p, vec2 legacy_drift, float cell, float time) {
	vec2 wobble = vec2(time * 0.004, 0.0);
	if (wx_cloud_on < 0.5) {
		return wx_value((p / cell - legacy_drift) * 7.0 + wobble);
	}
	vec4 w;
	float sum = wx_cloud_weights(p, w);
	if (sum <= 1e-6) {
		return wx_value((p / cell - legacy_drift) * 7.0 + wobble);
	}
	mat4 g[4] = mat4[](wx_cloud_g0, wx_cloud_g1, wx_cloud_g2, wx_cloud_g3);
	float v = 0.0;
	for (int k = 0; k < 4; k++) {
		if (w[k] > 0.0) {
			v += w[k] / sum * wx_value(wx_cloud_slot_q(g[k], p) * 7.0 + wobble);
		}
	}
	return v;
}

// 云带顶的淡斑（不跟云团漂、只要在真球面上稳定）：几群各自平面上取、按云团的权混；没有行星数据时就是锚群平面上的
float wx_deck_mottle(vec2 p) {
	if (wx_cloud_on < 0.5) {
		float a = wx_value(p / 14000.0 + vec2(311.0, 97.0));
		float b = wx_value(p / 3700.0 + vec2(53.0, 211.0));
		return (a - 0.5) * 1.2 + (b - 0.5) * 0.6;
	}
	vec4 w;
	float sum = wx_cloud_weights(p, w);
	mat4 g[4] = mat4[](wx_cloud_g0, wx_cloud_g1, wx_cloud_g2, wx_cloud_g3);
	float m = 0.0;
	for (int k = 0; k < 4; k++) {
		if (w[k] <= 0.0) {
			continue;
		}
		float inv_cell = length(g[k][0].xz);
		vec2 q = vec2(g[k][0].x * p.x + g[k][0].y * p.y, g[k][0].z * p.x + g[k][0].w * p.y) / max(1e-9, inv_cell) + g[k][2].yz;
		float a = wx_value(q / 14000.0 + vec2(311.0, 97.0));
		float b = wx_value(q / 3700.0 + vec2(53.0, 211.0));
		m += w[k] / max(1e-6, sum) * ((a - 0.5) * 1.2 + (b - 0.5) * 0.6);
	}
	return m;
}

// 极向（南半球 +1）：这一群的，没设用全局的
float wx_hemi() {
	return wx_group.w > 0.5 ? wx_group.z : wx_misc.x;
}

// 世界坐标 → 这一群的平面坐标（没设就是原样）
vec3 wx_group_pos(vec3 world) {
	return (wx_group_inv * vec4(world, 1.0)).xyz;
}

vec3 wx_srgb_to_linear(vec3 c) {
	return mix(pow((c + vec3(0.055)) * (1.0 / 1.055), vec3(2.4)), c * (1.0 / 12.92), lessThan(c, vec3(0.04045)));
}

// 便宜的二维值噪声（地皮、散布上的碎斑用；不必与 C++ 对得上）
float wx_blotch(vec2 p) {
	return wx_value(p);
}
