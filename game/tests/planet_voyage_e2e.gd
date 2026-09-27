extends Node
## 行星计划 P3：开船从岛群 #2051 飞到 114 km 外的 #2050（world_2051.tscn），只通过输入动作开船、走路：
## 2051 主码头 #1 出生、邑治的账开好 → 走上船、掌舵、升空、换疾档 → 船瞬移到换锚线前 10 km 的云海上空（2050 这时载入成细版）
## → 朝 2050 的主码头飞过换锚线：换锚那一帧人、船、相机都换到 2050 的平面（相机的经纬度连续、那一帧的帧时）、2050 挂上碰撞、2051 撤掉
## → 船瞬移到 2050 主码头外 3 km → 换回泊档、靠泊、下船走上停机坪：脚下是 2050、HUD 报群 #2050 与地方时（比钟点早两分多钟）、码头边的村子有人出门
## → 钟往前拨 26 个钟头（2051 已降回近景，村子的账存成了文本留在内存）→ 上船、升空，船瞬移到换锚线另一边 10 km
## → 飞回 2051：在换锚线外停船、离舵站稳，前后各一张截图、手动换锚（相机在真球面上不动）→ 掌舵接着飞，2051 升回细版：
##   各村的账从文本补回（新账房由近及远开账），离开 26 h 多只补最后 24 h（每本跳过两个多钟头），都补结到此刻。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/planet_voyage_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图；换锚前后那两张（shift_before / shift_after）拿来对照画面跳没跳。

const SCENE := preload("res://world_2051.tscn")
const TIMEOUT := 1800.0  # 模拟秒
const SETTLE_TIMEOUT := 60.0
const FOLK_TIMEOUT := 90.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const HOME := 2051
const AWAY := 2050
const SEAT_ID := 1  # 2051 的邑治（主码头 #1 旁）
const AWAY_DOCK := 2050001  # 2050 的主码头 #1（地图宿主的码头号带群号）
# 2051 平面里：两群陆地等远的那一点（换锚线上），与从 2051 主码头往 2050 主码头的方向
const LINE := Vector3(-27354.0, 0.0, 71282.0)
const OUT_DIR := Vector3(-0.3739, 0.0, 0.9275)
const CRUISE_Y := 1500.0  # 云海上空飞（云带顶 −3800 m，两群的岸缘都在几百米）

enum Phase { SETTLE, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, GEAR_UP, JUMP_OUT, CROSS_OUT, JUMP_IN, APPROACH, MOORING, WALK_ASHORE, CHECK_AWAY, ADVANCE_CLOCK,
	WALK_BACK, TAKE_HELM_BACK, CLIMB_BACK, JUMP_BACK, CROSS_BACK, STOP, LEAVE_HELM, SHOT_BEFORE, FORCE_SHIFT, SHOT_AFTER, RETAKE_HELM, CHECK_HOME, DONE }

var main: Expedition
var player: Player
var ship: Ship
var stream: GroupStream
var host: PlanetHost
var planet: PlanetView

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

# 换锚那一帧
var last_tick := 0
var frame_ms: Array[float] = []  # 最近几帧的真实帧时
var cross_max_ms := 0.0  # 跨群飞的最慢帧
var cross_frames := 0
var shifts_seen := 0
var shift_frame_ms := 0.0
var cam_before := PackedFloat64Array()  # 换锚前一帧相机在真球面上的位置（行星系，米，双精度）
var cam_speed := 0.0
var shift_jump_m := 0.0
var cross_jump_m := 0.0  # 飞过换锚线那一次
var manual_jump_m := 0.0  # 停船手动换锚那一次
var near_in := 0.0
var near_out := 0.0
var seat_clock_saved := 0.0
var clock_step_at := 0.0
var shot_ll := PackedFloat64Array()


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	stream = main.get_node("GroupStream")
	host = main.get_node("PlanetHost")
	planet = main.get_node("PlanetView")
	last_tick = Time.get_ticks_usec()


