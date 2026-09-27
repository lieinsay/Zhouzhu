// 行星计划 P2 的三维值噪声（行星系的单位向量乘尺度后取），P7 起单独一份：planet_sky.gdshaderinc（行星云层、云带壳、白礁）与
// planet_sea.gdshaderinc（云底、海面）都用它。

uint pn_hash3(ivec3 p) {
	uvec3 q = uvec3(p);
	uint h = q.x * 0x8da6b343u ^ q.y * 0xd8163841u ^ q.z * 0xcb1ab31fu;
	h ^= h >> 15u;
	h *= 0x85ebca6bu;
	h ^= h >> 13u;
	h *= 0xc2b2ae35u;
	h ^= h >> 16u;
	return h;
}

float pn_value3(vec3 p) {
	vec3 i = floor(p);
	vec3 f = p - i;
	vec3 u = f * f * (3.0 - 2.0 * f);
	ivec3 c = ivec3(i);
	float n000 = float(pn_hash3(c) & 0xFFFFu) / 65535.0;
	float n100 = float(pn_hash3(c + ivec3(1, 0, 0)) & 0xFFFFu) / 65535.0;
	float n010 = float(pn_hash3(c + ivec3(0, 1, 0)) & 0xFFFFu) / 65535.0;
	float n110 = float(pn_hash3(c + ivec3(1, 1, 0)) & 0xFFFFu) / 65535.0;
	float n001 = float(pn_hash3(c + ivec3(0, 0, 1)) & 0xFFFFu) / 65535.0;
	float n101 = float(pn_hash3(c + ivec3(1, 0, 1)) & 0xFFFFu) / 65535.0;
	float n011 = float(pn_hash3(c + ivec3(0, 1, 1)) & 0xFFFFu) / 65535.0;
	float n111 = float(pn_hash3(c + ivec3(1, 1, 1)) & 0xFFFFu) / 65535.0;
	return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y), mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float pn_fbm3(vec3 p, int octaves) {
	float sum = 0.0;
	float amp = 0.5;
	float norm = 0.0;
	for (int k = 0; k < octaves; k++) {
		sum += amp * pn_value3(p + vec3(19.1, 7.3, 3.7) * float(k));
		norm += amp;
		p = p * 2.03;
		amp *= 0.5;
	}
	return sum / norm;
}

// 按像素滤过的 fbm：一层的波长不到三四个像素就换成它的均值（0.5，免得闪成一片噪点）；r_kept 是留下的方差份额（全留 1）
float pn_fbm3_lod(vec3 p, int octaves, float base_m, float foot, out float r_kept) {
	float sum = 0.0;
	float amp = 0.5;
	float norm = 0.0;
	float var_all = 0.0;
	float var_kept = 0.0;
	float wave = base_m;
	for (int k = 0; k < octaves; k++) {
		float keep = 1.0 - smoothstep(0.15, 0.35, foot / wave);
		float v = keep > 0.001 ? pn_value3(p + vec3(19.1, 7.3, 3.7) * float(k)) : 0.5;
		sum += amp * mix(0.5, v, keep);
		norm += amp;
		var_all += amp * amp;
		var_kept += amp * amp * keep * keep;
		p = p * 2.03;
		amp *= 0.5;
		wave /= 2.03;
	}
	r_kept = var_kept / max(1e-9, var_all);
	return sum / norm;
}
