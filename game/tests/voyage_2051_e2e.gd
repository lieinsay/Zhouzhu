extends Node
## 地貌与聚落计划 P0 的验收：开船去 #2051——按需生成、能落地（world_seed42.tscn，剧情模式 seed42，世界存档目录用测试自己的 user://worlds/e2e_2051，开局清空）。
## 2026-09-29 以前 #2051 是开局、细版在仓库里；开局换到 #6329 以后它只是行星上普通的一群，走近了照样按需生成（不走缓存：开局把这个测试世界生成过的群清掉）。
## 开局那群的主码头出生 → 开发者瞬移（Expedition.request_planet_jump，行星地图点一下走的就是它）去 2051 的近邻 #2050（离 2051 群心 114 km，没生成过、当场生成）
## → 走上 2050 主码头的船、掌舵、升空、换疾档 → 船瞬移到 2050 陆地往 2051 那边 10 km 外的云海上空，船头朝 2051 的群心
## → 一路飞：船头朝着 2051、走得比 15 m/s 快（疾档八十来米每秒），它在船头方向 150 km 内，GroupProvider 按船头方向预取、当场生成（进程内的 C++ 生成器；
##   慢了的话焦点离它群心 60 km 内也会排进队），GroupStream 登记、后台载入，离 2051 的陆地比离 2050 的近了就换锚到 2051
##   （飞的是 2050 与 2051 陆地之间的云海：两群陆地等远的换锚线在路上）
## → 船瞬移到 2051 主码头外 1.5 km 的云海上空（往外一路都没有地的方向）→ 换回泊档、靠泊、下船走上停机坪：脚下是 2051 的地，
##   HUD 第一行「群 #2051」，码头边的村子铺出来、有人出门（这时下雨下雪就覆盖成晴，照 planet_voyage 的做法）。
## 记下：2050、2051 各自从排进生成到包写好的用时，飞的那一路（含 2051 生成、载入、换锚）的最慢一帧，落地的位置。
## 2051、2050 的群心从行星包（PlanetView.node_latlon / flat_of_latlon）推，主码头从各自的 world.json 找（main 那一座），不写死坐标。
## 这是 game/tests 里唯一写着 2051 的测试。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/voyage_2051_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`：每个阶段存一张截图。

const SCENE := preload("res://world_seed42.tscn")
const WORLD_DIR := "user://worlds/e2e_2051"
const TARGET := 2051  # 要去的那一群（2026-09-29 以前的开局）
const VIA := 2050  # 它最近的邻群：先瞬移到这里，再开船过去
const TIMEOUT := 4000.0  # 模拟秒
const TIMEOUT_REAL_S := 900.0  # 真实秒（生成器在真实时间里跑）
const SETTLE_TIMEOUT := 60.0
const FOLK_TIMEOUT := 90.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const CRUISE_Y := 1500.0  # 云海上空飞（两群之间没有岛；主岛岸缘几百米，别的岛浮到 1.3–1.6 km 的在群里，这一段飞的是群外的云海）
const OUT_OF_LAND := 10000.0  # 船瞬移到 2050 陆地边往外这么远

enum Phase { SETTLE, JUMP, LANDED_VIA, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, GEAR_UP, JUMP_OUT, CROSS, JUMP_IN, APPROACH, MOORING, WALK_ASHORE, CHECK, DONE }

var main: Expedition
var player: Player
var ship: Ship
var stream: GroupStream
var host: PlanetHost
var planet: PlanetView
var provider: GroupProvider

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
var start_us := Time.get_ticks_usec()

