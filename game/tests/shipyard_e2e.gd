extends Node
## 船坞端到端测试：加载主场景，只通过输入动作操作。
## 出生点走到画样台按 E → 船不在坞里，被拒 → 走上空艇掌舵、升空、飞向船坞、E 入坞 → 下船走下跳板到画样台
## → E 打开改装面板 → E 加一节储存区 / S 换成动力区 / W 换回 / 改驾驶区被拒 / 拆驾驶区被拒 / 拆掉动力区 → Tab 合上
## → 走上舵位按 E：没有动力区，上不了舵 → 回画样台补一节动力区 → 掌舵升空飞回泊位靠泊。
## 断言每一步的布局、船况与 Voyage 状态一路对得上。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/shipyard_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，会在每个阶段存一张截图。

const MAIN := preload("res://main.tscn")
const TIMEOUT := 400.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]

enum Phase {
	WALK_TO_BENCH, REFUSED_NO_SHIP, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, FLY_TO_YARD, DOCKING,
	WALK_DOWN, OPEN_REFIT, REFIT, WALK_UP, HELM_REFUSED, WALK_DOWN_AGAIN, OPEN_REFIT_AGAIN, RESTORE_POWER,
	WALK_UP_AGAIN, TAKE_HELM_AGAIN, CLIMB_OUT_AGAIN, FLY_TO_BERTH, MOORING, DONE,
}

var main: Voyage
var player: Player
var ship: Ship
var origin: Island
var yard: Shipyard

var phase := Phase.WALK_TO_BENCH
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var waypoints: Array[Vector3] = []
var climb_from := 0.0
var interact_frames := 0
var released_for := 0
# 改装脚本：每步 [[动作, 按住几帧]…, 之后该是的布局, 说明]；一步敲完就对布局
var steps: Array = []
var taps: Array = []
var tap_frames := 0
var tap_gap := 0
var speed_before_refit := 0.0


