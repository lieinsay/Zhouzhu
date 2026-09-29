extends Node
## 行星计划 P5：全球天气与风（world_seed42.tscn），只通过输入动作开船。开局那群（StoryStart，PLAN-LAND P0 起是 #6329）与最近的邻群（#6327）：
## 开局那群主码头出生：行星天气场在后台建好、全行星的天气贴图烘好，脚下是它自己的天气（场里只有它一群、权 1），近处的云团槽里有它
## → 走上船、掌舵、升空、换疾档
## → 一路取样：开局那群主码头到邻群主码头的连线上每 500 m 问一次场（大势、风），又经锚群的天气模型问一次（地上判雨、HUD 用的那一份）：没有瞬变，两头各是各的
## → 找换锚线（两群群心连线上离两群陆地一样远的点，同 planet_voyage_e2e），船瞬移到换锚线前 10 km 的云海上空，朝邻群的主码头飞过换锚线、
##   一直飞到场里邻群占到九成：每一帧录此地此刻的天气（云量、雨区、降水、气温、风），相邻两帧差得不多（换锚那一帧也是）
## → 船瞬移到信风带（纬度 14–23°、开局那群的经度 ±90°）、西风带（纬度 45–50°、经度 ±40°，都在开局那群的半球）开阔的云海上空（行星天气场里「背景」占得最多的一点；有几处背景都占九成以上就挑其中 ② 的风最大的），
##   松开舵等几秒：船往下风漂，漂向就是此地的风，风向跟 ② 的风差不多
## → 船瞬移进赤道永暴带（赤道上开局那群的经度 ±60° 里背景占得最多的一点）：风暴、禁航，HUD 报「赤道永暴带：风暴，禁航」，船被往外推
## 去掉 --headless、末尾加 `-- --shots=<目录>`：再从轨道上拍几张（开局那群一带的全景、当天晴着的一群、下着雨的一群，各报它们当天的天气），
## 天气读数另存一份 CSV（飞过换锚线那一段）。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/planet_weather_e2e.tscn

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 1500.0  # 模拟秒
const SETTLE_TIMEOUT := 150.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
var HOME := StoryStart.get_spawn_node()  # 开局那群
var AWAY := StoryStart.get_neighbor_node()  # 最近的邻群
var AWAY_DOCK := AWAY * 1000 + 1  # 邻群的主码头 #1（地图宿主里别的群的码头号带群号）
# 开局那群的平面里：两群陆地等远的那一点（换锚线上）与两群群心的方向——开局时量（同 planet_voyage_e2e）
var switch_line := Vector3.ZERO
var out_dir := Vector3.ZERO
var probe_at := 0.0
const LINE_PROBES := 10
const CRUISE_Y := 1500.0
# 开阔的云海（场里背景占得多，② 的风 6–11 m/s）：纬度按开局那群的半球，经度在开局那群的经度附近挑——开局时算（_open_sea）
var trade := Vector2.ZERO
var west := Vector2.ZERO
var storm := Vector2.ZERO  # 赤道永暴带（赤道上离岛群最远的一点：背景接手、风暴、禁航）
const OPEN_SEA_BG := 0.9
# 飞过换锚线那一段：相邻两帧最多变多少
const MAX_STEP_COVER := 0.01
const MAX_STEP_TEMP := 0.05
const MAX_STEP_WIND := 0.2

enum Phase { SETTLE, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, GEAR_UP, TRANSECT, JUMP_OUT, CROSS, WIND_TRADE, WIND_WEST, STORM_BAND, ORBIT, DONE }

var main: Expedition
var player: Player
var ship: Ship
var stream: GroupStream
var host: PlanetHost
var planet: PlanetView
var atmosphere: Atmosphere
var globe: PlanetGlobe

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var held := {}
var waypoints: Array[Vector3] = []
var climb_from := 0.0
var step := 0

