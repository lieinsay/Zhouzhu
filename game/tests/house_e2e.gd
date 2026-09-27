extends Node
## 搭建件拼一座完整的屋（world_2051.tscn，切片十一）：只经输入动作与背包里的点击操作，准星用测试给的射线（set_aim_ray，不靠相机）。
## 行李：地基 8、土 18、夯土墙 6、门墙 2、窗墙 2、草顶 8、山墙 8（开局的粟照旧）。
## 在主码头附近找一块 7 × 7 m 能盖、够平的地，瞬移过去 → 背包里点配方「夯土墙」6 回（随身：土 3 → 1）
## → 地基：对准地面摆第一块（新开一座），再对准旁边三格的地面接成 2 × 2（同一座）
## → 墙：对准地基面上靠各条边的地方摆 6 面夯土墙、南边一扇门墙、东边一扇窗墙；这时这座还缺顶 4 格
## → 草顶：对准墙顶，里面那格、往外低（北两格往北、南两格往南），两坡在屋脊接上；这时还缺山墙 4 面
## → 山墙：对准东西两山墙顶、坡顶底下那一截——一座完整的屋（HUD 提示「这座是一座完整的屋」）
## → 门：对着门按 E 开门、走进去（人在一座完整的屋里，天上下雨淋不着——Atmosphere 报屋里）；从里面关上门，往外走被门挡住；再开门出来
## → X 拆掉北墙：这座不完整了（缺墙 1 面）、夯土墙回背包；摆回去又完整
## → G 开样式面板、按 1 拿起「草房」：隔开一段找一块地，按 E 一次摆下整座（按件扣背包），第二座也是完整的屋
##
##   Godot --headless --path game --fixed-fps 60 res://tests/house_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_2051.tscn")
const TIMEOUT := 600.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "sprint", "jump", "interact"]
const FAR := Vector3(1e6, 0, 1e6)
const RECIPES := 2

var main: Expedition
var world: World
var player: Player
var atmosphere: Atmosphere

var elapsed := 0.0
var stage := "开局"
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var done := false

var sid := -1  # 这一座的 id


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	main.starting_pack = {"millet": 5, "foundation": 8, "earth": 18, "earth_wall": 6, "door_wall": 2, "window_wall": 2, "thatch_roof": 8, "gable": 8}
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	atmosphere = main.get_node("Atmosphere")
	_run()


func _log(line: String) -> void:
	print("house: " + line)


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


func _slot_of(item: String) -> int:
	for i in range(0, 36):
		if main.get_pack_slot(i)["item"] == item:
			return i
	return -1


## 背包里 item 那叠放上快捷栏第 hotbar 格（已经在就不动），按数字键拿起来
func _hold(item: String, hotbar: int) -> bool:
	if main.get_pack_slot(hotbar)["item"] != item:
		await _tap("inventory")
		if not main.is_pack_open():
			_finish(false, "Tab 没打开背包")
			return false
		var slot := _slot_of(item)
		if slot < 0:
			_finish(false, "背包里没有 %s" % item)
			return false
		await _click(main.get_pack_slot_center(0, slot))
		await _click(main.get_pack_slot_center(0, hotbar))
		await _tap("inventory")
	await _tap("hotbar_%d" % (hotbar + 1))
	if main.get_held_item() != item:
		_finish(false, "手上该拿着 %s，实为 %s" % [item, main.get_held_item()])
		return false
	return true


func _empty_hand() -> void:
	await _tap("hotbar_9")


func _pack_text() -> String:
	var parts := []
	for item in main.get_pack_items():
		parts.append("%s %d" % [item, main.get_item_count(item)])
	return ", ".join(parts)


# ---------------------------------------------------------------- 一座的局部坐标

func _structure(id: int) -> Dictionary:
	for s in main.get_structures():
		if int(s["id"]) == id:
			return s
	return {}


## 这一座局部坐标里的一点 → 全局（yaw 与 build_pieces.h 同：局部 +x → (cos, −sin)、+z → (sin, cos)）
func _to_global(s: Dictionary, lx: float, ly: float, lz: float) -> Vector3:
	var yaw: float = s["yaw"]
	return Vector3(s["x"] + cos(yaw) * lx + sin(yaw) * lz, s["y"] + ly, s["z"] - sin(yaw) * lx + cos(yaw) * lz)


