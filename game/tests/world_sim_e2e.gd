extends Node
## 看不见的地方也在按自己的逻辑运转（world_seed42.tscn，开局那群 #6329），只通过输入（动作、鼠标、转视角）操作：
## 清早 05:45 主码头出生 → M 开地图点到村 A 旁（主码头 2–6 km、主岛上户数最多的村：#6329 是 2.5 km 外的村003）：卯时一到有人出门，记下仓、水缸、账
## → 点回主码头、上船掌舵、换疾档飞到场景配的目的码头（#6329 是岛 #13 的码头 #28，23 km；从泊位外 400 m 的进场点进港）
##   （途中村 A 的村民散了、账收回远处账房，远处的账接着记）
## → 下船，F4 快进待到 11 点（途中断言村 A 与村 B——主码头 7–13 km、户数最多的村，#6329 是 12 km 外的村002——的账都在远处推进）→ F4 关
## → 上船飞回主码头 → 点地图回村 A：钟点一路连续（没倒流、没跳）、HUD 与全局钟一致；
##   仓里的粮按规矩变了（起初 + 收 − 播 − 吃，对得上账）、水缸有水；村民散在各自的岗位上，不在自家门口扎堆；地块都回到了农田里。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/world_sim_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 2400.0  # 模拟秒
const START_HOUR := 5.75
const STAY_UNTIL_HOUR := 11.0  # 在目的码头快进待到这个钟点再飞回去（船 20 m/s，疾档回程三个多钟头，得赶在收工前回到村里）
# 村 A：主码头 2–6 km、主岛上户数最多的村（不算邑治：它就在主码头旁）；村 B：主码头 7–13 km、户数最多的村，这一趟一直在远处
const A_MIN_M := 2000.0
const A_MAX_M := 6000.0
const B_MIN_M := 7000.0
const B_MAX_M := 13000.0
# 远处账房每帧的预算：游戏里 0.5 ms。e2e 跑的是 Debug 的扩展，远处结算比 Release 慢三十来倍——开局那群 1474 处村落每帧要约 1 ms
# （Release 下约 0.03 ms，`zhouzhu_site_survey --sim 24` 量的），0.5 ms 追不上、越落越多；这里调到 2 ms 查账的规矩（Release 下花不满，调了也一样）
const HINTERLAND_BUDGET_MS := 2.0
const APPROACH_OUT_M := 400.0  # 进场点：泊位沿栈桥往岸外这么远（从岸外进港，别从岛上空斜插下来撞坪）
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const CRUISE_CLEARANCE := 60.0
const SLOW_DOWN_AT := 2000.0

enum Phase {
	SETTLE, GO_A, A_MORNING, GO_DOCK, AT_DOCK,
	WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, GEAR_UP, CRUISE, APPROACH, MOORING, WALK_ASHORE,
	LAPSE, LAPSE_OFF,
	WALK_TO_SHIP_2, TAKE_HELM_2, CLIMB_OUT_2, GEAR_UP_2, CRUISE_2, APPROACH_2, MOORING_2, WALK_ASHORE_2,
	GO_A_2, A_AFTERNOON, DONE,
}

var main: Expedition
var world: World
var player: Player
var ship: Ship
var sites: SiteStream
var hinterland: Hinterland
var map: WorldMap

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var held := {}
var waypoints: Array[Vector3] = []
var climb_from := 0.0
var map_step := 0
var map_target := Vector3.ZERO
var village_a_pos := Vector3.ZERO
var b_pos := Vector3.ZERO
var VILLAGE_A := -1
var VILLAGE_B := -1
var approach_leg := 0