# 飞过换锚线
var last_wx := {}
var worst := {"cloud_cover": 0.0, "coverage": 0.0, "temp_c": 0.0, "wind": 0.0}
var worst_at := {}
var frames := 0
var shifts_seen := 0
var shift_frames := 0
var away_weight_min := 1.0
var away_weight_max := 0.0
var csv: Array[String] = []
var orbit = null
var orbit_views: Array = []
var orbit_index := -1
var orbit_wait := 0.0
var orbit_only := false  # --orbit-only：就绪后直接去拍轨道上那几张（调画面用）


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
		elif arg == "--orbit-only":
			orbit_only = true
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	stream = main.get_node("GroupStream")
	host = main.get_node("PlanetHost")
	planet = main.get_node("PlanetView")
	atmosphere = main.get_node("Atmosphere")
	globe = main.get_node("PlanetGlobe")


func _finish(ok: bool, why: String) -> void:
	_release_all()
	print("planet_weather: %s —— %s（阶段 %s，模拟 %.1f s）" % ["PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed])
	get_tree().quit(0 if ok else 1)


func _log(line: String) -> void:
	print("planet_weather: " + line)


func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _release_all() -> void:
	for action in MOVES:
		Input.action_release(action)


func _tap(action: String) -> void:
	if held.has(action):
		return
	Input.action_press(action)
	held[action] = 4


func _tick_taps() -> void:
	for action in held.keys():
		held[action] -= 1
		if held[action] <= 0:
			Input.action_release(action)
			held.erase(action)


func _goto(next: Phase) -> void:
	_log("%-12s 用时 %6.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	phase = next
	phase_started = elapsed
	step = 0


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	pending_shot = shot_name
	shot_wait = 3


func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	_log("截图 %s（钟 %s）" % [pending_shot, GameClock.get_clock_text()])
	pending_shot = ""


# ---------------------------------------------------------------- 小工具

func _anchor() -> GroupWorld:
	return stream.get_group_world(stream.get_anchor_node())


func _standing_on_ground() -> bool:
	var w := _anchor()
	var at := player.global_position
	var ground: float = w.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.0 and w.is_ready_at(at)


func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	to.y = 0.0
	if to.length() <= radius:
		_hold("move_forward", false)
		return true
	player.set_look_yaw(atan2(-to.x, -to.z))
	_hold("move_forward", true)
	return false


func _follow_waypoints() -> bool:
	if waypoints.is_empty():
		return true
	if _walk_toward(waypoints[0], 0.6):
		waypoints.pop_front()
	return waypoints.is_empty()


func _yaw_toward(dir: Vector3) -> float:
	return atan2(-dir.x, -dir.z)


func _fly_toward(target: Vector3, want_y: float, arrive: float) -> bool:
	var to := target - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))
	var flat := Vector2(to.x, to.z).length()
	var climb := want_y - ship.global_position.y
	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 2.0)
	_hold("descend", climb < -2.0)
	_hold("move_forward", flat > 30.0 and absf(bearing) < 60.0 and climb < 60.0)
	return Vector3(to.x, to.y, to.z).length() < arrive


func _weight_of(pw: Dictionary, node: int) -> float:
	var nodes: PackedInt32Array = pw.get("nodes", PackedInt32Array())
	var weights: PackedFloat64Array = pw.get("weights", PackedFloat64Array())
	for k in nodes.size():
		if nodes[k] == node:
			return weights[k]
	return 0.0


## 纬度、经度的叫法
func _ll_text(ll: Vector2) -> String:
	return "%s %.1f°、%s %.1f°" % ["北纬" if ll.x >= 0.0 else "南纬", absf(ll.x), "东经" if ll.y >= 0.0 else "西经", absf(ll.y)]


