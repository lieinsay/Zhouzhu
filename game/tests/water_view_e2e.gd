extends SceneTree
## 水面效果对照截图（复核稿 W0/W1a 验收）：按真实开局（6068、第85天、出生点）进世界，钟停住，
## 在出生点旁的主干河（stem285 一带）取岸边平视、斜俯、远眺三张；有湖塘时再取湖边两张。
## 开局那一刻（第85天清晨、下雨）拍一组 start；再沿真实时间序列往后找头一个白天无降水的时刻拍一组 clear
## （水情跟着那天的逐日指数走，不固定流量）。
## 要真渲染：不加 --headless，窗口放屏幕外，外面套 timeout。
##   godot --path game --position -4000,-4000 --resolution 1600x900 -s res://tests/water_view_e2e.gd -- --out=<目录> --tag=after

var out_dir := ""
var tag := "shot"
var only := "" # 调试用：只拍名字里含这段的

func _initialize() -> void:
	call_deferred("_run")

func require(ok: bool, message: String) -> void:
	if not ok:
		push_error("water_view_e2e: " + message)
		quit(1)
		assert(ok, message)

func shoot(camera: Camera3D, eye: Vector3, target: Vector3, name_: String) -> void:
	if not only.is_empty() and not name_.contains(only): return
	camera.global_position = eye
	camera.look_at(target, Vector3.UP)
	await create_timer(2.5).timeout
	await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	var path := out_dir.path_join("%s-%s.png" % [name_, tag])
	require(image != null and image.save_png(path) == OK, "保存 " + path)
	print("water_view_e2e: shot %s eye=%s target=%s" % [path, eye, target])

func above_ground(world, p: Vector3, clearance: float) -> Vector3:
	return Vector3(p.x, maxf(p.y, world.get_surface_height(p.x, p.z) + clearance), p.z)

func hide_overlays(node: Node) -> void:
	for child in node.get_children():
		if child is CanvasLayer: child.visible = false
		hide_overlays(child)

func shoot_set(world, camera: Camera3D, river: Dictionary, spawn: Vector3, moment: String) -> void:
	var state: Dictionary = world.get_river_at(river.x, river.z)
	print("water_view_e2e: %s state=%s weather=%s" % [moment, state, weather_text()])
	var r := Vector3(river.x, float(state.level), river.z)
	var d := Vector3(river.dir_x, 0, river.dir_z).normalized()
	var n := Vector3(-d.z, 0, d.x)
	var half: float = 0.5 * float(state.width)
	await shoot(camera, above_ground(world, r + n * (half + 10.0), 1.7), r + d * 30.0 - n * 2.0, "river-bank-" + moment)
	await shoot(camera, r + n * 3.0 + Vector3.UP * 14.0, r - n * 1.0 + d * 0.5, "river-top-" + moment)
	await shoot(camera, above_ground(world, r + n * 60.0 - d * 40.0 + Vector3.UP * 35.0, 20.0), r + d * 20.0, "river-oblique-" + moment)
	await shoot(camera, above_ground(world, r - d * 260.0 + n * 90.0 + Vector3.UP * 120.0, 60.0), r + d * 160.0, "river-far-" + moment)
	if world.has_method("find_lake_near"):
		var lake: Dictionary = world.find_lake_near(spawn.x, spawn.z, 20000)
		if not lake.is_empty():
			var c := Vector3(lake.x, float(lake.level), lake.z)
			var reach: float = maxf(40.0, float(lake.radius))
			await shoot(camera, above_ground(world, c + Vector3(reach * 1.2, reach * 0.45, reach * 0.8), 15.0), c, "lake-oblique-" + moment)
			var shore := above_ground(world, c + Vector3(reach * 0.9, 0, reach * 0.3), 1.7)
			shore.y = maxf(shore.y, c.y + 1.7)
			await shoot(camera, shore, c, "lake-shore-" + moment)

func weather_text() -> String:
	var atmosphere = current_scene.get_node_or_null("Atmosphere")
	if atmosphere == null: return "{}"
	var w: Dictionary = atmosphere.get_weather()
	return "{kind=%s precip_mm_h=%.2f fog=%.2f cloud=%.2f local_hour=%.1f}" % [w.get("kind_name", ""), float(w.get("precip_mm_h", 0)), float(w.get("fog", 0)), float(w.get("cloud", 0)), float(w.get("local_hour", 0))]

func _run() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--out="): out_dir = arg.substr(6)
		elif arg.begins_with("--tag="): tag = arg.substr(6)
		elif arg.begins_with("--only="): only = arg.substr(7)
	require(not out_dir.is_empty() and DirAccess.make_dir_recursive_absolute(out_dir) == OK, "缺 --out 或目录建不了")
	require(DisplayServer.get_name() != "headless", "截图要真渲染，不能 --headless")
	require(change_scene_to_file(ProjectSettings.get_setting("application/run/main_scene")) == OK, "项目默认启动场景加载失败")
	var deadline: int = Time.get_ticks_msec() + 120000
	while (current_scene == null or not current_scene is Expedition) and Time.get_ticks_msec() < deadline:
		await process_frame
	var main = current_scene
	require(main is Expedition, "项目默认启动没有进入真实世界")
	var world = main.get_node("World")
	var player = main.get_node("Player")
	var clock = root.get_node("GameClock")
	clock.set_paused(true)
	for i in range(600):
		await physics_frame
		if player.is_on_floor() and world.is_ready_at(player.global_position): break
	require(world.is_loaded() and world.get_node_id() == 6068, "新开局没有进入6068")
	var spawn: Vector3 = player.global_position
	var river: Dictionary = world.find_river_near(spawn.x, spawn.z, 1000, 30)
	require(not river.is_empty(), "出生附近缺少主干河")
	var state: Dictionary = world.get_river_at(river.x, river.z)
	print("water_view_e2e: day=%d hour=%.2f spawn=%s river=%s state=%s" % [int(clock.get_date().day_of_year), float(clock.get_date().get("hour", -1.0)), spawn, river, state])
	hide_overlays(root)
	var camera := Camera3D.new()
	camera.fov = 60.0
	camera.far = 40000.0
	main.add_child(camera)
	camera.current = true
	await shoot_set(world, camera, river, spawn, "start")
	# 沿真实时间序列往后找头一个白天无降水、少雾的时刻
	var atmosphere = main.get_node("Atmosphere")
	var start_day := int(clock.get_date().day_of_year)
	var found := false
	for day in range(start_day, start_day + 30):
		for hour in [10.0, 13.0, 16.0]:
			if day == start_day and hour <= 7.5: continue
			clock.set_date(day, hour)
			atmosphere.snap()
			await process_frame
			await process_frame
			var w: Dictionary = atmosphere.get_weather()
			if float(w.get("precip_mm_h", 1.0)) < 0.05 and float(w.get("fog", 1.0)) < 0.3 and not bool(w.get("night", true)):
				found = true
				print("water_view_e2e: clear moment day=%d hour=%.1f" % [day, hour])
				break
		if found: break
	if found:
		atmosphere.snap()
		await shoot_set(world, camera, river, spawn, "clear")
	else:
		print("water_view_e2e: 30 天内没找到白天无降水的时刻，只拍了开局")
	print("water_view_e2e: PASS tag=%s" % tag)
	quit(0)
