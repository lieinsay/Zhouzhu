extends Node
## 农事端到端测试：加载主场景，只通过输入动作操作。
## 出生点按 E（离河太远，垦不动）→ 顺着码头后面的路走上河滩 → 路尽头最近的一块可垦地
## → 按住 E 开垦 → 再按住 E 播种（粟 −1）
## → 等粟成熟 → 按住 E 收获（粟 +N），断言地块状态与粟数一路对得上。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/farm_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，会在每个阶段存一张截图。

const MAIN := preload("res://main.tscn")
const TIMEOUT := 400.0  # 模拟秒
const STAND_OFF := 2.2  # 停在离格心这么远：视线前方 reach 处正好落在格内

enum Phase { REFUSED_AT_SPAWN, WALK_TO_RIVER, TILL, SOW, GROW, HARVEST, DONE }

var main: Voyage
var player: Player
var farm: Farm
var island: Island

var phase := Phase.REFUSED_AT_SPAWN
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var cell := Vector2i(-1, -1)
var waypoints: Array[Vector3] = []
var grain_at_start := 0
var released_for := 0  # E 已经松开了多少帧
var mid_growth_shot := false


func _ready() -> void:
	main = MAIN.instantiate()
	add_child(main)
	player = main.get_node("Player")
	island = main.get_node("IslandA")
	farm = island.get_node("Farm")
	grain_at_start = main.grain
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	var trail := island.get_trail_points()
	if trail.is_empty():
		_finish(false, "岛上没有通往河滩的路")
		return
	cell = farm.find_nearest_arable(trail[trail.size() - 1])
	if cell == Vector2i(-1, -1):
		_finish(false, "岛上没有可垦的地")
		return
	var length := 0.0
	for k in range(1, trail.size()):
		length += trail[k - 1].distance_to(trail[k])
	waypoints.assign(Array(trail))
	print("farm_e2e: 出生点 %s；路长 %.0f m、爬高 %.1f m；路尽头最近的可垦地 %s 在 %s（离路尾 %.1f m，墒情 %.2f）；粟 %d" % [
		player.global_position, length, trail[trail.size() - 1].y - trail[0].y, cell, farm.get_cell_center(cell),
		trail[trail.size() - 1].distance_to(farm.get_cell_center(cell)), farm.get_moisture(cell), grain_at_start])


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时")
		return
	released_for = 0 if Input.is_action_pressed("interact") else released_for + 1

	match phase:
		Phase.REFUSED_AT_SPAWN:
			# 停机坪上、离河又远：按住 E 两秒也垦不出地来
			var here := farm.cell_at(player.global_position)
			if farm.available_action(here, main.grain) != Farm.ACTION_NONE:
				_finish(false, "出生点居然能垦（%s）" % here)
			elif not player.is_on_floor():
				pass  # 还没落地
			elif elapsed - phase_started < 2.0:
				Input.action_press("interact")
			else:
				Input.action_release("interact")
				var tilled := farm.get_plot_count(Farm.PLOT_TILLED)
				if tilled != 0 or main.is_working():
					_finish(false, "离河太远的地被垦出来了")
				else:
					_enter(Phase.WALK_TO_RIVER)
		Phase.WALK_TO_RIVER:
			if not waypoints.is_empty():
				if _pass_waypoint(waypoints[0]):
					waypoints.pop_front()
			elif _walk_toward(farm.get_cell_center(cell), STAND_OFF):
				_enter(Phase.TILL)
		Phase.TILL:
			if _work_until(Farm.PLOT_TILLED):
				if main.grain != grain_at_start:
					_finish(false, "开垦不该动粟（%d → %d）" % [grain_at_start, main.grain])
				else:
					_enter(Phase.SOW)
		Phase.SOW:
			if _work_until(Farm.PLOT_GROWING):
				if main.grain != grain_at_start - 1:
					_finish(false, "播种该耗 1 粟（%d → %d）" % [grain_at_start, main.grain])
				else:
					_enter(Phase.GROW)
		Phase.GROW:
			# 长的时候按 E 没用
			if elapsed - phase_started < 1.0:
				Input.action_press("interact")
			else:
				Input.action_release("interact")
			if not mid_growth_shot and farm.get_growth(cell) > 0.7:
				mid_growth_shot = true
				_shot("GROW_mid")
			if farm.get_plot(cell) == Farm.PLOT_RIPE:
				_enter(Phase.HARVEST)
			elif farm.get_plot(cell) != Farm.PLOT_GROWING:
				_finish(false, "长着长着状态变成了 %d" % farm.get_plot(cell))
		Phase.HARVEST:
			if _work_until(Farm.PLOT_TILLED):
				var expected := grain_at_start - 1 + farm.harvest_yield
				if main.grain != expected:
					_finish(false, "收获后粟该是 %d，实为 %d" % [expected, main.grain])
				else:
					_enter(Phase.DONE)
		Phase.DONE:
			_finish(true, "垦了、种了、收了：粟 %d → %d" % [grain_at_start, main.grain])


## 面朝目标格按住 E，直到那一格变成 want；E 先松开几帧再按，Voyage 才读得到「刚按下」。
func _work_until(want: int) -> bool:
	if farm.get_plot(cell) == want:
		Input.action_release("interact")
		return true
	_face(farm.get_cell_center(cell))
	if farm.cell_at(_aim_point()) != cell:
		_finish(false, "瞄着的不是目标格：%s ≠ %s" % [farm.cell_at(_aim_point()), cell])
	elif released_for >= 3:
		Input.action_press("interact")
	elif elapsed - phase_started > 10.0:
		_finish(false, "按了 10 秒 E，地块还是状态 %d" % farm.get_plot(cell))
	return false


func _aim_point() -> Vector3:
	return player.global_position + Basis(Vector3.UP, player.get_look_yaw()) * Vector3(0, 0, -main.reach)


func _face(target: Vector3) -> void:
	var to := target - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


## 路点不用停：走近了就奔下一个
func _pass_waypoint(target: Vector3) -> bool:
	var to := target - player.global_position
	to.y = 0.0
	_face(target)
	Input.action_press("move_forward")
	return to.length() <= 1.2


func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	to.y = 0.0
	_face(target)
	if to.length() <= radius:
		Input.action_release("move_forward")
		return player.get_real_velocity().length() < 0.05  # 站稳了再说
	Input.action_press("move_forward")
	return false


func _enter(next: Phase) -> void:
	print("farm_e2e: %-16s 用时 %5.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	_shot("%d_%s_end" % [phase, Phase.keys()[phase]])
	phase = next
	phase_started = elapsed


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


func _finish(ok: bool, why: String) -> void:
	Input.action_release("interact")
	Input.action_release("move_forward")
	set_physics_process(false)
	print("farm_e2e: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，目标格 %s 状态 %d，提示「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, cell,
		farm.get_plot(cell), main.get_node("Hud").get_child(1).text])
	get_tree().quit(0 if ok else 1)
