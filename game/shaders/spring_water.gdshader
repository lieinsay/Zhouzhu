shader_type spatial;
render_mode cull_disabled, blend_mix, depth_draw_opaque, world_vertex_coords;
#include "res://shaders/planet_curve.gdshaderinc"
varying vec3 world_pos;
void vertex() {
	world_pos = VERTEX;
	VERTEX = planet_visual(world_pos);
	NORMAL = planet_visual_normal(world_pos, NORMAL);
}
void fragment() {
	float streak = sin(world_pos.y * 1.7 + TIME * 9.0 + sin(world_pos.x * 3.2) * 2.0);
	ALBEDO = mix(vec3(0.36,0.52,0.57),vec3(0.78,0.88,0.91),streak * 0.5 + 0.5);
	ALPHA = 0.65;
	ROUGHNESS = 0.15;
}
