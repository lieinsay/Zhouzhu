extends Node
## 行星计划 P1 的目视检查（不做断言，只存图）：地平线外——曲率与邻近岛群。
##   远：主码头与主峰上望 205 km 外的 2049 群（山头露出地平线；主码头往西被本群的主岛挡着，另拍一张往北北西望 235 km 外的 2278）；主码头上空 5 / 20 / 50 km 各一张（周围一圈岛群）；
##       20 km 高空的清早与黄昏各一张（朝西望 2049、朝北望 2278：远处岛群的受光方向）。
##   近：主码头站着（望栈桥、望岸上）、邑治 350 m 与 1.8 km、八百米的林子——2 km 以内，改前改后对照（画面弯曲在 2 km 处只降 0.3 m）。
## 钟停在开局那天（暖季一 · 一月初四）、天气覆盖成晴，每张一样的条件。人跟着相机挪（流式焦点就是人），人开 god。
## `--view=<名字>` 只拍那一张。没有 PlanetView 的场景（改前）也能跑：远处那几张照固定的方位拍（改前远平面 60 km，看不见邻群）。
##
##   Godot --path game --fixed-fps 60 res://tools/planet_shots.tscn -- --shots=<目录> [--only=near|far]

const SCENE := preload("res://world_2051.tscn")
const WAIT_S := 9.0
const FAR_WAIT_MAX_S := 40.0  # 远处的岛群（后台建低模）最多等这么久
const DAY := 3
const TARGETS := {2049: 266.4, 2278: 338.6}  # 节点 → 从 2051 看的方位（改前没有 PlanetView 时用）

var main: Expedition
var world: World
var player: Player
var atmosphere: Atmosphere
var planet = null  # PlanetView（改前没有）
var far_groups = null  # FarGroups（改前没有）
var camera: Camera3D
var shots_dir := ""
var only := ""
var pick := ""  # --view=<名字>：只拍这一张（调试）
var views: Array = []  # [名字, 相机位置, 看向（Vector3 或 [节点, 俯角]）, 钟点, 远处的要不要等, 一年里的第几天（可省）, 视角（可省）]
var index := -1
var wait := 0.0
var waited := 0.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
		elif arg.begins_with("--only="):
			only = arg.trim_prefix("--only=")
		elif arg.begins_with("--view="):
			pick = arg.trim_prefix("--view=")
	if shots_dir.is_empty():
		shots_dir = OS.get_user_data_dir().path_join("planet_shots")
	DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1600, 900)
	GameClock.restart(10.0)
	GameClock.paused = true
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	atmosphere = main.get_node("Atmosphere")
	atmosphere.override_fade = 0.05
	planet = main.get_node_or_null("PlanetView")
	far_groups = main.get_node_or_null("FarGroups")
	camera = Camera3D.new()
	camera.far = 60000.0
	camera.fov = 60.0
	add_child(camera)


func _ground(p: Vector2) -> float:
	var h := world.get_surface_height(p.x, p.y)
	return -1e9 if is_nan(h) else h


func _dir(bearing_deg: float) -> Vector3:
	var a := deg_to_rad(bearing_deg)
	return Vector3(sin(a), 0.0, -cos(a))