var last_tick := 0
var jump_t0 := 0
var jump_max_ms := 0.0
var cross_max_ms := 0.0
var cross_frames := 0
var gen_max_ms := 0.0  # 2051 从排进生成到包写好期间的最慢一帧
var target_state := GroupProvider.NONE
var target_queued_km := 0.0  # 2051 排进生成那一刻，焦点离它的群心多远
var target_ready_km := 0.0
var target_seconds := {}
var via_seconds := {}
var flown_from := Vector3.ZERO
var flown_m := 0.0
var shift_seen := 0
var shift_line := ""
var via_dock_id := -1
var target_dock_id := -1
var results: Array[String] = []


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	provider = main.get_node("GroupProvider")
	provider.world_dir = WORLD_DIR
	# 这个测试世界里生成过的群、离开的群的账全清掉（仓库里的不动）：2050、2051 都从「没生成过」起
	for sub in ["groups", "memory", "logs", "works"]:
		_remove_tree(WORLD_DIR.path_join(sub))
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	stream = main.get_node("GroupStream")
	host = main.get_node("PlanetHost")
	planet = main.get_node("PlanetView")
	last_tick = Time.get_ticks_usec()
	if not provider.can_generate():
		print("voyage_2051: SKIP —— 调不了生成器：%s" % provider.get_problem())
		get_tree().quit(77)


func _remove_tree(dir: String) -> void:
	if not DirAccess.dir_exists_absolute(dir):
		return
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		_remove_tree(dir.path_join(d))
	DirAccess.remove_absolute(dir)


func _log(line: String) -> void:
	print("voyage_2051: " + line)


