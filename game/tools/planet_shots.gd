extends Node
## 行星计划 P1 的目视检查（不做断言，只存图）：地平线外——曲率与邻近岛群。
##   远：主码头与主峰上望「远 A」（开局那群 150–300 km 外、从主码头看主峰露得最高的那一群：山头露出地平线）与「远 B」（最近的邻群，StoryStart 的 neighbor）；
##       主码头上空 5 / 20 / 50 km 各一张（周围一圈岛群）；20 km 高空的清早与黄昏各一张（朝远 A、远 B：远处岛群的受光方向）。
##       （2026-09-29 以前开局是 #2051，远 A、远 B 是 205 km 外的 #2049 与 235 km 外的 #2278。）
##   近：主码头站着（望栈桥、望岸上）、邑治 350 m 与 1.8 km、八百米的林子——2 km 以内，改前改后对照（画面弯曲在 2 km 处只降 0.3 m）。
## 钟停在开局那天（开局配置的 calendar_start，#6329 是四月初二）、天气覆盖成晴，每张一样的条件。人跟着相机挪（流式焦点就是人），人开 god。
## `--view=<名字>` 只拍那一张。没有 PlanetView 的场景（改前）也能跑：远处那几张照固定的方位拍（改前远平面 60 km，看不见邻群）。
## 行星计划 P2 · 轨道（`--only=orbit`；不加 --only 时排在最后）：开发者的轨道相机（OrbitCamera，F7 的那一个）从邑治的村口起飞——
##   2 m、200 m、2 km、20 km、200 km、2,000 km、20,000 km × 白天（10 点）、晨昏（日落前后）、夜里（23 点），相机越高越往下看；
##   外加整颗星正对赤道（钟的那一群的经度）、G 定点永暴（北纬 37.5°、西经 10°，那里的正午）、晨昏线、夜里的灯（开局那群上空 1,500 km）、一群密的岛群（白天）。
##   行星贴图与大气表在后台做好了才拍（PlanetGlobe.is_ready），远处的岛群建好了才拍。
## 行星计划 P7 · 云带之下（`--only=sea`；不加 --only 时也拍）：主码头往外（背着主峰）找一片开阔的云海，相机从云带顶上一路往下——
##   云带顶上 150 m、云带里（−5.6 km）、刚出云底（−6.25 km）、云底下（−6.5 km）、海面上 20 m（平视、仰看云底）、黄昏的云底下、风暴里的海面、
##   白礁（行星包里老岛群附近多的一片）；人留在码头上（相机在海上，人下不了海）。
##
##   Godot --path game --fixed-fps 60 res://tools/planet_shots.tscn -- --shots=<目录> [--only=near|far|orbit|sea]

const SCENE := preload("res://world_seed42.tscn")
const WAIT_S := 9.0
const FAR_WAIT_MAX_S := 40.0  # 远处的岛群（后台建低模）最多等这么久
var day := 0  # 开局那天（StoryStart.get_calendar_start()）
var far_a := -1  # 远 A：150–300 km 外、从主码头看主峰露得最高的那一群
var far_b := -1  # 远 B：最近的邻群
var targets := {}  # 节点 → 从主码头看的方位（按行星包算好，看不见主峰时照这个方位）
var seat := Vector2.ZERO  # 邑治（world.json 里 seat 的那一村）

var main: Expedition
var world: World
var player: Player
var atmosphere: Atmosphere
var planet = null  # PlanetView（改前没有）
var far_groups = null  # FarGroups（改前没有）
var globe = null  # PlanetGlobe（P2）
var orbit = null  # OrbitCamera（P2）
const ORBIT_KM := [0.002, 0.2, 2.0, 20.0, 200.0, 2000.0, 20000.0]
const ORBIT_PITCH := [-3.0, -22.0, -30.0, -20.0, -42.0, -78.0, -90.0]
const ORBIT_TIMES := [["day", 10.0], ["dusk", 18.55], ["night", 23.0]]
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
	day = StoryStart.get_calendar_start()
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	atmosphere = main.get_node("Atmosphere")
	atmosphere.override_fade = 0.05
	planet = main.get_node_or_null("PlanetView")
	far_groups = main.get_node_or_null("FarGroups")
	globe = main.get_node_or_null("PlanetGlobe")
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