func _plan() -> void:
	var spawn: Transform3D = world.get_dock_spawn(world.find_dock(1))
	var dock := Vector2(spawn.origin.x, spawn.origin.z)
	# 主峰：全岛群最高的一点（1 km 格粗找，再细找）
	var peak := dock
	var peak_h := -1e9
	for i in range(-60, 61):
		for j in range(-60, 61):
			var p := Vector2(j, i) * 1000.0
			var h := _ground(p)
			if h > peak_h:
				peak_h = h
				peak = p
	for k in 3:
		var step := 300.0 / pow(3.0, k)
		var c := peak
		for i in range(-4, 5):
			for j in range(-4, 5):
				var p := c + Vector2(i, j) * step
				var h := _ground(p)
				if h > peak_h:
					peak_h = h
					peak = p
	var dock_eye := Vector3(dock.x, _ground(dock) + 40.0, dock.y)
	var peak_eye := Vector3(peak.x, peak_h + 30.0, peak.y)
	print("planet_shots: 主码头 %s（地面 %.0f m）；主峰 %.0f m %s" % [dock, _ground(dock), peak_h, peak])
	if only != "near":
		views.append(["far_dock_2049", dock_eye, [2049, -0.3], 10.0, true])
		views.append(["far_dock_2278", dock_eye, [2278, -0.3], 10.0, true])  # 主码头在北岸：往西望 2049 被本群的主岛挡着，往北北西也擦着岸
		views.append(["far_dock_high_2278", dock_eye + Vector3(0, 400, 0), [2278, -0.3], 10.0, true])  # 船刚升空：码头上空 400 m
		views.append(["far_dock_high_2278_zoom", dock_eye + Vector3(0, 400, 0), [2278, 0.0], 10.0, true, DAY, 6.0])
		views.append(["far_peak_2049", peak_eye, [2049, -0.3], 10.0, true])
		for km in [5, 20, 50]:
			var eye := Vector3(dock.x, float(km) * 1000.0, dock.y)
			views.append(["far_alt_%dkm" % km, eye, [2049, -8.0 - float(km) * 0.2], 10.0, true])
		var high := Vector3(dock.x, 20000.0, dock.y)
		views.append(["far_dawn_west", high, [2049, -3.0], 5.9, true])
		views.append(["far_dusk_west", high, [2049, -3.0], 18.2, true])
		views.append(["far_dawn_north", high, [2278, -3.0], 5.9, true])
		views.append(["far_dusk_north", high, [2278, -3.0], 18.2, true])
		# 拉近（视角 6°）看受光：清早太阳在东，朝东的坡与崖亮；黄昏太阳在西，朝西的亮
		views.append(["far_dawn_north_zoom", high, [2278, 0.0], 5.9, true, DAY, 6.0])
		views.append(["far_dusk_north_zoom", high, [2278, 0.0], 18.2, true, DAY, 6.0])
		views.append(["far_dawn_west_zoom", high, [2049, 0.0], 5.9, true, DAY, 8.0])
		views.append(["far_dusk_west_zoom", high, [2049, 0.0], 18.2, true, DAY, 8.0])
		# 深冬（第 180 天）：各群按自己的纬度与气候积雪（2049 的主峰 2.2 km）
		views.append(["far_winter_20km", high, [2049, -12.0], 12.0, true, 180])
		views.append(["far_winter_west_zoom", high, [2049, 0.0], 12.0, true, 180, 8.0])
	if only != "far":
		# 近：2 km 以内，改前改后对照
		var fwd := -spawn.basis.z
		var eye := spawn.origin + Vector3(0, 1.7, 0)
		views.append(["near_dock_pier", eye, eye + fwd * 100.0 + Vector3(0, -4.0, 0), 10.0, false])
		views.append(["near_dock_land", eye, eye - fwd * 100.0 + Vector3(0, 2.0, 0), 10.0, false])
		var seat := Vector2(10350, -21850)
		for d in [["near_village_350m", 350.0, 60.0], ["near_village_1800m", 1800.0, 260.0]]:
			var at := seat + Vector2(0.35, 1.0).normalized() * float(d[1])
			views.append([d[0], Vector3(at.x, _ground(at) + float(d[2]), at.y), Vector3(seat.x, _ground(seat), seat.y), 10.0, false])
		var toward_peak := (peak - dock).normalized()
		var ground_look := dock + toward_peak * 1500.0
		views.append(["near_dock_1500m", dock_eye, Vector3(ground_look.x, _ground(ground_look), ground_look.y), 10.0, false])
	if not pick.is_empty():
		views = views.filter(func(v): return v[0] == pick)


