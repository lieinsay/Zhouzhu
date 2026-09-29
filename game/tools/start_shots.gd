extends Node
## 开局那群的一组地面截图（地貌与聚落计划 P0 起：开局配置 src/story_start.h，StoryStart 的出生群、开局日；不做断言，只存图）：
##   1_dock   出生码头一带：岸外 150 m、比码头高 90 m，看码头与邑治之间（码头上的船在画面下沿）；
##   2_seat   邑治（world.json 里 seat 的那一村）：村心朝太阳那边 480 m、离地 90 m 看村心；
##   3_river  主码头附近的一条大河（World.find_river_near：主码头 8 km 内汇水 200 / 100 / 50 / 20 km² 起、先找到哪档用哪档、离码头最近的一条，
##            再往上游走一段，汇水别掉到六成以下）：那一点的上游 650 m、比水面高 170 m 顺流看；
##   4_over_dock 码头上空望主岛：岸外 3 km、比码头高 1.4 km，看主岛的陆地重心；
##   5_high   高处看整群：从陆地重心往太阳那边一个半径（九成陆地在这个半径里）、高 0.1 个半径，看重心。
## 机位都从这一群的数据算（码头的出生位姿、world.json 的村、河网、整张栅格每 1 km 抽的陆地）；太阳在上午的那一边（北半球东南、南半球东北）放相机、顺光看。
## 钟停在开局那天（--day 改）的 --hour 点（缺省 10 点）；天气照那天的，--clear 覆盖成晴。人开 god，跟在相机后面 40 m（流式焦点就是人）；截图藏起 HUD，读数打在日志里。
## 直接实例化 world_seed42.tscn（测试、工具的做法：没有存档目录，只有仓库里的包），就是开局那群。
##
##   Godot --path game --fixed-fps 60 res://tools/start_shots.tscn -- --shots=<目录> [--day=85] [--hour=10] [--clear] [--view=1_dock,3_river]

const SCENE := preload("res://world_seed42.tscn")
const WAIT_MIN_MS := 7000
const WAIT_MAX_MS := 45000
const TIMEOUT_MS := 900000
const RIVER_RADIUS := 8000.0
const RIVER_MIN_KM2 := [200.0, 100.0, 50.0, 20.0]  # 先找大的

var shots_dir := ""
var day := -1
var hour := 10.0
var clear := false
var pick := ""
var main: Expedition
var world: World
var player: Player
var atmosphere: Atmosphere
var far_groups = null
var camera: Camera3D
var views: Array = []  # [名字, 人, 相机, 看向, 视角]
var index := -1
var entered_ms := 0
var loaded_ms := 0
var busy := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
		elif arg.begins_with("--day="):
			day = int(arg.trim_prefix("--day="))
		elif arg.begins_with("--hour="):
			hour = float(arg.trim_prefix("--hour="))
		elif arg == "--clear":
			clear = true
		elif arg.begins_with("--view="):
			pick = arg.trim_prefix("--view=")
	if shots_dir.is_empty():
		shots_dir = OS.get_user_data_dir().path_join("start_shots")
	DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1600, 900)
	var clock := get_node("/root/GameClock")
	if day >= 0:
		clock.set_calendar_start(day)
	day = clock.get_calendar_start()
	clock.restart(hour)
	clock.set_paused(true)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	atmosphere = main.get_node("Atmosphere")
	atmosphere.override_fade = 0.05
	far_groups = main.get_node_or_null("FarGroups")
	camera = Camera3D.new()
	camera.far = 200000.0
	camera.fov = 60.0
	add_child(camera)


func _ground(x: float, z: float, fallback: float) -> float:
	var h := world.get_surface_height(x, z)
	return fallback if is_nan(h) else h


# 相机离地至少 p_clear 米（沿相机到看点的连线上也抽几处，别让山挡住）
func _lift(cam: Vector3, look: Vector3, p_clear: float) -> Vector3:
	var c := cam
	c.y = maxf(c.y, _ground(c.x, c.z, -1e9) + p_clear)
	for k in range(1, 6):
		var t := k / 8.0
		var q := c.lerp(look, t)
		var g := _ground(q.x, q.z, -1e9)
		if g + 15.0 > q.y:
			c.y += (g + 15.0 - q.y) / (1.0 - t)
	return c


func _add(v_name: String, cam: Vector3, look: Vector3, fov := 60.0) -> void:
	if not pick.is_empty() and not (v_name in pick.split(",")):
		return
	# 人在相机后面 40 m、同高（不入画）
	var back := cam - look
	back.y = 0.0
	var stand := cam + back.normalized() * 40.0
	views.append([v_name, stand, cam, look, fov])


