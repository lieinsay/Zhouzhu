extends Node
## 真实比例岛群（world_seed42.tscn，开局那群）端到端测试，只通过输入（动作、鼠标）操作：
## 出生在主码头的停机坪上、脚下有地 → 往前跑几秒一直踩在地上 → M 开地图 → 滚轮缩小 → 左键点乙岛（#1）→ 瞬移过去站在 #1 的地上
## → 再开地图点最远的码头 → 站在那座岛上 → 把人挪到云海上空 → 掉下去回到上一处站稳的地方。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/world_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 180.0  # 模拟秒
const SETTLE_TIMEOUT := 30.0

enum Phase { SETTLE, RUN, OPEN_MAP, ZOOM_OUT, CLICK_ISLAND, LAND_ISLAND, OPEN_MAP_2, CLICK_DOCK, LAND_DOCK, DROP, RESPAWN, DONE }

var main: Expedition
var world: World
var player: Player
var map: WorldMap

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var run_from := Vector3.ZERO
var lowest_clearance := INF
var target := Vector3.ZERO  # 这一段要去的地方（世界 xz）
var target_island := -1
var dock_target := Vector3.ZERO
var dock_island := -1
var drop_from := Vector3.ZERO
var pending_shot := ""
var shot_wait := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)  # 无头时根视口默认很小，地图上的点没处可点
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	map = main.get_map()
	# 乙岛 = #1；最远的码头 = 离出生点最远、又不在主岛上的那个
	var info: Dictionary = world.get_info()
	for isl in info["islands"]:
		if int(isl["id"]) == 1:
			target = Vector3(isl["center"][0], 0, isl["center"][1])
			target_island = 1
	var spawn := main.get_spawn_point()
	var best := -1.0
	for dock in info["docks"]:
		var p := Vector3(dock["pos"][0], 0, dock["pos"][1])
		var d := Vector2(p.x - spawn.x, p.z - spawn.z).length()
		if int(dock["island"]) != 0 and int(dock["island"]) != 1 and d > best:
			best = d
			dock_target = p
			dock_island = int(dock["island"])
	print("world_e2e: 出生点 %s，乙岛心 %s，最远码头 %s（岛 #%d，%.1f km）" % [spawn, target, dock_target, dock_island, best / 1000.0])


func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _shot(shot_name: String) -> void:
	pending_shot = shot_name  # 过几帧画好了再存：刚挪的人、刚挂的地这一帧还没画出来
	shot_wait = 3


func _goto(next: Phase) -> void:
	phase = next
	phase_started = elapsed


func _press_action(action: String) -> void:
	var ev := InputEventAction.new()
	ev.action = action
	ev.pressed = true
	Input.parse_input_event(ev)
	var up := InputEventAction.new()
	up.action = action
	up.pressed = false
	Input.parse_input_event(up)


func _mouse(button: MouseButton, at: Vector2) -> void:
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = button
		ev.position = at
		ev.global_position = at
		ev.pressed = pressed
		Input.parse_input_event(ev)


func _standing_on_ground() -> bool:
	var at := player.global_position
	var ground: float = world.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.0 and world.is_ready_at(at)