func _ready() -> void:
	main = MAIN.instantiate()
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	origin = main.get_node("IslandA")
	yard = origin.get_node("Shipyard")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			_shot("START")
	if not origin.has_shipyard() or not yard.is_present():
		_finish(false, "出发岛上没有船坞")
		return
	if Array(ship.get_layout()) != ["power", "bridge"]:
		_finish(false, "开局的船该是 动力区 + 驾驶区（%s）" % [ship.get_layout()])
		return
	if main.is_ship_in_yard():
		_finish(false, "开局船不该在坞里")
		return
	speed_before_refit = ship.get_max_speed()
	waypoints = [yard.get_bench_position()]
	print("shipyard_e2e: 出生点 %s，泊位 %s，坞位 %s（相距 %.0f m），画样台 %s，跳板 %s → %s，开局航速 %.1f m/s" % [
		player.global_position, ship.global_position, _cradle().origin, ship.global_position.distance_to(_cradle().origin),
		yard.get_bench_position(), yard.get_gangway_top(), yard.get_gangway_foot(), speed_before_refit])


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时")
		return
	if interact_frames > 0:
		interact_frames -= 1
		if interact_frames == 0:
			Input.action_release("interact")
	released_for = 0 if Input.is_action_pressed("interact") else released_for + 1

	match phase:
		Phase.WALK_TO_BENCH:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.REFUSED_NO_SHIP)
		Phase.REFUSED_NO_SHIP:
			if main.is_refit_open():
				_finish(false, "船不在坞里也打开了改装面板")
			elif elapsed - phase_started > 0.5:
				if not main.get_notice().contains("船坞里没有船"):
					_finish(false, "该提示船坞里没有船，实为「%s」" % main.get_notice())
				else:
					# 先回到栈桥正后方再上桥：从船坞直奔桥头会从岸崖边上走下去
					waypoints = [origin.get_spawn_transform().origin, origin.get_pier_end_position(), ship.global_position]
					_enter(Phase.WALK_TO_SHIP)
		Phase.WALK_TO_SHIP:
			if _follow_waypoints(0.6):
				_enter(Phase.TAKE_HELM)
		Phase.TAKE_HELM, Phase.TAKE_HELM_AGAIN:
			if main.get_state() == Voyage.PILOTING:
				climb_from = ship.global_position.y
				_enter(Phase.CLIMB_OUT if phase == Phase.TAKE_HELM else Phase.CLIMB_OUT_AGAIN)
			elif _walk_toward(ship.get_helm_position(), main.helm_range - 0.3):
				_tap_interact()
		Phase.CLIMB_OUT, Phase.CLIMB_OUT_AGAIN:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 6.0:
				_enter(Phase.FLY_TO_YARD if phase == Phase.CLIMB_OUT else Phase.FLY_TO_BERTH)
		Phase.FLY_TO_YARD:
			if _hop_toward(_cradle().origin, _berth().origin):
				_release_all()
				_tap_interact()
				_enter(Phase.DOCKING)
		Phase.DOCKING:
			if main.get_state() == Voyage.ON_FOOT:
				if not main.is_ship_in_yard():
					_finish(false, "入坞后船不在坞位上（离坞位 %.2f m）" % ship.global_position.distance_to(_cradle().origin))
				elif not player.is_on_floor():
					pass  # 等落到甲板上
				else:
					waypoints = [yard.get_gangway_top(), yard.get_gangway_foot(), yard.get_bench_position()]
					_enter(Phase.WALK_DOWN)
		Phase.WALK_DOWN, Phase.WALK_DOWN_AGAIN:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.OPEN_REFIT if phase == Phase.WALK_DOWN else Phase.OPEN_REFIT_AGAIN)
		Phase.OPEN_REFIT:
			if main.is_refit_open():
				if not player.is_input_blocked():
					_finish(false, "开着改装面板人还能走")
					return
				_shot("REFIT_open")
				steps = [
					[[["interact", 2]], ["power", "storage", "bridge"], "E：第 1 节后面加一节储存区"],
					[[["move_back", 2]], ["power", "power", "bridge"], "S：新加的那节换成动力区"],
					[[["move_forward", 2]], ["power", "storage", "bridge"], "W：换回储存区"],
					[[["move_right", 2], ["move_forward", 2]], ["power", "storage", "bridge"], "D 到驾驶区、W：驾驶区换不得"],
					[[["remove", 2]], ["power", "storage", "bridge"], "X：驾驶区拆不得"],
					[[["move_left", 2], ["move_left", 2], ["remove", 2]], ["storage", "bridge"], "A A X：拆掉船头的动力区"],
					[[["inventory", 2]], ["storage", "bridge"], "Tab 合上"],
				]
				_start_step()
				_enter(Phase.REFIT)
			elif elapsed - phase_started > 1.0:
				_finish(false, "在画样台按了 E，改装面板没打开（%s）" % main.get_notice())
		Phase.REFIT, Phase.RESTORE_POWER:
			if not _run_taps():
				return
			var step: Array = steps.pop_front()
			if Array(ship.get_layout()) != step[1]:
				_finish(false, "%s 之后布局该是 %s，实为 %s（提示「%s」）" % [step[2], step[1], ship.get_layout(), main.get_notice()])
				return
			print("shipyard_e2e:   %s → %s（%s）" % [step[2], ship.get_layout(), main.get_notice()])
			if step[2].contains("换不得") or step[2].contains("拆不得"):
				if not main.get_notice().contains("驾驶区"):
					_finish(false, "被拒时该说明是驾驶区的缘故，实为「%s」" % main.get_notice())
					return
			if not steps.is_empty():
				_start_step()
				return
			if main.is_refit_open() or player.is_input_blocked():
				_finish(false, "Tab 之后面板没合上或人还动不了")
			elif phase == Phase.REFIT:
				if ship.can_fly():
					_finish(false, "拆掉动力区之后还说能飞")
				else:
					_shot("REFIT_no_power")
					waypoints = [yard.get_gangway_foot(), yard.get_gangway_top(), ship.global_position]
					_enter(Phase.WALK_UP)
			else:
				var stats: Dictionary = ship.get_stats()
				if not ship.can_fly() or stats["hold"] != 4:
					_finish(false, "补回动力区后该能飞、货舱 4 格（%s）" % stats)
				elif ship.get_max_speed() >= speed_before_refit:
					_finish(false, "多了一节货，航速该比开局慢（%.1f ≥ %.1f）" % [ship.get_max_speed(), speed_before_refit])
				else:
					_shot("REFIT_done")
					waypoints = [yard.get_gangway_foot(), yard.get_gangway_top(), ship.global_position]
					_enter(Phase.WALK_UP_AGAIN)
		Phase.WALK_UP:
			if _follow_waypoints(0.6):
				_enter(Phase.HELM_REFUSED)
		Phase.HELM_REFUSED:
			if main.get_state() != Voyage.ON_FOOT:
				_finish(false, "没有动力区居然上了舵")
			elif _walk_toward(ship.get_helm_position(), main.helm_range - 0.3):
				if released_for >= 3 and interact_frames == 0 and not main.get_notice().contains("动力区"):
					_tap_interact()
				elif main.get_notice().contains("动力区"):
					print("shipyard_e2e:   上舵被拒：「%s」" % main.get_notice())
					waypoints = [ship.global_position, yard.get_gangway_top(), yard.get_gangway_foot(), yard.get_bench_position()]
					_enter(Phase.WALK_DOWN_AGAIN)
		Phase.OPEN_REFIT_AGAIN:
			if main.is_refit_open():
				steps = [
					[[["interact", 2], ["move_back", 2]], ["storage", "power", "bridge"], "E 加一节、S 换成动力区"],
					[[["inventory", 2]], ["storage", "power", "bridge"], "Tab 合上"],
				]
				_start_step()
				_enter(Phase.RESTORE_POWER)
			elif elapsed - phase_started > 1.0:
				_finish(false, "第二次在画样台按 E，改装面板没打开（%s）" % main.get_notice())
		Phase.WALK_UP_AGAIN:
			if _follow_waypoints(0.6):
				_enter(Phase.TAKE_HELM_AGAIN)
		Phase.FLY_TO_BERTH:
			if _hop_toward(_berth().origin, _cradle().origin):
				_release_all()
				_tap_interact()
				_enter(Phase.MOORING)
		Phase.MOORING:
			if main.get_state() == Voyage.ON_FOOT:
				if ship.global_position.distance_to(_berth().origin) > 0.5:
					_finish(false, "靠泊后船不在泊位上（离泊位 %.2f m）" % ship.global_position.distance_to(_berth().origin))
				elif main.is_ship_in_yard():
					_finish(false, "船已离坞却还说在坞里")
				else:
					_enter(Phase.DONE)
		Phase.DONE:
			_finish(true, "入坞、改成 %s、飞回泊位靠泊；航速 %.1f → %.1f m/s" % [ship.get_layout(), speed_before_refit, ship.get_max_speed()])


