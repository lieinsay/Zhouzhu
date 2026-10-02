extends Node3D
## Generator landmarks, waterworks and nonresident places, streamed in group coordinates.
## Kilometre landforms (cirques, mesas, folds) already live in terrain.bin.

var world: GroupWorld
var records: Array[Dictionary] = []
var mounted: Dictionary = {}
var cursor := 0
var rock: ShaderMaterial
var wood: ShaderMaterial
var water: ShaderMaterial
var wet_rock: ShaderMaterial

func configure(source: GroupWorld) -> void:
	world = source
	rock = _material(Color(0.64, 0.63, 0.57))
	wood = _material(Color(0.39, 0.29, 0.19))
	wet_rock = _material(Color(0.23, 0.28, 0.25))
	wet_rock.set_shader_parameter("roughness", 0.45)
	water = ShaderMaterial.new()
	water.shader = load("res://shaders/spring_water.gdshader")
	var data: Dictionary = world.get_info().get("landscape", {})
	for key in ["landforms", "relays", "uses", "ruins", "specials"]:
		for item in data.get(key, []):
			if key == "specials" and item.get("occupancy", "resident") == "resident":
				continue
			if key == "landforms" and item.get("kind", "") not in ["tower_karst", "natural_arch"]:
				continue
			var rec: Dictionary = item.duplicate()
			rec["category"] = key
			rec["position"] = _km(item.get("km", [0, 0]))
			records.append(rec)
	for item in data.get("springline", []):
		if item.size() < 8:
			continue
		var pos := _cell(item[1])
		pos.y = float(item[3])
		records.append({"category": "spring", "position": pos, "flow": float(item[2]), "length": float(item[4]) * 1000.0, "kind": int(item[7])})
	var works: Dictionary = data.get("waterworks", {})
	for key in ["big_works", "heads", "canals", "ponds", "sluices"]:
		for item in works.get(key, []):
			var rec: Dictionary = item.duplicate()
			rec["category"] = key
			rec["position"] = _km(item.get("km", [0, 0])) if item.has("km") else _cell(item["cell"]) if item.has("cell") else _cell(item.get("pts", [[0, 0]])[0], true)
			records.append(rec)

func _material(color: Color) -> ShaderMaterial:
	var result := ShaderMaterial.new()
	result.shader = load("res://shaders/curved_standard.gdshader")
	result.set_shader_parameter("albedo", color)
	return result

func _km(point: Array) -> Vector3:
	return Vector3(float(point[0]) * 1000.0, 0, -float(point[1]) * 1000.0)

func _cell(point: Array, half_cell := false) -> Vector3:
	var grid: Dictionary = world.get_info()["grid"]
	# Upstream waterworks use row/col + 0.5; springline uses integer cell indices.
	return Vector3(float(grid.x0) + (float(point[1]) - (0.5 if half_cell else 0.0)) * float(grid.res_m), 0, float(grid.z0) + (float(point[0]) - (0.5 if half_cell else 0.0)) * float(grid.res_m))

func _process(_delta: float) -> void:
	if not is_instance_valid(world) or records.is_empty():
		return
	var focus: Vector3 = world.get_focus()
	for _i in range(mini(8, records.size())):
		cursor = (cursor + 1) % records.size()
		var rec: Dictionary = records[cursor]
		var pos: Vector3 = rec.position
		var distance := Vector2(pos.x - focus.x, pos.z - focus.z).length()
		if distance < 4000.0 and not mounted.has(cursor):
			mounted[cursor] = _build(rec, cursor)
			break # One construction per frame.
		if distance > 6000.0 and mounted.has(cursor):
			mounted[cursor].queue_free()
			mounted.erase(cursor)

func _box(parent: Node3D, size: Vector3, pos: Vector3, material: Material, collision := true) -> MeshInstance3D:
	var view := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	view.mesh = mesh
	view.material_override = material
	parent.add_child(view)
	view.position = pos
	if collision:
		var body := StaticBody3D.new()
		var shape := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = size
		shape.shape = box
		body.add_child(shape)
		view.add_child(body)
	return view