# 钟
var last_clock := -1.0
var clock_checks := 0
var hud_mismatch := 0
var left_at := 0.0  # 离开村 A 时的钟
# 村 A 出门时的账（近处 Villagers 的）
var base := {}
var saw_collapse := false
var far_a_first := {}  # 村 A 在远处头一回看到的账
var far_a_last := {}
var b_first := {}  # 村 B（一直在远处）头一回看到的账
var b_last := {}
var far_checks := 0
var lapse_off_at := -1.0  # 关掉快进时的模拟秒（之后给远处一段时间补账）
var stale_max := 0.0  # 不快进时远处落后得最多的一次
var log_lines: Array[String] = []


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)  # 无头时根视口默认很小，地图上的点没处可点
	GameClock.restart(START_HOUR)  # 从清早开始：场景建起来之前拨，账房从这一秒记起
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	sites = main.get_node("SiteStream")
	hinterland = main.get_node("Hinterland")
	hinterland.budget_ms = maxf(hinterland.budget_ms, HINTERLAND_BUDGET_MS)
	map = main.get_map()
	var info: Dictionary = world.get_info()
	var home := world.get_dock_position(world.find_dock(main.origin_dock))
	var a_hh := 0
	var b_hh := 0
	for v in info["villages"]:
		var d := Vector2(v["pos"][0] - home.x, v["pos"][1] - home.z).length()
		if int(v["island"]) != 0 or bool(v.get("seat", false)):
			continue
		if d >= A_MIN_M and d <= A_MAX_M and int(v["households"]) > a_hh:
			a_hh = int(v["households"])
			VILLAGE_A = int(v["id"])
			village_a_pos = Vector3(v["pos"][0], 0, v["pos"][1])
		if d >= B_MIN_M and d <= B_MAX_M and int(v["households"]) > b_hh:
			b_hh = int(v["households"])
			VILLAGE_B = int(v["id"])
			b_pos = Vector3(v["pos"][0], 0, v["pos"][1])
	_log("群 #%d：出生 %s，钟 %s；村%03d（%d 户）在 %s（%.0f m），村%03d（%d 户）%.1f km" % [StoryStart.get_spawn_node(), player.global_position, GameClock.get_clock_text(), VILLAGE_A, a_hh, village_a_pos,
		Vector2(village_a_pos.x - player.global_position.x, village_a_pos.z - player.global_position.z).length(),
		VILLAGE_B, b_hh, Vector2(b_pos.x - player.global_position.x, b_pos.z - player.global_position.z).length() / 1000.0])


func _log(line: String) -> void:
	print("world_sim: " + line)


# ---------------------------------------------------------------- 输入

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


func _mouse(button: MouseButton, at: Vector2) -> void:
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = button
		ev.position = at
		ev.global_position = at
		ev.pressed = pressed
		Input.parse_input_event(ev)


func _press_key_action(action: String) -> void:
	for pressed in [true, false]:
		var ev := InputEventAction.new()
		ev.action = action
		ev.pressed = pressed
		Input.parse_input_event(ev)


# ---------------------------------------------------------------- 截图

func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _shot(shot_name: String) -> void:
	pending_shot = shot_name
	shot_wait = 3


# ---------------------------------------------------------------- 小工具

func _standing_on_ground() -> bool:
	var at := player.global_position
	var ground: float = world.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.0 and world.is_ready_at(at)


func _folk_a() -> Villagers:
	var v := sites.get_village(VILLAGE_A)
	return v.get_villagers() if v != null else null


func _count_phase(folk: Villagers, which: int) -> int:
	var n := 0
	for i in folk.get_villager_count():
		n += 1 if folk.get_villager_phase(i) == which else 0
	return n


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


func _look_at(target: Vector3) -> void:
	var to := target - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


func _terrain_ahead(berth: Vector3) -> float:
	var from := ship.global_position
	var dir := Vector3(berth.x - from.x, 0, berth.z - from.z)
	var flat := dir.length()
	dir = dir.normalized()
	var top := -INF
	var d := 0.0
	while d <= minf(2000.0, flat):
		for side in [-60.0, 0.0, 60.0]:
			var p: Vector3 = from + dir * d + Vector3(-dir.z, 0, dir.x) * side
			var h: float = world.get_surface_height(p.x, p.z)
			if not is_nan(h):
				top = maxf(top, h)
		d += 50.0
	return top


## p_hug：离目标这么远以外才贴着地形留余量（朝岸外的进场点飞时一路都留）
func _fly_toward(berth: Vector3, p_hug := 600.0) -> bool:
	var to := berth - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))
	var flat := Vector2(to.x, to.z).length()
	var want_y := berth.y + 8.0
	if flat > p_hug:
		var ground := _terrain_ahead(berth)
		if ground > -INF:
			want_y = maxf(want_y, ground + CRUISE_CLEARANCE)
	var climb := want_y - ship.global_position.y
	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 2.0)
	_hold("descend", climb < -2.0)
	_hold("move_forward", flat > 30.0 and absf(bearing) < 60.0 and climb < 40.0)
	return to.length() < main.get_dock_range() - 5.0