func _cradle() -> Transform3D:
	return yard.get_cradle_transform()


func _berth() -> Transform3D:
	return origin.get_berth_transform(ship.half_beam)


func _start_step() -> void:
	taps = (steps[0][0] as Array).duplicate()
	tap_frames = 0
	tap_gap = 4


## 按脚本一步步敲键；全敲完返回 true。步与步之间松开几帧，Voyage 才读得到「刚按下」
func _run_taps() -> bool:
	if tap_gap > 0:
		tap_gap -= 1
		return false
	if taps.is_empty():
		return true
	var tap: Array = taps[0]
	if tap_frames < int(tap[1]):
		Input.action_press(tap[0])
		tap_frames += 1
		return false
	Input.action_release(tap[0])
	taps.pop_front()
	tap_frames = 0
	tap_gap = 4
	return false


func _enter(next: Phase) -> void:
	print("shipyard_e2e: %-18s 用时 %5.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	_shot("%02d_%s_end" % [phase, Phase.keys()[phase]])
	phase = next
	phase_started = elapsed


func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _release_all() -> void:
	for action in MOVES:
		Input.action_release(action)


## 按一下 E，按住几帧再松
func _tap_interact() -> void:
	if interact_frames == 0:
		Input.action_press("interact")
		interact_frames = 4


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	to.y = 0.0
	if to.length() <= radius:
		_hold("move_forward", false)
		return true
	player.set_look_yaw(atan2(-to.x, -to.z))
	_hold("move_forward", true)
	return false


func _follow_waypoints(last_radius: float) -> bool:
	if waypoints.is_empty():
		return true
	var radius := last_radius if waypoints.size() == 1 else 0.6
	if _walk_toward(waypoints[0], radius):
		waypoints.pop_front()
	return waypoints.is_empty()


## 短途跳一下：舵对准目标、高度保持在目标之上 5 m、离得远就进车。
## 目标比 other 更近、且在可靠泊的距离里时返回 true（这时按 E，Voyage 会挑最近的那个）。
func _hop_toward(target: Vector3, other: Vector3) -> bool:
	var to := target - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))
	var flat := Vector2(to.x, to.z).length()
	var climb := to.y + 5.0
	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 1.5)
	_hold("descend", climb < -1.5)
	_hold("move_forward", flat > 6.0 and absf(bearing) < 30.0)
	return to.length() < ship.global_position.distance_to(other) - 2.0 and to.length() < main.dock_range - 5.0


func _finish(ok: bool, why: String) -> void:
	_release_all()
	Input.action_release("interact")
	Input.action_release("remove")
	Input.action_release("inventory")
	set_physics_process(false)
	print("shipyard_e2e: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，船 %s，Voyage 状态 %d，布局 %s，提示「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, ship.global_position,
		main.get_state(), ship.get_layout(), main.get_node("Hud").get_child(1).text])
	get_tree().quit(0 if ok else 1)