# 季节按半球换：日子写成南半球的（原来的开局在南纬），北半球的群往后挪半年（168 天）
func _season_day(p_south_day: int) -> int:
	var lat := float(world.get_info()["source"]["lat"])
	return p_south_day if lat < 0.0 else (p_south_day + 168) % 336


# 远 A、远 B：FarGroups 认得的群（600 km 内）里挑；远 A 按从此刻的相机（主码头）看主峰的仰角
func _pick_far() -> void:
	far_b = StoryStart.get_neighbor_node()
	var best := -INF
	if far_groups != null:
		var cam: Vector3 = main.get_viewport().get_camera_3d().global_position
		for n in far_groups.get_nodes():
			var d: float = planet.distance_to_node(n)
			if d < 150000.0 or d > 300000.0 or n == far_b:
				continue
			var p: Vector3 = planet.group_peak_visual(n)
			if p == Vector3.ZERO:
				continue
			var up := atan2(p.y - cam.y, Vector2(p.x - cam.x, p.z - cam.z).length())
			if up > best:
				best = up
				far_a = n
	if far_a < 0:
		far_a = far_b
	for n in [far_a, far_b]:
		targets[n] = planet.bearing_to_node(n)
	print("planet_shots: 远 A #%d（%.0f km、方位 %.0f°、主峰仰角 %.2f°），远 B #%d（邻群，%.0f km、方位 %.0f°）" % [far_a, planet.distance_to_node(far_a) / 1000.0, targets[far_a],
		rad_to_deg(best), far_b, planet.distance_to_node(far_b) / 1000.0, targets[far_b]])


func _plan() -> void:
	var spawn: Transform3D = world.get_dock_spawn(world.find_dock(main.origin_dock))
	var dock := Vector2(spawn.origin.x, spawn.origin.z)
	for v in world.get_info()["villages"]:
		if bool(v.get("seat", false)):
			seat = Vector2(float(v["pos"][0]), float(v["pos"][1]))
	if planet != null and planet.is_active():
		_pick_far()
	# 主峰：全岛群最高的一点（整张岛群栅格 1 km 格粗找，再细找）
	var g: Dictionary = world.get_info()["grid"]
	var peak := dock
	var peak_h := -1e9
	for i in range(int(float(g["rows"]) * float(g["res_m"]) / 1000.0) + 1):
		for j in range(int(float(g["cols"]) * float(g["res_m"]) / 1000.0) + 1):
			var p := Vector2(float(g["x0"]), float(g["z0"])) + Vector2(j, i) * 1000.0
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
	if only.is_empty() or only == "far":
		views.append(["far_dock_a", dock_eye, [far_a, -0.3], 10.0, true])
		views.append(["far_dock_b", dock_eye, [far_b, -0.3], 10.0, true])
		views.append(["far_dock_high_b", dock_eye + Vector3(0, 400, 0), [far_b, -0.3], 10.0, true])  # 船刚升空：码头上空 400 m
		views.append(["far_dock_high_b_zoom", dock_eye + Vector3(0, 400, 0), [far_b, 0.0], 10.0, true, day, 6.0])
		views.append(["far_peak_a", peak_eye, [far_a, -0.3], 10.0, true])
		for km in [5, 20, 50]:
			var eye := Vector3(dock.x, float(km) * 1000.0, dock.y)
			views.append(["far_alt_%dkm" % km, eye, [far_a, -8.0 - float(km) * 0.2], 10.0, true])
		var high := Vector3(dock.x, 20000.0, dock.y)
		views.append(["far_dawn_a", high, [far_a, -3.0], 5.9, true])
		views.append(["far_dusk_a", high, [far_a, -3.0], 18.2, true])
		views.append(["far_dawn_b", high, [far_b, -3.0], 5.9, true])
		views.append(["far_dusk_b", high, [far_b, -3.0], 18.2, true])
		# 拉近（视角 6°）看受光：清早太阳在东，朝东的坡与崖亮；黄昏太阳在西，朝西的亮
		views.append(["far_dawn_b_zoom", high, [far_b, 0.0], 5.9, true, day, 6.0])
		views.append(["far_dusk_b_zoom", high, [far_b, 0.0], 18.2, true, day, 6.0])
		views.append(["far_dawn_a_zoom", high, [far_a, 0.0], 5.9, true, day, 8.0])
		views.append(["far_dusk_a_zoom", high, [far_a, 0.0], 18.2, true, day, 8.0])
		# 深冬（开局那群那个半球最冷的时候）：各群按自己的纬度与气候积雪
		var winter := _season_day(180)
		views.append(["far_winter_20km", high, [far_a, -12.0], 12.0, true, winter])
		views.append(["far_winter_a_zoom", high, [far_a, 0.0], 12.0, true, winter, 8.0])
	if only.is_empty() or only == "near":
		# 近：2 km 以内，改前改后对照
		var fwd := -spawn.basis.z
		var eye := spawn.origin + Vector3(0, 1.7, 0)
		views.append(["near_dock_pier", eye, eye + fwd * 100.0 + Vector3(0, -4.0, 0), 10.0, false])
		views.append(["near_dock_land", eye, eye - fwd * 100.0 + Vector3(0, 2.0, 0), 10.0, false])
		for d in [["near_village_350m", 350.0, 60.0], ["near_village_1800m", 1800.0, 260.0]]:
			var at := seat + Vector2(0.35, 1.0).normalized() * float(d[1])
			views.append([d[0], Vector3(at.x, _ground(at) + float(d[2]), at.y), Vector3(seat.x, _ground(seat), seat.y), 10.0, false])
		var toward_peak := (peak - dock).normalized()
		var ground_look := dock + toward_peak * 1500.0
		views.append(["near_dock_1500m", dock_eye, Vector3(ground_look.x, _ground(ground_look), ground_look.y), 10.0, false])
	if (only.is_empty() or only == "orbit") and globe != null and planet != null and planet.is_active():
		_plan_orbit()
	if only.is_empty() or only == "sea":
		_plan_sea(dock, peak, spawn.origin)
	if not pick.is_empty():
		views = views.filter(func(v): return v[0] == pick)


