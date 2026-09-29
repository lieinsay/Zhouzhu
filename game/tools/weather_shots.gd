extends Node
## 时令与天气的目视检查（不做断言，只存图）：按日期、钟点、天气拍一串——
##   主码头望山：清早、正午、黄昏、夜里（月亮）；小雨、大雨、风暴、云海漫顶（开发者覆盖）；
##   主峰：暖季无雪、冷季积雪（雪线）、冷季暴风雪；八百米上下的阔叶林：暖季、入冷季秋色、深冬落光；高处的河：冷季封冻。
## 主峰、林子、河按地形自己找（离主码头近的）。人跟着相机挪（流式焦点就是人）。
##
##   Godot --path game --fixed-fps 60 res://tools/weather_shots.tscn -- --shots=<目录>

const SCENE := preload("res://world_seed42.tscn")
const WAIT_S := 9.0

var main: Expedition
var world: World
var player: Player
var atmosphere: Atmosphere
var camera: Camera3D
var shots_dir := ""
var views: Array = []  # [名字, 相机位置, 看向, 一年里的第几天, 钟点, 天气覆盖（"" = 自动）]
var index := -1
var wait := 0.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
	if shots_dir.is_empty():
		shots_dir = OS.get_user_data_dir().path_join("weather_shots")
	DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1600, 900)
	GameClock.restart(10.0)
	GameClock.paused = true  # 钟停着：每张按拨好的日期钟点拍
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	atmosphere = main.get_node("Atmosphere")
	atmosphere.override_fade = 0.05
	camera = Camera3D.new()
	camera.far = 60000.0
	camera.fov = 60.0
	add_child(camera)


func _ground(p: Vector2) -> float:
	var h := world.get_surface_height(p.x, p.y)
	return -1e9 if is_nan(h) else h


# 离 p_from 最近、按 p_step 格子扫得到的、满足 p_test 的点
func _find(p_from: Vector2, p_radius: float, p_step: float, p_test: Callable) -> Vector2:
	var best := Vector2.INF
	var best_d := INF
	var n := int(p_radius / p_step)
	for i in range(-n, n + 1):
		for j in range(-n, n + 1):
			var p := p_from + Vector2(i, j) * p_step
			var d := p.distance_to(p_from)
			if d < best_d and d <= p_radius and p_test.call(p):
				best_d = d
				best = p
	return best


# 八百米上下、林地为主
func _is_forest(p: Vector2) -> bool:
	var h := _ground(p)
	if h < 650.0 or h > 950.0:
		return false
	var c: Dictionary = world.get_cover_at(p.x, p.y)
	return not c.is_empty() and int(c["dominant"]) == 4


# 一千二百米以上、200 m 内有河
func _is_high_river(p: Vector2) -> bool:
	if _ground(p) < 1200.0:
		return false
	return not world.find_river_near(p.x, p.y, 200.0, 1.0).is_empty()


# 季节按半球换：日子写成南半球的（原来的开局 #2051 在南纬，第 0–83 天是暖季一），北半球的群往后挪半年（168 天）
func _season_day(p_south_day: int) -> int:
	var lat := float(world.get_info()["source"]["lat"])
	return p_south_day if lat < 0.0 else (p_south_day + 168) % 336