## 开阔的云海：纬度 p_lats 里、经度 center ± span（每度一点）上，行星天气场里「背景」（② 的大势）占 OPEN_SEA_BG 以上的点
## （离岛群够远：大群的天气管得远，离群心四百公里有时还只占一成），p_by_wind 时挑其中 ② 的风最大的
## （船才漂得动：开局那群一侧近处的信风只有五六米），否则挑背景占得最多的；一个都不够就挑背景占得最多的
func _open_sea(p_lats: Array, center: float, span: float, p_by_wind: bool) -> Vector2:
	var best := Vector2(float(p_lats[0]), center)
	var best_score := -INF
	var best_bg := 0.0
	var best_wind := 0.0
	for p_lat in p_lats:
		var lon := center - span
		while lon <= center + span:
			var here := Vector2(float(p_lat), wrapf(lon, -180.0, 180.0))
			var pw := atmosphere.planet_weather_latlon(here.x, here.y)
			var bg := float(pw["bg"])
			var wind := Vector2(float(pw["wind2_x"]), float(pw["wind2_z"])).length()
			var score := bg
			if p_by_wind and bg >= OPEN_SEA_BG:
				score = 10.0 + wind
			if score > best_score:
				best_score = score
				best = here
				best_bg = bg
				best_wind = wind
			lon += 1.0
	_log("开阔的云海：纬度 %s 里挑了 %s，场里背景占 %.2f、② 的风 %.1f m/s" % [p_lats, _ll_text(best), best_bg, best_wind])
	return best


func _angle_deg(a: Vector2, b: Vector2) -> float:
	if a.length() < 1e-6 or b.length() < 1e-6:
		return 180.0
	return rad_to_deg(absf(a.angle_to(b)))


## 船摆到某经纬度的云海上空、停住（舵在人手里，风照样推）
func _place_ship(ll: Vector2) -> void:
	var at: Vector3 = planet.flat_of_latlon(ll.x, ll.y) + Vector3(0, CRUISE_Y, 0)
	ship.set_pose(at, 0.0)
	_release_all()


# ---------------------------------------------------------------- 流程

