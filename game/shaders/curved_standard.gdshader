// 弯曲标准材质（单面）：见 curved_standard.gdshaderinc
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx, world_vertex_coords;

#include "res://shaders/curved_standard.gdshaderinc"
