extends SceneTree
var added:={}
func add_resource(pack:PCKPacker,path:String)->void:
	if added.has(path):return
	assert(path.begins_with("res://") and not path.contains(".."));added[path]=true
	assert(pack.add_file(path,path)==OK)
	if FileAccess.file_exists(path+".import"):
		assert(pack.add_file(path+".import",path+".import")==OK)
		var settings:=ConfigFile.new();assert(settings.load(path+".import")==OK)
		var imported:String=settings.get_value("remap","path","")
		if not imported.is_empty() and not added.has(imported):added[imported]=true;assert(pack.add_file(imported,imported)==OK)
func add_directory(pack:PCKPacker,path:String)->void:
	for file in DirAccess.get_files_at(path):
		if not file.ends_with(".import"):add_resource(pack,path.path_join(file))
	for directory in DirAccess.get_directories_at(path):add_directory(pack,path.path_join(directory))
## 打包实际导入资源，然后在没有 art 源目录的隔离项目中验证 PCK 与 C++ 加载。
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	var args := OS.get_cmdline_user_args()
	var mode := args[0]
	var pack_path := args[1]
	if mode == "build":
		var pack := PCKPacker.new()
		assert(pack.pck_start(pack_path) == OK)
		assert(pack.add_file("res://art/bindings.json","res://art/bindings.json") == OK)
		if FileAccess.file_exists("res://art/model-bindings.json"):
			add_resource(pack,"res://art/model-bindings.json");add_resource(pack,"res://shaders/model_surface.gdshader");add_directory(pack,"res://art/models")
		for shader in ["curved_standard.gdshader","curved_standard_two_sided.gdshader","curved_standard.gdshaderinc","planet_curve.gdshaderinc","character_face.gdshader","scene_detail.gdshaderinc","scatter.gdshader","farm.gdshader","weather.gdshaderinc","ground.gdshader","ground_detail.gdshaderinc"]:
			assert(pack.add_file("res://shaders/"+shader,"res://shaders/"+shader)==OK)
		var active: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://art/bindings.json"))
		for binding in active.bindings.values():
			var path: String = binding.path
			var imported := ConfigFile.new()
			assert(imported.load(path+".import") == OK)
			var texture: String = imported.get_value("remap","path")
			assert(pack.add_file(path+".import",path+".import") == OK)
			assert(pack.add_file(texture,texture) == OK)
			# 同时保留原始派生PNG；不依赖它进行Texture2D加载。
			assert(pack.add_file(path,path) == OK)
		assert(pack.flush() == OK)
		print("item_art_pack: built ",active.bindings.size())
	else:
		assert(not FileAccess.file_exists("res://art/bindings.json"))
		assert(ProjectSettings.load_resource_pack(pack_path))
		var active: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://art/bindings.json"))
		var panel = ClassDB.instantiate("InventoryPanel")
		var preview = ClassDB.instantiate("RefitPanel")
		assert(panel != null)
		for key in active.bindings:
			if key.begins_with("material."):
				var surface: ShaderMaterial=ArtSurface.create(key,Color(.5,.5,.5),1.0)
				assert(surface.get_shader_parameter("surface_enabled")==true)
				var image: Texture2D=surface.get_shader_parameter("surface_tex")
				assert(image!=null and image.resource_path==active.bindings[key].path)
				continue
			var info: Dictionary = panel.get_item_art(key.trim_prefix("item.")) if key.begins_with("item.") else preview.get_asset_art(key)
			assert(info.loaded and info.path == active.bindings[key].path)
		assert(not panel.get_item_art("missing_pack_icon").loaded)
		assert(active.bindings["material.iron"].version=="v2")
		var model_active:Dictionary=JSON.parse_string(FileAccess.get_file_as_string("res://art/model-bindings.json"))
		for variant in 4:
			var actor=ClassDB.instantiate("SceneCharacter")
			actor.configure(variant,variant,false);root.add_child(actor)
			assert(actor.get_visual_state().face_loaded)
			assert(actor.get_visual_state().imported_model and actor.get_visual_state().skinned_clothing>=2 and actor.get_visual_state().textured_surfaces>=12)
			assert(actor.get_visual_state().model_path==model_active.bindings[actor.get_visual_state().model_id].path)
			actor.free()
		for look in [0,1,2,3,4,5]:assert(ArtSurface.scatter_mesh(look,0)!=null)
		var actual_static:=0
		for id in model_active.bindings:
			if id.begins_with("actor."):continue
			var model:Node3D=ArtSurface.model_instance(id);assert(model!=null and model.get_meta("art_model_path")==model_active.bindings[id].path);actual_static+=1;model.free()
		assert(actual_static==14)
		panel.free()
		preview.free()
		print("item_art_pack: isolated PCK C++ loading passed ",active.bindings.size())
	quit(0)
