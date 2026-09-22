extends Node
## 船坞端到端测试：加载主场景，只通过输入动作操作。
## 出生点走到画样台按 E → 船不在坞里，被拒 → 走上空艇掌舵、升空、飞向船坞、E 入坞 → 下船走下跳板到画样台
## → E 打开装配视角 → 在三维格子里改船：舷门后放一节储存区、往上再叠一节、悬空被拒、舷门排不是最宽被拒、
##   左舷加一节、选动力区再加一节、拆支撑被拒、拆驾驶区被拒 → Tab 合上
## → 走上跳板、爬梯子到第二层、下来走到舵位 → 掌舵升空飞回泊位靠泊。
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
	WALK_DOWN, OPEN_REFIT, REFIT, WALK_UP, CLIMB_LADDER, WALK_TO_HELM, TAKE_HELM_AGAIN, CLIMB_OUT_AGAIN, FLY_TO_BERTH, MOORING, DONE,
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
# 改装脚本：每步 [[动作, 按住几帧]…, 之后该是的布局, 说明, 被拒时提示里该有的词]；一步敲完就对布局
var steps: Array = []
var taps: Array = []
var tap_frames := 0
var tap_gap := 0
var speed_before_refit := 0.0
var top_deck := Vector3.ZERO
var ladder_index := 0
var highest := -INF


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
	if Array(ship.get_layout()) != ["0,0,-1:power", "0,0,0:bridge"]:
		_finish(false, "开局的船该是 动力区 + 驾驶区（%s）" % [ship.get_layout()])
		return
	if main.is_ship_in_yard():
		_finish(false, "开局船不该在坞里")
		return
	speed_before_refit = ship.get_max_speed()
	# 从停机坪过来先走坪的岛内一侧（坞墩块横在坪上，正对着推会顶住），再折到画样台
	waypoints = [yard.get_bench_position() + _cradle().basis.x * 6.0, yard.get_bench_position()]
	print("shipyard_e2e: 出生点 %s，泊位接触线 %s，坞位接触线 %s（相距 %.0f m），画样台 %s，跳板 %s → %s，开局航速 %.1f m/s" % [
		player.global_position, _berth().origin, _cradle().origin, _berth().origin.distance_to(_cradle().origin),
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

	match phase:
		Phase.WALK_TO_BENCH:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.REFUSED_NO_SHIP)
		Phase.REFUSED_NO_SHIP:
			if main.is_refit_open():
				_finish(false, "船不在坞里也打开了改装")
			elif elapsed - phase_started > 0.5:
				if not main.get_notice().contains("船坞里没有船"):
					_finish(false, "该提示船坞里没有船，实为「%s」" % main.get_notice())
				else:
					# 先回到栈桥正后方再上桥：从船坞直奔桥头会从岸崖边上走下去
					waypoints = [yard.get_bench_position() + _cradle().basis.x * 6.0, origin.get_spawn_transform().origin, origin.get_pier_end_position(), ship.global_position]
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
			if ship.global_position.y > climb_from + 8.0:
				_enter(Phase.FLY_TO_YARD if phase == Phase.CLIMB_OUT else Phase.FLY_TO_BERTH)
		Phase.FLY_TO_YARD:
			if _hop_toward(_cradle().origin, _berth().origin):
				_release_all()
				_tap_interact()
				_enter(Phase.DOCKING)
		Phase.DOCKING:
			if main.get_state() == Voyage.ON_FOOT:
				if not main.is_ship_in_yard():
					_finish(false, "入坞后船舷没贴在坞位接触线上（船 %s）" % ship.global_position)
				elif not player.is_on_floor():
					pass  # 等落到甲板上
				else:
					waypoints = [yard.get_gangway_top(), yard.get_gangway_foot(), yard.get_bench_position()]
					_enter(Phase.WALK_DOWN)
		Phase.WALK_DOWN:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.OPEN_REFIT)
		Phase.OPEN_REFIT:
			if main.is_refit_open():
				if not player.is_input_blocked():
					_finish(false, "开着改装人还能走")
					return
				if main.get_refit_cursor() != Vector3i(0, 0, 0) or main.get_refit_kind() != "storage":
					_finish(false, "打开时光标该在驾驶区、默认放储存区（%s，%s）" % [main.get_refit_cursor(), main.get_refit_kind()])
					return
				_shot("REFIT_open")
				steps = [
					[[["move_back", 2], ["interact", 2]], ["0,0,-1:power", "0,0,0:bridge", "0,0,1:storage"], "S 到舷门后一格、E：放一节储存区", ""],
					[[["ascend", 2], ["interact", 2]], ["0,0,-1:power", "0,0,0:bridge", "0,0,1:storage", "0,1,1:storage"], "空格上一层、E：再叠一节", ""],
					[[["move_left", 2], ["interact", 2]], ["0,0,-1:power", "0,0,0:bridge", "0,0,1:storage", "0,1,1:storage"], "A 到左舷第二层、E：悬空被拒", "悬空"],
					[[["descend", 2], ["interact", 2]], ["0,0,-1:power", "0,0,0:bridge", "0,0,1:storage", "0,1,1:storage"], "Shift 下到第一层、E：舷门排不是最宽被拒", "最宽"],
					[[["move_forward", 2], ["interact", 2]], ["0,0,-1:power", "-1,0,0:storage", "0,0,0:bridge", "0,0,1:storage", "0,1,1:storage"], "W 到舷门排左舷、E：放一节", ""],
					[[["kind_3", 2], ["move_back", 2], ["interact", 2]], ["0,0,-1:power", "-1,0,0:storage", "0,0,0:bridge", "-1,0,1:power", "0,0,1:storage", "0,1,1:storage"], "3 选动力区、S、E：左舷后面放一节动力区", ""],
					[[["move_right", 2], ["remove", 2]], ["0,0,-1:power", "-1,0,0:storage", "0,0,0:bridge", "-1,0,1:power", "0,0,1:storage", "0,1,1:storage"], "D 到中线、X：上面还搭着，拆不了", "上面"],
					[[["move_forward", 2], ["remove", 2]], ["0,0,-1:power", "-1,0,0:storage", "0,0,0:bridge", "-1,0,1:power", "0,0,1:storage", "0,1,1:storage"], "W 到驾驶区、X：拆不得", "驾驶区"],
					[[["inventory", 2]], ["0,0,-1:power", "-1,0,0:storage", "0,0,0:bridge", "-1,0,1:power", "0,0,1:storage", "0,1,1:storage"], "Tab 合上", ""],
				]
				_start_step()
				_enter(Phase.REFIT)
			elif elapsed - phase_started > 1.0:
				_finish(false, "在画样台按了 E，改装没打开（%s）" % main.get_notice())
		Phase.REFIT:
			if not _run_taps():
				return
			var step: Array = steps.pop_front()
			if Array(ship.get_layout()) != step[1]:
				_finish(false, "%s 之后布局该是 %s，实为 %s（提示「%s」）" % [step[2], step[1], ship.get_layout(), main.get_notice()])
				return
			if step[3] != "" and not main.get_notice().contains(step[3]):
				_finish(false, "%s：提示里该有「%s」，实为「%s」" % [step[2], step[3], main.get_notice()])
				return
			print("shipyard_e2e:   %s → %s（%s）" % [step[2], ship.get_layout(), main.get_notice()])
			if not main.is_ship_in_yard():
				_finish(false, "%s 之后船舷没贴着坞位" % step[2])
				return
			if steps.size() == 2:
				_shot("REFIT_stacked")
			if not steps.is_empty():
				_start_step()
				return
			if main.is_refit_open() or player.is_input_blocked():
				_finish(false, "Tab 之后改装没合上或人还动不了")
				return
			var stats: Dictionary = ship.get_stats()
			if not ship.can_fly() or stats["hold"] != 12 or stats["sections"] != 6 or ship.get_levels() != 2:
				_finish(false, "改完该是 6 节、两层、货舱 12 格、能飞（%s）" % stats)
			elif ship.get_max_speed() >= speed_before_refit:
				_finish(false, "多了货，航速该比开局慢（%.1f ≥ %.1f）" % [ship.get_max_speed(), speed_before_refit])
			elif ship.get_ladder_feet().size() < 1:
				_finish(false, "两层的船该有梯子")
			else:
				_shot("REFIT_done")
				# 梯脚里离舷门那一列的甲板最近的那架，从驾驶区甲板爬上去；梯顶目标取第二层甲板靠船头那一头的走道
				var deck0: Vector3 = ship.get_deck_position(0, 0)
				ladder_index = 0
				for k in ship.get_ladder_feet().size():
					if ship.get_ladder_feet()[k].distance_to(deck0) < ship.get_ladder_feet()[ladder_index].distance_to(deck0):
						ladder_index = k
				top_deck = ship.to_global(ship.to_local(ship.get_deck_position(0, 1)) + Vector3(0, 0, -1.4))
				waypoints = [yard.get_gangway_foot(), yard.get_gangway_top(), deck0, ship.get_ladder_feet()[ladder_index]]
				_enter(Phase.WALK_UP)
		Phase.WALK_UP:
			if _follow_waypoints(0.5):
				_enter(Phase.CLIMB_LADDER)
		Phase.CLIMB_LADDER:
			# 面朝梯子那面墙一直往前：Player 碰到梯子区域就会往上爬，爬过顶再往前就落在第二层甲板上
			highest = maxf(highest, player.global_position.y)
			# 正对着梯子往前推：斜着走会从梯子边上滑出去掉下来
			var landing: Vector3 = ship.get_ladder_tops()[ladder_index]
			_face(landing)
			_hold("move_forward", true)
			var flat := Vector2(landing.x - player.global_position.x, landing.z - player.global_position.z).length()
			if flat < 1.2 and absf(player.global_position.y - top_deck.y) < 0.4 and player.is_on_floor():
				_hold("move_forward", false)
				_shot("LADDER_top")
				# 下去：回到梯顶落脚处，朝梯脚走——走进舷栏缺口就顺梯子下去，再走到舵旁
				waypoints = [ship.get_ladder_tops()[ladder_index], ship.get_ladder_feet()[ladder_index], ship.get_helm_position()]
				_enter(Phase.WALK_TO_HELM)
			elif elapsed - phase_started > 20.0:
				_finish(false, "20 s 没爬上第二层（最高到 y=%.1f，第二层甲板 y=%.1f，在梯子上 %s）" % [highest, top_deck.y, player.is_on_ladder()])
		Phase.WALK_TO_HELM:
			# 从第二层朝舵位走：走出梯顶的缺口掉进梯子区域后，面朝墙按后退顺梯子下去，落地再走到舵旁
			if player.is_on_ladder() and not player.is_on_floor():
				_hold("move_forward", false)
				_face(ship.get_ladder_tops()[ladder_index])
				_hold("move_back", true)
			elif _hold_back_released() and _follow_waypoints(main.helm_range - 0.3) and player.is_on_floor() and absf(player.global_position.y - ship.get_helm_position().y) < 1.5:
				_enter(Phase.TAKE_HELM_AGAIN)
			elif elapsed - phase_started > 20.0:
				_finish(false, "20 s 没走到舵旁（玩家 %s，舵 %s）" % [player.global_position, ship.get_helm_position()])
		Phase.FLY_TO_BERTH:
			if _hop_toward(_berth().origin, _cradle().origin):
				_release_all()
				_tap_interact()
				_enter(Phase.MOORING)
		Phase.MOORING:
			if main.get_state() == Voyage.ON_FOOT:
				if not ship.is_at(_berth(), 0.5):
					_finish(false, "靠泊后船舷没贴在泊位接触线上（船 %s）" % ship.global_position)
				elif main.is_ship_in_yard():
					_finish(false, "船已离坞却还说在坞里")
				else:
					_enter(Phase.DONE)
		Phase.DONE:
			_finish(true, "入坞、改成 %s（%d 节两层）、爬了梯子、飞回泊位靠泊；航速 %.1f → %.1f m/s" % [ship.get_layout(), ship.get_section_count(), speed_before_refit, ship.get_max_speed()])