func _plan() -> void:
	var info: Dictionary = world.get_info()
	var south := float(info["source"]["lat"]) < 0.0
	var sun := Vector3(0.7071, 0.0, -0.7071 if south else 0.7071)  # 上午太阳那一边（x 东、z 南）
	var di := world.find_dock(main.origin_dock)
	var dock := world.get_dock_position(di)
	var spawn: Transform3D = world.get_dock_spawn(di)
	var out := -spawn.basis.z  # 出生位姿面朝栈桥 = 朝外
	out.y = 0.0
	out = out.normalized()
	var side := Vector3(-out.z, 0.0, out.x)
	if side.dot(sun) < 0.0:
		side = -side
	var seat := dock
	var seat_name := ""
	for v in info["villages"]:
		if bool(v.get("seat", false)):
			seat = Vector3(float(v["pos"][0]), 0.0, float(v["pos"][1]))
			seat_name = String(v["name"])
	seat.y = _ground(seat.x, seat.z, dock.y)
	print("start_shots: 群 #%d（%s），第 %d 天 %.1f 点；主码头 #%d 在 %s，朝外 %s；邑治 %s 在 %s（离码头 %.1f km）" % [world.get_node_id(), info["source"].get("cls_zh", ""), day, hour,
		main.origin_dock, dock, out, seat_name, seat, Vector2(seat.x - dock.x, seat.z - dock.z).length() / 1000.0])
	# 1 出生码头一带：岸外 150 m、侧 40 m、比码头高 90 m；邑治在 3 km 内就看码头与邑治之间（四成处），不然看岛里 600 m 处的地
	var inland := dock - out * 600.0
	if Vector2(seat.x - dock.x, seat.z - dock.z).length() < 3000.0:
		inland = dock.lerp(seat, 0.4)
	inland.y = _ground(inland.x, inland.z, dock.y)
	_add("1_dock", _lift(dock + out * 150.0 + side * 40.0 + Vector3(0, 90, 0), inland, 20.0), inland)
	# 2 邑治：村心朝太阳那边 480 m、离地 90 m 看村心
	var scam := seat + sun * 480.0
	scam.y = _ground(scam.x, scam.z, seat.y) + 90.0
	_add("2_seat", _lift(scam, seat, 60.0), seat)
	# 3 大河：主码头 8 km 内先找汇水大的（200 → 20 km²），离码头最近的一条；顺着往上游走 1.5 km（每 250 m 贴回河心），汇水别掉到六成以下
	var r := {}
	for km2 in RIVER_MIN_KM2:
		r = world.find_river_near(dock.x, dock.z, RIVER_RADIUS, km2)
		if not r.is_empty():
			break
	if not r.is_empty():
		var p := Vector3(float(r["x"]), 0.0, float(r["z"]))
		var d := Vector3(float(r["dir_x"]), 0.0, float(r["dir_z"]))
		for k in 6:
			var q := p - d * 250.0
			var up: Dictionary = world.find_river_near(q.x, q.z, 200.0, float(r["acc_km2"]) * 0.6)
			if up.is_empty():
				break
			p = Vector3(float(up["x"]), 0.0, float(up["z"]))
			d = Vector3(float(up["dir_x"]), 0.0, float(up["dir_z"]))
		p.y = _ground(p.x, p.z, dock.y)
		var here: Dictionary = world.get_river_at(p.x, p.z)
		print("start_shots: 大河 离码头 %.1f km、汇水 %.0f km²、平岸宽 %.0f m（河口 %s）" % [Vector2(p.x - dock.x, p.z - dock.z).length() / 1000.0, float(here.get("acc_km2", r["acc_km2"])),
			float(here.get("bankfull_width", r["width"])), Vector2(float(r["mouth_x"]), float(r["mouth_z"]))])
		var perp := Vector3(-d.z, 0.0, d.x)
		if perp.dot(sun) < 0.0:
			perp = -perp
		var look := p + d * 700.0
		look.y = _ground(look.x, look.z, p.y)
		var rcam := p - d * 650.0 + perp * 260.0
		rcam.y = p.y + 170.0
		_add("3_river", _lift(rcam, look, 70.0), look)
	else:
		print("start_shots: 主码头 %.0f km 内没有汇水 %.0f km² 以上的河" % [RIVER_RADIUS / 1000.0, RIVER_MIN_KM2.back()])
	# 陆地的重心、九成陆地在多大半径内（整张岛群栅格每 1 km 抽一处）
	var g: Dictionary = info["grid"]
	var pts: Array[Vector2] = []
	var cx := 0.0
	var cz := 0.0
	for i in range(int(float(g["rows"]) * float(g["res_m"]) / 1000.0) + 1):
		for j in range(int(float(g["cols"]) * float(g["res_m"]) / 1000.0) + 1):
			var x := float(g["x0"]) + j * 1000.0
			var z := float(g["z0"]) + i * 1000.0
			if not is_nan(world.get_surface_height(x, z)):
				pts.append(Vector2(x, z))
				cx += x
				cz += z
	cx /= maxf(1.0, pts.size())
	cz /= maxf(1.0, pts.size())
	var dists: Array[float] = []
	for q in pts:
		dists.append(q.distance_to(Vector2(cx, cz)))
	dists.sort()
	var radius: float = dists[int(dists.size() * 0.9)] if not dists.is_empty() else 30000.0
	var rim := float(info["islands"][0]["rim_m"])
	var hc := Vector3(cx, rim, cz)
	print("start_shots: 陆地 %d km²（1 km 抽样）、重心 (%.0f, %.0f)、九成在 %.1f km 内" % [pts.size(), cx, cz, radius / 1000.0])
	# 4 码头上空望主岛：岸外 3 km、比码头高 1.4 km，看主岛的陆地重心
	_add("4_over_dock", dock + out * 3000.0 + Vector3(0, 1400, 0), hc.lerp(dock, 0.35))
	# 5 高处看整群：从陆地重心往太阳那边一个半径、高 0.1 个半径（云层之上），看重心
	var hcam := hc + sun * radius
	hcam.y = rim + radius * 0.1
	_add("5_high", hcam, hc)


