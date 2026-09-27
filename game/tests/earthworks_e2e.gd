extends Node
## 改地形（world_2051.tscn，切片十一：方格台阶）：只经输入动作操作，准星用一条测试给的射线（set_aim_ray：从格子上方往下打，不靠相机）；
## 世界存档目录用测试自己的 user://worlds/e2e_earthworks（开局清空）。行李：木锹、石镐（开局的粟照旧）。
## 主码头出生：准星对着停机坪——挖不了（这是码头的坪）
## → 在主码头附近（岸边是崖缘、土薄，往里找）找一块 5 × 6 格都能动土、土层 2 m、够平的地，瞬移过去站在边上
## → 木锹放上快捷栏第 1 格：按住 E 把 2 × 2 格一格格挖到原地面下 2 m（每格 4 级，出土 16 份）；再往下挖——木锹挖不动（挖到石头了：要石镐）；
##   石镐放上快捷栏第 2 格，一格凿下一级（出石料 1）
## → 走进坑里：落到坑底（比原地面低 2 m）、站得稳；按住前进 + 跳三秒——跳不出去（坑壁 2 m，跳 1.3 m）
## → 土放上快捷栏第 3 格：对着坑里另一格按两下 E，垒成 1 m 高的一级台阶（扣土 2）→ 前进 + 跳：先上台阶、再上地面，出来了
## → 坑边隔两格垒一个 1 m 的土台（扣土 2）、跳上去站着
## → 存档：GroupStream.save_world()，works/2051.txt 在；拆掉场景、同一个存档目录再开一回——坑、台阶、土台都在（级数逐格一样），地面块照挂补丁
##
##   Godot --headless --path game --fixed-fps 60 res://tests/earthworks_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_2051.tscn")
const WORLD_DIR := "user://worlds/e2e_earthworks"
const TIMEOUT := 600.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "sprint", "jump", "interact"]
const FAR := Vector3(1e6, 0, 1e6)  # 量「压不压着人」时给个远处的人

var main: Expedition
var world: World
var player: Player

var elapsed := 0.0
var stage := "开局"
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var done := false

var i0 := 0
var j0 := 0
var natural := {}  # 格 → 原地面高（挖之前量的）
var saved_steps := {}  # 格 → 存档前的级数


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	_remove_tree(WORLD_DIR)
	_open_scene()
	_run()


func _open_scene() -> void:
	main = SCENE.instantiate()
	main.starting_pack = {"millet": 5, "wood_shovel": 1, "stone_pick": 1}
	main.get_node("GroupProvider").world_dir = WORLD_DIR
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")


func _remove_tree(dir: String) -> void:
	if not DirAccess.dir_exists_absolute(dir):
		return
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		_remove_tree(dir.path_join(d))
	DirAccess.remove_absolute(dir)


func _log(line: String) -> void:
	print("earthworks: " + line)


func _physics_process(delta: float) -> void:
	elapsed += delta
	if not done and elapsed > TIMEOUT:
		_finish(false, "超时")


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
	if not shots_dir.is_empty():
		await _frames(8)


# ---------------------------------------------------------------- 等、按

func _frames(n: int) -> void:
	for i in n:
		await get_tree().physics_frame


func _until(cond: Callable, seconds: float) -> bool:
	var t := 0.0
	while not cond.call():
		if done:
			return false
		await get_tree().physics_frame
		t += get_physics_process_delta_time()
		if t > seconds:
			return false
	return true


func _tap(action: String) -> void:
	Input.action_press(action)
	await _frames(3)
	Input.action_release(action)
	await _frames(4)


