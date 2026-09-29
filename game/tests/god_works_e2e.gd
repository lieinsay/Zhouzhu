extends Node
## 开发者营造（world_seed42.tscn，拓展切片十一）：god 下 F8 开面板挑工具，直接挖地、垒土、摆搭建件与整座样式，不看背包、不扣料；挖垒带笔刷、按一下改一级。
## 只经输入动作操作（F3 走 DevTools 的输入事件），准星用测试给的射线（set_aim_ray，不靠相机）。行李只有粟 5（挖垒搭建的东西一样都没有）。
## 在主码头附近找一块 9 × 9 m 能动土、够平的地，瞬移过去
## → 没开 god 按 F8：面板不开、提示先按 F3 → F3 开 god → F8 开面板：S 挪到「挖」、D 笔刷 3 → 5、E 拿在手上（面板合上）
## → 准星对着那块地的中心按一下 E：5 × 5 = 25 格各低一级、当场改（不读条），背包不多土
## → 按住 E 一秒：接着一级级往下（隔 0.2 s 一下），25 格一样深
## → F8、S 挪到「垒」、E：按一下 25 格各高一级；[ 笔刷 5 → 3，再按一下只改中间 9 格
## → F8、挪到「地基」、E：在隔开的另一块地上摆一块地基（新开一座），背包里没有地基也摆得下
## → F8、挪到样式「草房」、E：再隔开一块地一次摆下整座（20 件），是一座完整的屋；摆完样式还拿在手上
## → 按 1 拿回快捷栏第一格：放下开发者工具；F3 关 god：再 F8 不开；背包从头到尾没动
##
##   Godot --headless --path game --fixed-fps 60 res://tests/god_works_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 600.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "sprint", "jump", "interact"]
const FAR := Vector3(1e6, 0, 1e6)

var main: Expedition
var world: World
var player: Player

var elapsed := 0.0
var stage := "开局"
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var done := false
var pack_start := ""


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	main.starting_pack = {"millet": 5}
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	_run()


func _log(line: String) -> void:
	print("god_works: " + line)


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


## 走 DevTools 的 _input（F3 是输入事件，不是轮询）
func _event(action: String) -> void:
	for pressed in [true, false]:
		var ev := InputEventAction.new()
		ev.action = action
		ev.pressed = pressed
		Input.parse_input_event(ev)
	await _frames(4)


func _pack_text() -> String:
	var parts := []
	for item in main.get_pack_items():
		parts.append("%s %d" % [item, main.get_item_count(item)])
	return ", ".join(parts)


## F8 开面板，W/S 把光标挪到叫 row_name 的那一行，E 拿在手上
func _pick(row_name: String) -> bool:
	var names: PackedStringArray = main.get_god_tool_names()
	var want := -1
	for k in names.size():
		if names[k] == row_name:
			want = k
	if want < 0:
		_finish(false, "开发者工具里没有「%s」（%s）" % [row_name, names])
		return false
	await _tap("dev_works")
	if not main.is_dev_works_open():
		_finish(false, "F8 没打开开发者营造面板（%s）" % main.get_notice())
		return false
	var cursor: int = int(main.get_god_tool()["index"]) + 1  # 开面板时光标在手上那行（0 = 空手）
	while cursor < want + 1:
		await _tap("move_back")
		cursor += 1
	while cursor > want + 1:
		await _tap("move_forward")
		cursor -= 1
	await _tap("interact")
	var tool: Dictionary = main.get_god_tool()
	if main.is_dev_works_open() or int(tool["index"]) != want:
		_finish(false, "E 该拿起「%s」、合上面板（手上 %s）" % [row_name, tool])
		return false
	return true


# ---------------------------------------------------------------- 找地

func _cell_free(x: float, z: float) -> bool:
	var f: Dictionary = world.get_earth_facts_at(x, z, FAR)
	return not f.is_empty() and f["solid"] and f["blocked"] == "" and float(f["shore"]) > 5.0 and float(f["soil_depth"]) >= 1.0


## 以 p 为中心 9 × 9 m：每 1 m 一格都能动土，高低差不到 0.35 m
func _site_ok(p: Vector3) -> bool:
	var lo := 1e9
	var hi := -1e9
	for dx in range(-4, 5):
		for dz in range(-4, 5):
			var x := p.x + dx
			var z := p.z + dz
			if not _cell_free(x, z):
				return false
			var y: float = world.get_surface_height(x, z)
			if is_nan(y):
				return false
			lo = min(lo, y)
			hi = max(hi, y)
	return hi - lo < 0.35


