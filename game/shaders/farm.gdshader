shader_type spatial;
render_mode cull_back,specular_schlick_ggx;
#include "res://shaders/planet_curve.gdshaderinc"
#include "res://shaders/scene_detail.gdshaderinc"
global uniform float wx_wet;
varying vec3 local_pos;
varying vec3 local_normal;
void vertex(){
	local_pos=VERTEX;local_normal=NORMAL;
	COLOR.rgb=mix(pow((COLOR.rgb+vec3(.055))/1.055,vec3(2.4)),COLOR.rgb/12.92,lessThan(COLOR.rgb,vec3(.04045)));
	planet_curve_model(MODEL_MATRIX,VERTEX,NORMAL);
}
void fragment(){
	vec3 col=COLOR.rgb*scene_detail(local_pos,local_normal,COLOR.a,length(VERTEX));
	float wet=clamp(wx_wet,0.0,1.0);ALBEDO=col*mix(1.0,.88,wet);ROUGHNESS=mix(1.0,.72,wet);
}
