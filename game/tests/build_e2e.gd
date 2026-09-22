extends Node
## 布置模式（开发者工具）端到端测试：加载主场景，只通过输入动作操作。
## 出发岛的聚落清单先改到 user:// 的临时文件（不动仓库里的村落）。
## 出生点按 B 进布置模式（人站住、相机换成布置视角）→ 在停机坪上按 E 被拒 → 把光标挪到河滩上一块能盖的地 → E 放一座民居
## → 同一处再按 E 被拒（已经有了）→ 挪开 4 m 再放被拒（挨得太近）→ 4 选望楼、R 转三下（45°）、挪到旁边再放
## → 挪回民居上按 X 拆掉 → F5 存盘，文件里的行与清单一致 → Tab 合上、人能动 → 重新读文件，清单不变
## → 顺着路走到望楼跟前，被墙挡住走不进去 → 换回仓库里的村落文件，每一座都还站得住。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/build_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，会在每个阶段存一张截图。

const MAIN := preload("res://main.tscn")
const SCRATCH := "user://build_e2e_layout.txt"
const TIMEOUT := 300.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "sprint"]
const KEYS := ["interact", "remove", "inventory", "build_mode", "rotate_left", "rotate_right", "kind_1", "kind_2", "kind_3", "kind_4", "save"]

enum Phase {
	OPEN, REFUSED_ON_APRON, TO_SITE_A, PLACE_HOUSE, PLACE_AGAIN, TO_NEAR, PLACE_TOO_CLOSE, PICK_TOWER, TO_SITE_B, PLACE_TOWER,
	BACK_TO_HOUSE, REMOVE_HOUSE, SAVE, CLOSE, RELOAD, WALK_TO_TOWER, SHIPPED_VILLAGE, DONE,
}

var main: Voyage
var player: Player
var island: Island
var settlement: Settlement

var phase := Phase.OPEN
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var shipped_path := ""
var site_a := Vector3.ZERO
var site_b := Vector3.ZERO
var tower_center := Vector3.ZERO
var waypoints: Array[Vector3] = []
var taps: Array = []
var tap_frames := 0
var tap_gap := 0
var last_player := Vector3.ZERO
var still_frames := 0