func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s）" % Phase.keys()[phase])
		return
	match phase:
		Phase.SETTLE:
			var gs: Dictionary = globe.get_state()
			if not stream.is_ready() or not planet.is_weather_ready() or not bool(gs.get("weather_ready", false)) or not _standing_on_ground():
				if in_phase > SETTLE_TIMEOUT:
					_finish(false, "%.0f s 还没就绪：GroupStream %s、行星天气场 %s、天气贴图 %s、站稳 %s" % [SETTLE_TIMEOUT, stream.is_ready(), planet.is_weather_ready(),
						gs.get("weather_ready", false), _standing_on_ground()])
				return
			var ws: Dictionary = planet.get_weather_state()
			_log("行星天气场：%d 群有逐日天气，后台 %.0f ms；天气贴图：各格 %.0f ms、烘一回 %.0f ms（已烘 %d 回）" % [int(ws["tables"]), float(ws["ms"]),
				float(gs["weather_grid_ms"]), float(gs["weather_bake_ms"]), int(gs["weather_bakes"])])
			if int(ws["tables"]) != 8000:
				_finish(false, "行星包里该有 8000 群的逐日天气，实为 %d" % int(ws["tables"]))
				return
			# 脚下：场里只有开局那群（它自己的陆地上，场就是它自己的天气）
			var pw := atmosphere.planet_weather_at(player.global_position)
			if _weight_of(pw, HOME) < 0.999 or float(pw["bg"]) > 0.0:
				_finish(false, "开局那群 #%d 的主码头上场里该只有它：%s / %s，背景 %.3f" % [HOME, pw["nodes"], pw["weights"], float(pw["bg"])])
				return
			var wx: Dictionary = atmosphere.get_weather()
			if absf(float(wx["cloud_cover"]) - float(pw["cloud_cover"])) > 0.02:
				_finish(false, "脚下天与光取的云量 %.3f 与场的 %.3f 对不上" % [float(wx["cloud_cover"]), float(pw["cloud_cover"])])
				return
			var slots: Array = atmosphere.get_cloud_slots()
			var has_home := false
			for s in slots:
				has_home = has_home or (int(s["node"]) == HOME and float(s["fade"]) > 0.99)
			if not has_home:
				_finish(false, "近处的云团槽里该有 #%d：%s" % [HOME, slots])
				return
			# 贴图上开局那群那一格就是场的云量
			var ll: Vector2 = planet.node_latlon(HOME)
			var tex: Color = globe.weather_texel(ll.x, ll.y)
			var at_home := atmosphere.planet_weather_latlon(ll.x, ll.y)
			_log("#%d 主码头：今天「%s」，场里它的权 1、云量 %.2f、雨区 %.2f；贴图上群心那一格云量 %.2f、群的份 %.2f；近处的云团槽 %s" % [HOME, wx["day_kind_name"],
				float(pw["cloud_cover"]), float(pw["coverage"]), tex.r, tex.b, slots])
			if absf(tex.r - float(at_home["cloud_cover"])) > 0.1 or tex.b < 0.9:
				_finish(false, "贴图上 #%d 那一格（云量 %.2f、群的份 %.2f）与场（%.2f）对不上" % [HOME, tex.r, tex.b, float(at_home["cloud_cover"])])
				return
			var hemi := 1.0 if ll.x >= 0.0 else -1.0
			trade = _open_sea([14.0 * hemi, 17.0 * hemi, 20.0 * hemi, 23.0 * hemi], ll.y, 90.0, true)
			west = _open_sea([45.0 * hemi, 50.0 * hemi], ll.y, 40.0, true)
			storm = _open_sea([1.0 * hemi], ll.y, 60.0, false)
			var berth: Transform3D = main.get_origin_berth()
			waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
			_goto(Phase.ORBIT if orbit_only and not shots_dir.is_empty() else Phase.WALK_TO_SHIP)
		Phase.WALK_TO_SHIP:
			if _follow_waypoints():
				_goto(Phase.TAKE_HELM)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到船上")
		Phase.TAKE_HELM:
			if main.get_state() == 1:
				climb_from = ship.global_position.y
				_goto(Phase.CLIMB_OUT)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_OUT:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 8.0:
				_hold("ascend", false)
				_goto(Phase.GEAR_UP)
		Phase.GEAR_UP:
			if ship.get_gear() >= 2:
				_goto(Phase.TRANSECT)
			elif not held.has("gear_up"):
				_tap("gear_up")
		Phase.TRANSECT:
			_transect()
			_goto(Phase.JUMP_OUT)
		Phase.JUMP_OUT:
			# 换锚线：两群群心连线上离两群陆地一样远的那一点（船挪过去、读 GroupStream 量的到各群陆地的距离，差多少挪一半；同 planet_voyage_e2e）
			if step == 0:
				var c_home: Vector3 = planet.flat_of_latlon(planet.node_latlon(HOME).x, planet.node_latlon(HOME).y)
				var c_away: Vector3 = planet.flat_of_latlon(planet.node_latlon(AWAY).x, planet.node_latlon(AWAY).y)
				out_dir = Vector3(c_away.x - c_home.x, 0.0, c_away.z - c_home.z).normalized()
				switch_line = (c_home + c_away) * 0.5
				switch_line.y = 0.0
				ship.set_pose(switch_line + Vector3(0, CRUISE_Y, 0), _yaw_toward(out_dir))
				probe_at = elapsed
				step = 1
			elif step <= LINE_PROBES:
				if elapsed - probe_at < 0.6:
					return
				var h_km := float(stream.get_group_state(HOME)["land_km"])
				var a_km := float(stream.get_group_state(AWAY)["land_km"])
				if absf(a_km - h_km) < 0.2:
					_log("换锚线：%s（开局那群的平面），离两群陆地 %.1f / %.1f km，量了 %d 回" % [switch_line, h_km, a_km, step])
					step = LINE_PROBES + 1
					return
				switch_line += out_dir * (a_km - h_km) * 500.0
				ship.set_pose(switch_line + Vector3(0, CRUISE_Y, 0), _yaw_toward(out_dir))
				probe_at = elapsed
				step += 1
				if step > LINE_PROBES:
					_finish(false, "挪了 %d 回还没找到两群陆地等远的点（此刻离两群 %.1f / %.1f km）" % [LINE_PROBES, h_km, a_km])
			elif step == LINE_PROBES + 1:
				ship.set_pose(switch_line - out_dir * 10000.0 + Vector3(0, CRUISE_Y, 0), _yaw_toward(out_dir))
				probe_at = elapsed
				step += 1
			elif elapsed - probe_at > 1.0:
				last_wx = {}
				shifts_seen = main.get_shift_count()
				csv = ["t,anchor,x,z,cloud_cover,coverage,precip,temp_c,wind_x,wind_z,w_home,w_away,bg"]
				_goto(Phase.CROSS)
		Phase.CROSS:
			_cross(delta, in_phase)
		Phase.WIND_TRADE:
			_wind_band("信风带", trade, in_phase, Phase.WIND_WEST)
		Phase.WIND_WEST:
			_wind_band("西风带", west, in_phase, Phase.STORM_BAND)
		Phase.STORM_BAND:
			_storm_band(in_phase)
		Phase.ORBIT:
			_orbit(delta)
		Phase.DONE:
			pass