func _process(_delta: float) -> void:
	if world == null:
		return
	var now := Time.get_ticks_msec()
	if now > TIMEOUT_MS:
		print("start_shots: 超时")
		get_tree().quit(1)
		return
	if not world.is_loaded():
		return
	if loaded_ms == 0:
		loaded_ms = now
		print("start_shots: 开局那群载入 %.1f s；%s" % [now / 1000.0, main.get_status_text().get_slice("\n", 0)])
		return
	if views.is_empty():
		if now - loaded_ms < 3000:
			return
		_plan()
		player.set_god_mode(true)
		_hide_hud()
		if views.is_empty():
			print("start_shots: 没有机位")
			get_tree().quit(1)
			return
		index = 0
		_enter()
		return
	if busy or index >= views.size():
		return
	var v: Array = views[index]
	player.global_position = v[1]
	camera.global_position = v[2]
	camera.look_at(v[3], Vector3.UP)
	var waited := now - entered_ms
	var far_busy: bool = far_groups != null and not far_groups.is_idle()
	if waited < WAIT_MIN_MS or ((world.get_pending_count() > 0 or far_busy) and waited < WAIT_MAX_MS):
		return
	busy = true
	await get_tree().process_frame
	await get_tree().process_frame
	await get_tree().process_frame
	var path := shots_dir.path_join("%d_%s.png" % [world.get_node_id(), v[0]])
	get_viewport().get_texture().get_image().save_png(path)
	var w: Dictionary = atmosphere.get_weather()
	var clock := get_node("/root/GameClock")
	var date: Dictionary = clock.get_date()
	print("start_shots: %s  相机 %s → %s  等了 %.1f s（未完 %d）  %s %s %s  天气 %s（当天 %s）%.1f °C  太阳高 %.0f°  %s" % [path, v[2], v[3], waited / 1000.0, world.get_pending_count(),
		w.get("season_name", ""), date.get("text", ""), clock.get_clock_text(), w.get("kind_name", "?"), w.get("day_kind_name", "?"), float(w.get("temp_c", 0.0)),
		float(w.get("sun_altitude", 0.0)), main.get_status_text().get_slice("\n", 0)])
	index += 1
	busy = false
	if index >= views.size():
		print("start_shots: 完成 → %s" % shots_dir)
		get_tree().quit(0)
		return
	_enter()


func _enter() -> void:
	var v: Array = views[index]
	if clear:
		atmosphere.set_weather_override("clear", 0.0)
	camera.fov = float(v[4])
	player.global_position = v[1]
	world.prepare_at(v[1])
	world.prepare_at(v[3])
	camera.global_position = v[2]
	camera.look_at(v[3], Vector3.UP)
	camera.make_current()
	atmosphere.snap()
	entered_ms = Time.get_ticks_msec()


# 截图不要 HUD（左上的读数、中间的操作提示、快捷栏）：场景里、开发者工具里的 CanvasLayer 都藏起来；读数照旧打在日志里
func _hide_hud() -> void:
	for root in [main, get_node("/root/DeveloperTools"), get_node("/root/GameMenu")]:
		for c in root.find_children("*", "CanvasLayer", true, false):
			c.visible = false
		if root is CanvasLayer:
			root.visible = false