func _find_site(around: Vector3, avoid: Array) -> Vector3:
	for r in [30.0, 50.0, 80.0, 120.0, 170.0, 230.0, 300.0, 380.0]:
		for k in 24:
			var a := k * TAU / 24.0
			var p := around + Vector3(cos(a) * r, 0, sin(a) * r)
			var near := false
			for q in avoid:
				if Vector2(p.x - q.x, p.z - q.z).length() < 16.0:
					near = true
			if near:
				continue
			if _site_ok(p):
				p.y = world.get_surface_height(p.x, p.z)
				return p
	return Vector3.INF


## 瞬移到 site 南边 3.2 m（人在笔刷外、够得着中心那格），god 下悬着不掉
func _go(site: Vector3) -> bool:
	main.teleport_to(site + Vector3(0, 0, 3.2))
	if not await _until(func() -> bool: return world.is_ready_at(player.global_position), 30.0):
		_finish(false, "瞬移过去块没挂好")
		return false
	await _frames(20)
	return true


## 以 site 为心 n × n 格的级数
func _steps(site: Vector3, n: int) -> Array:
	var out := []
	var r := (n - 1) / 2
	for dz in range(-r, r + 1):
		for dx in range(-r, r + 1):
			out.append(world.get_edit_step_at(site.x + dx, site.z + dz))
	return out


func _all(values: Array, v: int) -> bool:
	for x in values:
		if int(x) != v:
			return false
	return true


# ---------------------------------------------------------------- 流程

