extends Node
## 真实开局的场景材质基础检查：不改变地形/天气，仅读取实际地面节点及正式绑定。
var output := ""
var failed := false
func check(ok: bool,message: String) -> void:
	if not ok:failed=true;push_error("scene_materials: "+message)
func _ready() -> void:
	output=ProjectSettings.globalize_path("res://../output/scene-art-20261027/foundation-headless")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
	DirAccess.make_dir_recursive_absolute(output)
	var main: Expedition=load("res://world_seed42.tscn").instantiate()
	main.get_node("GroupProvider").world_dir=output.path_join("test-save")
	add_child(main)
	var bindings: Dictionary=JSON.parse_string(FileAccess.get_file_as_string("res://art/bindings.json"))
	var materials:={}
	for key in bindings.bindings:
		if not key.begins_with("material."):continue
		var material: ShaderMaterial=ArtSurface.create(key,Color(.5,.5,.5),.9)
		var texture: Texture2D=material.get_shader_parameter("surface_tex")
		check(material.get_shader_parameter("surface_enabled")==true and texture!=null,"基础色可加载 "+key)
		if key in ["material.rammed_earth","material.brick"]:check(material.get_shader_parameter("surface_uv_metres")==false,"夯土层纹/砖缝保持水平 "+key)
		if key=="material.wood":check(material.get_shader_parameter("surface_uv_metres")==true,"木纹沿构件长轴")
		var image: Image=texture.get_image() if texture!=null else null
		check(image!=null and image.has_mipmaps(),"3D基础色具有mipmap "+key)
		materials[key]=texture.resource_path if texture!=null else ""
	var ground: MeshInstance3D=null
	for i in 300:
		await get_tree().physics_frame
		for child in main.get_node("World").find_children("*","MeshInstance3D",true,false):
			var node:=child as MeshInstance3D
			if node.mesh==null or node.mesh.get_surface_count()==0:continue
			var material=node.get_active_material(0)
			if material is ShaderMaterial and material.shader.resource_path=="res://shaders/ground.gdshader":ground=node;break
		if ground!=null:break
	check(ground!=null,"真实地面网格已挂载")
	var ground_paths:={}
	if ground!=null:
		var material:=ground.get_active_material(0) as ShaderMaterial
		for key in ["stone","soil","grass","gravel","litter"]:
			check(material.get_shader_parameter("ground_"+key+"_enabled")==true,"地表细节绑定 "+key)
			var texture: Texture2D=material.get_shader_parameter("ground_"+key+"_tex")
			ground_paths[key]=texture.resource_path if texture!=null else ""
		var arrays:=ground.mesh.surface_get_arrays(0)
		check(arrays[Mesh.ARRAY_CUSTOM1]!=null and arrays[Mesh.ARRAY_CUSTOM1].size()>0,"真实地面携带源表面语义")
	var report:={"passed":not failed,"materials":materials,"ground":ground_paths,"scope":"真实开局正式材质与源表面属性读取；像素另验"}
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "))
	print("scene_materials: ",JSON.stringify(report))
	main.queue_free();await get_tree().process_frame;get_tree().quit(1 if failed else 0)
