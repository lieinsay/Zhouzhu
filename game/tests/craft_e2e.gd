extends Node
## 合成与摆放（world_seed42.tscn）：背包、配方、摆放、船舱，只通过输入（动作、鼠标事件、转视角）操作；去林子那一下用开发者瞬移。
## 行李里带着石料 12、黏土 8（开局的粟、菽照旧）：
## 主码头出生：背包 36 格、快捷栏空着，开局的东西在主区 → 瞬移到最近的一片林子边上，走到树跟前按住 E 伐 6 份木料（空手 4 秒一份；一棵伐倒了换下一棵）
## → Tab 开背包（不在船上：没有船舱）：鼠标点配方「木板」5 回（木料 1 → 木板 4）、再点「工作台」；「木箱」暗着（要工作台）
##   → 鼠标左键拿起工作台、点快捷栏第 1 格放下；右键把木板拿一半、放进空格；Shift+左键把一叠木板搬上快捷栏 → Tab 合上
## → 按 1 拿起工作台：身前出虚影，找一处摆得下的朝向按 E 摆下（实物带碰撞）；身边 3 m 有工作台
## → 看着工作台按 E：背包开着、配方里「木箱」亮了：合木箱、石斧、木锹、窑（石料 8 + 黏土 4）
##   → 鼠标悬停在木箱上按 3：木箱上了快捷栏第 3 格 → 合上，按 3 摆木箱
## → 看着木箱按 E：木箱的 18 格列在背包上面；Shift+左键把粟搬进木箱；键盘 W 挪到木箱那格、E 拿起、S 挪回、E 放下（粟回背包）；
##   再搬进去、合上 → X 收木箱被拒（里面有东西）→ 打开、搬空、合上 → X 收回背包
## → 石斧放上快捷栏选中：再伐一棵，一份只要 2 秒（快一倍）
## → 摆窑、看着窑按 E：合陶罐（黏土 3 + 木料 1）；随身合火把、摆一支（带一盏光）
## → 瞬移回主码头、走上船：Tab 背包上面是「船舱（12 格）」，Shift+左键把陶罐搬进船舱 → 合上；下船再开背包：没有船舱。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/craft_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 600.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "sprint", "jump"]
const PACK := 0
const OTHER := 1
const RECIPES := 2

var main: Expedition
var world: World
var player: Player
var ship: Ship
var scatter: ScatterStream

var elapsed := 0.0
var stage := "开局"
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var done := false

var face := Vector3.ZERO  # 正在伐的那棵树
var pile := Vector3.ZERO  # 林边落脚的那块空地
var felled := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	main.starting_pack = {"millet": 5, "bean": 3, "stone": 12, "clay": 8}
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	scatter = main.get_node("ScatterStream")
	_run()


func _log(line: String) -> void:
	print("craft: " + line)


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
		await _frames(8)  # 等截完再往下走


# ---------------------------------------------------------------- 等、按、点

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
	await _frames(4)  # 松开几帧，下一次 just_pressed 才认