func _run() -> void:
	await _frames(30)
	if not await _until(func() -> bool: return world.is_loaded() and player.is_on_floor(), 60.0):
		_finish(false, "主码头站不稳")
		return
	pack_start = _pack_text()
	var dock := player.global_position

	stage = "找地"
	var site := _find_site(dock, [])
	if site == Vector3.INF:
		_finish(false, "主码头 380 m 内找不到 9 × 9 m 能动土的平地")
		return
	# 格心：site 落在的那一格的中心（笔刷以格为心）
	var f0: Dictionary = world.get_earth_facts_at(site.x, site.z, FAR)
	var center: Vector3 = world.get_edit_cell_center(int(f0["i"]), int(f0["j"]))
	site = Vector3(center.x, center.y, center.z)
	_log("挖在 %s（离主码头 %.0f m）" % [site, site.distance_to(dock)])
	if not await _go(site):
		return

	stage = "没开 god"
	await _tap("dev_works")
	if main.is_dev_works_open() or not String(main.get_notice()).contains("F3"):
		_finish(false, "没开 god 按 F8 不该开面板、该提示先按 F3（%s）" % main.get_notice())
		return
	await _event("god_mode")
	if not player.is_god_mode():
		_finish(false, "F3 没开 god")
		return
	_log("F8 先要 god；F3 开了 god")

	stage = "挖"
	await _tap("dev_works")
	if not main.is_dev_works_open():
		_finish(false, "god 下 F8 没打开面板")
		return
	await _tap("move_back")  # 空手 → 挖
	await _tap("move_right")  # 笔刷 3 → 5
	await _shot("GOD_panel")
	await _tap("interact")
	var tool: Dictionary = main.get_god_tool()
	if tool.get("kind", "") != "dig" or int(tool["brush"]) != 5 or not tool["active"] or main.is_dev_works_open():
		_finish(false, "该拿着挖、笔刷 5、面板合上（%s）" % tool)
		return
	main.set_aim_ray(site + Vector3(0, 2.5, 0), site - Vector3(0, 3.0, 0))
	await _frames(3)
	var target: Dictionary = main.get_dig_target()
	if not target["on"]:
		_finish(false, "准星该落到那一格（%s；%s）" % [target, main.get_prompt()])
		return
	await _tap("interact")
	var s5 := _steps(site, 5)
	if not _all(s5, -1) or int(main.get_dig_target()["brush_done"]) != 25:
		_finish(false, "按一下该 25 格各低一级（%s；改了 %s 格；%s）" % [s5, main.get_dig_target()["brush_done"], main.get_notice()])
		return
	if _all(_steps(site, 7), -1):
		_finish(false, "笔刷外的一圈不该动")
		return
	if main.get_item_count("earth") != 0:
		_finish(false, "开发者挖不进背包（土 %d）" % main.get_item_count("earth"))
		return
	_log("按一下：5 × 5 各低一级（%s）" % main.get_notice())
	Input.action_press("interact")  # 按住
	await _until(func() -> bool: return false, 1.0)
	Input.action_release("interact")
	await _frames(4)
	s5 = _steps(site, 5)
	var deep := int(s5[12])
	if deep > -4 or not _all(s5, deep):
		_finish(false, "按住一秒该接着挖好几级、25 格一样深（%s）" % [s5])
		return
	_log("按住一秒：挖到 %d 级（%.1f m）" % [deep, deep * 0.5])
	player.set_look_pitch(-0.9)  # 准星射线留着：框罩着 5 × 5 的笔刷
	await _shot("GOD_dig")

	stage = "垒"
	if not await _pick("垒"):
		return
	main.set_aim_ray(site + Vector3(0, 2.5, 0), site - Vector3(0, 6.0, 0))
	await _frames(3)
	await _tap("interact")
	s5 = _steps(site, 5)
	if not _all(s5, deep + 1):
		_finish(false, "垒：25 格各高一级（%s；%s）" % [s5, main.get_notice()])
		return
	await _tap("brush_smaller")
	if main.get_god_brush() != 3:
		_finish(false, "[ 该把笔刷换成 3（%d）" % main.get_god_brush())
		return
	await _tap("interact")
	var s3 := _steps(site, 3)
	s5 = _steps(site, 5)
	var ring := 0
	for v in s5:
		if int(v) == deep + 1:
			ring += 1
	if not _all(s3, deep + 2) or ring != 16:
		_finish(false, "3 × 3 再垒一级：中间 9 格高一级、外圈 16 格不动（%s）" % [s5])
		return
	if main.get_item_count("earth") != 0:
		_finish(false, "开发者垒不扣背包")
		return
	_log("垒：5 × 5 一级、换 3 × 3 再一级")
	# ] ] 换到 9 × 9：按一下的那一帧主线程多久（一次换账、受影响的块只重画一回）
	await _tap("brush_larger")
	await _tap("brush_larger")
	if main.get_god_brush() != 9:
		_finish(false, "] 两下该换到 9（%d）" % main.get_god_brush())
		return
	var t0 := Time.get_ticks_usec()
	await _frames(2)
	var idle_ms := (Time.get_ticks_usec() - t0) / 1000.0
	Input.action_press("interact")
	t0 = Time.get_ticks_usec()
	await _frames(2)  # physics_frame 在这一帧的物理之前发：两帧才罩住按下那一帧
	var ms := (Time.get_ticks_usec() - t0) / 1000.0 - idle_ms
	Input.action_release("interact")
	await _frames(4)
	var nine: Dictionary = main.get_dig_target()
	if int(nine["brush_done"]) < 60:
		_finish(false, "9 × 9 该改六十格以上（%s；%s）" % [nine, main.get_notice()])
		return
	_log("9 × 9 按一下：改 %d 格、跳过 %d 格（%s），比不按多 %.1f ms" % [nine["brush_done"], nine["brush_skipped"], main.get_notice(), ms])
	Input.action_press("interact")
	t0 = Time.get_ticks_usec()
	await _frames(2)
	_log("再按一下（块都改过了，只重画补丁）：比不按多 %.1f ms" % ((Time.get_ticks_usec() - t0) / 1000.0 - idle_ms))
	Input.action_release("interact")
	await _frames(4)
	t0 = Time.get_ticks_usec()
	for dz in range(-4, 5):
		for dx in range(-4, 5):
			world.get_earth_facts_at(site.x + dx, site.z + dz, player.global_position)
	_log("其中量 81 格的事实 %.1f ms" % ((Time.get_ticks_usec() - t0) / 1000.0))

	stage = "地基"
	var site2 := _find_site(dock, [site])
	if site2 == Vector3.INF:
		_finish(false, "找不到第二块地")
		return
	if not await _go(site2):
		return
	if not await _pick("地基"):
		return
	main.set_aim_ray(site2 + Vector3(0, 3, 0), site2 - Vector3(0, 3, 0))
	await _frames(3)
	var ghost: Dictionary = main.get_piece_ghost()
	if not ghost["on"] or not ghost["ok"] or int(ghost["structure"]) != -1:
		_finish(false, "地基该新开一座、摆得下（%s；%s）" % [ghost, main.get_prompt()])
		return
	var before: Dictionary = main.get_works_counts()
	await _tap("interact")
	var after: Dictionary = main.get_works_counts()
	if int(after["structures"]) != int(before["structures"]) + 1 or int(after["pieces"]) != int(before["pieces"]) + 1 or main.get_item_count("foundation") != 0:
		_finish(false, "背包里没有地基也该摆下一块（%s → %s；%s）" % [before, after, main.get_notice()])
		return
	_log("地基：背包里没有也摆下了")

	stage = "样式"
	var site3 := _find_site(dock, [site, site2])
	if site3 == Vector3.INF:
		_finish(false, "找不到第三块地")
		return
	if not await _go(site3 + Vector3(0, 0, 3.0)):
		return
	if not await _pick("样式「草房」"):
		return
	main.set_aim_ray(site3 + Vector3(0, 3, 0), site3 - Vector3(0, 3, 0))
	await _frames(3)
	ghost = main.get_piece_ghost()
	if not ghost["on"] or not ghost["ok"]:
		_finish(false, "「草房」该摆得下（%s；%s）" % [ghost, main.get_prompt()])
		return
	before = main.get_works_counts()
	await _tap("interact")
	after = main.get_works_counts()
	if int(after["structures"]) != int(before["structures"]) + 1 or int(after["pieces"]) != int(before["pieces"]) + 20:
		_finish(false, "一次摆下整座「草房」：多一座、多 20 件（%s → %s；%s）" % [before, after, main.get_notice()])
		return
	var last: Dictionary = main.get_structures()[main.get_structures().size() - 1]
	if not last["complete"]:
		_finish(false, "「草房」该是一座完整的屋（还缺 %s）" % last["missing"])
		return
	await _frames(2)
	if main.get_god_tool().get("kind", "") != "style" or main.get_style_index() != int(main.get_god_tool()["style"]):
		_finish(false, "开发者工具里的样式摆完该接着拿在手上（%s；样式 %d）" % [main.get_god_tool(), main.get_style_index()])
		return
	main.clear_aim_ray()
	player.set_look_pitch(-0.4)
	await _shot("GOD_style")
	_log("样式「草房」：背包里一件没有也一次摆下，完整的屋")

	stage = "放下"
	await _tap("hotbar_1")
	if int(main.get_god_tool()["index"]) != -1 or main.get_hotbar_selected() != 0:
		_finish(false, "按 1 该放下开发者工具、拿回快捷栏第一格（%s；快捷栏第 %d 格）" % [main.get_god_tool(), main.get_hotbar_selected() + 1])
		return
	if not await _pick("挖（土石都挖得动）"):
		return
	await _event("god_mode")
	await _frames(3)
	if player.is_god_mode() or int(main.get_god_tool()["index"]) != -1:
		_finish(false, "关了 god 该放下开发者工具（%s）" % main.get_god_tool())
		return
	await _tap("dev_works")
	if main.is_dev_works_open():
		_finish(false, "没开 god 不该开面板")
		return
	if _pack_text() != pack_start:
		_finish(false, "背包从头到尾不该动（%s → %s）" % [pack_start, _pack_text()])
		return
	_finish(true, "F8 先要 god → 挖 5 × 5 按一下 25 格、按住挖到 %d 级 → 垒 5 × 5、[ 换 3 × 3 → 地基、样式「草房」背包里没有也摆下 → 按 1、关 god 都放下工具；背包没动（%s）" % [deep, pack_start])


func _finish(ok: bool, why: String) -> void:
	if done:
		return
	done = true
	for action in MOVES:
		Input.action_release(action)
	print("god_works: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，手持「%s」，开发者工具 %s，背包 %s，提示「%s」，通知「%s」）" % [
		"PASS" if ok else "FAIL", why, stage, elapsed, player.global_position, main.get_held_item(), main.get_god_tool(), _pack_text(), main.get_prompt(), main.get_notice()])
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