# 看向的点：Vector3 照原样；[节点, 俯角°] 朝那一群的主峰（有 PlanetView 时按画面上的位置），俯角另加
func _look_point(eye: Vector3, look) -> Vector3:
	if look is Vector3:
		return look
	var node: int = look[0]
	var pitch: float = look[1]
	var dir := _dir(float(TARGETS.get(node, 270.0)))
	if planet != null and planet.is_active():
		var p: Vector3 = planet.group_peak_visual(node)
		if p != Vector3.ZERO:
			dir = p - eye
			dir.y = 0.0
			dir = dir.normalized()
			var base_pitch := atan2(p.y - eye.y, Vector2(p.x - eye.x, p.z - eye.z).length())
			pitch += rad_to_deg(base_pitch) if pitch > -1.0 else 0.0
	var a := deg_to_rad(pitch)
	return eye + (dir * cos(a) + Vector3(0, sin(a), 0)) * 1000.0


func _apply(v: Array) -> void:
	GameClock.set_date(int(v[5]) if v.size() > 5 else DAY, float(v[3]))
	camera.fov = float(v[6]) if v.size() > 6 else 60.0
	atmosphere.set_weather_override("clear", 0.0)
	player.global_position = v[1] - Vector3(0, 6, 0)  # 人在相机下面：相机别钻进人的模型里
	world.prepare_at(v[1])
	camera.global_position = v[1]
	camera.look_at(_look_point(v[1], v[2]))
	camera.current = true
	atmosphere.snap()


# 相机 2 km 以内（地面上一圈圈取点）画面坐标与平面坐标差多少（米）：近处不变的量
func _near_shift(eye: Vector3) -> float:
	var worst := 0.0
	for r in [50.0, 200.0, 500.0, 1000.0, 1500.0, 2000.0]:
		for k in 48:
			var a := TAU * k / 48.0
			var q := Vector2(eye.x + r * cos(a), eye.z + r * sin(a))
			var h := world.get_surface_height(q.x, q.y)
			var p := Vector3(q.x, eye.y - 2.0 if is_nan(h) else h, q.y)
			worst = maxf(worst, (planet.to_visual(p) - p).length())
	return worst


func _far_ready() -> bool:
	return far_groups == null or far_groups.is_idle()


func _process(delta: float) -> void:
	if not world.is_loaded():
		return
	if views.is_empty():
		_plan()
		player.set_god_mode(true)
	wait -= delta
	if index >= 0:
		var v: Array = views[index]
		player.global_position = v[1] - Vector3(0, 6, 0)
		camera.global_position = v[1]
		camera.look_at(_look_point(v[1], v[2]))
		waited += delta
		var far_wait: bool = v[4] and not _far_ready() and waited < FAR_WAIT_MAX_S
		if wait > 0.0 or far_wait:
			return
		var path := shots_dir.path_join(v[0] + ".png")
		get_viewport().get_texture().get_image().save_png(path)
		var w: Dictionary = atmosphere.get_weather()
		var extra := ""
		if planet != null and planet.is_active():
			var ll: Vector2 = planet.get_camera_latlon()
			extra = "，相机 %.3f°, %.3f°" % [ll.x, ll.y]
		if far_groups != null:
			extra += "，远处的岛群 %d 群 %d 块 %d 面" % [far_groups.get_group_count(), far_groups.get_chunk_count(), far_groups.get_triangle_count()]
		if planet != null and planet.is_active() and not v[4]:
			extra += "，2 km 内画面位移最多 %.3f m" % _near_shift(v[1])
		var env: Environment = main.get_node("WorldEnvironment").environment
		extra += "，雾 %.7f（天空 %.2f）" % [env.fog_density, env.fog_sky_affect]
		print("planet_shots: %s（%.1f 点，太阳高 %.1f° 方位 %.0f°%s）" % [path, float(v[3]), w["sun_altitude"], w["sun_azimuth"], extra])
	index += 1
	if index >= views.size():
		get_tree().quit(0)
		return
	_apply(views[index])
	wait = WAIT_S
	waited = 0.0
