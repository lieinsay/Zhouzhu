// 行星计划 P7：云带之下——从下往上看的天（云底、海、霾）与海面（浪、白沫、白礁）。先 include weather.gdshaderinc、planet_curve.gdshaderinc、planet_sky.gdshaderinc。
//
// 垂直结构（D13）：云带顶 −3800 m、云底 −5000 m、海面 −5600 m（都是游戏 y）。相机降到云带顶以下：
//   - 天空着色器（sky.gdshader）换成 planet_under_sky：视线往上打到云底（一层压着的灰，按 planet_base_lumps 有些起伏）、往下打到海（菲涅耳反云底的光），远处蒙霾；
//     屏幕上这一段几乎都被云底、海面网格盖着，主要管反光（天空的辐照立方图）与网格外的远处；
//   - 云底网格（cloud_base.gdshader）、海面网格（sea.gdshader）跟着相机走（SeaSurface 建），顶点走 planet_curve 弯曲，颜色与这里同一份光（Atmosphere 给的 under_*）。
// 海面：几道 Gerstner 顶点波（近处，浪高、波长按当地的风：src/sea_state.* 的 sea_waves）+ 两层按风错开平移的法线贴图（远处按 mipmap 渐平）+ 白沫（浪尖、风起的白头）+ 白礁（planet_reef）。

const float PS_BASE_Y = -5000.0; // D13：云带底

// 天与光（Atmosphere）每帧设：头顶的云底多亮（云底是光源：透下来的光）、霾（= 引擎的雾色）、海水漫射的那一份、霾的浓淡（= 引擎的雾，每米）
uniform float under_band = 0.0; // 相机在云带顶以下多少（0..1）：天换成从下往上看
uniform vec3 under_base_color = vec3(0.08, 0.085, 0.095);
uniform vec3 under_haze_color = vec3(0.1, 0.105, 0.115);
uniform vec3 under_sea_color = vec3(0.002, 0.004, 0.005);
uniform float under_haze_density = 0.00012;

// 云底的起伏（行星系单位向量 p）：几公里的大斑、一两公里一道的云卷、几百米的团，0.55..1.3 上下（暗处是云厚的地方）
float planet_base_lumps(vec3 p) {
	float R = planet_datum_r;
	float a = pn_value3(p * (R / 7000.0) + vec3(3.0, 17.0, 5.0));
	float b = pn_value3(p * (R / 1600.0) + vec3(29.0, 7.0, 13.0));
	float c = pn_value3(p * (R / 420.0) + vec3(1.0, 41.0, 23.0));
	float d = pn_value3(p * (R / 120.0) + vec3(9.0, 2.0, 31.0));
	float roll = smoothstep(0.35, 0.65, b) * smoothstep(0.3, 0.7, c); // 一团团厚的（暗）
	return 0.98 + 0.5 * (a - 0.5) + 0.3 * (b - 0.5) + 0.3 * (c - 0.5) + 0.12 * (d - 0.5) - 0.22 * roll;
}

// 阴天的天（CIE 阴天）：头顶最亮，往地平线暗到三分之一；s 是视线仰角的正弦
float planet_overcast_gradient(float s) {
	return (1.0 + 2.0 * clamp(s, 0.0, 1.0)) / 3.0;
}

// 从下往上看的天：dir 是相机局部系里的视线（单位），cam_y 是相机的游戏 y；按视线与云底、海面两个同心球面求交（同 planet_surface 的求交）
vec3 planet_under_sky(vec3 dir, float cam_y) {
	float R = planet_datum_r;
	float r0 = R + cam_y;
	float ts = ps_hit(r0, dir.y, R + PS_SEA_Y);
	float tb = ps_hit(r0, dir.y, R + PS_BASE_Y);
	vec3 col = under_haze_color;
	float t = 1e9;
	if (ts > 0.0 && (tb < 0.0 || ts < tb)) {
		// 海：菲涅耳反云底的光；白礁
		vec3 X = vec3(0.0, r0, 0.0) + dir * ts;
		vec3 up = X / length(X);
		float c = max(0.0, dot(-dir, up));
		float fres = 0.02 + 0.98 * pow(1.0 - c, 5.0);
		col = mix(under_sea_color, under_base_color * planet_overcast_gradient(c) * 0.85, fres); // 反的是对称那一边的云底
		vec3 pn = normalize(planet_axes * X);
		float reef = planet_reef(pn, ts * planet_pixel / max(0.05, c));
		col = mix(col, under_base_color * vec3(1.05, 1.07, 1.0) * 0.9, reef * 0.85);
		t = ts;
	} else if (tb > 0.0) {
		vec3 X = vec3(0.0, r0, 0.0) + dir * tb;
		col = under_base_color * planet_base_lumps(normalize(planet_axes * X)) * planet_overcast_gradient(dir.y);
		t = tb;
	}
	float haze = 1.0 - exp(-under_haze_density * t);
	return mix(col, under_haze_color, haze);
}