## 开地图，对着目标放大几格，左键点它；瞬移完进 p_next
func _run_map_click(p_next: Phase) -> void:
	match map_step:
		0:
			_press_key_action("map")
			map_step = 1
		1, 2, 3, 4, 5, 6, 7, 8:
			if main.is_map_open():
				_mouse(MOUSE_BUTTON_WHEEL_DOWN, Vector2(640, 360))
				map_step += 1
		9, 10, 11, 12, 13, 14, 15, 16:
			_mouse(MOUSE_BUTTON_WHEEL_UP, map.world_to_screen(map_target.x, map_target.z))
			map_step += 1
		17:
			_mouse(MOUSE_BUTTON_LEFT, map.world_to_screen(map_target.x, map_target.z))
			map_step = 18
		_:
			if not main.is_map_open():
				_goto(p_next)
			elif elapsed - phase_started > 5.0:
				_finish(false, "点了地图没瞬移（%s）" % main.get_notice())


# ---------------------------------------------------------------- 钟

## 每帧：全局钟只往前走、不跳（快进时按倍率）；HUD 上的钟点就是全局钟；近处村子读的也是它
func _check_clock(delta: float) -> bool:
	var now: float = GameClock.get_seconds()
	if last_clock >= 0.0:
		var step := now - last_clock
		var most: float = delta * GameClock.get_rate() * 1.01 + 1e-6
		if step < -1e-6:
			_finish(false, "钟倒流了：%.3f → %.3f" % [last_clock, now])
			return false
		if step > most:
			_finish(false, "钟一帧跳了 %.3f s（该至多 %.3f）" % [step, most])
			return false
	last_clock = now
	var status: String = main.get_status_text()
	if status.contains(GameClock.get_clock_text()):
		hud_mismatch = 0
	else:
		hud_mismatch += 1
		if hud_mismatch > 3:
			_finish(false, "HUD 上的钟点与全局钟不一样：「%s」vs「%s」" % [status, GameClock.get_clock_text()])
			return false
	for v in sites.get_children():
		if v is WorldVillage and v.get_villagers() != null and absf(v.get_villagers().get_hour() - GameClock.get_hour()) > 1e-6:
			_finish(false, "%s 的钟点和全局钟不一样" % v.get_site_name())
			return false
	clock_checks += 1
	return true


## 远处的账：村 A 散了之后账收回账房（不再在近处），账上的钟跟着全局钟往前记；村 B 一直在远处，也在记
func _check_far() -> bool:
	var a: Dictionary = hinterland.get_ledger_summary(VILLAGE_A, false)
	var b: Dictionary = hinterland.get_ledger_summary(VILLAGE_B, false)
	if not saw_collapse and sites.get_village(VILLAGE_A) != null and sites.get_village(VILLAGE_A).has_folk():
		return true
	if not saw_collapse:
		if not a.get("opened", false) or a["deployed"]:
			_finish(false, "村%03d 的村民散了，账却没收回远处账房：%s" % [VILLAGE_A, a])
			return false
		saw_collapse = true
		_log("村%03d 的村民散了（%s）：账收回远处账房，地块 %d 块、粮 %d、水缸 %.1f" % [VILLAGE_A, GameClock.get_clock_text(), a["plots"], a["grain"], a["water"]])
	var stale: float = GameClock.get_seconds() - float(a["clock"])
	var step: float = hinterland.settle_hours * GameClock.get_day_seconds() / 24.0
	# 远处每处落后满半个钟头就轮到它，预算紧时再多落后一轮。快进时每帧的预算不变，远处会越落越多：
	# 那时只要求账上的钟在往前走；关了快进 90 s 以后（补账的时间）再查落后多少
	var catching_up: bool = GameClock.is_fast_forward() or (lapse_off_at >= 0.0 and elapsed - lapse_off_at < 90.0)
	if not catching_up:
		stale_max = maxf(stale_max, stale)
		if stale > step * 2.5:
			_finish(false, "村%03d 的账在远处落后了 %.0f s（结算步长 %.0f s）：%s" % [VILLAGE_A, stale, step, hinterland.get_stats()])
			return false
	if not far_a_last.is_empty() and float(a["clock"]) < float(far_a_last["clock"]):
		_finish(false, "村%03d 的账钟倒退了" % VILLAGE_A)
		return false
	if far_a_first.is_empty():
		far_a_first = a
	far_a_last = a
	if b.get("opened", false):
		if b["deployed"]:
			_finish(false, "村%03d 一直在远处，账却在近处" % VILLAGE_B)
			return false
		if b_first.is_empty():
			b_first = b
		b_last = b
	far_checks += 1
	return true