func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s）" % Phase.keys()[phase])
		return
	match phase:
		Phase.SETTLE:
			if _standing_on_ground():
				var off := Vector2(player.global_position.x - main.get_spawn_point().x, player.global_position.z - main.get_spawn_point().z).length()
				if off > 5.0:
					_finish(false, "出生后离出生点 %.1f m" % off)
					return
				if world.get_island_at(player.global_position.x, player.global_position.z) != 0:
					_finish(false, "出生点不在主岛上")
					return
				print("world_e2e: 出生站稳（%.1f s），块 %d，待建 %d" % [in_phase, world.get_tile_count(), world.get_pending_count()])
				_shot("1_spawn")
				run_from = player.global_position
				_goto(Phase.RUN)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "出生后 %.0f s 还没站稳：%s" % [SETTLE_TIMEOUT, player.global_position])
		Phase.RUN:
			Input.action_press("move_forward")
			Input.action_press("sprint")
			var at := player.global_position
			var ground: float = world.get_surface_height(at.x, at.z)
			if not is_nan(ground):
				lowest_clearance = minf(lowest_clearance, at.y - ground)
			if in_phase > 6.0:
				Input.action_release("move_forward")
				Input.action_release("sprint")
				var moved := Vector2(at.x - run_from.x, at.z - run_from.z).length()
				if moved < 25.0:
					_finish(false, "跑了 6 s 只挪了 %.1f m" % moved)
					return
				if lowest_clearance < -0.5:
					_finish(false, "跑的时候陷进地里 %.2f m" % -lowest_clearance)
					return
				print("world_e2e: 跑了 %.1f m，最低离地 %.2f m" % [moved, lowest_clearance])
				_goto(Phase.OPEN_MAP)
		Phase.OPEN_MAP:
			if in_phase > 0.3:  # 等落地
				_press_action("map")
				_goto(Phase.ZOOM_OUT)
		Phase.ZOOM_OUT:
			if not main.is_map_open():
				_finish(false, "按 M 地图没开")
				return
			# 滚轮在屏幕中心往外滚，直到乙岛心落进屏幕
			var screen := get_viewport().get_visible_rect().size
			var p: Vector2 = map.world_to_screen(target.x, target.z)
			if Rect2(Vector2(40, 90), screen - Vector2(80, 180)).has_point(p):
				_shot("2_map")
				_goto(Phase.CLICK_ISLAND)
			elif in_phase > 5.0:
				_finish(false, "滚轮缩小了 5 s 乙岛还不在屏幕里（zoom %.2f，点 %s）" % [map.get_zoom(), p])
			else:
				_mouse(MOUSE_BUTTON_WHEEL_DOWN, screen * 0.5)
		Phase.CLICK_ISLAND:
			if in_phase > 0.2:
				_mouse(MOUSE_BUTTON_LEFT, map.world_to_screen(target.x, target.z))
				_goto(Phase.LAND_ISLAND)
		Phase.LAND_ISLAND:
			if main.is_map_open() and in_phase > 0.5:
				_finish(false, "点了乙岛，地图没合上（没瞬移）")
				return
			if in_phase > 1.0 and _standing_on_ground():
				var at := player.global_position
				var island: int = world.get_island_at(at.x, at.z)
				var off := Vector2(at.x - target.x, at.z - target.z).length()
				if island != target_island:
					_finish(false, "瞬移后站在岛 #%d，不是 #%d" % [island, target_island])
					return
				if off > 3000.0:
					_finish(false, "瞬移后离点的地方 %.0f m" % off)
					return
				print("world_e2e: 瞬移到乙岛（离点处 %.0f m，站稳用了 %.1f s）" % [off, in_phase])
				_shot("3_island1")
				_goto(Phase.OPEN_MAP_2)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "瞬移到乙岛后 %.0f s 还没站稳：%s" % [SETTLE_TIMEOUT, player.global_position])
		Phase.OPEN_MAP_2:
			if in_phase > 1.0:
				_press_action("map")
				_goto(Phase.CLICK_DOCK)
		Phase.CLICK_DOCK:
			if in_phase < 0.2:
				return
			var screen := get_viewport().get_visible_rect().size
			var p: Vector2 = map.world_to_screen(dock_target.x, dock_target.z)
			if Rect2(Vector2(40, 90), screen - Vector2(80, 180)).has_point(p):
				_mouse(MOUSE_BUTTON_LEFT, p)
				_goto(Phase.LAND_DOCK)
			elif in_phase > 5.0:
				_finish(false, "码头不在屏幕里")
			else:
				_mouse(MOUSE_BUTTON_WHEEL_DOWN, screen * 0.5)
		Phase.LAND_DOCK:
			if in_phase > 1.0 and _standing_on_ground():
				var at := player.global_position
				var island: int = world.get_island_at(at.x, at.z)
				var off := Vector2(at.x - dock_target.x, at.z - dock_target.z).length()
				if island != dock_island or off > 400.0:
					_finish(false, "瞬移到码头后在岛 #%d、离码头 %.0f m（应在 #%d、400 m 内）" % [island, off, dock_island])
					return
				print("world_e2e: 瞬移到码头（岛 #%d，离码头 %.0f m）" % [island, off])
				_shot("4_dock")
				drop_from = at
				_goto(Phase.DROP)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "瞬移到码头后 %.0f s 还没站稳：%s" % [SETTLE_TIMEOUT, player.global_position])
		Phase.DROP:
			if in_phase > 1.0:
				# 挪到主码头外 1.5 km 的云海上空、回头望着主岛，让人掉下去
				var info: Dictionary = world.get_info()
				var dock := Vector3.ZERO
				for d in info["docks"]:
					if d.get("main", false):
						dock = Vector3(d["pos"][0], 0, d["pos"][1])
				# 外法线方向：从邑治（岛内）指向主码头
				var seat := Vector3.ZERO
				for v in info["villages"]:
					if v.get("seat", false):
						seat = Vector3(v["pos"][0], 0, v["pos"][1])
				var out := Vector3(dock.x - seat.x, 0, dock.z - seat.z).normalized()
				var above := dock + out * 1500.0 + Vector3(0, 420, 0)
				if not is_nan(world.get_surface_height(above.x, above.z)):
					_finish(false, "主码头外 1.5 km 居然有地")
					return
				drop_from = player.global_position
				player.global_position = above
				player.set_look_yaw(atan2(out.x, out.z))  # 背对外面 = 望着岛
				_shot("5_cliffs")
				_goto(Phase.RESPAWN)
		Phase.RESPAWN:
			if in_phase > 1.0 and _standing_on_ground():
				var back := Vector2(player.global_position.x - drop_from.x, player.global_position.z - drop_from.z).length()
				if back > 20.0:
					_finish(false, "掉下去后回到的地方离上一处站稳的地方 %.0f m" % back)
					return
				_finish(true, "出生 → 跑 → 地图瞬移到乙岛 → 瞬移到最远码头 → 坠落回位")
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "掉下去 %.0f s 还没回来：%s" % [SETTLE_TIMEOUT, player.global_position])


func _finish(ok: bool, why: String) -> void:
	Input.action_release("move_forward")
	Input.action_release("sprint")
	set_physics_process(false)
	print("world: %s —— %s（模拟 %.1f s，玩家 %s，块 %d）" % ["PASS" if ok else "FAIL", why, elapsed, player.global_position, world.get_tile_count()])
	get_tree().quit(0 if ok else 1)