func _finish(ok: bool, why: String) -> void:
	_release_all()
	for r in results:
		_log(r)
	print("voyage_2051: %s —— %s（阶段 %s，模拟 %.1f s，真实 %.0f s）" % ["PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, (Time.get_ticks_usec() - start_us) / 1e6])
	get_tree().quit(0 if ok else 1)


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
	var now := Time.get_ticks_usec()
	var ms := (now - last_tick) / 1000.0
	last_tick = now
	if phase == Phase.JUMP:
		jump_max_ms = maxf(jump_max_ms, ms)
	if phase == Phase.CROSS:
		cross_max_ms = maxf(cross_max_ms, ms)
		cross_frames += 1
		if target_state == GroupProvider.QUEUED or target_state == GroupProvider.RUNNING:
			gen_max_ms = maxf(gen_max_ms, ms)
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	pending_shot = shot_name
	shot_wait = 3


func _goto(next: Phase) -> void:
	_log("%-14s 用时 %6.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	phase = next
	phase_started = elapsed
	step = 0


# ---------------------------------------------------------------- 小工具

func _anchor() -> GroupWorld:
	return stream.get_group_world(stream.get_anchor_node())


func _standing_on_ground() -> bool:
	var w := _anchor()
	if w == null:
		return false
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
	return atan2(-dir.x, -dir.z)  # 船头朝 −z


## 朝目标飞：舵对准方位、按 want_y 定高度；到了 arrive 以内返回 true
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
	return to.length() < arrive


## 那一群的群心在此刻锚群的平面里（行星包的经纬度，PlanetView 按真球面换算）
func _center_flat(node: int) -> Vector3:
	var ll: Vector2 = planet.node_latlon(node)
	var p: Vector3 = planet.flat_of_latlon(ll.x, ll.y)
	p.y = 0.0
	return p


## 那一群 world.json 里标 main 的码头号（没有就第一座）
func _main_dock_id(node: int) -> int:
	var w: GroupWorld = stream.get_group_world(node)
	if w == null:
		return -1
	var first := -1
	for d in w.get_info()["docks"]:
		if first < 0:
			first = int(d["id"])
		if bool(d.get("main", false)):
			return int(d["id"])
	return first


## 地图宿主的码头号：别的群的 = 群号 × 1000 + 它自己的号
func _host_dock(node: int, id: int) -> int:
	return node * 1000 + id


# ---------------------------------------------------------------- 流程

func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（模拟 %.0f s）" % TIMEOUT)
		return
	if (Time.get_ticks_usec() - start_us) / 1e6 > TIMEOUT_REAL_S:
		_finish(false, "超时（真实 %.0f s）" % TIMEOUT_REAL_S)
		return
	match phase:
		Phase.SETTLE:
			if stream.is_ready() and _standing_on_ground() and in_phase > 1.0:
				var home := StoryStart.get_spawn_node()
				if stream.get_anchor_node() != home:
					_finish(false, "开局锚该是开局那群 #%d，实为 #%d" % [home, stream.get_anchor_node()])
					return
				if provider.is_available(TARGET) or provider.is_available(VIA):
					_finish(false, "开局 #%d、#%d 该都没有包（仓库里不带、测试世界清过）：%s / %s" % [TARGET, VIA, provider.get_group_info(TARGET), provider.get_group_info(VIA)])
					return
				_log("开局那群 #%d 主码头出生 %s；#%d 离这里 %.0f km、#%d %.0f km，都没有包" % [home, player.global_position, TARGET, planet.distance_to_node(TARGET) / 1000.0,
					VIA, planet.distance_to_node(VIA) / 1000.0])
				_shot("1_spawn")
				jump_t0 = Time.get_ticks_usec()
				jump_max_ms = 0.0
				last_tick = jump_t0
				if not main.request_planet_jump(VIA):
					_finish(false, "瞬移去 #%d 起不了：%s" % [VIA, main.get_jump_state()["last"]])
					return
				_goto(Phase.JUMP)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "%.0f s 没在开局那群站稳" % SETTLE_TIMEOUT)
		Phase.JUMP:
			var js: Dictionary = main.get_jump_state()
			if int(js["stage"]) != 0:
				if (Time.get_ticks_usec() - jump_t0) / 1e6 > 300.0:
					_finish(false, "瞬移 300 s（真实）没好（%s）" % js)
				return
			var last: Dictionary = js["last"]
			if int(last.get("node", -1)) != VIA:
				return
			if not bool(last.get("ok", false)):
				_finish(false, "去 #%d 失败：%s" % [VIA, last.get("why", "?")])
				return
			via_seconds = provider.get_request(VIA)
			var line := "瞬移去 #%d（没生成过）：从点下去到能走 %.1f s（生成 %.1f s）、最慢一帧 %.1f ms；落在%s" % [VIA, (Time.get_ticks_usec() - jump_t0) / 1e6,
				float(via_seconds.get("run_s", 0.0)), jump_max_ms, last["where"]]
			results.append(line)
			_log(line)
			_goto(Phase.LANDED_VIA)
		Phase.LANDED_VIA:
			if stream.get_anchor_node() != VIA or not _standing_on_ground():
				if in_phase > 60.0:
					_finish(false, "落在 #%d 60 s：锚 #%d、站稳 %s" % [VIA, stream.get_anchor_node(), _standing_on_ground()])
				return
			via_dock_id = _main_dock_id(VIA)
			var berth: Transform3D = host.get_dock_berth_by_id(_host_dock(VIA, via_dock_id))
			if via_dock_id < 0 or ship.global_position.distance_to(berth.origin) > 40.0:
				_finish(false, "#%d 的主码头 #%d：船该停在它的泊位上（船 %s、泊位 %s）" % [VIA, via_dock_id, ship.global_position, berth.origin])
				return
			_log("站在 #%d 主码头 #%d 的坪上 %s；船在泊位上" % [VIA, via_dock_id, player.global_position])
			_shot("2_via_dock")
			waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
			_goto(Phase.WALK_TO_SHIP)
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
				_log("换到%s档，极速 %.1f m/s" % [ship.get_gear_name(), ship.get_max_speed()])
				_goto(Phase.JUMP_OUT)
			elif not held.has("gear_up"):
				_tap("gear_up")
		Phase.JUMP_OUT:
			# 2050 群心 → 2051 群心的连线上：从 2050 的群心往外走，最后一块 2050 的地再往外 OUT_OF_LAND，云海上空、船头朝 2051 的群心
			if step == 0:
				var w := _anchor()
				var a := _center_flat(VIA)
				var b := _center_flat(TARGET)
				var dir := (b - a).normalized()
				var span := (b - a).length()
				var edge := 0.0
				var d := 0.0
				while d < span * 0.5:
					var p := a + dir * d
					if not is_nan(w.get_surface_height(p.x, p.z)):
						edge = d
					d += 100.0
				var at := a + dir * (edge + OUT_OF_LAND)
				at.y = CRUISE_Y
				ship.set_pose(at, _yaw_toward(dir))
				flown_from = at
				_log("两群群心相距 %.1f km；#%d 的地沿连线铺到 %.1f km，船瞬移到 %.1f km 处（离 #%d 群心 %.1f km）" % [span / 1000.0, VIA, edge / 1000.0,
					(edge + OUT_OF_LAND) / 1000.0, TARGET, (span - edge - OUT_OF_LAND) / 1000.0])
				step = 1
			elif in_phase > 1.0:
				_shot("3_out")
				cross_max_ms = 0.0
				cross_frames = 0
				shift_seen = main.get_shift_count()
				_goto(Phase.CROSS)
		Phase.CROSS:
			_fly_toward(_center_flat(TARGET), CRUISE_Y, 50.0)
			flown_m = Vector2(ship.global_position.x - flown_from.x, ship.global_position.z - flown_from.z).length() if main.get_shift_count() == shift_seen else flown_m
			# 2051 的生成：排进队、在做、做好
			var r: Dictionary = provider.get_request(TARGET)
			var st := int(r["state"])
			if st != target_state:
				var km := planet.distance_to_node(TARGET) / 1000.0
				if st == GroupProvider.QUEUED or (st == GroupProvider.RUNNING and target_state == GroupProvider.NONE):
					target_queued_km = km
					_log("#%d 排进生成的队（优先级 %d）：焦点离它的群心 %.1f km（飞了 %.1f km）" % [TARGET, int(r.get("priority", -1)), km, flown_m / 1000.0])
				if st == GroupProvider.READY:
					target_ready_km = km
					target_seconds = r
					_log("#%d 生成好了：排队 %.2f s、生成 %.2f s（%s）；此刻离群心 %.1f km" % [TARGET, float(r.get("queued_s", 0.0)), float(r.get("run_s", 0.0)), r.get("seconds", {}), km])
				if st == GroupProvider.FAILED:
					_finish(false, "#%d 生成失败：%s" % [TARGET, r.get("error", "?")])
					return
				target_state = st
			# 换锚到 2051
			if main.get_shift_count() > shift_seen:
				var t := stream.get_group_state(TARGET)
				var v := stream.get_group_state(VIA)
				if stream.get_anchor_node() != TARGET:
					_finish(false, "飞着换锚换到了 #%d，该是 #%d" % [stream.get_anchor_node(), TARGET])
					return
				if target_state != GroupProvider.READY:
					_finish(false, "换锚到 #%d 时它的生成状态是 %d（该已生成好）" % [TARGET, target_state])
					return
				shift_line = "换锚到 #%d：飞了 %.1f km，离 #%d 陆地 %.1f km、#%d 陆地 %.1f km（档 %d、载入 %s）" % [TARGET, flown_m / 1000.0, TARGET, float(t["land_km"]), VIA,
					float(v["land_km"]), int(t["tier"]), t["loaded"]]
				_log(shift_line)
				_shot("4_after_shift")
				_goto(Phase.JUMP_IN)
			elif int(in_phase * 60.0) % 3600 == 0:
				var t := stream.get_group_state(TARGET)
				_log("飞着：%.1f km，离 #%d 群心 %.1f km，速 %.1f m/s；#%d 的生成状态 %d%s" % [flown_m / 1000.0, TARGET, planet.distance_to_node(TARGET) / 1000.0, ship.get_speed(),
					TARGET, target_state, "" if t.is_empty() else "、档 %d、离陆地 %.1f km" % [int(t["tier"]), float(t["land_km"])]])
			if in_phase > 3500.0:
				_finish(false, "飞了 %.0f s 还没换锚到 #%d（生成状态 %d，%s）" % [in_phase, TARGET, target_state, stream.get_group_state(TARGET)])
		Phase.JUMP_IN:
			# 2051 的地面块在后台挂上以前先等它成细版、载入完
			var t := stream.get_group_state(TARGET)
			if step == 0:
				if int(t["tier"]) != GroupStream.TIER_DETAIL or not bool(t["loaded"]):
					# 接着朝群心飞到离它的陆地 8 km（细版 30 km 进），再近了就停在云海上空等（群里有岛浮到一千多米，别撞上）
					if float(t["land_km"]) > 8.0:
						_fly_toward(_center_flat(TARGET), CRUISE_Y, 50.0)
					else:
						_release_all()
					if in_phase > 300.0:
						_finish(false, "换锚后 300 s #%d 还没成细版、载入完（%s）" % [TARGET, t])
					return
				target_dock_id = _main_dock_id(TARGET)
				if target_dock_id < 0:
					_finish(false, "#%d 的 world.json 里没有码头" % TARGET)
					return
				# 省点时间：船瞬移到 2051 主码头外 1.5 km 的云海上空（往外一路都没有地的那个方向），船头朝码头
				var berth: Transform3D = host.get_dock_berth_by_id(_host_dock(TARGET, target_dock_id))
				var w := _anchor()
				var best_dir := Vector3.ZERO
				var best_clear := -1
				for k in 32:
					var ang := TAU * k / 32.0
					var dir := Vector3(cos(ang), 0.0, sin(ang))
					var clear := 0
					for d in range(100, 1600, 100):
						var p := berth.origin + dir * float(d)
						if is_nan(w.get_surface_height(p.x, p.z)):
							clear += 1
					if clear > best_clear:
						best_clear = clear
						best_dir = dir
				var at := berth.origin + best_dir * 1500.0
				at.y = berth.origin.y + 60.0
				ship.set_pose(at, _yaw_toward(-best_dir))
				_release_all()
				_log("#%d 成了细版、载入完；船瞬移到主码头 #%d 外 1.5 km（泊位 %s）" % [TARGET, target_dock_id, berth.origin])
				step = 1
			elif in_phase > 1.0:
				_goto(Phase.APPROACH)
		Phase.APPROACH:
			if ship.get_gear() > 0 and not held.has("gear_down"):
				_tap("gear_down")
			var berth: Transform3D = host.get_dock_berth_by_id(_host_dock(TARGET, target_dock_id))
			var flat := Vector2(berth.origin.x - ship.global_position.x, berth.origin.z - ship.global_position.z).length()
			var want_y := berth.origin.y + (8.0 if flat < 400.0 else 40.0)
			# 新聚落可在主泊位 40 m 内另设船台；进入主泊位近处后再按 E，
			# 否则最近泊位选择会正确地把船靠到那座船台。
			if _fly_toward(berth.origin, want_y, 12.0):
				_release_all()
				_shot("5_approach")
				_tap("interact")
				_goto(Phase.MOORING)
			elif in_phase > 180.0:
				_finish(false, "180 s 没飞到 #%d 的泊位（还差 %.0f m）" % [TARGET, flat])
		Phase.MOORING:
			if main.get_state() == 0:
				_release_all()
				var id := _host_dock(TARGET, target_dock_id)
				var berth: Transform3D = host.get_dock_berth_by_id(id)
				waypoints = [ship.global_position, berth.origin + berth.basis.x * 0.8, host.get_dock_spawn_by_id(id).origin]
				_goto(Phase.WALK_ASHORE)
			elif in_phase > 5.0 and main.get_state() == 1:
				_tap("interact")
			elif in_phase > 40.0:
				_finish(false, "40 s 没靠上 #%d 的泊位（状态 %d，%s）" % [TARGET, main.get_state(), main.get_prompt()])
		Phase.WALK_ASHORE:
			if _follow_waypoints():
				_release_all()
				if _standing_on_ground():
					_log("下了船站在 #%d 主码头 #%d 的停机坪上 %s" % [TARGET, target_dock_id, player.global_position])
					_shot("6_ashore")
					_goto(Phase.CHECK)
				elif in_phase > 20.0:
					_finish(false, "走完了路点还没站稳（%s）" % player.global_position)
			elif in_phase > 60.0:
				var hits := []
				for k in player.get_slide_collision_count():
					var hit := player.get_slide_collision(k)
					hits.append([str(hit.get_collider().get_path()), hit.get_normal()])
				_finish(false, "60 s 没走下船：玩家 %s，路点 %s，碰撞 %s" % [player.global_position, waypoints, hits])
		Phase.CHECK:
			var status := main.get_status_text()
			if step == 0:
				if stream.get_anchor_node() != TARGET or host.get_anchor_node() != TARGET:
					_finish(false, "落地后锚该是 #%d（GroupStream %d、PlanetHost %d）" % [TARGET, stream.get_anchor_node(), host.get_anchor_node()])
					return
				if not status.begins_with("群 #%d" % TARGET):
					_finish(false, "HUD 第一行该以「群 #%d」开头：%s" % [TARGET, status.get_slice("\n", 0)])
					return
				var info: Dictionary = provider.get_group_info(TARGET)
				if String(info.get("source", "")) != "cache":
					_finish(false, "#%d 的包该是这回生成进测试世界的（%s）" % [TARGET, info])
					return
				_log("HUD：%s；包在 %s" % [status.get_slice("\n", 0), info.get("dir", "?")])
				step = 1
			# 码头边铺出来的村招了人、有人在外面；此刻下雨下雪（村民照规矩在屋里）就覆盖成晴看他们出门
			var sites: SiteStream = stream.get_group_sites(TARGET)
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
				_log("码头边的村招了 %d 人都在屋里，此地此刻「%s」（降水 %.2f）、%s——覆盖成晴，看他们出门" % [folk_total, w.get("kind_name", "?"), float(w.get("precip", 0.0)), GameClock.get_clock_text()])
				atmo.set_weather_override("clear", 0.0)
				step = 2
			if out > 0:
				var built: Array = sites.get_built_villages()
				results.append(shift_line)
				results.append("#%d：排进生成时离群心 %.1f km、生成好时 %.1f km；排队 %.2f s、生成 %.2f s；飞的那一路 %d 帧最慢 %.1f ms，其中 #%d 生成期间最慢 %.1f ms" % [TARGET,
					target_queued_km, target_ready_km, float(target_seconds.get("queued_s", 0.0)), float(target_seconds.get("run_s", 0.0)), cross_frames, cross_max_ms, TARGET, gen_max_ms])
				results.append("落在 #%d 主码头 #%d 的坪上 %s；码头边铺出来的村 %s，%s 有 %d 人在外面（招了 %d 人）" % [TARGET, target_dock_id, player.global_position, built, village, out, folk_total])
				_shot("7_folk")
				_goto(Phase.DONE)
			elif in_phase > FOLK_TIMEOUT:
				_finish(false, "#%d 的码头边 %.0f s 里没人出门（铺出来的村 %s）" % [TARGET, FOLK_TIMEOUT, sites.get_built_villages()])
		Phase.DONE:
			if pending_shot.is_empty():
				var stats: Dictionary = provider.get_stats()
				_finish(true, "开局那群 → 瞬移 #%d（当场生成）→ 开船飞 %.1f km，#%d 路上按需生成、换锚 → 靠泊主码头 #%d、下船站在它的地上，HUD「群 #%d」、村里有人出门（这回生成了 %d 群、失败 %d）" % [
					VIA, flown_m / 1000.0, TARGET, target_dock_id, TARGET, int(stats["generated"]), int(stats["failed"])])