# 轨道：[名字, {lat, lon, height, heading, pitch}, 钟点, 远处的要不要等, 日子, 视角, 人站在哪]
func _plan_orbit() -> void:
	var gate := seat + Vector2(0.35, 1.0).normalized() * 350.0  # 村口：邑治东南 350 m
	var ground := _ground(gate)
	var ll: Vector2 = planet.latlon_at(Vector3(gate.x, 0.0, gate.y))
	var to_seat := seat - gate
	var heading := rad_to_deg(atan2(to_seat.x, -to_seat.y))
	var behind := gate - to_seat.normalized() * 25.0  # 人站在相机后头 25 m（2 m 那张别拍到人）
	var stand := Vector3(behind.x, _ground(behind) + 1.0, behind.y)
	for t in ORBIT_TIMES:
		for k in ORBIT_KM.size():
			var km: float = ORBIT_KM[k]
			var h := ground + km * 1000.0 if km < 10.0 else km * 1000.0
			var name := "orbit_%s_%s" % [t[0], ("%dm" % int(km * 1000.0)) if km < 1.0 else ("%dkm" % int(km))]
			views.append([name, {"lat": ll.x, "lon": ll.y, "height": h, "heading": heading, "pitch": ORBIT_PITCH[k]}, t[1], true, day, 60.0, stand])
	# 整颗星正对赤道（钟的那一群的经度，钟点 12 点就是那里的正午）；G 定点永暴（那里的正午：钟点 = 12 + 经度差 / 15）；晨昏线（太阳直射钟的经度，往东 90°）
	var clock_lon: float = planet.get_clock_longitude()
	var g_hour := fposmod(12.0 + (clock_lon - (-10.0)) / 15.0, 24.0)
	var dusk_lon := fposmod(clock_lon + 90.0 + 180.0, 360.0) - 180.0
	views.append(["orbit_globe_equator", {"lat": -5.0, "lon": clock_lon, "height": 2.0e7, "heading": 0.0, "pitch": -90.0}, 12.0, true, day, 60.0, stand])
	views.append(["orbit_g_storm", {"lat": 30.0, "lon": -10.0, "height": 5.0e6, "heading": 0.0, "pitch": -90.0}, g_hour, true, day, 60.0, stand])
	views.append(["orbit_terminator", {"lat": -15.0, "lon": dusk_lon, "height": 2.0e7, "heading": 0.0, "pitch": -90.0}, 12.0, true, day, 60.0, stand])
	views.append(["orbit_night_lights", {"lat": ll.x - 8.0, "lon": ll.y, "height": 1.5e6, "heading": 0.0, "pitch": -62.0}, 23.5, true, day, 60.0, stand])
	views.append(["orbit_islands_day", {"lat": ll.x, "lon": ll.y, "height": 1.2e6, "heading": 0.0, "pitch": -90.0}, 12.0, true, day, 60.0, stand])
	views.append(["orbit_blend_45km", {"lat": ll.x, "lon": ll.y, "height": 45000.0, "heading": heading, "pitch": -25.0}, 10.0, true, day, 60.0, stand])  # 交叉淡化的中间（30–60 km）
	views.append(["orbit_limb_dawn", {"lat": ll.x, "lon": ll.y, "height": 4.0e5, "heading": 90.0, "pitch": -12.0}, 5.6, true, day, 60.0, stand])
	print("planet_shots: 轨道 — 村口 %s（地面 %.0f m，%.3f°, %.3f°），朝邑治 %.0f°" % [gate, ground, ll.x, ll.y, heading])