func _ready() -> void:
	main = MAIN.instantiate()
	settlement = main.get_node("IslandA/Settlement")
	shipped_path = settlement.layout_path
	var scratch_abs := ProjectSettings.globalize_path(SCRATCH)
	if FileAccess.file_exists(SCRATCH):
		DirAccess.remove_absolute(scratch_abs)
	settlement.layout_path = SCRATCH
	add_child(main)
	player = main.get_node("Player")
	island = main.get_node("IslandA")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	if not main.dev_tools:
		_finish(false, "主场景的 dev_tools 该是开着的")
		return
	if settlement.get_count() != 0:
		_finish(false, "临时清单该是空的（%s）" % [settlement.get_layout()])
		return
	if not FileAccess.file_exists(shipped_path):
		_finish(false, "仓库里该有出发岛的村落文件 %s" % shipped_path)
		return
	var trail := island.get_trail_points()
	if trail.is_empty():
		_finish(false, "岛上没有通往河滩的路")
		return
	site_a = settlement.find_site("house", trail[trail.size() - 1], 0.0, 40.0)
	if is_nan(site_a.y):
		_finish(false, "路尽头 40 m 内找不到能盖民居的地")
		return
	waypoints.assign(Array(trail))
	print("build_e2e: 出生点 %s，路尽头 %s，民居的地 %s（岛内 %s），仓库村落 %s" % [player.global_position, trail[trail.size() - 1], site_a, island.to_local(site_a), shipped_path])
	_queue([["build_mode", 2]])


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时")
		return
	if not _run_taps():
		return

	match phase:
		Phase.OPEN:
			if not main.is_build_open():
				_finish(false, "按了 B 布置模式没打开（%s）" % main.get_notice())
			elif not player.is_input_blocked():
				_finish(false, "开着布置模式人还能走")
			elif main.get_build_cursor().distance_to(player.global_position) > 1.0:
				_finish(false, "光标该从脚下开始（%s vs %s）" % [main.get_build_cursor(), player.global_position])
			elif main.get_build_kind() != "house":
				_finish(false, "默认该选民居（%s）" % main.get_build_kind())
			else:
				_shot("BUILD_open")
				_queue([["interact", 2]])
				_enter(Phase.REFUSED_ON_APRON)
		Phase.REFUSED_ON_APRON:
			if settlement.get_count() != 0:
				_finish(false, "停机坪上居然盖起来了")
			elif not main.get_notice().contains("停机坪"):
				_finish(false, "该提示停机坪上不能盖，实为「%s」" % main.get_notice())
			else:
				_enter(Phase.TO_SITE_A)
		Phase.TO_SITE_A:
			if _steer_cursor(site_a):
				_shot("BUILD_ghost")
				_queue([["interact", 2]])
				_enter(Phase.PLACE_HOUSE)
		Phase.PLACE_HOUSE:
			if settlement.get_count() != 1 or settlement.get_building_kind(0) != "house":
				_finish(false, "该放下一座民居（%d 座，提示「%s」）" % [settlement.get_count(), main.get_notice()])
			elif settlement.get_building_position(0).distance_to(site_a) > 1.0:
				_finish(false, "民居该在 %s，实在 %s" % [site_a, settlement.get_building_position(0)])
			elif not settlement.is_dirty():
				_finish(false, "放了东西该记为有改动未存")
			else:
				_shot("BUILD_house")
				_queue([["interact", 2]])
				_enter(Phase.PLACE_AGAIN)
		Phase.PLACE_AGAIN:
			if settlement.get_count() != 1:
				_finish(false, "同一处再放居然放下了")
			elif main.get_build_selected() != 0 or not main.get_notice().contains("已经有"):
				_finish(false, "光标在民居上该说已经有了（选中 %d，提示「%s」）" % [main.get_build_selected(), main.get_notice()])
			else:
				_enter(Phase.TO_NEAR)
		Phase.TO_NEAR:
			if _steer_cursor(site_a + Vector3(4.0, 0, 0)):
				_queue([["interact", 2]])
				_enter(Phase.PLACE_TOO_CLOSE)
		Phase.PLACE_TOO_CLOSE:
			if settlement.get_count() != 1:
				_finish(false, "挨着民居 4 m 居然放下了")
			elif not main.get_notice().contains("挨得太近"):
				_finish(false, "该说挨得太近，实为「%s」" % main.get_notice())
			else:
				_queue([["kind_4", 2], ["rotate_right", 2], ["rotate_right", 2], ["rotate_right", 2]])
				_enter(Phase.PICK_TOWER)
		Phase.PICK_TOWER:
			if main.get_build_kind() != "tower" or absf(main.get_build_yaw_deg() - 45.0) > 0.5:
				_finish(false, "该选中望楼、转了 45°（%s，%.1f°）" % [main.get_build_kind(), main.get_build_yaw_deg()])
			else:
				site_b = settlement.find_site("tower", site_a + Vector3(12.0, 0, 0), deg_to_rad(45.0), 30.0)
				if is_nan(site_b.y):
					_finish(false, "民居旁边 30 m 内找不到能盖望楼的地")
				else:
					_enter(Phase.TO_SITE_B)
		Phase.TO_SITE_B:
			if _steer_cursor(site_b):
				_queue([["interact", 2]])
				_enter(Phase.PLACE_TOWER)
		Phase.PLACE_TOWER:
			if settlement.get_count() != 2 or settlement.get_building_kind(1) != "tower":
				_finish(false, "该放下望楼（%d 座，提示「%s」）" % [settlement.get_count(), main.get_notice()])
			elif absf(settlement.get_building_yaw_deg(1) - 45.0) > 0.5:
				_finish(false, "望楼该转了 45°（%.1f°）" % settlement.get_building_yaw_deg(1))
			else:
				tower_center = settlement.get_building_position(1)
				_shot("BUILD_tower")
				_enter(Phase.BACK_TO_HOUSE)
		Phase.BACK_TO_HOUSE:
			if _steer_cursor(site_a):
				_queue([["remove", 2]])
				_enter(Phase.REMOVE_HOUSE)
		Phase.REMOVE_HOUSE:
			if settlement.get_count() != 1 or settlement.get_building_kind(0) != "tower":
				_finish(false, "拆掉民居后该只剩望楼（%s，提示「%s」）" % [settlement.get_layout(), main.get_notice()])
			else:
				_queue([["save", 2]])
				_enter(Phase.SAVE)
		Phase.SAVE:
			if settlement.is_dirty():
				_finish(false, "F5 之后还说有改动未存（%s）" % main.get_notice())
			elif not FileAccess.file_exists(SCRATCH):
				_finish(false, "F5 之后文件没写出来（%s）" % main.get_notice())
			else:
				var lines: Array = []
				for line in FileAccess.get_file_as_string(SCRATCH).split("\n"):
					if line.strip_edges() != "" and not line.begins_with("#"):
						lines.append(line.strip_edges())
				if lines != Array(settlement.get_layout()):
					_finish(false, "文件里的行 %s 与清单 %s 不一致" % [lines, settlement.get_layout()])
				else:
					_queue([["inventory", 2]])
					_enter(Phase.CLOSE)
		Phase.CLOSE:
			if main.is_build_open() or player.is_input_blocked():
				_finish(false, "Tab 之后布置模式没合上或人还动不了")
			else:
				_enter(Phase.RELOAD)
		Phase.RELOAD:
			var before: Array = Array(settlement.get_layout())
			if not settlement.load():
				_finish(false, "重新读临时文件失败")
			elif Array(settlement.get_layout()) != before or settlement.get_count() != 1:
				_finish(false, "重新读出来的清单变了：%s → %s" % [before, settlement.get_layout()])
			else:
				waypoints.append(tower_center)
				last_player = player.global_position
				still_frames = 0
				_enter(Phase.WALK_TO_TOWER)
		Phase.WALK_TO_TOWER:
			# 顺着路走到河滩，再直冲望楼：应当被台基与墙挡在外面
			var flat := Vector2(tower_center.x - player.global_position.x, tower_center.z - player.global_position.z).length()
			if waypoints.size() > 1:
				if _walk_toward(waypoints[0], 0.8):
					waypoints.pop_front()
			else:
				_walk_toward(tower_center, 0.3)
				still_frames = still_frames + 1 if player.global_position.distance_to(last_player) < 0.02 else 0
				last_player = player.global_position
				if flat < 1.5:
					_finish(false, "人走进望楼里去了（离中心 %.1f m）" % flat)
				elif still_frames >= 90 or elapsed - phase_started > 60.0:
					_hold("move_forward", false)
					if flat > 6.0:
						_finish(false, "人没走到望楼跟前就停了（离中心 %.1f m，位置 %s）" % [flat, player.global_position])
					else:
						_shot("BUILD_blocked")
						settlement.layout_path = shipped_path
						_enter(Phase.SHIPPED_VILLAGE)
		Phase.SHIPPED_VILLAGE:
			if settlement.get_count() < 5:
				_finish(false, "仓库里的村落该至少有 5 座（%d）" % settlement.get_count())
				return
			for k in settlement.get_count():
				var why: String = settlement.survey_building(k)
				if why != "":
					_finish(false, "仓库村落里第 %d 座（%s）站不住了：%s" % [k, settlement.get_layout()[k], why])
					return
			_shot("BUILD_village")
			_enter(Phase.DONE)
		Phase.DONE:
			_finish(true, "布置模式开合、放 / 拒 / 拆 / 存 / 读都对；望楼挡得住人；仓库村落 %d 座都站得住" % settlement.get_count())