func _mouse_at(at: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = at
	motion.global_position = at
	Input.parse_input_event(motion)
	await _frames(2)


func _click(at: Vector2) -> void:
	await _mouse_at(at)
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = MOUSE_BUTTON_LEFT
		ev.pressed = pressed
		ev.position = at
		ev.global_position = at
		Input.parse_input_event(ev)
	await _frames(3)


## 背包里 item 那叠放上快捷栏第 hotbar 格（开背包、点两下、合上），再按数字键拿起来
func _hold(item: String, hotbar: int) -> bool:
	await _tap("inventory")
	if not main.is_pack_open():
		_finish(false, "Tab 没打开背包")
		return false
	var slot := -1
	for i in range(0, 36):
		if main.get_pack_slot(i)["item"] == item:
			slot = i
			break
	if slot < 0:
		_finish(false, "背包里没有 %s" % item)
		return false
	if slot != hotbar:
		await _click(main.get_pack_slot_center(0, slot))
		await _click(main.get_pack_slot_center(0, hotbar))
	await _tap("inventory")
	await _tap("hotbar_%d" % (hotbar + 1))
	if main.get_held_item() != item:
		_finish(false, "手上该拿着 %s，实为 %s" % [item, main.get_held_item()])
		return false
	return true


func _pack_text() -> String:
	var parts := []
	for item in main.get_pack_items():
		parts.append("%s %d" % [item, main.get_item_count(item)])
	return ", ".join(parts)


# ---------------------------------------------------------------- 格子

func _center(i: int, j: int) -> Vector3:
	return world.get_edit_cell_center(i, j)


func _step(i: int, j: int) -> int:
	var c := _center(i, j)
	return world.get_edit_step_at(c.x, c.z)


## 准星：从这一格上方 2.5 m 往下打
func _aim_cell(i: int, j: int) -> void:
	var c := _center(i, j)
	main.set_aim_ray(c + Vector3(0, 2.5, 0), c - Vector3(0, 3.0, 0))


func _cell_free(i: int, j: int) -> bool:
	var c := _center(i, j)
	if c == Vector3.ZERO:
		return false
	var f: Dictionary = world.get_earth_facts_at(c.x, c.z, FAR)
	return not f.is_empty() and f["solid"] and f["blocked"] == "" and float(f["shore"]) > 5.0 and float(f["soil_depth"]) >= 2.0


## 以 (i, j) 为左上的坑：往外一圈、往东多两格，5 × 6 格都能动土、高低差不到 0.9 m
func _block_ok(i: int, j: int) -> bool:
	var lo := 1e9
	var hi := -1e9
	for di in range(-1, 4):
		for dj in range(-1, 5):
			if not _cell_free(i + di, j + dj):
				return false
			var y := _center(i + di, j + dj).y
			lo = min(lo, y)
			hi = max(hi, y)
	return hi - lo < 0.9


var reasons := {}


func _tally(i: int, j: int) -> void:
	var c := _center(i, j)
	if c == Vector3.ZERO:
		reasons["没有格心"] = reasons.get("没有格心", 0) + 1
		return
	var f: Dictionary = world.get_earth_facts_at(c.x, c.z, FAR)
	var why := "ok"
	if f.is_empty():
		why = "空"
	elif not f["solid"]:
		why = "不实"
	elif f["blocked"] != "":
		why = f["blocked"]
	elif float(f["shore"]) <= 5.0:
		why = "岸"
	elif float(f["soil_depth"]) < 2.0:
		why = "土层 %.1f" % float(f["soil_depth"])
	reasons[why] = reasons.get(why, 0) + 1


func _find_site(around: Vector3) -> bool:
	for r in [30.0, 50.0, 80.0, 120.0, 170.0, 230.0, 300.0, 380.0]:
		for k in 24:
			var a := k * TAU / 24.0
			var p := around + Vector3(cos(a) * r, 0, sin(a) * r)
			var f: Dictionary = world.get_earth_facts_at(p.x, p.z, FAR)
			if not f.is_empty():
				_tally(int(f["i"]), int(f["j"]))
	_log("候选格：%s" % reasons)
	for r in [30.0, 50.0, 80.0, 120.0, 170.0, 230.0, 300.0, 380.0]:
		for k in 24:
			var a := k * TAU / 24.0
			var p := around + Vector3(cos(a) * r, 0, sin(a) * r)
			var f: Dictionary = world.get_earth_facts_at(p.x, p.z, FAR)
			if f.is_empty():
				continue
			if _block_ok(int(f["i"]), int(f["j"])):
				i0 = int(f["i"])
				j0 = int(f["j"])
				return true
	return false


# ---------------------------------------------------------------- 挖、垒、走

## 按住 E 把 (i, j) 挖到 target 级；返回挖没挖到
func _dig_to(i: int, j: int, target: int, seconds := 20.0) -> bool:
	_aim_cell(i, j)
	await _frames(3)
	Input.action_press("interact")
	var ok := await _until(func() -> bool:
		_aim_cell(i, j)
		return _step(i, j) <= target, seconds)
	Input.action_release("interact")
	await _frames(6)
	return ok


func _raise(i: int, j: int, times: int) -> bool:
	for k in times:
		_aim_cell(i, j)
		await _frames(3)
		var before := _step(i, j)
		await _tap("interact")
		if _step(i, j) != before + 1:
			_finish(false, "垒 (%d, %d) 没垒上（%s；准星那格 %s）" % [i, j, main.get_notice(), main.get_dig_target()])
			return false
	return true


func _walk_to(target: Vector3, radius: float, seconds := 12.0, jump := false) -> bool:
	var t := 0.0
	var ok := await _until(func() -> bool:
		var to := target - player.global_position
		to.y = 0.0
		if to.length() <= radius:
			return true
		player.set_look_yaw(atan2(-to.x, -to.z))
		Input.action_press("move_forward")
		if jump:
			t += get_physics_process_delta_time()
			if player.is_on_floor() and fmod(t, 0.6) < 0.05:
				Input.action_press("jump")
			else:
				Input.action_release("jump")
		return false, seconds)
	Input.action_release("move_forward")
	Input.action_release("jump")
	await _frames(20)
	return ok


func _in_pit() -> bool:
	var p := player.global_position
	var a := _center(i0, j0)
	var b := _center(i0 + 1, j0 + 1)
	return p.x > min(a.x, b.x) - 0.55 and p.x < max(a.x, b.x) + 0.55 and p.z > min(a.z, b.z) - 0.55 and p.z < max(a.z, b.z) + 0.55


# ---------------------------------------------------------------- 流程

func _run() -> void:
	await _frames(30)
	if not await _until(func() -> bool: return world.is_loaded() and player.is_on_floor(), 60.0):
		_finish(false, "主码头站不稳")
		return
	main.clear_aim_ray()
	# 主码头的停机坪上挖不了
	stage = "停机坪"
	if not await _hold("wood_shovel", 0):
		return
	var here: Dictionary = world.get_earth_facts_at(player.global_position.x, player.global_position.z, FAR)
	_aim_cell(int(here["i"]), int(here["j"]))
	await _frames(4)
	var dt: Dictionary = main.get_dig_target()
	if dt["ok"] or not String(dt["why"]).contains("码头"):
		_finish(false, "停机坪上该挖不了（这是码头的坪），实为 %s" % dt)
		return
	_log("停机坪上挖不了：%s" % dt["why"])

	# 找地、过去
	stage = "找地"
	if not _find_site(player.global_position):
		_finish(false, "主码头 380 m 内找不到 5 × 6 格能动土的平地（%s）" % reasons)
		return
	var rim := _center(i0 - 1, j0)
	_log("坑在格 (%d, %d)：%s（离主码头 %.0f m）" % [i0, j0, _center(i0, j0), rim.distance_to(player.global_position)])
	main.teleport_to(rim)
	if not await _until(func() -> bool: return player.is_on_floor() and world.is_ready_at(player.global_position), 30.0):
		_finish(false, "瞬移到坑边站不稳")
		return
	await _frames(20)
	for di in range(-1, 4):
		for dj in range(-1, 5):
			natural[Vector2i(i0 + di, j0 + dj)] = _center(i0 + di, j0 + dj).y
	# 实测（主线程）：一块里头一回改（叶块留洞、当场重建）、同一块再改（只重画补丁）、改回原样（洞没了、再重建叶块）
	var probe := _center(i0 + 3, j0 + 4)
	var t := Time.get_ticks_usec()
	world.set_edit_step_at(probe.x, probe.z, -1)
	var first_ms := (Time.get_ticks_usec() - t) / 1000.0
	t = Time.get_ticks_usec()
	world.set_edit_step_at(probe.x, probe.z, -2)
	var again_ms := (Time.get_ticks_usec() - t) / 1000.0
	t = Time.get_ticks_usec()
	world.set_edit_step_at(probe.x, probe.z, 0)
	var back_ms := (Time.get_ticks_usec() - t) / 1000.0
	_log("实测（主线程）：一块头一回改 %.1f ms、同一块再改 %.1f ms、改回原样 %.1f ms" % [first_ms, again_ms, back_ms])
	if world.get_edit_cell_count() != 0:
		_finish(false, "改回原样以后该没有改过的格（%d）" % world.get_edit_cell_count())
		return

	# 挖 2 × 2 × 2 m 的坑
	stage = "挖坑"
	player.set_look_pitch(-0.9)
	var pit := [Vector2i(i0, j0), Vector2i(i0, j0 + 1), Vector2i(i0 + 1, j0), Vector2i(i0 + 1, j0 + 1)]
	var t0 := Time.get_ticks_msec()
	for c in pit:
		if not await _dig_to(c.x, c.y, -4):
			_finish(false, "(%d, %d) 挖不到 2 m（%d 级；%s；准星那格 %s）" % [c.x, c.y, _step(c.x, c.y), main.get_notice(), main.get_dig_target()])
			return
	_log("挖 16 级用了 %.1f s（真实时间）；背包 %s" % [(Time.get_ticks_msec() - t0) / 1000.0, _pack_text()])
	if main.get_item_count("earth") != 16:
		_finish(false, "挖 16 级该出土 16 份，实为 %d" % main.get_item_count("earth"))
		return
	for c in pit:
		var drop: float = natural[c] - _center(c.x, c.y).y
		if abs(drop - 2.0) > 0.15:
			_finish(false, "(%d, %d) 的地面该低 2 m，实为 %.2f m" % [c.x, c.y, drop])
			return
	if world.get_patch_count() < 1 or world.get_edit_cell_count() != 4:
		_finish(false, "改过 4 格该有补丁（补丁 %d、改过 %d 格）" % [world.get_patch_count(), world.get_edit_cell_count()])
		return
	await _shot("EARTH_pit")
	# 再往下：木锹挖不动，石镐凿得动
	_aim_cell(i0, j0)
	await _frames(4)
	dt = main.get_dig_target()
	if dt["ok"] or not String(dt["why"]).contains("石镐"):
		_finish(false, "土层 2 m 以下木锹该挖不动（要石镐），实为 %s" % dt)
		return
	_log("木锹挖不动：%s" % dt["why"])
	if not await _hold("stone_pick", 1):
		return
	if not await _dig_to(i0, j0, -5, 10.0):
		_finish(false, "石镐凿不下去（%s）" % main.get_dig_target())
		return
	if main.get_item_count("stone") != 1:
		_finish(false, "凿一级石该出石料 1，实为 %d" % main.get_item_count("stone"))
		return
	_log("石镐凿下一级：石料 1")

	# 走进坑里、跳不出来
	stage = "坑里"
	main.clear_aim_ray()
	var inside := _center(i0 + 1, j0)
	if not await _walk_to(inside, 0.3):
		_finish(false, "走不进坑里")
		return
	await _until(func() -> bool: return player.is_on_floor(), 5.0)
	await _frames(30)
	var floor_y: float = natural[Vector2i(i0 + 1, j0)] - 2.0
	if not _in_pit() or abs(player.global_position.y - floor_y) > 0.3 or not player.is_on_floor():
		_finish(false, "该站在坑底（y ≈ %.2f），实为 %s、着地 %s" % [floor_y, player.global_position, player.is_on_floor()])
		return
	_log("站在坑底：y %.2f（原地面 %.2f）" % [player.global_position.y, natural[Vector2i(i0 + 1, j0)]])
	player.set_look_pitch(-0.35)
	await _shot("EARTH_inside")
	# 往外跳三秒
	var out := _center(i0 + 1, j0 - 2)
	await _walk_to(out, 0.3, 3.0, true)
	if not _in_pit():
		_finish(false, "2 m 的坑不该跳得出去（人在 %s）" % player.global_position)
		return
	_log("跳不出去：人还在坑里 %s" % player.global_position)

	# 垒一级台阶爬出来
	stage = "垒台阶"
	if not await _walk_to(_center(i0 + 1, j0 + 1), 0.25):
		_finish(false, "坑里走不到东边那格")
		return
	if not await _hold("earth", 2):
		return
	var earth_before := main.get_item_count("earth")
	if not await _raise(i0, j0 + 1, 2):
		return
	if main.get_item_count("earth") != earth_before - 2 or _step(i0, j0 + 1) != -2:
		_finish(false, "垒两级该扣土 2、那格到 −2 级（土 %d→%d，%d 级）" % [earth_before, main.get_item_count("earth"), _step(i0, j0 + 1)])
		return
	# 站着的那格垒不了
	_aim_cell(i0 + 1, j0 + 1)
	await _frames(4)
	dt = main.get_dig_target()
	if dt["ok"] or not String(dt["why"]).contains("站在上面"):
		_finish(false, "脚下那格该垒不了，实为 %s" % dt)
		return
	main.clear_aim_ray()
	await _shot("EARTH_step")
	var north := _center(i0 - 2, j0 + 1)
	await _walk_to(north, 0.6, 10.0, true)
	await _until(func() -> bool: return player.is_on_floor(), 3.0)
	await _frames(20)
	var ground: float = world.get_surface_height(player.global_position.x, player.global_position.z)
	if _in_pit() or not player.is_on_floor() or abs(player.global_position.y - ground) > 0.3:
		_finish(false, "踩着台阶该出得来、站在地面上（人在 %s，地面 %.2f）" % [player.global_position, ground])
		return
	_log("踩着 1 m 的台阶出来了：%s" % player.global_position)

	# 坑边隔两格垒一个 1 m 的土台，跳上去
	stage = "土台"
	var mi := i0 + 1
	var mj := j0 + 3
	if not await _walk_to(_center(mi - 2, mj), 0.3):
		_finish(false, "走不到土台边")
		return
	if not await _raise(mi, mj, 2):
		return
	main.clear_aim_ray()
	if not await _walk_to(_center(mi, mj), 0.25, 8.0, true):
		_finish(false, "跳不上 1 m 的土台（人在 %s）" % player.global_position)
		return
	await _until(func() -> bool: return player.is_on_floor(), 3.0)
	await _frames(20)
	var mound_y: float = natural[Vector2i(mi, mj)] + 1.0
	if abs(player.global_position.y - mound_y) > 0.3:
		_finish(false, "该站在土台上（y ≈ %.2f），实为 %s" % [mound_y, player.global_position])
		return
	_log("站在土台上：y %.2f（原地面 %.2f）" % [player.global_position.y, natural[Vector2i(mi, mj)]])
	player.set_look_yaw(atan2(player.global_position.x - _center(i0, j0).x, player.global_position.z - _center(i0, j0).z) + PI)
	player.set_look_pitch(-0.5)
	await _shot("EARTH_mound")

	# 存档、重开
	stage = "存档"
	for di in range(-1, 4):
		for dj in range(-1, 5):
			saved_steps[Vector2i(i0 + di, j0 + dj)] = _step(i0 + di, j0 + dj)
	main.get_node("GroupStream").save_world()
	await _frames(5)
	var works_path := WORLD_DIR.path_join("works").path_join("2051.txt")
	if not FileAccess.file_exists(works_path):
		_finish(false, "存档里没有 %s" % works_path)
		return
	var cells_before := world.get_edit_cell_count()
	for action in MOVES:
		Input.action_release(action)
	remove_child(main)
	main.queue_free()
	await _frames(10)
	_open_scene()
	stage = "重开"
	if not await _until(func() -> bool: return world.is_loaded() and world.get_edit_cell_count() == cells_before, 60.0):
		_finish(false, "重开以后改过的格该回来（%d 格，实为 %d）" % [cells_before, world.get_edit_cell_count() if world.is_loaded() else -1])
		return
	for c in saved_steps:
		if _step(c.x, c.y) != saved_steps[c]:
			_finish(false, "重开以后 (%d, %d) 该是 %d 级，实为 %d" % [c.x, c.y, saved_steps[c], _step(c.x, c.y)])
			return
	main.teleport_to(_center(i0 - 1, j0))
	if not await _until(func() -> bool: return player.is_on_floor() and world.is_ready_at(player.global_position) and world.get_patch_count() >= 1, 30.0):
		_finish(false, "重开以后坑边该挂着补丁（%d）" % world.get_patch_count())
		return
	player.set_look_pitch(-0.9)
	await _shot("EARTH_reloaded")
	_finish(true, "停机坪挖不了 → 挖 2 × 2 × 2 m 的坑（出土 16）→ 木锹挖不动石、石镐凿一级 → 站在坑底、跳不出去 → 垒一级台阶爬出来 → 垒土台跳上去 → 存档重开都还在（%d 格）" % cells_before)


func _finish(ok: bool, why: String) -> void:
	if done:
		return
	done = true
	for action in MOVES:
		Input.action_release(action)
	print("earthworks: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，手持「%s」，背包 %s，提示「%s」，通知「%s」）" % [
		"PASS" if ok else "FAIL", why, stage, elapsed, player.global_position, main.get_held_item(), _pack_text(), main.get_prompt(), main.get_notice()])
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