func _plan() -> void:
	var start := StoryStart.get_calendar_start()  # 主码头那几张拍开局那天
	var spawn: Transform3D = world.get_dock_spawn(world.find_dock(main.origin_dock))
	var dock := Vector2(spawn.origin.x, spawn.origin.z)
	# 主峰：主码头 40 km 内最高的一点
	var peak := dock
	var peak_h := -1e9
	for i in range(-40, 41):
		for j in range(-40, 41):
			var p := dock + Vector2(i, j) * 1000.0
			var h := _ground(p)
			if h > peak_h:
				peak_h = h
				peak = p
	for k in 3:  # 在最高的那一格附近再细找两回
		var step := 300.0 / pow(3.0, k)
		var c := peak
		for i in range(-4, 5):
			for j in range(-4, 5):
				var p := c + Vector2(i, j) * step
				var h := _ground(p)
				if h > peak_h:
					peak_h = h
					peak = p
	var peak3 := Vector3(peak.x, peak_h, peak.y)
	var toward_peak := (peak - dock).normalized()
	var eye := Vector3(dock.x, _ground(dock) + 40.0, dock.y)
	var far_look := Vector3(peak.x, peak_h * 0.6, peak.y)
	views.append(["dock_morning", eye, far_look, start, 7.0, ""])
	views.append(["dock_noon", eye, far_look, start, 12.5, ""])
	views.append(["dock_dusk", eye, eye + Vector3(-1000.0, 40.0, 0.0), start, 18.9, ""])
	views.append(["dock_night", eye, far_look, 14, 23.5, ""])  # 第 14 天：满月前后（月相全行星一样，不按半球挪）
	views.append(["dock_rain", eye, far_look, start, 12.5, "rain"])
	views.append(["dock_heavy_rain", eye, far_look, start, 12.5, "heavy_rain"])
	views.append(["dock_storm", eye, far_look, start, 15.0, "storm"])
	var high := eye + Vector3(-toward_peak.x * 600.0, 520.0, -toward_peak.y * 600.0)
	views.append(["sea_fog_from_above", high, Vector3(dock.x, 200.0, dock.y) + Vector3(toward_peak.x, 0, toward_peak.y) * 3000.0, start, 9.0, "sea_fog"])
	# 主峰：从 3 km 外、比峰低四百米看
	var peak_eye2 := peak - toward_peak * 3000.0
	var peak_eye := Vector3(peak_eye2.x, peak_h - 300.0, peak_eye2.y)
	views.append(["peak_warm", peak_eye, peak3, _season_day(20), 12.0, ""])
	views.append(["peak_winter", peak_eye, peak3, _season_day(200), 12.0, ""])
	views.append(["peak_late_winter", peak_eye, peak3, _season_day(236), 12.0, ""])
	views.append(["peak_blizzard", peak_eye, peak3, _season_day(200), 12.0, "blizzard"])
	# 八百米上下的林子（林地为主）
	var forest := _find(dock, 20000.0, 400.0, _is_forest)
	if forest != Vector2.INF:
		var fh := _ground(forest)
		var f_eye := Vector3(forest.x - 60.0, fh + 25.0, forest.y - 40.0)
		var f_look := Vector3(forest.x, fh + 4.0, forest.y)
		views.append(["forest_warm", f_eye, f_look, _season_day(20), 12.0, ""])
		views.append(["forest_autumn", f_eye, f_look, _season_day(128), 12.0, ""])
		views.append(["forest_winter", f_eye, f_look, _season_day(180), 12.0, ""])
		views.append(["forest_spring", f_eye, f_look, _season_day(262), 12.0, ""])
	# 高处的河（1200 m 以上）
	var river_at := _find(peak, 12000.0, 300.0, _is_high_river)
	if river_at != Vector2.INF:
		var r: Dictionary = world.find_river_near(river_at.x, river_at.y, 200.0, 1.0)
		var rp := Vector2(r["x"], r["z"])
		var rh := _ground(rp)
		var dir := Vector2(float(r["dir_x"]), float(r["dir_z"]))
		var back := rp - dir * 60.0
		var r_eye := Vector3(back.x, maxf(_ground(back), rh) + 30.0, back.y)
		views.append(["river_high_warm", r_eye, Vector3(rp.x, rh, rp.y), _season_day(20), 12.0, ""])
		views.append(["river_high_frozen", r_eye, Vector3(rp.x, rh, rp.y), _season_day(174), 12.0, ""])
	print("weather_shots: 主峰 %.0f m %s；林子 %s；高处的河 %s；共 %d 张" % [peak_h, peak, forest, river_at, views.size()])


func _apply(v: Array) -> void:
	GameClock.set_date(int(v[3]), float(v[4]))
	if String(v[5]).is_empty():
		atmosphere.clear_weather_override()
	else:
		atmosphere.set_weather_override(String(v[5]), 0.0)
	player.global_position = v[1] - Vector3(0, 6, 0)  # 人在相机下面：相机别钻进人的模型里
	world.prepare_at(v[1])
	camera.global_position = v[1]
	camera.look_at(v[2])
	camera.current = true


func _process(delta: float) -> void:
	if not world.is_loaded():
		return
	if views.is_empty():
		_plan()
		player.set_god_mode(true)
	wait -= delta
	if index >= 0 and wait > 0.0:
		player.global_position = views[index][1] - Vector3(0, 6, 0)
		camera.global_position = views[index][1]
		camera.look_at(views[index][2])
		return
	if index >= 0:
		var path := shots_dir.path_join(views[index][0] + ".png")
		get_viewport().get_texture().get_image().save_png(path)
		var w: Dictionary = atmosphere.get_weather()
		print("weather_shots: %s（%s %s %.1f 点：%s，雪线 %.0f m，太阳高 %.0f°）" % [path, w["season_name"], w["date"], float(views[index][4]), w["kind_name"], w["snow_line"], w["sun_altitude"]])
	index += 1
	if index >= views.size():
		get_tree().quit(0)
		return
	_apply(views[index])
	wait = WAIT_S
