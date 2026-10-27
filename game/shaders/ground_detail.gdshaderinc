// 只调制已有地表基色；份额来自CUSTOM1的源表面语义，远景退回既有颜色。
uniform bool ground_stone_enabled = false;
uniform bool ground_soil_enabled = false;
uniform bool ground_grass_enabled = false;
uniform bool ground_gravel_enabled = false;
uniform bool ground_litter_enabled = false;
uniform sampler2D ground_stone_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ground_soil_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ground_grass_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ground_gravel_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D ground_litter_tex : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 ground_stone_mean = vec3(1.0);
uniform vec3 ground_soil_mean = vec3(1.0);
uniform vec3 ground_grass_mean = vec3(1.0);
uniform vec3 ground_gravel_mean = vec3(1.0);
uniform vec3 ground_litter_mean = vec3(1.0);
uniform float ground_stone_tile_metres = 1.0;
uniform float ground_soil_tile_metres = 1.0;
uniform float ground_grass_tile_metres = 1.0;
uniform float ground_gravel_tile_metres = 0.5;
uniform float ground_litter_tile_metres = 1.0;

vec3 ground_surface_detail(vec3 position, vec3 normal, vec4 shares, float eye_distance) {
	float fade = 1.0 - smoothstep(35.0,110.0,eye_distance);
	if (fade<=0.001) return vec3(1.0);
	vec4 weights=max(shares,vec4(0.0));
	weights/=max(1.0,dot(weights,vec4(1.0)));
	float soil=max(0.0,1.0-dot(weights,vec4(1.0)));
	vec3 detail=vec3(1.0);
	if (ground_stone_enabled && weights.r>0.001) {
		vec3 n=pow(abs(normalize(normal)),vec3(4.0));n/=max(dot(n,vec3(1.0)),0.0001);
		vec3 p=position/max(ground_stone_tile_metres,.001);
		vec3 color=texture(ground_stone_tex,p.zy).rgb*n.x+texture(ground_stone_tex,p.xz).rgb*n.y+texture(ground_stone_tex,p.xy).rgb*n.z;
		detail+=(clamp(color/max(ground_stone_mean,vec3(.001)),vec3(.65),vec3(1.35))-vec3(1.0))*weights.r;
	}
	if(ground_gravel_enabled && weights.g>.001)detail+=(clamp(texture(ground_gravel_tex,position.xz/max(ground_gravel_tile_metres,.001)).rgb/max(ground_gravel_mean,vec3(.001)),vec3(.65),vec3(1.35))-vec3(1.0))*weights.g;
	if(ground_grass_enabled && weights.b>.001)detail+=(clamp(texture(ground_grass_tex,position.xz/max(ground_grass_tile_metres,.001)).rgb/max(ground_grass_mean,vec3(.001)),vec3(.65),vec3(1.35))-vec3(1.0))*weights.b;
	if(ground_litter_enabled && weights.a>.001)detail+=(clamp(texture(ground_litter_tex,position.xz/max(ground_litter_tile_metres,.001)).rgb/max(ground_litter_mean,vec3(.001)),vec3(.65),vec3(1.35))-vec3(1.0))*weights.a;
	if(ground_soil_enabled && soil>.001)detail+=(clamp(texture(ground_soil_tex,position.xz/max(ground_soil_tile_metres,.001)).rgb/max(ground_soil_mean,vec3(.001)),vec3(.65),vec3(1.35))-vec3(1.0))*soil;
	return mix(vec3(1.0),detail,fade);
}