# 从 p 起朝 away 走，找一处 2.5 km 内没有地的开阔云海，再往外 3 km
func _open_sea(from: Vector2, away: Vector2) -> Vector2:
	for k in 400:
		var p := from + away * (float(k) * 500.0)
		var clear := true
		for r in [0.0, 1000.0, 2500.0]:
			for a in 12:
				var q: Vector2 = p + Vector2(cos(TAU * a / 12.0), sin(TAU * a / 12.0)) * float(r)
				if not is_nan(world.get_surface_height(q.x, q.y)):
					clear = false
		if clear:
			return p + away * 3000.0
	return from + away * 200000.0


# 云带之下（行星计划 P7）：[名字, 相机, 看向, 钟点, 等远处, 日子, 视角, 人站在哪, 天气]
func _plan_sea(dock: Vector2, peak: Vector2, stand: Vector3) -> void:
	var away := (dock - peak).normalized()
	var sea := _open_sea(dock, away)
	var side := Vector2(-away.y, away.x)
	var person := stand + Vector3(0, 1.0, 0)
	var at := func(h: float) -> Vector3: return Vector3(sea.x, h, sea.y)
	var look := func(h: float, pitch_deg: float) -> Vector3:
		var a := deg_to_rad(pitch_deg)
		var d := Vector3(side.x, 0.0, side.y) * cos(a) + Vector3(0, sin(a), 0)
		return Vector3(sea.x, h, sea.y) + d * 1000.0
	views.append(["sea_deck_top", at.call(-4850.0), look.call(-4850.0, -8.0), 10.0, false, day, 60.0, person, "clear"])
	views.append(["sea_in_band", at.call(-5600.0), look.call(-5600.0, -5.0), 10.0, false, day, 60.0, person, "clear"])
	views.append(["sea_base_exit", at.call(-6250.0), look.call(-6250.0, -6.0), 10.0, false, day, 60.0, person, "clear"])
	views.append(["sea_under_base", at.call(-6500.0), look.call(-6500.0, -8.0), 10.0, false, day, 60.0, person, "clear"])
	views.append(["sea_surface", at.call(-6780.0), look.call(-6780.0, -3.0), 10.0, false, day, 60.0, person, "clear"])
	views.append(["sea_surface_up", at.call(-6780.0), look.call(-6780.0, 30.0), 10.0, false, day, 60.0, person, "clear"])
	# 黄昏朝太阳望（方位取那一刻天与光算的太阳方位，仰 4°）：云底那一侧染暖
	views.append(["sea_dusk", at.call(-6700.0), ["sun", 4.0], 18.4, false, day, 60.0, person, "clear"])
	views.append(["sea_storm", at.call(-6780.0), look.call(-6780.0, -4.0), 14.0, false, day, 60.0, person, "storm"])
	print("planet_shots: 云带之下 — 开阔云海 %s（离主码头 %.1f km，背着主峰）" % [sea, (sea - dock).length() / 1000.0])


