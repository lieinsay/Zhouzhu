extends SceneTree
## Runs only in the tool's isolated project, with no autoloads or world/save data.
## CPU surface arrays come from unchanged C++ builders and the existing game DLL.

var output_dir := ""
var exported: Array = []
var failures: Array = []

func _initialize() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):
			output_dir = arg.trim_prefix("--output=")
	call_deferred("_run")

func _array3(v: Vector3) -> Array:
	return [v.x, v.y, v.z]

func _material_info(material: Material) -> Dictionary:
	var info := {"color": Color.WHITE, "roughness": 0.95, "vertex_color": false}
	if material!=null and material.has_meta("art_asset"):info.artAsset=str(material.get_meta("art_asset"))
	if material is StandardMaterial3D:
		info.color = material.albedo_color
		info.roughness = material.roughness
		info.metallic=material.metallic
		info.vertex_color = material.vertex_color_use_as_albedo
		info.doubleSided = material.cull_mode == BaseMaterial3D.CULL_DISABLED
		if material.emission_enabled:
			info.emission = _array3(Vector3(material.emission.r, material.emission.g, material.emission.b))
			info.emissionEnergy = material.emission_energy_multiplier
	elif material is ShaderMaterial:
		var shader_color = material.get_shader_parameter("albedo")
		if shader_color is Color:
			info.color = shader_color
		elif shader_color is Vector3:
			info.color = Color(shader_color.x, shader_color.y, shader_color.z)
		var roughness = material.get_shader_parameter("roughness")
		var metallic=material.get_shader_parameter("metallic")
		if metallic is float or metallic is int:info.metallic=metallic
		if roughness is float or roughness is int:
			info.roughness = roughness
		info.vertex_color = material.get_shader_parameter("vertex_color") == true or material.shader.resource_path.ends_with("scatter.gdshader")
		info.doubleSided = material.shader.resource_path.contains("two_sided")
		var emission = material.get_shader_parameter("emission")
		if emission is Color:
			info.emission = [emission.r, emission.g, emission.b]
		elif emission is Vector3:
			info.emission = _array3(emission)
		var energy = material.get_shader_parameter("emission_energy")
		if energy is float or energy is int:
			info.emissionEnergy = energy
	return info