## 准星：从 a 打到 b（这一座局部坐标）
func _aim_local(s: Dictionary, a: Vector3, b: Vector3) -> void:
	main.set_aim_ray(_to_global(s, a.x, a.y, a.z), _to_global(s, b.x, b.y, b.z))


## 摆手上那件：准星照 aim 设好，虚影绿了按 E，件数多一件
func _place(label: String) -> bool:
	await _frames(3)
	var ghost: Dictionary = main.get_piece_ghost()
	if not ghost["on"] or not ghost["ok"]:
		_finish(false, "%s 摆不下（虚影 %s；提示「%s」）" % [label, ghost, main.get_prompt()])
		return false
	var before: int = main.get_works_counts()["pieces"]
	await _tap("interact")
	if int(main.get_works_counts()["pieces"]) != before + 1:
		_finish(false, "%s 按 E 没摆下（%s）" % [label, main.get_notice()])
		return false
	return true


# ---------------------------------------------------------------- 找地、走

func _cell_free(x: float, z: float) -> bool:
	var f: Dictionary = world.get_earth_facts_at(x, z, FAR)
	return not f.is_empty() and f["solid"] and f["blocked"] == "" and float(f["shore"]) > 5.0


## 以 p 为中心 7 × 7 m：每 1 m 一格都能动土（没有树、房子、坪、田、水），高低差不到 0.35 m
func _site_ok(p: Vector3) -> bool:
	var lo := 1e9
	var hi := -1e9
	for dx in range(-3, 4):
		for dz in range(-3, 4):
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


func _find_site(around: Vector3, min_r: float, avoid := Vector3.INF) -> Vector3:
	for r in [30.0, 50.0, 80.0, 120.0, 170.0, 230.0, 300.0, 380.0]:
		if r < min_r:
			continue
		for k in 24:
			var a := k * TAU / 24.0
			var p := around + Vector3(cos(a) * r, 0, sin(a) * r)
			if avoid != Vector3.INF and Vector2(p.x - avoid.x, p.z - avoid.z).length() < 14.0:
				continue
			if _site_ok(p):
				p.y = world.get_surface_height(p.x, p.z)
				return p
	return Vector3.INF


func _walk_to(target: Vector3, radius: float, seconds := 10.0) -> bool:
	var ok := await _until(func() -> bool:
		var to := target - player.global_position
		to.y = 0.0
		if to.length() <= radius:
			return true
		player.set_look_yaw(atan2(-to.x, -to.z))
		Input.action_press("move_forward")
		return false, seconds)
	Input.action_release("move_forward")
	await _frames(15)
	return ok


# ---------------------------------------------------------------- 流程