# 看向的点：Vector3 照原样；["sun", 仰角°] 朝此刻的太阳方位；[节点, 俯角°] 朝那一群的主峰（有 PlanetView 时按画面上的位置），俯角另加
func _look_point(eye: Vector3, look) -> Vector3:
	if look is Vector3:
		return look
	if look[0] is String:
		var sun := deg_to_rad(float(atmosphere.get_weather()["sun_azimuth"]))
		var up := deg_to_rad(float(look[1]))
		return eye + (_dir(rad_to_deg(sun)) * cos(up) + Vector3(0, sin(up), 0)) * 1000.0
	var node: int = look[0]
	var pitch: float = look[1]
	var dir := _dir(float(targets.get(node, 270.0)))
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
	if v[1] is Dictionary:
		_apply_orbit(v)
		return
	GameClock.set_date(int(v[5]) if v.size() > 5 else day, float(v[3]))
	camera.fov = float(v[6]) if v.size() > 6 else 60.0
	atmosphere.set_weather_override(str(v[8]) if v.size() > 8 else "clear", 0.0)
	player.global_position = _stand_of(v)
	world.prepare_at(v[1])
	camera.global_position = v[1]
	camera.look_at(_look_point(v[1], v[2]))
	camera.current = true
	atmosphere.snap()


# 人站在哪：云带之下那几张站在码头上（v[7]），别的在相机下面 6 m（相机别钻进人的模型里）
func _stand_of(v: Array) -> Vector3:
	return v[7] if v.size() > 7 else v[1] - Vector3(0, 6, 0)


func _apply_orbit(v: Array) -> void:
	# v: [名字, 状态, 钟点, 等远处, 日子, 视角, 人站在哪]
	GameClock.set_date(int(v[4]), float(v[2]))
	atmosphere.set_weather_override("clear", 0.0)
	var stand: Vector3 = v[6]
	player.global_position = stand
	world.prepare_at(stand)
	if orbit == null:
		orbit = ClassDB.instantiate("OrbitCamera")
		main.add_child(orbit)
	orbit.fov = float(v[5])
	var st: Dictionary = v[1]
	orbit.set_orbit_state(st["lat"], st["lon"], st["height"], st["heading"], st["pitch"])
	orbit.current = true
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
	if planet != null and not planet.is_active() and index < 0:
		return  # PlanetView 锚定了（晚一帧）才排
	if views.is_empty():
		_plan()
		player.set_god_mode(true)
	wait -= delta
	if index >= 0:
		var v: Array = views[index]
		var is_orbit: bool = v[1] is Dictionary
		if is_orbit:
			var st: Dictionary = v[1]
			orbit.set_orbit_state(st["lat"], st["lon"], st["height"], st["heading"], st["pitch"])
			player.global_position = v[6]
		else:
			player.global_position = _stand_of(v)
			camera.global_position = v[1]
			camera.look_at(_look_point(v[1], v[2]))
		waited += delta
		var far_wait: bool = (v[3] if is_orbit else v[4]) and not _far_ready() and waited < FAR_WAIT_MAX_S
		if is_orbit and globe != null and not globe.is_ready() and waited < 120.0:
			far_wait = true
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
		if planet != null and planet.is_active() and not v[4] and v.size() <= 7:
			extra += "，2 km 内画面位移最多 %.3f m" % _near_shift(v[1])
		var env: Environment = main.get_node("WorldEnvironment").environment
		extra += "，雾 %.7f（天空 %.2f）" % [env.fog_density, env.fog_sky_affect]
		if is_orbit:
			var gs: Dictionary = globe.get_state()
			extra += "，离地 %.0f m，行星 %.2f 大气 %.2f 云带顶网格 %.2f 云层叠画 %s 锚群藏着 %s，近 %.2f m 远 %.0f km" % [orbit.get_clearance(), gs["blend_planet"], gs["blend_atmo"],
				gs["deck_alpha"], gs["overlay"], gs["anchor_hidden"], orbit.near, orbit.far / 1000.0]
		print("planet_shots: %s（%.1f 点，太阳高 %.1f° 方位 %.0f°%s）" % [path, float(v[2] if is_orbit else v[3]), w["sun_altitude"], w["sun_azimuth"], extra])
	index += 1
	if index >= views.size():
		get_tree().quit(0)
		return
	_apply(views[index])
	wait = WAIT_S
	waited = 0.0
