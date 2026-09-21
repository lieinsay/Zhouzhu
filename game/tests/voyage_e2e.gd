extends Node
## 航程端到端测试：加载主场景，只通过输入动作（和玩家一样的那一层）操作，
## 走上空艇 → 掌舵 → 飞到对岸 → 靠泊 → 下船，断言玩家最终站在目的岛的地面上。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/voyage_e2e.tscn
##
## 退出码 0 = 通过。--fixed-fps 让模拟时间与墙钟脱钩，整条航程几秒跑完。
## 去掉 --headless、末尾加 `-- --shots=<目录>`，会在每个阶段存一张截图（目视检查用）。

const MAIN := preload("res://main.tscn")
const TIMEOUT := 300.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]

enum Phase { WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, CRUISE, MOORING, WALK_ASHORE, DONE }

var main: Voyage
var player: Player
var ship: Ship
var origin: Island
var destination: Island

var phase := Phase.WALK_TO_SHIP
var waypoints: Array[Vector3] = []
var elapsed := 0.0
var phase_started := 0.0
var climb_from := 0.0
var shots_dir := ""
var cruise_shot_taken := false
var interact_frames := 0  # > 0：E 还按着，再过这么多帧松开


func _ready() -> void:
	main = MAIN.instantiate()
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	origin = main.get_node("IslandA")
	destination = main.get_node("IslandB")
	waypoints = [origin.get_pier_end_position(), ship.global_position]
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE  # 截图跑不抢鼠标
			_shot("START")
	print("e2e: 出生点 %s，泊位 %s，对岸泊位 %s（相距 %.0f m）" % [
		player.global_position, ship.global_position, _destination_berth(),
		ship.global_position.distance_to(_destination_berth())])


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
		Phase.WALK_TO_SHIP:
			if _follow_waypoints():
				_enter(Phase.TAKE_HELM)
		Phase.TAKE_HELM:
			if main.get_state() == Voyage.PILOTING:
				climb_from = ship.global_position.y
				_enter(Phase.CLIMB_OUT)
			elif _walk_toward(ship.get_helm_position(), main.helm_range - 0.3):
				_tap_interact()
		Phase.CLIMB_OUT:
			# 先升起来再转向：船身平行于岸，原地转会把船尾扫到栈桥上
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 6.0:
				_enter(Phase.CRUISE)
		Phase.CRUISE:
			if not cruise_shot_taken and elapsed - phase_started > 14.0:
				cruise_shot_taken = true
				_shot("CRUISE_mid")
			if _fly_toward(_destination_berth()):
				_release_all()
				_tap_interact()
				_enter(Phase.MOORING)
		Phase.MOORING:
			if main.get_state() == Voyage.ON_FOOT:
				waypoints = [ship.global_position, destination.get_pier_end_position(), destination.get_spawn_transform().origin]
				_enter(Phase.WALK_ASHORE)
		Phase.WALK_ASHORE:
			if main.has_arrived() or _follow_waypoints():
				_release_all()
				_enter(Phase.DONE)
		Phase.DONE:
			# 多等半秒，让落地判定有机会跑到
			if main.has_arrived():
				_finish(true, "玩家站在目的岛上")
			elif elapsed - phase_started > 0.5:
				_finish(false, "走完了路点，但 Voyage 没判定抵达")


func _destination_berth() -> Vector3:
	return destination.get_berth_transform(ship.half_beam).origin


func _enter(next: Phase) -> void:
	print("e2e: %-13s 用时 %5.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	_shot("%d_%s_end" % [phase, Phase.keys()[phase]])
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


## 按一下 E，按住几帧再松：当帧按、下一帧就松，Voyage 那边读不到「刚按下」（实测）。
func _tap_interact() -> void:
	if interact_frames == 0:
		Input.action_press("interact")
		interact_frames = 4


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


## 朝目标走；到了返回 true。转视角就是真人转鼠标那一下，其余只按前进。
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


## 朝泊位飞：舵对准方位、升降对准高度（略高于泊位，靠泊时自己落下去）、远了进车近了滑行。
## 进入可靠泊的距离返回 true。
func _fly_toward(berth: Vector3) -> bool:
	var to := berth - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))  # 正 = 目标在右舷
	var flat := Vector2(to.x, to.z).length()
	var climb := to.y + 8.0

	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 2.0)
	_hold("descend", climb < -2.0)
	_hold("move_forward", flat > 110.0 and absf(bearing) < 60.0)
	return to.length() < main.dock_range - 5.0


func _finish(ok: bool, why: String) -> void:
	_release_all()
	set_physics_process(false)
	print("e2e: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，船 %s）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, ship.global_position])
	if not ok:
		print("e2e: 舵 %s（距玩家 %.2f m），Voyage 状态 %d，玩家着地 %s，提示「%s」" % [
			ship.get_helm_position(), player.global_position.distance_to(ship.get_helm_position()),
			main.get_state(), player.is_on_floor(), main.get_node("Hud").get_child(1).text])
	get_tree().quit(0 if ok else 1)