## 开局那群主码头 → 邻群主码头的连线上每 500 m 问一次场，又经锚群的天气模型问一次
func _transect() -> void:
	var a: Vector3 = main.get_origin_berth().origin
	var b: Vector3 = host.get_dock_berth_by_id(AWAY_DOCK).origin
	var n := int((b - a).length() / 500.0)
	var prev_f := {}
	var prev_m := {}
	var worst_f := {"cloud_cover": 0.0, "coverage": 0.0, "temp_c": 0.0, "wind": 0.0}
	var worst_m := {"cloud_cover": 0.0, "coverage": 0.0, "temp_c": 0.0, "wind": 0.0}
	var half_at := -1.0
	for k in n + 1:
		var p := a.lerp(b, float(k) / n)
		p.y = CRUISE_Y
		var f := atmosphere.planet_weather_at(p)
		var m := atmosphere.weather_at(p)
		if not prev_f.is_empty():
			for key in ["cloud_cover", "coverage", "temp_c"]:
				worst_f[key] = maxf(worst_f[key], absf(float(f[key]) - float(prev_f[key])))
				worst_m[key] = maxf(worst_m[key], absf(float(m[key]) - float(prev_m[key])))
			worst_f["wind"] = maxf(worst_f["wind"], Vector2(float(f["wind_plane_x"]) - float(prev_f["wind_plane_x"]), float(f["wind_plane_z"]) - float(prev_f["wind_plane_z"])).length())
			worst_m["wind"] = maxf(worst_m["wind"], Vector2(float(m["wind_x"]) - float(prev_m["wind_x"]), float(m["wind_z"]) - float(prev_m["wind_z"])).length())
		if half_at < 0.0 and _weight_of(f, AWAY) > 0.5:
			half_at = float(k) * 0.5
		prev_f = f
		prev_m = m
	var end_f := atmosphere.planet_weather_at(b)
	_log("#%d → #%d 主码头的连线上（%.0f km，每 500 m）：场每一步最多变 云量 %.3f、雨区 %.3f、气温 %.2f °C、风 %.2f m/s；经锚群的天气模型 云量 %.3f、雨区 %.3f、气温 %.2f（含云影的日变化）、风 %.2f；离开局那群的码头 %.1f km 起邻群过半" % [
		HOME, AWAY, (b - a).length() / 1000.0, worst_f["cloud_cover"], worst_f["coverage"], worst_f["temp_c"], worst_f["wind"], worst_m["cloud_cover"], worst_m["coverage"],
		worst_m["temp_c"], worst_m["wind"], half_at])
	# 经天气模型取的气温含云影的日变化（云团一过就差一两度，是局地的、不是瞬变）：大势只比场的；模型的比云量、雨区、风
	if worst_f["cloud_cover"] > 0.05 or worst_f["coverage"] > 0.05 or worst_f["temp_c"] > 0.3 or worst_f["wind"] > 0.6:
		_finish(false, "两群之间 500 m 一步场变得太多（瞬变）：%s" % [worst_f])
		return
	if worst_m["cloud_cover"] > 0.05 or worst_m["coverage"] > 0.05 or worst_m["wind"] > 0.6:
		_finish(false, "两群之间 500 m 一步经天气模型取的变得太多（瞬变）：%s" % [worst_m])
		return
	if _weight_of(end_f, AWAY) < 0.9 or half_at < 0.0:
		_finish(false, "到了邻群 #%d 的主码头场里它该占大头：%s / %s" % [AWAY, end_f["nodes"], end_f["weights"]])