# ---------------------------------------------------------------- 流程

func _goto(next: Phase) -> void:
	_log("%-15s 用时 %6.1f s，钟 %s" % [Phase.keys()[phase], elapsed - phase_started, GameClock.get_clock_text()])
	phase = next
	phase_started = elapsed
	map_step = 0
	approach_leg = 0


func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s）" % Phase.keys()[phase])
		return
	if not _check_clock(delta):
		return
	if phase >= Phase.WALK_TO_SHIP and phase <= Phase.GO_A_2 and not _check_far():
		return
	match phase:
		Phase.SETTLE:
			if in_phase > 1.0 and _standing_on_ground():
				map_target = village_a_pos + Vector3(0, 0, 90)  # 村心南边 90 m，别落在房顶上
				_goto(Phase.GO_A)
			elif in_phase > 30.0:
				_finish(false, "出生 30 s 还没站稳")
		Phase.GO_A:
			_run_map_click(Phase.A_MORNING)
		Phase.A_MORNING:
			var folk := _folk_a()
			if in_phase < 1.0 or folk == null:
				if in_phase > 20.0:
					_finish(false, "瞬移到村%03d 旁 20 s，村民还没招出来" % VILLAGE_A)
				return
			if map_step == 0:
				_look_at(village_a_pos)
				_log("村%03d：%d 人（%s）；开张时全在屋里：%s" % [VILLAGE_A, folk.get_villager_count(), GameClock.get_clock_text(),
					_count_phase(folk, Villagers.INDOORS) == folk.get_villager_count()])
				map_step = 1
			elif _count_phase(folk, Villagers.WALKING) >= 3 and GameClock.get_hour() >= 6.0:
				base = folk.get_ledger_summary()
				base["people"] = folk.get_villager_count()
				left_at = GameClock.get_seconds()
				_log("村%03d：卯时 %s 有 %d 人出门；账：粮 %d（收 %d 播 %d 吃 %d），水缸 %.1f / %.0f" % [VILLAGE_A, GameClock.get_clock_text(),
					_count_phase(folk, Villagers.WALKING), base["grain"], base["harvested"], base["sown"], base["eaten"], base["water"], folk.get_water_cap()])
				_shot("1_village_a_morning")
				map_target = main.get_spawn_point()
				_goto(Phase.GO_DOCK)
			elif in_phase > 120.0:
				_finish(false, "村%03d 两分钟里没人出门（%s）" % [VILLAGE_A, GameClock.get_clock_text()])
		Phase.GO_DOCK:
			_run_map_click(Phase.AT_DOCK)
		Phase.AT_DOCK:
			if in_phase > 1.0 and _standing_on_ground():
				var berth: Transform3D = main.get_origin_berth()
				waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
				_goto(Phase.WALK_TO_SHIP)
			elif in_phase > 30.0:
				_finish(false, "点回主码头 30 s 还没站稳")
		Phase.WALK_TO_SHIP, Phase.WALK_TO_SHIP_2:
			if _follow_waypoints():
				_goto(phase + 1)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到船上")
		Phase.TAKE_HELM, Phase.TAKE_HELM_2:
			if main.get_state() == 1:
				climb_from = ship.global_position.y
				_goto(phase + 1)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_OUT, Phase.CLIMB_OUT_2:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 8.0:
				_hold("ascend", false)
				_goto(phase + 1)
		Phase.GEAR_UP, Phase.GEAR_UP_2:
			if ship.get_gear() >= 2:
				_goto(phase + 1)
			elif not held.has("gear_up"):
				_tap("gear_up")
		Phase.CRUISE, Phase.CRUISE_2:
			# 先飞到泊位外的进场点（从岸外进港）
			var berth := _approach_point()
			_fly_toward(berth, 0.0)
			if in_phase > 20.0 and in_phase - delta <= 20.0:
				_shot("2_cruise" if phase == Phase.CRUISE else "6_cruise_back")
			if Vector2(berth.x - ship.global_position.x, berth.z - ship.global_position.z).length() < SLOW_DOWN_AT:
				_goto(phase + 1)
		Phase.APPROACH, Phase.APPROACH_2:
			if ship.get_gear() > 0 and not held.has("gear_down"):
				_tap("gear_down")
			if approach_leg == 0:
				var p := _approach_point()
				_fly_toward(p, 0.0)
				if Vector2(p.x - ship.global_position.x, p.z - ship.global_position.z).length() < 40.0:
					approach_leg = 1
			elif _fly_toward(_flight_berth()):
				_release_all()
				_tap("interact")
				_goto(phase + 1)
		Phase.MOORING, Phase.MOORING_2:
			if main.get_state() == 0:
				var berth: Transform3D = main.get_destination_berth() if phase == Phase.MOORING else main.get_origin_berth()
				var spawn: Vector3 = world.get_dock_spawn(world.find_dock(main.destination_dock)).origin if phase == Phase.MOORING else main.get_spawn_point()
				waypoints = [ship.global_position, berth.origin + berth.basis.x * 0.8, spawn]
				_goto(phase + 1)
			elif in_phase > 5.0 and main.get_state() == 1:
				_tap("interact")
			elif in_phase > 40.0:
				_finish(false, "40 s 没靠上泊位（%s）" % main.get_prompt())
		Phase.WALK_ASHORE:
			if _follow_waypoints():
				_release_all()
				if main.has_arrived() and _standing_on_ground():
					_log("到了码头 #%d（%s）：离村%03d %.1f km" % [main.destination_dock, GameClock.get_clock_text(), VILLAGE_A,
						Vector2(village_a_pos.x - player.global_position.x, village_a_pos.z - player.global_position.z).length() / 1000.0])
					_shot("3_far_dock")
					_press_key_action("time_lapse")  # F4 快进
					_goto(Phase.LAPSE)
				elif in_phase > 20.0:
					_finish(false, "走完了路点，但没判定抵达")
			elif in_phase > 60.0:
				_finish(false, "60 s 没走下船")
		Phase.LAPSE:
			if in_phase > 0.2 and not GameClock.is_fast_forward():
				_finish(false, "按了 F4 钟却没快进（%s）" % DeveloperTools.get_notice())
				return
			if in_phase > 3.0 and in_phase - delta <= 3.0:
				_shot("4_time_lapse")
			if GameClock.get_hour() >= STAY_UNTIL_HOUR:
				_press_key_action("time_lapse")
				lapse_off_at = elapsed
				_goto(Phase.LAPSE_OFF)
			elif in_phase > 200.0:
				_finish(false, "快进 200 s 还没到 %.1f 点（%s）" % [STAY_UNTIL_HOUR, GameClock.get_clock_text()])
		Phase.LAPSE_OFF:
			if GameClock.is_fast_forward():
				if in_phase > 1.0:
					_finish(false, "再按 F4 没关掉快进")
				return
			# 远处的账在推进：村 A 与村 B 的账钟都跟上了全局钟，村 A 在远处收了粮、两处都开过饭
			if far_a_first.is_empty() or b_first.is_empty():
				_finish(false, "远处的账没看到（村 A %s，村 B %s）" % [far_a_first, b_first])
				return
			var a_gain: int = int(far_a_last["harvested"]) - int(far_a_first["harvested"])
			var a_ate: int = int(far_a_last["eaten"]) - int(far_a_first["eaten"])
			var a_span: float = float(far_a_last["clock"]) - float(far_a_first["clock"])
			var b_done: int = int(b_last["done"]) - int(b_first["done"])
			var b_span: float = float(b_last["clock"]) - float(b_first["clock"])
			_log("在远处待到 %s：村%03d 的账从 %.0f s 记到 %.0f s，收 +%d、吃 +%d、水缸 %.1f；村 B 的账推进了 %.0f s（%d 人），干完 +%d 件活；账房 %s" % [
				GameClock.get_clock_text(), VILLAGE_A, far_a_first["clock"], far_a_last["clock"], a_gain, a_ate, far_a_last["water"],
				b_span, b_last["people"], b_done, hinterland.get_stats()])
			# 快进时远处每帧仍只花那么多预算，Debug 下会落后几个钟头（见 README）：这里只要求两处的账都在往前记、村 A 在远处收了粮、村 B 有活干完；
			# 饭点、账对不对得上，等飞回去展开时（先补到此刻）一并查
			if a_gain <= 0 or b_done <= 0 or a_span < GameClock.get_day_seconds() * 0.1 or b_span < GameClock.get_day_seconds() * 0.1:
				_finish(false, "远处的账没按规矩推进")
				return
			var berth: Transform3D = main.get_destination_berth()
			waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
			_goto(Phase.WALK_TO_SHIP_2)
		Phase.WALK_ASHORE_2:
			if _follow_waypoints():
				_release_all()
				if _standing_on_ground():
					map_target = village_a_pos + Vector3(0, 0, 90)
					_goto(Phase.GO_A_2)
				elif in_phase > 20.0:
					_finish(false, "回到主码头没站稳")
			elif in_phase > 60.0:
				_finish(false, "60 s 没走下船")
		Phase.GO_A_2:
			_run_map_click(Phase.A_AFTERNOON)
		Phase.A_AFTERNOON:
			var folk := _folk_a()
			if in_phase < 1.0 or folk == null:
				if in_phase > 20.0:
					_finish(false, "回到村%03d 旁 20 s，村民还没招出来" % VILLAGE_A)
				return
			if map_step == 0:
				_look_at(village_a_pos)
				map_step = 1
				return
			if in_phase < 2.0:
				return  # 摆好一帧再看
			_check_return(folk)
		Phase.DONE:
			if in_phase > 0.5:
				_finish(true, "群 #%d：村%03d 出门 → 飞 %.1f km 到码头 #%d 待到下午 → 飞回：钟点连续（查了 %d 帧）、账对得上、村民在各自岗位上；远处的账一路在记（不快进时最多落后 %.0f s）" % [
					StoryStart.get_spawn_node(), VILLAGE_A, main.get_origin_berth().origin.distance_to(main.get_destination_berth().origin) / 1000.0, main.destination_dock,
					clock_checks, stale_max])