func _queue(script: Array) -> void:
	taps = script.duplicate()
	tap_frames = 0
	tap_gap = 4


## 按脚本一步步敲键；全敲完（并且松开后又过了几帧）返回 true。步与步之间松开几帧，Voyage 才读得到「刚按下」
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


## 把布置光标挪到 target（取整后的光标正好落到 target 上才算到）：按相机方位选 WASD，远了按 Shift
func _steer_cursor(target: Vector3) -> bool:
	var cursor: Vector3 = main.get_build_cursor()
	var to := target - cursor
	to.y = 0.0
	if to.length() < 0.01:
		for action in MOVES:
			Input.action_release(action)
		return true
	var yaw: float = main.get_build_camera_yaw()
	var forward := Vector3(-sin(yaw), 0, -cos(yaw))
	var right := Vector3(cos(yaw), 0, -sin(yaw))
	var f := forward.dot(to)
	var r := right.dot(to)
	_hold("move_forward", f > 0.1)
	_hold("move_back", f < -0.1)
	_hold("move_right", r > 0.1)
	_hold("move_left", r < -0.1)
	_hold("sprint", to.length() > 8.0)
	if elapsed - phase_started > 40.0:
		_finish(false, "光标 40 s 没挪到 %s（在 %s）" % [target, cursor])
	return false


func _enter(next: Phase) -> void:
	print("build_e2e: %-16s 用时 %5.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	_shot("%02d_%s_end" % [phase, Phase.keys()[phase]])
	phase = next
	phase_started = elapsed


func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


func _face(target: Vector3) -> void:
	var to := target - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	to.y = 0.0
	if to.length() <= radius:
		_hold("move_forward", false)
		return true
	_face(target)
	_hold("move_forward", true)
	return false


func _finish(ok: bool, why: String) -> void:
	for action in MOVES + KEYS:
		Input.action_release(action)
	set_physics_process(false)
	print("build_e2e: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，光标 %s，清单 %s，提示「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, main.get_build_cursor(), settlement.get_layout(), main.get_notice()])
	get_tree().quit(0 if ok else 1)