func _build(rec: Dictionary, index: int) -> Node3D:
	var root := Node3D.new()
	root.name = "Landscape_%d" % index
	add_child(root)
	var pos: Vector3 = rec.position
	var height: float = world.get_surface_height(pos.x, pos.z)
	root.position = Vector3(pos.x, height if is_finite(height) else pos.y, pos.z)
	match rec.category:
		"landforms":
			if rec.kind == "tower_karst":
				var radius := sqrt(float(rec.get("area_km2", 1.0)) * 1e6 / PI)
				var count := clampi(roundi(float(rec.get("density_per_km2", 10.0)) * float(rec.get("area_km2", 1.0))), 1, 256)
				var rng := RandomNumberGenerator.new()
				rng.seed = int(world.get_info()["source"].seed) * 1000003 + index
				for _j in range(count):
					var angle := rng.randf() * TAU
					var distance := sqrt(rng.randf()) * radius
					var offset := Vector3(cos(angle) * distance, 0, sin(angle) * distance)
					var y: float = world.get_surface_height(pos.x + offset.x, pos.z + offset.z)
					if not is_finite(y):
						continue
					var h := float(rec.get("tower_h_m", 60)) * rng.randf_range(0.7, 1.2)
					var pillar := MeshInstance3D.new()
					var cylinder := CylinderMesh.new()
					cylinder.height = h
					cylinder.bottom_radius = clampf(h * 0.09, 3.0, 12.0)
					cylinder.top_radius = cylinder.bottom_radius * rng.randf_range(0.45, 0.8)
					cylinder.radial_segments = 8
					pillar.mesh = cylinder
					pillar.material_override = rock
					root.add_child(pillar)
					pillar.position = offset + Vector3(0, y - root.position.y + h * 0.5 - 1.0, 0)
					pillar.create_trimesh_collision()
			else:
				var opening := maxf(4.0, float(rec.get("opening_m", 20)))
				var radius := opening * 0.5
				root.rotation.y = deg_to_rad(float(rec.get("spur_strike_deg", 0)))
				for j in range(12):
					var angle := PI * (j + 0.5) / 12.0
					var block := _box(root, Vector3(opening * 0.18, maxf(2.0, opening * 0.14), maxf(3.0, opening * 0.3)), Vector3(cos(angle) * radius, sin(angle) * radius, 0), rock)
					block.rotation.z = angle - PI * 0.5
		"spring":
			var coast := _coast(pos)
			pos.x = coast.x
			pos.z = coast.z
			root.position = pos
			var nx := float(world.get_landscape_at(pos.x - 1, pos.z).get("shore", 0)) - float(world.get_landscape_at(pos.x + 1, pos.z).get("shore", 0))
			var nz := float(world.get_landscape_at(pos.x, pos.z - 1).get("shore", 0)) - float(world.get_landscape_at(pos.x, pos.z + 1).get("shore", 0))
			root.rotation.y = atan2(nx, nz)
			if int(rec.kind) == 0:
				_box(root, Vector3(minf(float(rec.length), 100.0), 3, 0.04), Vector3(0, -1.5, 0.06), wet_rock, false)
				return root
			root.position.y = pos.y
			var sample: Dictionary = world.get_landscape_at(pos.x,pos.z)
			var h := maxf(1.0, pos.y - float(sample.get("bottom",pos.y-12.0))) if int(rec.kind)==2 else 12.0
			var width := clampf(sqrt(float(rec.flow)) * 0.5, 0.25, 10.0)
			_box(root, Vector3(width, h, 0.15), Vector3(0, -h * 0.5, 0), water, false)
		"canals":
			var pts: Array = rec.get("pts", [])
			for j in range(pts.size() - 1):
				var a := _cell(pts[j], true)
				var b := _cell(pts[j + 1], true)
				var y: float = world.get_surface_height(a.x, a.z)
				if not is_finite(y):
					continue
				var view := _box(root, Vector3(maxf(0.5, float(rec.get("width_m", 2.0))), 0.08, a.distance_to(b)), (a + b) * 0.5 - root.position + Vector3(0, y + 0.08, 0), water, false)
				view.rotation.y = atan2(b.x - a.x, b.z - a.z)
		"ponds":
			var side := sqrt(maxf(4.0, float(rec.get("area_m2", 100))))
			_box(root, Vector3(side, 0.1, side), Vector3(0, 0.08, 0), water, false)
		"big_works", "heads", "sluices":
			_box(root, Vector3(6, 2, 2), Vector3(0, 1, 0), rock)
			_box(root, Vector3(0.4, 4, 0.4), Vector3(-2, 2, 0), wood)
			_box(root, Vector3(0.4, 4, 0.4), Vector3(2, 2, 0), wood)
		"relays":
			# Resident houses come from the regular village stream and ledger.
			if rec.get("occupancy", "rotation") != "resident":
				_box(root, Vector3(8, 5, 6), Vector3(0, 2.5, 0), wood)
			var lookout := _km(rec.get("lookout_km", rec.get("km", [0, 0])))
			var y: float = world.get_surface_height(lookout.x, lookout.z)
			if is_finite(y):
				_box(root, Vector3(3, 7, 3), Vector3(lookout.x - pos.x, y - root.position.y + 3.5, lookout.z - pos.z), rock)
		"ruins":
			_box(root, Vector3(7, 0.7, 5), Vector3(0, 0.35, 0), rock)
		"specials":
			_box(root, Vector3(5, 2.5, 4), Vector3(0, 1.25, 0), wood)
		"uses":
			if rec.get("kind", "") == "shrine":
				_box(root, Vector3(4, 3, 4), Vector3(0, 1.5, 0), wood)
			elif rec.get("kind", "") == "tomb":
				_box(root, Vector3(1, 1.5, 0.4), Vector3(0, 0.75, 0), rock)
	return root

func _coast(pos: Vector3) -> Vector3:
	var grid: Dictionary = world.get_info()["grid"]
	var start := float(world.get_landscape_at(pos.x, pos.z).get("shore", 0))
	var best := pos
	var distance := INF
	for j in range(16):
		var direction := Vector3(cos(j * TAU / 16), 0, sin(j * TAU / 16))
		var end := pos + direction * float(grid.res_m) * 2
		var edge := float(world.get_landscape_at(end.x, end.z).get("shore", 0))
		if (start > 0) == (edge > 0):
			continue
		var lo := pos
		var hi := end
		for _k in range(16):
			var mid := (lo + hi) * 0.5
			if (float(world.get_landscape_at(mid.x, mid.z).get("shore", 0)) > 0) == (start > 0):
				lo = mid
			else:
				hi = mid
		var at := (lo + hi) * 0.5
		if pos.distance_squared_to(at) < distance:
			distance = pos.distance_squared_to(at)
			best = at
	return best
