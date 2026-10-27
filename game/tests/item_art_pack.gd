extends SceneTree
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
		for variant in 4:
			var actor=ClassDB.instantiate("SceneCharacter")
			actor.configure(variant,variant,false);root.add_child(actor)
			assert(actor.get_visual_state().face_loaded)
			actor.free()
		for look in [0,1,2,3,4,5]:assert(ArtSurface.scatter_mesh(look,0)!=null)
		panel.free()
		preview.free()
		print("item_art_pack: isolated PCK C++ loading passed ",active.bindings.size())
	quit(0)