## 飞过换锚线：每一帧录此地此刻的天气
func _cross(delta: float, in_phase: float) -> void:
	var berth: Transform3D = host.get_dock_berth_by_id(AWAY_DOCK)
	_fly_toward(berth.origin, CRUISE_Y, 50.0)
	var wx: Dictionary = atmosphere.get_weather()
	var pw := atmosphere.planet_weather_at(ship.global_position)
	var w_away := _weight_of(pw, AWAY)
	away_weight_min = minf(away_weight_min, w_away)
	away_weight_max = maxf(away_weight_max, w_away)
	var shifted := main.get_shift_count() > shifts_seen
	if shifted:
		shifts_seen = main.get_shift_count()
		shift_frames += 1
		_log("换锚 → %d（离开局那群陆地 %.1f km、邻群陆地 %.1f km）：场里邻群的权 %.2f" % [stream.get_anchor_node(), float(stream.get_group_state(HOME)["land_km"]),
			float(stream.get_group_state(AWAY)["land_km"]), w_away])
	if not last_wx.is_empty():
		for key in ["cloud_cover", "coverage", "temp_c"]:
			var d := absf(float(wx[key]) - float(last_wx[key]))
			if d > worst[key]:
				worst[key] = d
				worst_at[key] = "%.1f s%s" % [in_phase, "（换锚那一帧）" if shifted else ""]
		# 风：锚群的平面轴换锚时转零点几度——按大小与方向比（转过的那一点算进去也不大）
		var dw := Vector2(float(wx["wind_x"]) - float(last_wx["wind_x"]), float(wx["wind_z"]) - float(last_wx["wind_z"])).length()
		if dw > worst["wind"]:
			worst["wind"] = dw
			worst_at["wind"] = "%.1f s%s" % [in_phase, "（换锚那一帧）" if shifted else ""]
	last_wx = wx
	frames += 1
	if frames % 30 == 0:
		csv.append("%.2f,%d,%.0f,%.0f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f" % [in_phase, stream.get_anchor_node(), ship.global_position.x, ship.global_position.z,
			float(wx["cloud_cover"]), float(wx["coverage"]), float(wx["precip"]), float(wx["temp_c"]), float(wx["wind_x"]), float(wx["wind_z"]), _weight_of(pw, HOME), w_away,
			float(pw["bg"])])
	var away_land := float(stream.get_group_state(AWAY)["land_km"])
	# 飞到场里邻群占九成（或到它岸边 1 km）：两群陆地只隔十几公里，原来的「离邻群陆地 12 km」那时还在两群正当中
	if stream.get_anchor_node() == AWAY and (w_away >= 0.9 or away_land < 1.0):
		_release_all()
		_log("飞过换锚线到邻群陆地 %.1f km（%d 帧、换锚 %d 回）：相邻两帧最多变 云量 %.4f（%s）、雨区 %.4f（%s）、气温 %.3f °C（%s）、风 %.3f m/s（%s）；一路上场里邻群的权 %.2f → %.2f" % [
			away_land, frames, shift_frames, worst["cloud_cover"], worst_at.get("cloud_cover", "-"), worst["coverage"], worst_at.get("coverage", "-"), worst["temp_c"],
			worst_at.get("temp_c", "-"), worst["wind"], worst_at.get("wind", "-"), away_weight_min, away_weight_max])
		if not shots_dir.is_empty():
			var fa := FileAccess.open(shots_dir.path_join("cross_weather.csv"), FileAccess.WRITE)
			if fa:
				fa.store_string("\n".join(csv) + "\n")
		if worst["cloud_cover"] > MAX_STEP_COVER or worst["coverage"] > MAX_STEP_COVER or worst["temp_c"] > MAX_STEP_TEMP or worst["wind"] > MAX_STEP_WIND:
			_finish(false, "飞过换锚线时天气有瞬变：%s（%s）" % [worst, worst_at])
			return
		# 两群的陆地只隔十五公里上下：起飞处就在开局那群的岸边，场里对面那群也占三成（两群隔七十多公里时起飞处是一成上下），
		# 所以查的是「从三成半以下涨到八成半以上」——从少数涨到多数
		if shift_frames < 1 or away_weight_min > 0.35 or away_weight_max < 0.85:
			_finish(false, "该飞过换锚线、场里邻群的权从三成半以下涨到八成半以上（换锚 %d 回，权 %.2f → %.2f）" % [shift_frames, away_weight_min, away_weight_max])
			return
		_goto(Phase.WIND_TRADE)
	elif in_phase > 600.0:
		_finish(false, "飞了 %.0f s 还没到邻群（锚 %d、离它陆地 %.1f km、场里它的权 %.2f）" % [in_phase, stream.get_anchor_node(), away_land, w_away])


