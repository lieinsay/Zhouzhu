// 顶点alpha为显式表面语义，非透明度：.50阔叶 .55针叶 .60秆 .65籽粒 .80土 .85砾 .90树皮 1石。
uniform bool detail_bark_enabled=false;
uniform bool detail_foliage_enabled=false;
uniform bool detail_needle_enabled=false;
uniform bool detail_hemp_enabled=false;
uniform bool detail_grain_enabled=false;
uniform bool detail_soil_enabled=false;
uniform bool detail_gravel_enabled=false;
uniform bool detail_stone_enabled=false;
uniform sampler2D detail_bark_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform sampler2D detail_foliage_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform sampler2D detail_needle_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform sampler2D detail_hemp_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform sampler2D detail_grain_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform sampler2D detail_soil_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform sampler2D detail_gravel_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform sampler2D detail_stone_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform vec3 detail_bark_mean=vec3(1.0);
uniform vec3 detail_foliage_mean=vec3(1.0);
uniform vec3 detail_needle_mean=vec3(1.0);
uniform vec3 detail_hemp_mean=vec3(1.0);
uniform vec3 detail_grain_mean=vec3(1.0);
uniform vec3 detail_soil_mean=vec3(1.0);
uniform vec3 detail_gravel_mean=vec3(1.0);
uniform vec3 detail_stone_mean=vec3(1.0);
uniform float detail_bark_tile_metres=.5;
uniform float detail_foliage_tile_metres=.25;
uniform float detail_needle_tile_metres=.25;
uniform float detail_hemp_tile_metres=.25;
uniform float detail_grain_tile_metres=.25;
uniform float detail_soil_tile_metres=1.0;
uniform float detail_gravel_tile_metres=.5;
uniform float detail_stone_tile_metres=1.0;
vec3 scene_sample(sampler2D tex,vec3 mean_color,float metres,vec3 p,vec3 n){
	vec3 w=pow(abs(normalize(n)),vec3(4.0));w/=max(dot(w,vec3(1.0)),.0001);p/=max(metres,.001);
	vec3 color=texture(tex,p.zy).rgb*w.x+texture(tex,p.xz).rgb*w.y+texture(tex,p.xy).rgb*w.z;
	return clamp(color/max(mean_color,vec3(.001)),vec3(.65),vec3(1.35));
}
vec3 scene_detail(vec3 p,vec3 n,float semantic,float eye_distance){
	float detail_fade=1.0-smoothstep(35.0,90.0,eye_distance);if(detail_fade<.001)return vec3(1.0);
	vec3 ratio=vec3(1.0);
	if(semantic<.525){if(detail_foliage_enabled)ratio=scene_sample(detail_foliage_tex,detail_foliage_mean,detail_foliage_tile_metres,p,n);}
	else if(semantic<.575){if(detail_needle_enabled)ratio=scene_sample(detail_needle_tex,detail_needle_mean,detail_needle_tile_metres,p,n);}
	else if(semantic<.625){if(detail_hemp_enabled)ratio=scene_sample(detail_hemp_tex,detail_hemp_mean,detail_hemp_tile_metres,p,n);}
	else if(semantic<.70){if(detail_grain_enabled)ratio=scene_sample(detail_grain_tex,detail_grain_mean,detail_grain_tile_metres,p,n);}
	else if(semantic<.825){if(detail_soil_enabled)ratio=scene_sample(detail_soil_tex,detail_soil_mean,detail_soil_tile_metres,p,n);}
	else if(semantic<.875){if(detail_gravel_enabled)ratio=scene_sample(detail_gravel_tex,detail_gravel_mean,detail_gravel_tile_metres,p,n);}
	else if(semantic<.95){if(detail_bark_enabled)ratio=scene_sample(detail_bark_tex,detail_bark_mean,detail_bark_tile_metres,p,n);}
	else{if(detail_stone_enabled)ratio=scene_sample(detail_stone_tex,detail_stone_mean,detail_stone_tile_metres,p,n);}
	return mix(vec3(1.0),ratio,detail_fade);
}
