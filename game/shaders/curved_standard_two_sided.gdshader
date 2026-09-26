// 弯曲标准材质（双面：帆这类薄片）：见 curved_standard.gdshaderinc
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_disabled, diffuse_burley, specular_schlick_ggx, world_vertex_coords;

#include "res://shaders/curved_standard.gdshaderinc"