## 开阔的云海：松开舵等几秒，船往下风漂——漂向就是此地的风（天与光给的，场的），风向跟 ② 的差不多
func _wind_band(band: String, ll: Vector2, in_phase: float, next: Phase) -> void:
	if step == 0:
		if ship.get_gear() > 0 and not held.has("gear_down"):
			_tap("gear_down")  # 泊档：油门松开时船对空气停得快
		_place_ship(ll)
		step = 1
		return
	if in_phase < 10.0:
		return
	var at := ship.global_position
	var wind: Vector2 = atmosphere.wind_at(at)
	var pw := atmosphere.planet_weather_at(at)
	var v := Vector2(ship.velocity.x, ship.velocity.z)
	var w2 := Vector2(float(pw["wind2_plane_x"]), float(pw["wind2_plane_z"]))
	var drift_angle := _angle_deg(v, wind)
	var band_angle := _angle_deg(wind, w2)
	_log("%s（%s，离锚群 %.0f km）：此地的风 %.1f m/s（%s），② 的风 %.1f m/s；船漂 %.2f m/s，漂向与此地的风差 %.1f°，此地的风与 ② 差 %.1f°；场里背景 %.2f、群 %s" % [
		band, _ll_text(ll), Vector2(at.x, at.z).length() / 1000.0, wind.length(), atmosphere.get_weather()["wind_name"], w2.length(), v.length(), drift_angle, band_angle,
		float(pw["bg"]), pw["nodes"]])
	if wind.length() < 5.5:
		_finish(false, "%s的风该过 5 m/s（船才漂），实为 %.1f m/s" % [band, wind.length()])
		return
	if v.length() < 0.3 or drift_angle > 15.0:
		_finish(false, "%s里船该往下风漂（漂 %.2f m/s、与风差 %.1f°）" % [band, v.length(), drift_angle])
		return
	if band_angle > 45.0:
		_finish(false, "%s里此地的风该跟 ② 的风走（差 %.1f°）" % [band, band_angle])
		return
	_goto(next)


## 赤道永暴带：风暴、禁航，HUD 报出来，船被往外推
func _storm_band(in_phase: float) -> void:
	if step == 0:
		_place_ship(storm)
		step = 1
		return
	if in_phase < 6.0:
		return
	var at := ship.global_position
	var wx: Dictionary = atmosphere.get_weather()
	var pw := atmosphere.planet_weather_at(at)
	var push := Vector2(float(pw["push_plane_x"]), float(pw["push_plane_z"]))
	var v := Vector2(ship.velocity.x, ship.velocity.z)
	var sailing := atmosphere.sailing_text(at)
	var status := main.get_status_text()
	_log("赤道永暴带（%s）：此地「%s」风暴 %.2f、能出航 %s；场里带 %d、背景 %.2f、推 %.0f m/s；船漂 %.0f m/s、与推的方向差 %.1f°；HUD「风：%s」" % [_ll_text(storm),
		wx["kind_name"], float(wx["storm"]), wx["sailable"], int(pw["zone_kind"]), float(pw["bg"]), push.length(), v.length(), _angle_deg(v, push), sailing])
	if int(pw["zone_kind"]) != 1 or float(wx["storm"]) < 0.8 or bool(wx["sailable"]):
		_finish(false, "赤道永暴带里该是风暴、禁航")
		return
	if not sailing.begins_with("赤道永暴带：风暴，禁航") or status.find("赤道永暴带：风暴，禁航") < 0:
		_finish(false, "HUD 该报「赤道永暴带：风暴，禁航」：%s" % status.get_slice("\n", 2))
		return
	if push.length() < 100.0 or v.length() < 60.0 or _angle_deg(v, push) > 30.0:
		_finish(false, "永暴带里船该被往外推（推 %.0f m/s，船 %.0f m/s，差 %.1f°）" % [push.length(), v.length(), _angle_deg(v, push)])
		return
	_goto(Phase.ORBIT if not shots_dir.is_empty() else Phase.DONE)
	if shots_dir.is_empty():
		_finish(true, "行星天气场建好、开局那群脚下是它自己的天气 → 两群之间一路取样、飞过换锚线天气没有瞬变 → 信风带、西风带船往下风漂、风跟 ② 走 → 赤道永暴带风暴禁航、船被往外推")