func _mouse_at(at: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = at
	motion.global_position = at
	Input.parse_input_event(motion)
	await _frames(2)


func _click(at: Vector2, button: MouseButton = MOUSE_BUTTON_LEFT, shift := false) -> void:
	await _mouse_at(at)
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = button
		ev.pressed = pressed
		ev.position = at
		ev.global_position = at
		ev.shift_pressed = shift
		Input.parse_input_event(ev)
	await _frames(3)


func _click_slot(section: int, index: int, button: MouseButton = MOUSE_BUTTON_LEFT, shift := false) -> bool:
	var at: Vector2 = main.get_pack_slot_center(section, index)
	if at.x < 0.0:
		_finish(false, "面板上找不到第 %d 区第 %d 格" % [section, index])
		return false
	await _click(at, button, shift)
	return true


func _craft(recipe: String, times := 1, all := false) -> bool:
	var index: int = main.get_recipe_index(recipe)
	if index < 0:
		_finish(false, "没有配方 %s" % recipe)
		return false
	for i in times:
		if not await _click_slot(RECIPES, index, MOUSE_BUTTON_LEFT, all):
			return false
	return true


## 左键拿起 item 那叠、点快捷栏第 hotbar 格放下（窗口里真鼠标会盖掉悬停，点击带着位置，不受影响）
func _to_hotbar(item: String, hotbar: int) -> bool:
	var slot := _slot_of(item, 9)
	if slot < 0:
		_finish(false, "主区里没有 %s" % item)
		return false
	await _click_slot(PACK, slot)
	await _click_slot(PACK, hotbar)
	if main.get_pack_slot(hotbar)["item"] != item or main.get_pack_cursor_stack()["count"] != 0:
		_finish(false, "%s 该到快捷栏第 %d 格（%s，拖着 %s）" % [item, hotbar + 1, main.get_pack_slot(hotbar), main.get_pack_cursor_stack()])
		return false
	return true


func _slot_of(item: String, from := 0) -> int:
	for i in range(from, 36):
		if main.get_pack_slot(i)["item"] == item:
			return i
	return -1


func _count(item: String) -> int:
	return main.get_item_count(item)


func _pack_text() -> String:
	var parts := []
	for item in main.get_pack_items():
		parts.append("%s %d" % [item, main.get_item_count(item)])
	return ", ".join(parts)


# ---------------------------------------------------------------- 走、摆、看

func _standing() -> bool:
	return player.is_on_floor() and world.is_ready_at(player.global_position)


func _walk_to(target: Vector3, radius: float, seconds := 30.0) -> bool:
	var ok := await _until(func() -> bool:
		var to := target - player.global_position
		to.y = 0.0
		if to.length() <= radius:
			return true
		player.set_look_yaw(atan2(-to.x, -to.z))
		Input.action_press("move_forward")
		return false, seconds)
	Input.action_release("move_forward")
	await _frames(10)
	return ok


func _look_at_point(p: Vector3) -> void:
	var to := p - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


func _props() -> Array:
	return main.get_placed_props()


func _prop(id: int) -> Dictionary:
	for p in _props():
		if int(p["id"]) == id:
			return p
	return {}


## 手上拿着能摆的：转着找一个摆得下的朝向，按 E 摆下；返回新那件的 id
func _place_held(away_from: Vector3) -> int:
	var before: int = _props().size()
	var base := atan2(player.global_position.x - away_from.x, player.global_position.z - away_from.z) + PI  # 背对 away_from
	for attempt in 3:
		for k in 8:
			player.set_look_yaw(base + [0, 1, -1, 2, -2, 3, -3, 4][k] * PI / 4.0)
			await _frames(3)
			var ghost: Dictionary = main.get_ghost()
			if not ghost["on"]:
				_finish(false, "手上拿着 %s 却没有虚影" % main.get_held_item())
				return -1
			if ghost["ok"]:
				await _tap("interact")
				if _props().size() != before + 1:
					_finish(false, "按 E 没摆下（%s）" % main.get_notice())
					return -1
				var placed: Dictionary = _props()[-1]
				_log("摆下 %s：(%.1f, %.1f, %.1f) 朝 %d×90°" % [placed["item"], placed["x"], placed["y"], placed["z"], placed["quarter"]])
				return int(placed["id"])
		# 这一圈都摆不下（坡、树、石头）：往外挪几步再试
		var why: String = main.get_ghost()["why"]
		_log("这里摆不下（%s），挪几步" % why)
		var out := Vector3(sin(base), 0, cos(base)) * -4.0
		await _walk_to(player.global_position + out, 0.5, 8.0)
	_finish(false, "转了三圈都摆不下 %s（%s）" % [main.get_held_item(), main.get_ghost()["why"]])
	return -1


## 看着那一件（够得着、在身前）
func _face_prop(id: int) -> bool:
	var p := _prop(id)
	var center := Vector3(p["x"], p["y"], p["z"])
	var ok := await _until(func() -> bool:
		_look_at_point(center)
		if main.get_facing_prop() == id:
			Input.action_release("move_forward")
			return true
		Input.action_press("move_forward")
		return false, 6.0)
	Input.action_release("move_forward")
	await _frames(5)
	if not ok or main.get_facing_prop() != id:
		_finish(false, "看不着 %s（看着的是 %d）" % [p.get("item", "?"), main.get_facing_prop()])
		return false
	return true


func _open_pack() -> bool:
	await _tap("inventory")
	if not main.is_pack_open():
		_finish(false, "Tab 没打开背包")
		return false
	return true


func _close_pack() -> bool:
	await _tap("inventory")
	await _frames(3)
	if main.is_pack_open():
		_finish(false, "Tab 没合上背包")
		return false
	return true


## 离人最近、一路走得过去（直线上没有别的树干挡着）的一棵立着的树；没有返回空字典
func _next_tree() -> Dictionary:
	var at := player.global_position
	var best := {}
	for kind in ["broadleaf", "conifer"]:
		for r in [20.0, 40.0, 80.0]:
			var tree: Dictionary = scatter.find_item_near(at, r, kind)
			if tree.is_empty():
				continue
			var p: Vector3 = tree["position"]
			var stop := p + (at - p).normalized() * (float(tree["reach"]) - 0.5)
			if not scatter.path_clear(at, stop, 0.6):
				continue
			if best.is_empty() or at.distance_to(p) < at.distance_to(best["position"]):
				best = tree
			break
	return best


## 伐近处的树，直到背包里 item 有 want 份（一棵伐倒了——变树桩、没了树干的碰撞——就换下一棵）；返回按住 E 时平均几秒一份
func _chop_until(item: String, want: int) -> float:
	var start := _count(item)
	var held := 0.0
	var tries := 0
	while _count(item) < want:
		tries += 1
		if tries > 6:
			_finish(false, "伐了 6 棵还不够 %d 份 %s（背包 %d）" % [want, item, _count(item)])
			return -1.0
		var tree := _next_tree()
		if tree.is_empty():
			_finish(false, "近处找不到走得过去的立着的树")
			return -1.0
		face = tree["position"]
		var key := int(tree["key"])
		if not await _walk_to(face, float(tree["reach"]) - 0.5, 20.0):
			_finish(false, "走不到树跟前（%s，人在 %s）" % [face, player.global_position])
			return -1.0
		_look_at_point(face)
		await _frames(3)
		if not main.get_prompt().contains("伐木"):
			_finish(false, "树跟前该提示伐木，实为「%s」" % main.get_prompt())
			return -1.0
		var solids := scatter.get_solid_count()
		var t0 := elapsed
		Input.action_press("interact")
		await _until(func() -> bool: return _count(item) >= want or not scatter.is_standing(key), 60.0)
		Input.action_release("interact")
		held += elapsed - t0
		await _frames(5)
		if not scatter.is_standing(key):
			felled += 1
			if scatter.get_solid_count() != solids - 1:
				_finish(false, "树伐倒了树干的碰撞该拆掉（树干 %d → %d）" % [solids, scatter.get_solid_count()])
				return -1.0
			_log("伐倒一棵%s（%s）：背包%s %d；%s" % [tree["name"], face, item, _count(item), main.get_notice()])
	return held / float(maxi(1, _count(item) - start))


# ---------------------------------------------------------------- 流程

func _run() -> void:
	await _frames(2)
	if not await _until(_standing, 30.0):
		_finish(false, "出生 30 s 还没站稳")
		return
	# 开局：36 格，快捷栏空着，开局的东西在主区
	for i in 9:
		if main.get_pack_slot(i)["count"] != 0:
			_finish(false, "开局快捷栏该空着（第 %d 格 %s）" % [i + 1, main.get_pack_slot(i)])
			return
	if main.get_pack_slot(9)["item"] != "millet" or _count("stone") != 12 or main.get_held_item() != "" or main.get_hold_size() != 12:
		_finish(false, "开局：主区第一格该是粟、石料 12、手上空着、船舱 12 格（%s，手持「%s」，船舱 %d）" % [_pack_text(), main.get_held_item(), main.get_hold_size()])
		return
	var spawn: Vector3 = main.get_spawn_point()

	# 瞬移到最近的一片林子边上：离出生最近的一棵阔叶树往出生那边退 6 m
	stage = "去林子"
	var first: Dictionary = world.find_scatter_near(spawn.x, spawn.z, 3000.0, "broadleaf")
	if first.is_empty():
		_finish(false, "主码头 3 km 内该有树")
		return
	var root: Vector3 = first["position"]
	pile = root + Vector3(spawn.x - root.x, 0, spawn.z - root.z).normalized() * 6.0
	_log("最近的%s %s，离出生 %.0f m；落脚 %s" % [first["name"], root, Vector2(root.x - spawn.x, root.z - spawn.z).length(), pile])
	if not main.teleport_to(pile):
		_finish(false, "瞬移不到林子边上")
		return
	if not await _until(_standing, 30.0):
		_finish(false, "林子边上站不稳")
		return

	# 空手伐 6 份木料：4 秒一份
	stage = "伐木"
	var per := await _chop_until("timber", 6)
	if per < 0.0:
		return
	if per < 3.5:
		_finish(false, "空手伐木该 4 秒一份，实为 %.1f 秒" % per)
		return
	_log("空手伐了 6 份木料（倒了 %d 棵），%.1f 秒一份" % [felled, per])
	await _shot("1_gather")

	# 背包：合木板、工作台，鼠标搬东西
	stage = "随身合成"
	if not await _open_pack():
		return
	if main.get_pack_other_title() != "":
		_finish(false, "不在船上开背包不该有另一边（%s）" % main.get_pack_other_title())
		return
	if not await _craft("plank", 5):
		return
	if _count("plank") != 20 or _count("timber") != 1:
		_finish(false, "5 份木料劈板：该是木板 20、木料 1（%s；%s）" % [_pack_text(), main.get_notice()])
		return
	if not await _craft("chest"):
		return
	if _count("chest") != 0 or main.get_notice().find("工作台") < 0:
		_finish(false, "身边没有工作台，木箱该合不了、说要工作台（%s；%s）" % [_pack_text(), main.get_notice()])
		return
	if not await _craft("workbench"):
		return
	if _count("workbench") != 1 or _count("plank") != 16:
		_finish(false, "合工作台：工作台 1、木板 16（%s）" % _pack_text())
		return
	await _shot("2_pack")
	# 左键拿起工作台、放到快捷栏第 1 格
	var bench_slot := _slot_of("workbench")
	if bench_slot < 9:
		_finish(false, "新合的工作台该进主区（第 %d 格）" % bench_slot)
		return
	await _click_slot(PACK, bench_slot)
	if main.get_pack_cursor_stack()["item"] != "workbench":
		_finish(false, "左键该拿起工作台（拖着 %s）" % main.get_pack_cursor_stack())
		return
	await _click_slot(PACK, 0)
	if main.get_pack_slot(0)["item"] != "workbench" or main.get_pack_cursor_stack()["count"] != 0:
		_finish(false, "左键该把工作台放进快捷栏第 1 格（%s，拖着 %s）" % [main.get_pack_slot(0), main.get_pack_cursor_stack()])
		return
	# 右键拿一半木板、放进一个空格
	var plank_slot := _slot_of("plank")
	await _click_slot(PACK, plank_slot, MOUSE_BUTTON_RIGHT)
	var empty_slot := -1
	for i in range(9, 36):
		if main.get_pack_slot(i)["count"] == 0:
			empty_slot = i
			break
	await _click_slot(PACK, empty_slot)
	if main.get_pack_slot(plank_slot)["count"] != 8 or main.get_pack_slot(empty_slot)["count"] != 8:
		_finish(false, "右键拿一半：两叠木板该各 8（%s / %s）" % [main.get_pack_slot(plank_slot), main.get_pack_slot(empty_slot)])
		return
	# Shift+左键：主区那叠木板搬上快捷栏
	await _click_slot(PACK, empty_slot, MOUSE_BUTTON_LEFT, true)
	if main.get_pack_slot(1)["item"] != "plank" or main.get_pack_slot(empty_slot)["count"] != 0:
		_finish(false, "Shift+左键该把木板搬上快捷栏第 2 格（%s）" % main.get_pack_slot(1))
		return
	if not await _close_pack():
		return

	# 按 1 拿起工作台，摆下
	stage = "摆工作台"
	await _walk_to(pile, 1.0, 20.0)
	await _tap("hotbar_1")
	if main.get_held_item() != "workbench":
		_finish(false, "按 1 手上该是工作台（%s）" % main.get_held_item())
		return
	var bench := await _place_held(face)
	if bench < 0:
		return
	var bench_node: Node3D = main.get_prop_node(bench)
	if bench_node == null or not (bench_node is StaticBody3D) or main.get_ghost()["on"]:
		_finish(false, "工作台该是带碰撞的实物、手上空了虚影收起（%s，虚影 %s）" % [bench_node, main.get_ghost()])
		return
	await _frames(3)
	if (main.get_stations() & 2) == 0:
		_finish(false, "身边该有工作台（设施位 %d）" % main.get_stations())
		return
	await _shot("3_workbench")

	# 看着工作台按 E：合木箱、石斧、木锹、窑
	stage = "工作台合成"
	if not await _face_prop(bench):
		return
	await _tap("interact")
	if not main.is_pack_open():
		_finish(false, "看着工作台按 E 该打开背包（%s）" % main.get_notice())
		return
	for recipe in ["chest", "stone_axe", "wood_shovel", "kiln"]:
		if not await _craft(recipe):
			return
	if _count("chest") != 1 or _count("stone_axe") != 1 or _count("wood_shovel") != 1 or _count("kiln") != 1 or _count("stone") != 2 or _count("clay") != 4:
		_finish(false, "工作台旁该合出木箱、石斧、木锹、窑（%s；%s）" % [_pack_text(), main.get_notice()])
		return
	await _shot("4_bench_recipes")
	# 鼠标悬停在木箱上按 3：放上快捷栏第 3 格
	await _mouse_at(main.get_pack_slot_center(PACK, _slot_of("chest")))
	await _tap("hotbar_3")
	if main.get_pack_slot(2)["item"] != "chest":
		_finish(false, "悬停在木箱上按 3，木箱该到快捷栏第 3 格（%s）" % main.get_pack_slot(2))
		return
	if not await _close_pack():
		return
	await _tap("hotbar_3")
	var chest := await _place_held(face)
	if chest < 0:
		return

	# 木箱：搬进搬出
	stage = "木箱"
	if not await _face_prop(chest):
		return
	await _tap("interact")
	if not main.is_pack_open() or main.get_pack_other_title().find("木箱") < 0:
		_finish(false, "看着木箱按 E：背包上面该是木箱（%s）" % main.get_pack_other_title())
		return
	await _click_slot(PACK, _slot_of("millet"), MOUSE_BUTTON_LEFT, true)
	if main.get_other_count("millet") != 5 or _count("millet") != 0:
		_finish(false, "Shift+左键该把 5 份粟搬进木箱（木箱 %d，背包 %d）" % [main.get_other_count("millet"), _count("millet")])
		return
	await _shot("5_chest")
	# 键盘：光标在主区第一格；W 挪到木箱最后一行第一格、再 W 到第一行第一格（粟），E 拿起，S S 回主区第一格，E 放下
	await _mouse_at(Vector2(2, 2))  # 鼠标挪开，键盘光标说了算
	await _tap("move_back")
	await _tap("move_forward")
	var focus: Vector2i = main.get_pack_focus()
	if focus != Vector2i(PACK, 9):
		_finish(false, "键盘光标该回到主区第一格（%s）" % focus)
		return
	await _tap("move_forward")
	await _tap("move_forward")
	if main.get_pack_focus() != Vector2i(OTHER, 0):
		_finish(false, "W W 该挪到木箱第一格（%s）" % main.get_pack_focus())
		return
	await _tap("interact")
	await _tap("move_back")
	await _tap("move_back")
	await _tap("interact")
	if _count("millet") != 5 or main.get_other_count("millet") != 0 or main.get_pack_slot(9)["item"] != "millet":
		_finish(false, "键盘 E 拿起、E 放下：粟该回到主区第一格（%s，木箱 %d）" % [main.get_pack_slot(9), main.get_other_count("millet")])
		return
	await _click_slot(PACK, 9, MOUSE_BUTTON_LEFT, true)
	if not await _close_pack():
		return
	await _tap("remove")
	if _prop(chest).is_empty() or main.get_notice().find("还有东西") < 0:
		_finish(false, "木箱里有东西，X 该收不起来（%s）" % main.get_notice())
		return
	await _tap("interact")
	if main.get_other_count("millet") != 5:
		_finish(false, "再开木箱：粟该还在里面（%d）" % main.get_other_count("millet"))
		return
	await _click_slot(OTHER, 0, MOUSE_BUTTON_LEFT, true)  # Shift+左键搬进去时占的是木箱第一格
	if _count("millet") != 5:
		_finish(false, "Shift+左键该把粟从木箱搬回背包（背包 %d）" % _count("millet"))
		return
	if not await _close_pack():
		return
	await _tap("remove")
	if not _prop(chest).is_empty() or _count("chest") != 1:
		_finish(false, "搬空了 X 该把木箱收回背包（%s）" % main.get_notice())
		return
	_log("木箱：搬进、键盘搬回、有东西收不起、搬空收回")

	# 石斧：采木料快一倍
	stage = "石斧"
	if not await _open_pack():
		return
	if not await _to_hotbar("stone_axe", 3):
		return
	if not await _close_pack():
		return
	await _tap("hotbar_4")
	if main.get_held_item() != "stone_axe":
		_finish(false, "按 4 手上该是石斧（%s）" % main.get_held_item())
		return
	var fast := await _chop_until("timber", _count("timber") + 2)
	if fast < 0.0:
		return
	if fast > 2.6:
		_finish(false, "拿着石斧伐木该 2 秒一份，实为 %.1f 秒" % fast)
		return
	_log("拿着石斧 %.1f 秒一份（空手 %.1f）" % [fast, per])

	# 窑：摆下、烧陶罐；随身合火把、摆一支
	stage = "窑与火把"
	await _walk_to(pile, 1.0, 20.0)
	if not await _open_pack():
		return
	if not await _to_hotbar("kiln", 4):
		return
	if not await _craft("torch"):
		return
	if not await _to_hotbar("torch", 5):
		return
	if not await _close_pack():
		return
	await _tap("hotbar_5")
	var kiln := await _place_held(face)
	if kiln < 0:
		return
	if not await _face_prop(kiln):
		return
	await _tap("interact")
	if not main.is_pack_open() or (main.get_stations() & 4) == 0:
		_finish(false, "看着窑按 E 该开背包、身边有窑（设施位 %d）" % main.get_stations())
		return
	if not await _craft("pottery"):
		return
	if _count("pottery") != 1:
		_finish(false, "窑旁该烧出陶罐（%s；%s）" % [_pack_text(), main.get_notice()])
		return
	if not await _close_pack():
		return
	await _tap("hotbar_6")
	var torch := await _place_held(face)
	if torch < 0:
		return
	var torch_node: Node3D = main.get_prop_node(torch)
	if torch_node == null or torch_node.find_children("*", "OmniLight3D", true, false).is_empty():
		_finish(false, "火把该带一盏光")
		return
	if not shots_dir.is_empty():
		# 退开几步、回头看摆出来的这几件
		var mid := Vector3.ZERO
		for p in _props():
			mid += Vector3(p["x"], p["y"], p["z"])
		mid /= float(_props().size())
		var back := (player.global_position - mid)
		back.y = 0.0
		await _walk_to(mid + back.normalized() * 5.5, 0.6, 10.0)
		_look_at_point(mid)
		await _frames(10)
	await _shot("6_props")
	_log("摆了 %d 件：%s" % [_props().size(), ", ".join(_props().map(func(p): return p["item"]))])

	# 回主码头上船：船舱
	stage = "船舱"
	if not main.teleport_to(spawn):
		_finish(false, "瞬移不回主码头")
		return
	if not await _until(_standing, 30.0):
		_finish(false, "主码头站不稳")
		return
	var dock: Transform3D = main.get_origin_berth()
	if not await _walk_to(dock.origin + dock.basis.x * 0.8, 0.5) or not await _walk_to(ship.global_position, 0.6):
		_finish(false, "走不到船上")
		return
	if not main.is_aboard():
		_finish(false, "站在舷门格该算在船上")
		return
	if not await _open_pack():
		return
	if main.get_pack_other_title() != "船舱（12 格）":
		_finish(false, "在船上开背包：上面该是船舱 12 格（%s）" % main.get_pack_other_title())
		return
	await _click_slot(PACK, _slot_of("pottery"), MOUSE_BUTTON_LEFT, true)
	if main.get_hold_count("pottery") != 1 or _count("pottery") != 0:
		_finish(false, "Shift+左键该把陶罐搬进船舱（船舱 %d，背包 %d）" % [main.get_hold_count("pottery"), _count("pottery")])
		return
	await _shot("7_hold")
	if not await _close_pack():
		return
	if not await _walk_to(dock.origin + dock.basis.x * 3.0, 0.6):  # 接触线的 X 指向码头那边
		_finish(false, "下不了船")
		return
	if main.is_aboard():
		_finish(false, "下了船还算在船上")
		return
	if not await _open_pack():
		return
	if main.get_pack_other_title() != "":
		_finish(false, "下了船开背包不该有船舱（%s）" % main.get_pack_other_title())
		return
	if not await _close_pack():
		return
	_finish(true, "采木料 → 随身合板、工作台 → 摆工作台、合木箱石斧木锹窑 → 木箱搬进搬出、收回 → 石斧快一倍 → 窑烧陶罐、火把 → 陶罐搬进船舱（背包：%s；船舱陶罐 %d）" % [_pack_text(), main.get_hold_count("pottery")])


func _finish(ok: bool, why: String) -> void:
	if done:
		return
	done = true
	for action in MOVES + ["interact", "inventory"]:
		Input.action_release(action)
	print("craft: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，手持「%s」，背包 %s，提示「%s」，通知「%s」）" % [
		"PASS" if ok else "FAIL", why, stage, elapsed, player.global_position, main.get_held_item(), _pack_text(), main.get_prompt(), main.get_notice()])
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