func _cradle() -> Transform3D:
	return yard.get_cradle_contact()


func _berth() -> Transform3D:
	return origin.get_berth_contact()


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


func _hold_back_released() -> bool:
	_hold("move_back", false)
	return true


func _face(target: Vector3) -> void:
	var to := target - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	var rise := to.y
	to.y = 0.0
	# 高度也得到了：挂在梯子半空时脚下的路点还不算到
	if to.length() <= radius and absf(rise) < 1.5:
		_hold("move_forward", false)
		return true
	_face(target)
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
	for action in ["interact", "remove", "inventory", "kind_1", "kind_2", "kind_3"]:
		Input.action_release(action)
	set_physics_process(false)
	print("shipyard_e2e: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，船 %s，Voyage 状态 %d，布局 %s，提示「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, ship.global_position,
		main.get_state(), ship.get_layout(), main.get_node("Hud").get_child(1).text])
	if not ok:
		print("shipyard_e2e: 玩家在船的局部坐标 %s，在梯子上 %s，着地 %s，剩余路点 %s，梯脚 %s，舷门边 %.1f / %.1f" % [
			ship.to_local(player.global_position), player.is_on_ladder(), player.is_on_floor(), waypoints, ship.get_ladder_feet(),
			ship.get_port_edge_x(), ship.get_starboard_edge_x()])
	get_tree().quit(0 if ok else 1)