## 从轨道上看（只在 --shots 时）：开局那群一带的全景、当天晴着的一群、下着雨的一群
func _orbit(delta: float) -> void:
	if orbit_views.is_empty():
		_release_all()
		# 钟拨到开局那天的正午（开局配置的 calendar_start；钟点是钟的那一群的地方时）；挑 1500 km 内当天晴着的、下着雨的群
		GameClock.set_date(StoryStart.get_calendar_start(), 12.0)
		var home: Vector2 = planet.node_latlon(HOME)
		var clear_node := -1
		var rain_node := -1
		var best_clear := 1.0
		var best_rain := 0.0
		for node in range(0, 8000):
			var ll: Vector2 = planet.node_latlon(node)
			if absf(ll.x - home.x) > 12.0 or absf(ll.y - home.y) > 14.0:
				continue
			var pw := atmosphere.planet_weather_latlon(ll.x, ll.y)
			var cc := float(pw["cloud_cover"])
			var cov := float(pw["coverage"])
			if cc < best_clear:
				best_clear = cc
				clear_node = node
			if cov > best_rain:
				best_rain = cov
				rain_node = node
		_log("轨道：开局那群一带今天晴得最透的是 #%d（云量 %.2f）、雨下得最开的是 #%d（雨区 %.2f）" % [clear_node, best_clear, rain_node, best_rain])
		orbit = ClassDB.instantiate("OrbitCamera")
		main.add_child(orbit)
		orbit.fov = 50.0
		orbit_views = [["orbit_globe", home, 8.0e6], ["orbit_region", home, 1.5e6], ["orbit_clear_%d" % clear_node, planet.node_latlon(clear_node), 6.0e5],
			["orbit_rain_%d" % rain_node, planet.node_latlon(rain_node), 6.0e5]]
		orbit_index = 0
		_orbit_view()
		return
	if not pending_shot.is_empty():
		return  # 等这一张存好（_process 里存）
	if orbit_wait > 0.0:
		orbit_wait -= delta
		if orbit_wait <= 0.0:
			_shot(orbit_views[orbit_index][0])
			orbit_wait = -1.0
		return
	# 这一张存好了：报它当天的天气、换下一张
	var v: Array = orbit_views[orbit_index]
	var ll: Vector2 = v[1]
	var pw := atmosphere.planet_weather_latlon(ll.x, ll.y)
	var tex: Color = globe.weather_texel(ll.x, ll.y)
	_log("  %s：%s，今天「%s」云量 %.2f、雨区 %.2f；贴图那一格云量 %.2f、雨 %.2f" % [v[0], _ll_text(ll), pw["day_kind"], float(pw["cloud_cover"]),
		float(pw["coverage"]), tex.r, tex.g])
	orbit_index += 1
	if orbit_index >= orbit_views.size():
		_finish(true, "行星天气场建好、开局那群脚下是它自己的天气 → 两群之间一路取样、飞过换锚线天气没有瞬变 → 信风带、西风带船往下风漂、风跟 ② 走 → 赤道永暴带风暴禁航、船被往外推 → 轨道上拍了 %d 张" % orbit_views.size())
		return
	_orbit_view()


func _orbit_view() -> void:
	var v: Array = orbit_views[orbit_index]
	var at: Vector2 = v[1]
	orbit.set_orbit_state(at.x, at.y, float(v[2]), 0.0, -90.0)
	orbit.current = true
	atmosphere.snap()
	orbit_wait = 8.0  # 等几秒：远处的岛群、贴图跟上
