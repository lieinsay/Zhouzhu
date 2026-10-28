shader_type spatial;
render_mode cull_disabled,depth_draw_opaque,diffuse_burley,specular_schlick_ggx,world_vertex_coords;
#include "res://shaders/planet_curve.gdshaderinc"
global uniform float wx_wet;
uniform sampler2D model_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform bool model_has_texture=false;
uniform vec4 model_tint:source_color=vec4(1.0);
uniform int model_role=0; // 0作者肤/脸贴图，1头发，2上衣，3裤，4鞋，5器物
uniform bool model_mature=false;
uniform bool model_iris=false;
uniform bool model_detail_enabled=false;
uniform sampler2D model_detail_tex:source_color,filter_linear_mipmap_anisotropic,repeat_enable;
uniform vec3 model_detail_mean=vec3(1.0);
uniform float model_detail_tile_metres=.25;
uniform float model_roughness=.9;
void vertex(){vec3 original=VERTEX;VERTEX=planet_visual(original);NORMAL=planet_visual_normal(original,NORMAL);}
void fragment(){
	vec4 original=vec4(1.0);if(model_has_texture)original=texture(model_tex,UV);
	if(original.a<.35)discard;
	vec3 col=original.rgb*model_tint.rgb;
	if(model_iris){float pigment=dot(original.rgb,vec3(.2126,.7152,.0722));col=vec3(.72,.39,.16)*pigment;}
	if(model_role==2||model_role==3||model_role==4){
		col=model_tint.rgb;
		if(model_detail_enabled)col*=clamp(texture(model_detail_tex,UV*3.0).rgb/max(model_detail_mean,vec3(.001)),vec3(.75),vec3(1.25));
	}
	if(model_role==1&&model_mature){float gray=dot(col,vec3(.2126,.7152,.0722));col=mix(col,vec3(gray)*1.65,.7);}
	float wet=clamp(wx_wet,0.0,1.0);if(model_role>1)col*=mix(1.0,.9,wet);
	ALBEDO=col;ROUGHNESS=model_roughness*mix(1.0,.8,wet);SPECULAR=model_role==0?.18:.3;
}