func _flight_berth() -> Vector3:
	return main.get_destination_berth().origin if phase <= Phase.APPROACH else main.get_origin_berth().origin


## 进场点：这一程要去的泊位沿栈桥往岸外 APPROACH_OUT_M（岸缘点 → 泊位的方向），高同泊位
func _approach_point() -> Vector3:
	var berth := _flight_berth()
	var dock_id: int = main.destination_dock if phase <= Phase.APPROACH else main.origin_dock
	var root := world.get_dock_position(world.find_dock(dock_id))
	var out := Vector3(berth.x - root.x, 0, berth.z - root.z).normalized()
	return berth + out * APPROACH_OUT_M


func _check_return(folk: Villagers) -> void:
	var now: Dictionary = folk.get_ledger_summary()
	var away_h: float = (GameClock.get_seconds() - left_at) * 24.0 / GameClock.get_day_seconds()
	if absf(float(now["clock"]) - GameClock.get_seconds()) > 1.0:
		_finish(false, "展开后村%03d 的账没记到此刻：%.1f vs %.1f" % [VILLAGE_A, now["clock"], GameClock.get_seconds()])
		return
	# 账：起初 + 收 − 播 − 吃
	var d_h: int = int(now["harvested"]) - int(base["harvested"])
	var d_s: int = int(now["sown"]) - int(base["sown"])
	var d_e: int = int(now["eaten"]) - int(base["eaten"])
	var d_hungry: int = int(now["hungry"]) - int(base["hungry"])
	var expect: int = int(base["grain"]) + d_h - d_s - d_e
	_log("回到村%03d（%s，离开了 %.1f 个钟头）：粮 %d → %d（收 +%d、播 −%d、吃 −%d，应为 %d；午饭没吃上 %d 人次），水缸 %.1f → %.1f（挑进 +%.1f）" % [
		VILLAGE_A, GameClock.get_clock_text(), away_h, base["grain"], now["grain"], d_h, d_s, d_e, expect, d_hungry, base["water"], now["water"],
		float(now["water_in"]) - float(base["water_in"])])
	if int(now["grain"]) != expect:
		_finish(false, "粮对不上账")
		return
	# 开过饭：吃上的加没吃上的（开春青黄不接：卯时仓里只有十来份粮，午饭时扣掉给农夫留的种子就不够分，全村饿一顿——按规矩，不算错）
	if d_h <= 0 or d_e + d_hungry <= 0 or int(now["grain"]) == int(base["grain"]):
		_finish(false, "离开的这些钟头里村里没收粮、没开饭，或仓一点没变")
		return
	if float(now["water"]) <= 0.0 or float(now["water"]) > folk.get_water_cap() + 1e-6 or float(now["water_in"]) <= float(base["water_in"]):
		_finish(false, "水缸不对：%.1f / %.0f，挑进 %.1f → %.1f" % [now["water"], folk.get_water_cap(), base["water_in"], now["water_in"]])
		return
	if away_h < 8.0:
		_finish(false, "只离开了 %.1f 个钟头，不到大半天" % away_h)
		return
	# 村民：多半在外面，在自家门口扎堆的不到三成，有人在干活；在岗的（在做活或在去做活、送东西的路上，不算串门、歇着、回家）不少于四分之一
	# （只数正在做活的不够：村子大、房子多，那一刻走在去地里、送粮路上的多）
	var outside := 0
	var at_door := 0
	var working := 0
	var on_duty := 0
	for i in folk.get_villager_count():
		if folk.get_villager_phase(i) == Villagers.INDOORS:
			continue
		outside += 1
		working += 1 if folk.get_villager_phase(i) == Villagers.WORKING else 0
		on_duty += 0 if folk.get_villager_task(i) in ["chat", "wait", "go_home"] else 1
		var p := folk.get_villager_position(i)
		var door := folk.get_villager_door(i)
		if Vector2(p.x - door.x, p.z - door.z).length() < 3.0:
			at_door += 1
		var ground: float = world.get_surface_height(p.x, p.z)
		if is_nan(ground) or absf(ground - p.y) > 0.6:
			_finish(false, "%s 脚下没地（%s）" % [folk.get_villager_name(i), p])
			return
	var jobs := {}
	for i in folk.get_villager_count():
		if folk.get_villager_phase(i) != Villagers.INDOORS:
			var key := "%s·%s" % [folk.get_villager_job(i), folk.get_villager_task(i)]
			jobs[key] = jobs.get(key, 0) + 1
	_log("村民 %d 人：在外 %d、在干活 %d、在岗 %d、在自家门口 %d；%s" % [folk.get_villager_count(), outside, working, on_duty, at_door, jobs])
	if outside * 2 < folk.get_villager_count() or at_door * 3 > outside or working == 0 or on_duty * 4 < outside:
		_finish(false, "村民不在各自岗位上（在外 %d / %d，门口 %d，干活 %d，在岗 %d）" % [outside, folk.get_villager_count(), at_door, working, on_duty])
		return
	# 地：认领的地都回到了农田里
	var farm: Farm = world.get_node("Farm")
	var cells: Array = folk.get_claimed_cells()
	var back := 0
	for cell in cells:
		back += 1 if farm.get_plot(cell) != Farm.PLOT_WILD else 0
	_log("认领的地 %d 块，农田里有 %d 块（熟 %d / 长着 %d）" % [cells.size(), back, farm.get_plot_count(Farm.PLOT_RIPE), farm.get_plot_count(Farm.PLOT_GROWING)])
	if cells.is_empty() or back < cells.size() - 1:
		_finish(false, "地块没回到农田里")
		return
	_shot("5_village_a_afternoon")
	_goto(Phase.DONE)


func _finish(ok: bool, why: String) -> void:
	_release_all()
	for action in held.keys():
		Input.action_release(action)
	set_physics_process(false)
	print("world_sim: %s —— %s（阶段 %s，模拟 %.1f s，钟 %s；玩家 %s，船 %s；账房 %s）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, GameClock.get_clock_text(), player.global_position, ship.global_position,
		hinterland.get_stats()])
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