func _run() -> void:
	await _frames(30)
	if not await _until(func() -> bool: return world.is_loaded() and player.is_on_floor(), 60.0):
		_finish(false, "主码头站不稳")
		return

	stage = "找地"
	var site := _find_site(player.global_position, 0.0)
	if site == Vector3.INF:
		_finish(false, "主码头 380 m 内找不到 7 × 7 m 能盖的平地")
		return
	_log("盖在 %s（离主码头 %.0f m）" % [site, site.distance_to(player.global_position)])
	main.teleport_to(site + Vector3(0, 0, 5))
	if not await _until(func() -> bool: return player.is_on_floor() and world.is_ready_at(player.global_position), 30.0):
		_finish(false, "瞬移过去站不稳")
		return
	await _frames(20)

	# 合 6 面夯土墙（随身：土 3 → 1）
	stage = "合墙"
	await _tap("inventory")
	var recipe: int = main.get_recipe_index("earth_wall")
	if recipe < 0:
		_finish(false, "没有夯土墙的配方")
		return
	for k in 6:
		await _click(main.get_pack_slot_center(RECIPES, recipe))
	await _tap("inventory")
	if main.get_item_count("earth_wall") != 12 or main.get_item_count("earth") != 0:
		_finish(false, "合 6 回该有夯土墙 12、土用完（墙 %d，土 %d）" % [main.get_item_count("earth_wall"), main.get_item_count("earth")])
		return
	_log("随身合了 6 面夯土墙")

	# 地基：第一块新开一座，再接成 2 × 2
	stage = "地基"
	if not await _hold("foundation", 0):
		return
	main.set_aim_ray(site + Vector3(0, 3, 0), site - Vector3(0, 3, 0))
	await _frames(3)
	if int(main.get_piece_ghost()["structure"]) != -1:
		_finish(false, "第一块地基该新开一座（虚影 %s）" % main.get_piece_ghost())
		return
	if not await _place("第一块地基"):
		return
	var structures: Array = main.get_structures()
	if structures.size() != 1:
		_finish(false, "该有一座（%d）" % structures.size())
		return
	sid = int(structures[0]["id"])
	var s := _structure(sid)
	for c in [Vector2(3, 1), Vector2(1, 3), Vector2(3, 3)]:
		var g := _to_global(s, c.x, 0.0, c.y)
		main.set_aim_ray(g + Vector3(0, 3, 0), g - Vector3(0, 3, 0))
		if not await _place("地基 %s" % c):
			return
		if int(main.get_piece_ghost()["structure"]) != sid and main.get_structures().size() != 1:
			_finish(false, "地基该接在同一座上（%d 座）" % main.get_structures().size())
			return
	if main.get_structures().size() != 1 or int(_structure(sid)["pieces"]) != 4:
		_finish(false, "该是一座、四块地基（%s）" % main.get_structures())
		return
	_log("地基 2 × 2：地坪 %.2f、朝向 %.0f°" % [s["y"], rad_to_deg(s["yaw"])])
	await _shot("HOUSE_foundation")

	# 墙：地基面上靠各条边（局部坐标：格 2 m，墙在格边上）
	stage = "墙"
	var walls := [
		["earth_wall", 1.0, 0.15], ["earth_wall", 3.0, 0.15],  # 北
		["earth_wall", 0.15, 1.0], ["earth_wall", 0.15, 3.0],  # 西
		["earth_wall", 3.85, 1.0], ["window_wall", 3.85, 3.0],  # 东
		["earth_wall", 1.0, 3.85], ["door_wall", 3.0, 3.85],  # 南
	]
	var hot := {"earth_wall": 1, "window_wall": 2, "door_wall": 3}
	for w in walls:
		if main.get_held_item() != w[0] and not await _hold(w[0], hot[w[0]]):
			return
		_aim_local(s, Vector3(w[1], 2.0, w[2]), Vector3(w[1], -0.5, w[2]))
		if not await _place("%s（%.2f, %.2f）" % w):
			return
	var house: Dictionary = _structure(sid)
	if house["complete"] or not String(house["missing"]).contains("顶 4 格"):
		_finish(false, "围好墙还缺顶 4 格（%s）" % house)
		return
	_log("围好了墙：还缺 %s" % house["missing"])
	await _shot("HOUSE_walls")

	# 坡顶：对准墙顶，摆在里面那格、往外低
	stage = "顶"
	if not await _hold("thatch_roof", 4):
		return
	for r in [[1.0, 0.0, 2], [3.0, 0.0, 2], [1.0, 4.0, 0], [3.0, 4.0, 0]]:
		_aim_local(s, Vector3(r[0], 4.6, r[1]), Vector3(r[0], 2.0, r[1]))
		await _frames(3)
		var ghost: Dictionary = main.get_piece_ghost()
		if int(ghost["dir"]) != r[2] or int(ghost["tier"]) != 0:
			_finish(false, "对准 (%.0f, %.0f) 的墙顶：顶该往 %d 低、最下一阶（虚影 %s）" % [r[0], r[1], r[2], ghost])
			return
		if not await _place("草顶 (%.0f, %.0f)" % [r[0], r[1]]):
			return
	house = _structure(sid)
	if house["complete"] or not String(house["missing"]).contains("山墙 4 面"):
		_finish(false, "两坡顶在屋脊接上，还缺山墙 4 面（%s）" % house)
		return
	_log("坡顶接上了：还缺 %s" % house["missing"])

	# 山墙：东西两山、坡顶底下那一截（从墙顶上方一点往下打）
	stage = "山墙"
	if not await _hold("gable", 5):
		return
	for g in [[0.0, 1.0], [0.0, 3.0], [4.0, 1.0], [4.0, 3.0]]:
		_aim_local(s, Vector3(g[0], 3.45, g[1]), Vector3(g[0], 2.5, g[1]))
		if not await _place("山墙 (%.0f, %.0f)" % [g[0], g[1]]):
			return
	house = _structure(sid)
	if not house["complete"]:
		_finish(false, "四面墙、门、顶、山墙：该是一座完整的屋（还缺 %s）" % house["missing"])
		return
	# 准星对着这座：提示说完整
	await _empty_hand()
	_aim_local(s, Vector3(2.0, 1.5, 7.0), Vector3(1.0, 1.5, 3.0))
	await _frames(4)
	if not main.get_prompt().contains("完整的屋"):
		_finish(false, "对着这座该提示「这座是一座完整的屋」，实为「%s」" % main.get_prompt())
		return
	_log("一座完整的屋：%d 件" % int(house["pieces"]))
	main.clear_aim_ray()
	player.set_look_yaw(atan2(player.global_position.x - _to_global(s, 2, 0, 2).x, player.global_position.z - _to_global(s, 2, 0, 2).z))
	player.set_look_pitch(-0.3)
	await _shot("HOUSE_complete")

	# 门：开门进去、下雨淋不着；关门走不出去
	stage = "进门"
	var door_out := _to_global(s, 3.0, 0.0, 6.0)
	if not await _walk_to(door_out, 0.4):
		_finish(false, "走不到门外")
		return
	_aim_local(s, Vector3(3.0, 1.2, 6.0), Vector3(3.0, 1.2, 2.0))
	await _frames(3)
	var look: Vector2i = main.get_look_piece()
	if look.x != sid or look.y < 0:
		_finish(false, "准星该对着门（对着 %s）" % look)
		return
	await _tap("interact")
	if not main.get_notice().contains("开了门"):
		_finish(false, "对着门按 E 该开门（%s）" % main.get_notice())
		return
	main.clear_aim_ray()
	atmosphere.set_weather_override("rain", 0.0)
	var inside := _to_global(s, 3.0, 0.0, 2.2)
	if not await _walk_to(inside, 0.4):
		_finish(false, "门开着走不进去（人在 %s）" % player.global_position)
		return
	# 往里走到西北那格、面朝门站着（相机在人身后、屋里；站在门口背对门时相机会顺着门洞留在门外）
	if not await _walk_to(_to_global(s, 1.2, 0.0, 1.2), 0.3):
		_finish(false, "屋里走不到西北那格（人在 %s）" % player.global_position)
		return
	player.set_look_yaw(float(s["yaw"]) + PI)
	if not await _until(func() -> bool: return main.is_inside_house() and float(atmosphere.get_look()["indoor"]) > 0.9, 5.0):
		_finish(false, "人该在一座完整的屋里、屋里淋不着雨（在屋里 %s，indoor %.2f）" % [main.is_inside_house(), atmosphere.get_look()["indoor"]])
		return
	if not main.get_status_text().contains("自己盖的屋里"):
		_finish(false, "HUD 该报在自己盖的屋里（%s）" % main.get_status_text())
		return
	_log("进了屋：在一座完整的屋里，下着雨屋里淋不着（indoor %.2f）" % atmosphere.get_look()["indoor"])
	await _shot("HOUSE_inside")
	# 从里面关门（对着门框上的过梁）、往外走被挡住
	_aim_local(s, Vector3(3.0, 2.6, 2.2), Vector3(3.0, 2.6, 6.0))
	await _frames(3)
	await _tap("interact")
	if not main.get_notice().contains("关上了门"):
		_finish(false, "从里面对着门按 E 该关门（%s）" % main.get_notice())
		return
	main.clear_aim_ray()
	await _walk_to(door_out, 0.4, 3.0)
	var lz := _local_z(s, player.global_position)
	if lz > 3.9:
		_finish(false, "门关着该走不出去（人在局部 z %.2f）" % lz)
		return
	_log("门关着出不去：人在局部 z %.2f" % lz)
	_aim_local(s, Vector3(3.0, 1.2, 2.6), Vector3(3.0, 1.2, 6.0))
	await _frames(3)
	await _tap("interact")
	main.clear_aim_ray()
	if not await _walk_to(door_out, 0.4):
		_finish(false, "再开门出不来（人在 %s）" % player.global_position)
		return
	atmosphere.clear_weather_override()

	# 拆北墙：不完整了、墙回背包；摆回去又完整
	stage = "拆墙"
	# 绕过屋子东边走到北墙外
	for w in [Vector2(6.5, 6.0), Vector2(6.5, -2.5), Vector2(3.0, -2.5)]:
		if not await _walk_to(_to_global(s, w.x, 0.0, w.y), 0.5):
			_finish(false, "走不到北墙外（卡在 %s，人在 %s）" % [w, player.global_position])
			return
	var walls_before := main.get_item_count("earth_wall")
	_aim_local(s, Vector3(3.0, 1.5, -2.0), Vector3(3.0, 1.5, 1.0))
	await _frames(3)
	await _tap("remove")
	house = _structure(sid)
	if house["complete"] or not String(house["missing"]).contains("墙 1 面") or main.get_item_count("earth_wall") != walls_before + 1:
		_finish(false, "拆了北墙该不完整、墙回背包（%s；墙 %d → %d；%s）" % [house, walls_before, main.get_item_count("earth_wall"), main.get_notice()])
		return
	_log("拆了北墙：还缺 %s" % house["missing"])
	if not await _hold("earth_wall", 1):
		return
	_aim_local(s, Vector3(3.0, 2.0, 0.15), Vector3(3.0, -0.5, 0.15))
	if not await _place("北墙摆回去"):
		return
	if not _structure(sid)["complete"]:
		_finish(false, "北墙摆回去该又完整（%s）" % _structure(sid))
		return

	# 整座样式：G、1 拿起「草房」，找一块地一次摆下
	stage = "样式"
	await _empty_hand()
	var names: PackedStringArray = main.get_style_names()
	var pick := -1
	for k in names.size():
		if names[k] == "草房":
			pick = k
	if pick < 0 or pick > 8:
		_finish(false, "样式里该有「草房」（%s）" % names)
		return
	await _tap("style_panel")
	if not main.is_style_open():
		_finish(false, "G 没打开样式面板")
		return
	await _tap("hotbar_%d" % (pick + 1))
	if main.get_style_index() != pick or main.is_style_open():
		_finish(false, "按 %d 该拿起「草房」、合上面板（拿着 %d）" % [pick + 1, main.get_style_index()])
		return
	var site2 := _find_site(site, 20.0, site)
	if site2 == Vector3.INF:
		_finish(false, "找不到第二块地")
		return
	main.teleport_to(site2 + Vector3(0, 0, 6))
	await _until(func() -> bool: return player.is_on_floor() and world.is_ready_at(player.global_position), 30.0)
	await _frames(20)
	main.set_aim_ray(site2 + Vector3(0, 3, 0), site2 - Vector3(0, 3, 0))
	await _frames(3)
	var ghost2: Dictionary = main.get_piece_ghost()
	if not ghost2["on"] or not ghost2["ok"]:
		_finish(false, "「草房」该摆得下（%s；%s）" % [ghost2, main.get_prompt()])
		return
	var before_pieces: int = main.get_works_counts()["pieces"]
	var foundations_before := main.get_item_count("foundation")
	await _tap("interact")
	var counts: Dictionary = main.get_works_counts()
	if int(counts["structures"]) != 2 or int(counts["pieces"]) != before_pieces + 20 or main.get_item_count("foundation") != foundations_before - 4:
		_finish(false, "一次摆下整座：两座、多 20 件、地基扣 4（%s；地基 %d → %d；%s）" % [counts, foundations_before, main.get_item_count("foundation"), main.get_notice()])
		return
	var second: Dictionary = main.get_structures()[1]
	if not second["complete"]:
		_finish(false, "照样式摆的那座该是完整的屋（还缺 %s）" % second["missing"])
		return
	main.clear_aim_ray()
	player.set_look_pitch(-0.3)
	await _shot("HOUSE_style")
	_finish(true, "合墙 → 地基 2 × 2 → 墙门窗 → 草顶 → 山墙：完整的屋 → 开门进屋、屋里淋不着、关门出不去 → 拆北墙不完整、摆回去又完整 → 样式「草房」一次摆下也完整（%d 件）" % int(counts["pieces"]))


func _local_z(s: Dictionary, p: Vector3) -> float:
	var yaw: float = s["yaw"]
	var dx: float = p.x - s["x"]
	var dz: float = p.z - s["z"]
	return sin(yaw) * dx + cos(yaw) * dz


func _finish(ok: bool, why: String) -> void:
	if done:
		return
	done = true
	for action in MOVES:
		Input.action_release(action)
	print("house: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，手持「%s」，背包 %s，提示「%s」，通知「%s」）" % [
		"PASS" if ok else "FAIL", why, stage, elapsed, player.global_position, main.get_held_item(), _pack_text(), main.get_prompt(), main.get_notice()])
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