func _finish(ok: bool, why: String) -> void:
	_release_all()
	print("planet_voyage: %s —— %s（阶段 %s，模拟 %.1f s）" % ["PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed])
	get_tree().quit(0 if ok else 1)


func _log(line: String) -> void:
	print("planet_voyage: " + line)


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


func _process(_delta: float) -> void:
	# 真实帧时（两次 _process 之间）：换锚那一帧、跨群飞的最慢帧
	var now := Time.get_ticks_usec()
	var ms := (now - last_tick) / 1000.0
	last_tick = now
	frame_ms.append(ms)
	if frame_ms.size() > 8:
		frame_ms.pop_front()
	if phase == Phase.CROSS_OUT or phase == Phase.CROSS_BACK:
		cross_max_ms = maxf(cross_max_ms, ms)
		cross_frames += 1
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return  # 无头不存图
	pending_shot = shot_name
	shot_wait = 3


func _goto(next: Phase) -> void:
	_log("%-14s 用时 %6.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	phase = next
	phase_started = elapsed
	step = 0


# ---------------------------------------------------------------- 小工具

func _world(node: int) -> GroupWorld:
	return stream.get_group_world(node)


func _anchor() -> GroupWorld:
	return _world(stream.get_anchor_node())


func _standing_on_ground() -> bool:
	var w := _anchor()
	var at := player.global_position
	var ground: float = w.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.0 and w.is_ready_at(at)


func _cam_state() -> PackedFloat64Array:
	# 相机在真球面上的位置（行星系，米）：只取决于它真在哪，与哪一群是锚无关（PlanetView 每帧用双精度算的）
	return planet.get_camera_planet_position()


func _latlon_m(a: PackedFloat64Array, b: PackedFloat64Array) -> float:
	if a.size() < 3 or b.size() < 3:
		return INF
	var dx := b[0] - a[0]
	var dy := b[1] - a[1]
	var dz := b[2] - a[2]
	return sqrt(dx * dx + dy * dy + dz * dz)


func _yaw_toward(dir: Vector3) -> float:
	# 船头朝 −z：朝 dir 的 yaw
	return atan2(-dir.x, -dir.z)


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


## 朝目标飞：舵对准方位、按 p_y 定高度；到了 p_arrive 以内返回 true
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


func _seat_ledger() -> Dictionary:
	var h: Hinterland = stream.get_group_hinterland(HOME)
	return h.get_ledger_summary(SEAT_ID, false) if h else {}


# ---------------------------------------------------------------- 流程

func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s）" % Phase.keys()[phase])
		return
	# 换锚：Expedition 在这个物理帧的开头换过（本脚本在它之后跑）——记下相机前后、那一帧
	if main.get_shift_count() > shifts_seen:
		shifts_seen = main.get_shift_count()
		var after := _cam_state()
		shift_jump_m = _latlon_m(cam_before, after)
		_log("换锚 #%d：锚 → %d，这一步 %.2f ms；相机在真球面上前后差 %.3f m（这一帧该走 %.3f m）" % [shifts_seen, stream.get_anchor_node(), main.get_last_shift_ms(), shift_jump_m, cam_speed * delta])
	cam_before = _cam_state()
	cam_speed = ship.get_speed() if main.get_state() != 0 else player.velocity.length()
	match phase:
		Phase.SETTLE:
			if not stream.is_ready():
				if in_phase > SETTLE_TIMEOUT:
					_finish(false, "GroupStream %.0f s 还没就绪（PlanetView 没锚定？）" % SETTLE_TIMEOUT)
				return
			var seat := _seat_ledger()
			if _standing_on_ground() and bool(seat.get("opened", false)):
				var nodes := stream.get_group_nodes()
				if not nodes.has(HOME) or not nodes.has(AWAY):
					_finish(false, "GroupStream 的目录该有 2051 与 2050，实为 %s" % [nodes])
					return
				if stream.get_anchor_node() != HOME or host.get_anchor_node() != HOME:
					_finish(false, "开局锚该是 2051")
					return
				var away := stream.get_group_state(AWAY)
				if int(away["tier"]) != GroupStream.TIER_FAR:
					_finish(false, "开局 2050 该是远景（离 2050 陆地 %.0f km），实为档 %d" % [float(away["land_km"]), int(away["tier"])])
					return
				if not main.get_status_text().begins_with("群 #2051"):
					_finish(false, "HUD 第一行该以「群 #2051」开头：%s" % main.get_status_text())
					return
				_log("出生在 %s；邑治的账开了（%s）；2050 离陆地 %.0f km、远景" % [player.global_position, seat, float(away["land_km"])])
				_shot("1_spawn")
				var berth: Transform3D = main.get_origin_berth()
				waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
				_goto(Phase.WALK_TO_SHIP)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "%.0f s 还没站稳 / 邑治的账还没开（%s）" % [SETTLE_TIMEOUT, seat])
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
				_log("换到%s档，极速 %.0f m/s" % [ship.get_gear_name(), ship.get_max_speed()])
				_goto(Phase.JUMP_OUT)
			elif not held.has("gear_up"):
				_tap("gear_up")
		Phase.JUMP_OUT:
			# 瞬移到换锚线前 10 km（2051 这边）的云海上空，船头朝 2050 的主码头
			if step == 0:
				var at := LINE - OUT_DIR * 10000.0 + Vector3(0, CRUISE_Y, 0)
				ship.set_pose(at, _yaw_toward(OUT_DIR))
				step = 1
			elif in_phase > 1.0:
				var away := stream.get_group_state(AWAY)
				var home := stream.get_group_state(HOME)
				_log("瞬移到换锚线前 10 km：离 2051 陆地 %.1f km、2050 陆地 %.1f km；2050 档 %d" % [float(home["land_km"]), float(away["land_km"]), int(away["tier"])])
				if absf(float(away["land_km"]) - float(home["land_km"])) < 15.0:
					_finish(false, "瞬移的落点离换锚线不到 10 km")
					return
				_shot("2_before_line")
				cross_max_ms = 0.0
				cross_frames = 0
				_goto(Phase.CROSS_OUT)
		Phase.CROSS_OUT:
			var berth: Transform3D = host.get_dock_berth_by_id(AWAY_DOCK)
			_fly_toward(berth.origin, CRUISE_Y, 50.0)
			if shifts_seen >= 1:
				# 换锚那一帧：这一帧与下一帧的真实帧时（Expedition 在物理帧开头换，这一帧的 _process 还没走完，看下一帧记下的）
				if step == 0:
					step = 1
					shift_frame_ms = 0.0
					# 前后两次取样之间船正好走了一帧：差出来的就是换锚跳的
					cross_jump_m = absf(shift_jump_m - cam_speed * delta)
					if cross_jump_m > 0.1:
						_finish(false, "换锚那一帧相机在真球面上挪了 %.3f m（这一帧该走 %.3f m）" % [shift_jump_m, cam_speed * delta])
						return
				else:
					step += 1
					shift_frame_ms = maxf(shift_frame_ms, frame_ms.back() if not frame_ms.is_empty() else 0.0)
					if step >= 3:
						var a := _world(AWAY)
						var h := _world(HOME)
						if stream.get_anchor_node() != AWAY or host.get_anchor_node() != AWAY or planet.get_anchor_node() != AWAY:
							_finish(false, "换锚后锚该是 2050（GroupStream %d、PlanetHost %d、PlanetView %d）" % [stream.get_anchor_node(), host.get_anchor_node(), planet.get_anchor_node()])
							return
						if not a.is_physics_on() or h.is_physics_on():
							_finish(false, "换锚后 2050 该挂上碰撞、2051 撤掉（%s / %s）" % [a.is_physics_on(), h.is_physics_on()])
							return
						if not a.get_transform().is_equal_approx(Transform3D()) or h.get_transform().is_equal_approx(Transform3D()):
							_finish(false, "换锚后 2050 该归位、2051 摆上刚体变换")
							return
						_log("换锚那一帧前后的帧时最多 %.1f ms（headless Debug）；跨群飞了 %d 帧，最慢 %.1f ms" % [shift_frame_ms, cross_frames, cross_max_ms])
						_shot("3_after_line")
						_goto(Phase.JUMP_IN)
			elif in_phase > 400.0:
				var away := stream.get_group_state(AWAY)
				_finish(false, "飞了 %.0f s 还没换锚（2050 档 %d、离 %.1f km；2051 离 %.1f km）" % [in_phase, int(away["tier"]), float(away["land_km"]), float(stream.get_group_state(HOME)["land_km"])])
		Phase.JUMP_IN:
			# 省点时间：船瞬移到 2050 主码头外 1.5 km 的云海上空（往外 1.5 km 一路都没有地的那个方向），船头朝码头
			if step == 0:
				var berth: Transform3D = host.get_dock_berth_by_id(AWAY_DOCK)
				var w := _anchor()
				var best_dir := Vector3.ZERO
				var best_clear := -1
				for k in 32:
					var a := TAU * k / 32.0
					var dir := Vector3(cos(a), 0.0, sin(a))
					var clear := 0
					for d in range(100, 1600, 100):
						var p := berth.origin + dir * float(d)
						if is_nan(w.get_surface_height(p.x, p.z)):
							clear += 1
					if clear > best_clear:
						best_clear = clear
						best_dir = dir
				var inward := -best_dir
				var at := berth.origin - inward * 1500.0
				at.y = berth.origin.y + 60.0
				ship.set_pose(at, _yaw_toward(inward))
				step = 1
			elif in_phase > 1.0:
				_goto(Phase.APPROACH)
		Phase.APPROACH:
			if ship.get_gear() > 0 and not held.has("gear_down"):
				_tap("gear_down")
			var berth: Transform3D = host.get_dock_berth_by_id(AWAY_DOCK)
			var flat := Vector2(berth.origin.x - ship.global_position.x, berth.origin.z - ship.global_position.z).length()
			var want_y := berth.origin.y + (8.0 if flat < 400.0 else 40.0)
			if int(in_phase * 60.0) % 600 == 0:
				_log("进港：离泊位 %.0f m，船 %s 速 %.1f m/s 档 %d，泊位 %s" % [flat, ship.global_position, ship.get_speed(), ship.get_gear(), berth.origin])
			if _fly_toward(berth.origin, want_y, main.get_dock_range() - 5.0):
				_release_all()
				_shot("4_approach")
				_tap("interact")
				_goto(Phase.MOORING)
			elif in_phase > 180.0:
				_finish(false, "180 s 没飞到 2050 的泊位（还差 %.0f m）" % flat)
		Phase.MOORING:
			if main.get_state() == 0:
				var berth: Transform3D = host.get_dock_berth_by_id(AWAY_DOCK)
				waypoints = [ship.global_position, berth.origin + berth.basis.x * 0.8, host.get_dock_spawn_by_id(AWAY_DOCK).origin]
				_goto(Phase.WALK_ASHORE)
			elif in_phase > 5.0 and main.get_state() == 1:
				_tap("interact")
			elif in_phase > 40.0:
				_finish(false, "40 s 没靠上 2050 的泊位（状态 %d，%s）" % [main.get_state(), main.get_prompt()])
		Phase.WALK_ASHORE:
			if _follow_waypoints():
				_release_all()
				if _standing_on_ground():
					_log("下了船站在 2050 的停机坪上 %s" % player.global_position)
					_shot("5_ashore_2050")
					_goto(Phase.CHECK_AWAY)
				elif in_phase > 20.0:
					_finish(false, "走完了路点还没站稳（%s）" % player.global_position)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走下船")
		Phase.CHECK_AWAY:
			# HUD：群号、地方时（2050 在 151.61°E，比钟点所在的 152.15°E 晚约 2.2 分钟）
			var status := main.get_status_text()
			if step == 0:
				if not status.begins_with("群 #2050"):
					_finish(false, "2050 上 HUD 第一行该以「群 #2050」开头：%s" % status)
					return
				var src: Dictionary = _world(AWAY).get_info()["source"]
				var offset_h := (float(src["lon"]) - planet.get_clock_longitude()) / 15.0
				var local := fposmod(GameClock.get_hour() + offset_h, 24.0)
				var expect := "地方时 %02d:%02d" % [int(local), int(floor(local * 60.0 + 1e-6)) % 60]
				var alt := fposmod(local + 1.0 / 60.0, 24.0)
				var expect2 := "地方时 %02d:%02d" % [int(alt), int(floor(alt * 60.0 + 1e-6)) % 60]
				if not status.contains(expect) and not status.contains(expect2):
					_finish(false, "HUD 该报地方时（钟点 %.3f h + %.2f min = %s）：%s" % [GameClock.get_hour(), offset_h * 60.0, expect, status])
					return
				if absf(_world(AWAY).get_local_offset_hours() - offset_h) > 1e-4:
					_finish(false, "2050 的地方时差 %.4f h，该是 %.4f h" % [_world(AWAY).get_local_offset_hours(), offset_h])
					return
				_log("HUD：%s（地方时比钟点晚 %.2f 分钟）" % [status.get_slice("\n", 0), -offset_h * 60.0])
				step = 1
			# 村民：码头边铺出来的村招了人，有人在外面走 / 干活。2050 开局这几天是风暴（它自己的 weather.tsv），村民照规矩在屋里避雨——
			# 那就把天气覆盖成晴（开发者覆盖改在每一群的模型上），看他们雨停了出门
			var sites: SiteStream = stream.get_group_sites(AWAY)
			var out := 0
			var folk_total := 0
			var village := ""
			for v in sites.get_children():
				if v is WorldVillage and v.get_villagers() != null:
					var folk: Villagers = v.get_villagers()
					folk_total += folk.get_villager_count()
					for i in folk.get_villager_count():
						if folk.get_villager_phase(i) != Villagers.INDOORS:
							out += 1
							village = v.get_site_name()
			var atmo: Atmosphere = main.get_node("Atmosphere")
			if step == 1 and folk_total > 0 and out == 0 and in_phase > 5.0:
				var w: Dictionary = atmo.get_weather()
				_log("2050：码头边的村招了 %d 人都在屋里，此地此刻「%s」（风暴 %.2f、降水 %.2f）——覆盖成晴，看他们出门" % [folk_total, w.get("kind_name", "?"), float(w.get("storm", 0.0)), float(w.get("precip", 0.0))])
				atmo.set_weather_override("clear", 0.0)
				step = 2
			if out > 0:
				_log("2050：%s 有 %d 人在外面（招了 %d 人；钟点 %s）" % [village, out, folk_total, GameClock.get_clock_text()])
				_shot("6_folk_2050")
				atmo.clear_weather_override()
				_goto(Phase.ADVANCE_CLOCK)
			elif in_phase > FOLK_TIMEOUT:
				_finish(false, "2050 的码头边 %.0f s 里没人出门（铺出来的村 %s）" % [FOLK_TIMEOUT, sites.get_built_villages()])
		Phase.ADVANCE_CLOCK:
			# 2051 此刻降回了近景：村子的账存成文本留在内存
			var home := stream.get_group_state(HOME)
			if int(home["tier"]) == GroupStream.TIER_DETAIL or int(home["ledgers_saved"]) == 0:
				_finish(false, "人在 2050 时 2051 该降回近景、账存成文本（档 %d、存了 %d 本、离陆地 %.0f km）" % [int(home["tier"]), int(home["ledgers_saved"]), float(home["land_km"])])
				return
			# 近景的门槛临时调低（45 / 50 km）：2051 降到远景——World 拆掉，农田、采场、散布的账存进内存（回程前调回来，它在后台重新载入）
			near_in = stream.get_near_in()
			near_out = stream.get_near_out()
			stream.set_near_in(45000.0)
			stream.set_near_out(50000.0)
			clock_step_at = GameClock.get_seconds()
			GameClock.step_hours(26.0)
			_log("2051 离陆地 %.0f km、档 %d，存了 %d 本账；钟往前拨 26 个钟头 → %s" % [float(home["land_km"]), int(home["tier"]), int(home["ledgers_saved"]), GameClock.get_clock_text()])
			var berth: Transform3D = host.get_dock_berth_by_id(AWAY_DOCK)
			waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
			_goto(Phase.WALK_BACK)
		Phase.WALK_BACK:
			if step == 0:
				var home := stream.get_group_state(HOME)
				if int(home["tier"]) == GroupStream.TIER_FAR and not bool(home["loaded"]):
					_log("2051 降到远景、World 拆了（离陆地 %.0f km；场景根下还有 World：%s）" % [float(home["land_km"]), main.get_node_or_null("World") != null])
					step = 1
				elif in_phase > 10.0:
					_finish(false, "近景门槛调低后 10 s，2051 该降到远景、World 拆掉（%s）" % [home])
					return
			if _follow_waypoints() and step == 1:
				_goto(Phase.TAKE_HELM_BACK)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走回船上")
		Phase.TAKE_HELM_BACK:
			if main.get_state() == 1:
				climb_from = ship.global_position.y
				_goto(Phase.CLIMB_BACK)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_BACK:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 8.0:
				_hold("ascend", false)
				if ship.get_gear() < 2:
					_tap("gear_up")
				else:
					_goto(Phase.JUMP_BACK)
		Phase.JUMP_BACK:
			# 换锚线另一边（2050 这边）10 km，船头朝 2051 的主码头；这一趟手动换锚（截图对照），先关了自动换
			if step == 0:
				stream.set_auto_shift(false)
				stream.set_near_in(near_in)
				stream.set_near_out(near_out)
				var at: Vector3 = stream.to_anchor(HOME, LINE + OUT_DIR * 10000.0)
				at.y = CRUISE_Y
				var home_berth: Transform3D = host.get_dock_berth_by_id(1)
				var dir := home_berth.origin - at
				dir.y = 0.0
				ship.set_pose(at, _yaw_toward(dir.normalized()))
				step = 1
			elif in_phase > 1.0:
				cross_frames = 0
				_goto(Phase.CROSS_BACK)
		Phase.CROSS_BACK:
			var home_berth: Transform3D = host.get_dock_berth_by_id(1)
			_fly_toward(home_berth.origin, CRUISE_Y, 50.0)
			var home := stream.get_group_state(HOME)
			var away := stream.get_group_state(AWAY)
			# 过了换锚线 3 km（离 2051 的陆地比 2050 的近 3 km 以上）就停船
			if float(home["land_km"]) + 3.0 < float(away["land_km"]):
				_release_all()
				_log("过了换锚线：离 2051 陆地 %.1f km、2050 陆地 %.1f km；2051 档 %d" % [float(home["land_km"]), float(away["land_km"]), int(home["tier"])])
				_goto(Phase.STOP)
			elif in_phase > 400.0:
				_finish(false, "飞了 %.0f s 还没过换锚线" % in_phase)
		Phase.STOP:
			_hold("move_back", ship.get_speed() > 1.0 and in_phase < 20.0)
			if ship.get_speed() < 0.3 and in_phase > 2.0:
				_release_all()
				_goto(Phase.LEAVE_HELM)
			elif in_phase > 60.0:
				_finish(false, "60 s 没停下船（%.1f m/s）" % ship.get_speed())
		Phase.LEAVE_HELM:
			# 离舵站在甲板上：没人掌舵的船悬着一动不动（不随风漂），截图前后画面只差换锚
			if main.get_state() == 0:
				if in_phase > 2.0 and player.is_on_floor():
					_goto(Phase.SHOT_BEFORE)
			elif not held.has("interact") and in_phase > 0.5:
				_tap("interact")
			if in_phase > 30.0:
				_finish(false, "30 s 没离舵（%s）" % main.get_notice())
		Phase.SHOT_BEFORE:
			if step == 0:
				_shot("shift_before")
				step = 1
			elif pending_shot.is_empty() and in_phase > 0.5:
				shot_ll = _cam_state()
				if not stream.force_shift_to(HOME):
					_finish(false, "2051 这时该是细版、载入完了，换得过去（%s）" % [stream.get_group_state(HOME)])
					return
				_goto(Phase.FORCE_SHIFT)
		Phase.FORCE_SHIFT:
			if stream.get_anchor_node() == HOME and main.get_shift_count() >= 2:
				_shot("shift_after")
				_goto(Phase.SHOT_AFTER)
			elif in_phase > 2.0:
				_finish(false, "手动换锚 2 s 没换过去")
		Phase.SHOT_AFTER:
			if pending_shot.is_empty():
				var d := _latlon_m(shot_ll, _cam_state())
				manual_jump_m = d
				_log("手动换锚：相机前后在真球面上差 %.4f m（锚 %d → %d）" % [d, AWAY, stream.get_anchor_node()])
				if d > 0.05:
					_finish(false, "手动换锚前后相机在真球面上挪了 %.3f m（该是厘米以内）" % d)
					return
				stream.set_auto_shift(true)
				_goto(Phase.RETAKE_HELM)
		Phase.RETAKE_HELM:
			if main.get_state() == 1:
				_goto(Phase.CHECK_HOME)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没重新上舵")
		Phase.CHECK_HOME:
			# 2051 升回细版：邑治的账从文本补回、只补最后一天，补结到此刻
			var home_berth: Transform3D = host.get_dock_berth_by_id(1)
			_fly_toward(home_berth.origin, CRUISE_Y, 50.0)
			var h: Hinterland = stream.get_group_hinterland(HOME)
			var home_world := main.get_node_or_null("World")
			if home_world == null or not (home_world is World) or not home_world.is_loaded():
				_finish(false, "2051 重新载入后场景根下该有 World（%s）" % [home_world])
				return
			if h == null:
				if in_phase > 60.0:
					_finish(false, "回到 2051 这边 60 s，2051 还没升回细版（%s）" % [stream.get_group_state(HOME)])
				return
			# 新账房由近及远开账（船在西南岸外，近的先开）：补回的那些都结到了此刻（远处最久没结的落后不到一个钟头），
			# 每本都只补了最后一天（离开 26 h 多：每本至少跳过两个钟头）
			var stats: Dictionary = h.get_stats()
			var restored := int(stats.get("restored", 0))
			var hour_s: float = GameClock.get_day_seconds() / 24.0
			if restored >= 20 and float(stats.get("stalest_s", 1e9)) <= hour_s:
				var skipped := float(stats.get("skipped_hours", 0.0))
				var away_h: float = (GameClock.get_seconds() - clock_step_at) / hour_s
				_log("2051 的账补回 %d 本（还有 %d 本存着没开）、跳过 %.0f 个钟头（每本平均 %.1f；离开 %.1f 个钟头）；最久没结的落后 %.0f s（一个钟头 %.0f s）" % [restored,
						int(stats.get("waiting_restore", 0)), skipped, skipped / restored, away_h, float(stats["stalest_s"]), hour_s])
				if skipped / restored < 1.5:
					_finish(false, "离开 26 个钟头以上该只补最后 24 个钟头（每本该跳过两个钟头以上，实为 %.2f）" % (skipped / restored))
					return
				_shot("7_home_again")
				_goto(Phase.DONE)
			elif in_phase > 240.0:
				_finish(false, "240 s 里 2051 的账没补回 / 没补结（%s）" % [stats])
		Phase.DONE:
			_finish(true, "2051 主码头上船 → 换锚线前 10 km → 飞过换锚线（相机在真球面上跳了 %.3f m、那一帧前后 %.1f ms、跨群最慢 %.1f ms）→ 2050 主码头靠泊下船（地方时、有人出门）→ 2051 降到远景、拨钟 26 h → 飞回、2051 重新载入、停船手动换锚（相机挪了 %.4f m）→ 2051 的账补回、只补最后一天" % [cross_jump_m, shift_frame_ms, cross_max_ms, manual_jump_m])
