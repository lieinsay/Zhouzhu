shader_type spatial;
render_mode blend_mix,depth_prepass_alpha,cull_back,diffuse_burley,specular_schlick_ggx;
#include "res://shaders/planet_curve.gdshaderinc"
uniform sampler2D face_tex : source_color,filter_linear_mipmap_anisotropic,repeat_disable;
void vertex(){planet_curve_model(MODEL_MATRIX,VERTEX,NORMAL);}
void fragment(){vec4 face=texture(face_tex,UV);ALBEDO=face.rgb;ALPHA=face.a;ROUGHNESS=.65;SPECULAR=.25;}
