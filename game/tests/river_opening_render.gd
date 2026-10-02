extends Node
## GPU regression for the opening brook, through the same monthly clock action as F6.
## Godot --path game res://tests/river_opening_render.tscn -- --shots=<directory>
## Requires a rendering device. Does not load or write a player save.

const SCENE := preload("res://world_seed42.tscn")
var shots := ""
var main: Expedition
var world: World
var camera: Camera3D
var water: Array[MeshInstance3D] = []

func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="): shots = arg.trim_prefix("--shots=")
	if not shots.is_empty(): DirAccess.make_dir_recursive_absolute(shots)
	get_tree().root.size = Vector2i(1280, 720)
	GameClock.restart(10.0)
	GameClock.set_paused(true)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	var deadline := Time.get_ticks_msec() + 60000
	while not world.is_loaded():
		if Time.get_ticks_msec() > deadline:
			_fail("world load timeout")
			return
		await get_tree().process_frame
	var spawn := main.get_spawn_point()
	var river: Dictionary = world.find_river_near(spawn.x, spawn.z, 3000.0, 1.0)
	if river.is_empty():
		_fail("no opening brook")
		return
	var p := Vector3(river.x, 0, river.z)
	p.y = world.get_surface_height(p.x, p.z)
	var flow := Vector3(river.dir_x, 0, river.dir_z)
	var side := Vector3(flow.z, 0, -flow.x)
	var cam := p - flow * 12.0 + side * 4.0
	cam.y = world.get_surface_height(cam.x, cam.z) + 2.0
	var look := p + flow * 12.0
	look.y = world.get_surface_height(look.x, look.z) + 0.1
	var player: Player = main.get_node("Player")
	player.set_god_mode(true)
	player.global_position = cam
	player.set_physics_process(false)
	world.prepare_at(cam)
	world.prepare_at(p)
	camera = Camera3D.new()
	camera.far = 200000.0
	camera.fov = 60.0
	add_child(camera)
	camera.global_position = cam
	camera.look_at(look)
	camera.make_current()
	for root in [main, get_node("/root/DeveloperTools"), get_node("/root/GameMenu")]:
		for layer in root.find_children("*", "CanvasLayer", true, false): layer.visible = false
	var atmosphere: Atmosphere = main.get_node("Atmosphere")
	atmosphere.set_weather_override("clear", 0)
	atmosphere.snap()
	var settle := Time.get_ticks_msec()
	while Time.get_ticks_msec() - settle < 7000 or world.get_pending_count() > 0:
		if Time.get_ticks_msec() > deadline:
			_fail("terrain load timeout")
			return
		await get_tree().process_frame
	for child in world.get_children():
		if child is MeshInstance3D and String(child.name).begins_with("River_"):
			water.append(child)
	if water.is_empty():
		_fail("no rendered river chunks")
		return
	var original: ShaderMaterial = water[0].mesh.surface_get_material(0)
	var marker := Shader.new()
	var code := original.shader.code
	# Keep production vertex/stage/discard logic and depth testing. Mark only surviving
	# fragments to distinguish real water pixels from the differently coloured riverbed.
	var last := code.rfind("}")
	marker.code = code.substr(0, last) + "\nALBEDO = vec3(1.0, 0.0, 1.0); EMISSION = vec3(1.0, 0.0, 1.0); ALPHA = 1.0;\n}" + code.substr(last + 1)
	print("river_opening_render: source=", world.get_data_dir(), " point=", p, " bankfull_width=", river.width)
	var wet_views := 0
	var dry_views := 0
	for month in 12:
		if month > 0: get_node("/root/DeveloperTools").shift_days(28)
		for frame in 4: await get_tree().process_frame
		var state: Dictionary = world.get_river_at(p.x, p.z)
		var day: float = GameClock.get_calendar_day()
		if absf(float(original.get_shader_parameter("calendar_day")) - day) > 0.001:
			_fail("GPU calendar did not follow the F6 monthly action")
			return
		if not state.get("temporary_flow", false):
			_fail("opening path lost its temporary-flow classification")
			return
		if bool(state.wet): wet_views += 1
		else: dry_views += 1
		await RenderingServer.frame_post_draw
		if not shots.is_empty():
			get_viewport().get_texture().get_image().save_png(shots.path_join("month_%02d.png" % month))
		var diagnostic := original.duplicate() as ShaderMaterial
		diagnostic.shader = marker
		for node in water: node.material_override = diagnostic
		for frame in 3: await get_tree().process_frame
		await RenderingServer.frame_post_draw
		var img := get_viewport().get_texture().get_image()
		var center := camera.unproject_position(Vector3(p.x, float(state.level), p.z))
		var visible := 0
		for y in range(maxi(0, int(center.y) - 60), mini(img.get_height(), int(center.y) + 60)):
			for x in range(maxi(0, int(center.x) - 90), mini(img.get_width(), int(center.x) + 90)):
				var color := img.get_pixel(x, y)
				# Tone mapping lifts the marker's green channel; test chroma, not raw shader RGB.
				if minf(color.r, color.b) - color.g > 0.16: visible += 1
		for node in water: node.material_override = null
		print("river_opening_render: month=%d day=%.3f depth=%.4f width=%.3f water_pixels=%d center=%s" % [month, day, state.depth, state.width, visible, center])
		if (bool(state.wet) and visible < 12) or (not bool(state.wet) and visible > 2):
			if not shots.is_empty(): img.save_png(shots.path_join("missing_water_mask.png"))
			_fail("rendered water disagrees with source wet/dry state")
			return
	if wet_views == 0 or dry_views == 0:
		_fail("monthly regression must cover both wet and dry days")
		return
	print("river_opening_render: PASS %d wet and %d dry monthly views match source" % [wet_views,dry_views])
	get_tree().quit(0)

func _fail(message: String) -> void:
	push_error("river_opening_render: FAIL " + message)
	get_tree().quit(1)