func _surface_parts(node: MeshInstance3D, frame: Transform3D) -> Array:
	var parts: Array = []
	var mesh := node.mesh
	if mesh == null:
		return parts
	var transform := frame * node.global_transform
	var normal_basis := transform.basis.inverse().transposed()
	var tint: Color = node.get_meta("export_instance_color", Color.WHITE)
	for surface in mesh.get_surface_count():
		if mesh is ArrayMesh and mesh.surface_get_primitive_type(surface) != Mesh.PRIMITIVE_TRIANGLES:
			failures.append("非三角网格：" + str(node.name))
			continue
		var arrays := mesh.surface_get_arrays(surface)
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		var uvs: PackedVector2Array=arrays[Mesh.ARRAY_TEX_UV] if arrays[Mesh.ARRAY_TEX_UV]!=null else PackedVector2Array()
		var uv2s: PackedVector2Array=arrays[Mesh.ARRAY_TEX_UV2] if arrays[Mesh.ARRAY_TEX_UV2]!=null else PackedVector2Array()
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX] if arrays[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
		var colors: PackedColorArray = arrays[Mesh.ARRAY_COLOR] if arrays[Mesh.ARRAY_COLOR] != null else PackedColorArray()
		if indices.is_empty():
			for i in vertices.size():
				indices.append(i)
		if indices.size() % 3 != 0 or normals.size() != vertices.size():
			failures.append("顶点/法线/索引契约不完整：" + str(node.name))
			continue
		var info := _material_info(node.get_active_material(surface))
		var buckets := {}
		for tri in range(0, indices.size(), 3):
			var a := indices[tri]
			var b := indices[tri + 1]
			var c := indices[tri + 2]
			var color: Color = info.color * tint
			if info.vertex_color and colors.size() == vertices.size():
				# Actual scatter triangles have one flat vertex colour per face.
				color *= colors[a]
				if colors[a] != colors[b] or colors[a] != colors[c]:
					failures.append("网格包含插值顶点色，不能折为平涂材质：" + str(node.name))
					continue
			# Scatter alpha is a foliage classification flag, not transparency.
			var alpha: float = 1.0 if node.has_meta("export_instance_color") else info.color.a
			var key := "%0.8f,%0.8f,%0.8f,%0.8f" % [color.r, color.g, color.b, alpha]
			if not buckets.has(key):
				var part := {"positions": [], "normals": [], "indices": [], "color": [color.r, color.g, color.b], "alpha": alpha, "roughness": info.roughness, "node": str(node.name)}
				part.uvs=[];part.uv2s=[]
				for optional in ["doubleSided", "emission", "emissionEnergy", "metallic", "artAsset"]:
					if info.has(optional):
						part[optional] = info[optional]
				buckets[key] = part
			var part: Dictionary = buckets[key]
			var start: int = part.positions.size() / 3
			# Godot's clockwise face indices become WebGL counterclockwise.
			var order := [a, c, b] if transform.basis.determinant() >= 0 else [a, b, c]
			for index in order:
				part.positions.append_array(_array3(transform * vertices[index]))
				part.normals.append_array(_array3((normal_basis * normals[index]).normalized()))
				if uvs.size()==vertices.size():part.uvs.append_array([uvs[index].x,uvs[index].y])
				if uv2s.size()==vertices.size():part.uv2s.append_array([uv2s[index].x,uv2s[index].y])
			part.indices.append_array([start, start + 1, start + 2])
		parts.append_array(buckets.values())
	return parts

func _walk(node: Node, frame: Transform3D, parts: Array, prefixes: Array = []) -> void:
	if node is MeshInstance3D:
		var allowed := prefixes.is_empty()
		for prefix in prefixes:
			if str(node.name).begins_with(prefix):
				allowed = true
		if allowed:
			parts.append_array(_surface_parts(node, frame))
	for child in node.get_children():
		_walk(child, frame, parts, prefixes)

func _save(row: Dictionary, node: Node3D, prefixes: Array = []) -> void:
	var parts: Array = []
	_walk(node, node.global_transform.affine_inverse(), parts, prefixes)
	if parts.is_empty():
		failures.append("未生成真实网格：" + row.id)
		return
	var filename: String = row.id.replace(":", "-") + ".json"
	var file := FileAccess.open(output_dir.path_join(filename), FileAccess.WRITE)
	if file == null:
		failures.append("无法写网格：" + filename)
		return
	file.store_string(JSON.stringify({"schemaVersion": 1, "units": "m", "winding": "ccw", "parts": parts}))
	file.close()
	row.erase("node")
	row.url = "/assets/models/" + filename
	row.format = "mesh-json"
	row.kind = "game"
	exported.append(row)

func _export_ship_modules() -> void:
	if not ClassDB.class_exists("Ship"):
		failures.append("现有游戏 DLL 未加载，船模块没有可读取的真实节点")
		return
	var specs := [
		["bridge", "驾驶区", ["0,0,0:bridge"], [[0,0,0]]],
		["storage", "储存区", ["0,0,0:bridge", "0,0,-1:storage"], [[0,0,-1]]],
		["sail", "帆", ["0,0,0:bridge", "0,0,-1:sail"], [[0,0,-1]]],
		["floatstone", "浮石舱", ["0,0,0:bridge", "0,0,-1:floatstone"], [[0,0,-1]]],
		["deck", "空节", ["0,0,0:bridge", "0,0,-1:deck"], [[0,0,-1]]],
		["long_sail", "双桅帆", ["0,0,0:bridge", "0,0,-2:long_sail"], [[0,0,-2], [0,0,-1]]],
		["tall_hold", "双层货舱", ["0,0,0:bridge", "0,0,-1:tall_hold"], [[0,0,-1], [0,1,-1]]],
		["wide_deck", "宽空节", ["0,0,0:bridge", "1,0,0:deck", "0,0,-1:wide_deck"], [[0,0,-1], [1,0,-1]]],
	]
	for spec in specs:
		var ship_scene: PackedScene = load("res://ship.tscn")
		var ship: Node3D = ship_scene.instantiate()
		ship.set("layout", PackedStringArray(spec[2]))
		root.add_child(ship)
		ship.set_physics_process(false)
		ship.set_process_unhandled_input(false)
		if ship.call("get_layout").size() != spec[2].size():
			failures.append("现有 DLL 不接受船模块布局：" + spec[0])
			ship.free()
			continue
		var prefixes: Array = []
		for cell in spec[3]:
			prefixes.append("M%d_%d_%d_" % cell)
		prefixes.append("N%d_%d_%d_" % spec[3][0])
		var row := {"id": "module:" + spec[0], "name": spec[1], "usage": "ship-module", "collection": "modules", "recordIds": [spec[0]], "itemIds": [], "source": "game/bin/zhouzhu.windows.template_debug.x86_64.dll", "notes": "从现有游戏 DLL 的真实模块节点导出，使用合法参考布局；不包含整船共用船首与连接梯子。", "layout": spec[2]}
		_save(row, ship.get_node("Visual"), prefixes)
		ship.free()

func _run() -> void:
	if output_dir.is_empty():
		push_error("需要 --output= 目录")
		quit(2)
		return
	DirAccess.make_dir_recursive_absolute(output_dir)
	if ClassDB.class_exists("ContentStudioModels"):
		var bridge: Node3D = ClassDB.instantiate("ContentStudioModels")
		root.add_child(bridge)
		var rows: Array = bridge.call("generate")
		for row in rows:
			row.itemIds = [row.item] if not str(row.item).is_empty() else []
			row.collection = "items"
			row.recordIds = row.itemIds.duplicate()
			row.notes = "直接调用当前游戏 C++ 网格生成函数；散布保留原始模型及确定性实例色，不包含季节、风、积雪与世界曲率。" if row.usage == "gather-source" else "直接调用当前游戏放置实物的 C++ 生成函数；以平地零垫高导出。"
			_save(row, row.node)
		bridge.free()
	else:
		failures.append("工具专用模型导出扩展未加载")
	_export_ship_modules()
	var file := FileAccess.open(output_dir.path_join("godot-manifest.json"), FileAccess.WRITE)
	if file != null:
		file.store_string(JSON.stringify({"schemaVersion": 1, "models": exported, "failures": failures}))
		file.close()
	print("content_studio_model_export: models=%d failures=%d" % [exported.size(), failures.size()])
	for failure in failures:
		push_error(failure)
	quit(0 if failures.is_empty() else 1)
