extends Node
## 船舱端到端测试（拓展：能进的船舱能进去、船上能摆工作台等设备、舱里算屋里）。加载主场景，只通过输入（动作与鼠标事件）操作。
## 开局把船换成带舱的布局（舷门排三节：空节 / 驾驶区 / 空节，舷门后一格空节上搭一节储存区——那节空节成了一间舱），摆进船坞；下着雨。
## 走上跳板 → 在甲板上：不算舱里、淋着雨 → 从驾驶区穿门洞走进舱：算舱里、屋里（下雨淋不着）、相机拉近
## → 舱里摆一只木箱（靠后墙）、一张工作台 → 在船上的工作台旁合一把木锹
## → 出舱到驾驶区：拿着工作台，虚影红、说驾驶区上摆不了 → 走到舷门排另一头的空节上再摆一张工作台
## → 下船到画样台改船：选中摆着工作台的那节空节，拆除钮是灰的、说工作台放不下；拖一节空节到舱后面，那儿不亮、红色虚影说挡着舱门
## → 回船掌舵升空、往前飞：船上的三件跟着船走（在船的局部坐标里不动）。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/cabin_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，会在几个节点存截图。

const MAIN := preload("res://main.tscn")
const TIMEOUT := 300.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const CABIN := ["0,0,-1:sail", "-1,0,0:deck", "0,0,0:bridge", "1,0,0:deck", "0,0,1:deck", "0,1,1:storage"]
const PACK := 0
const RECIPES := 2

var main: Expedition
var player: Player
var ship: Ship
var yard: Shipyard
var atmosphere: Atmosphere

var elapsed := 0.0
var stage := "开局"
var shots_dir := ""
var mouse := Vector2.ZERO
var side := 1.0  # 跳板接在哪一舷（船的局部 x 的符号）
var done := false


func _ready() -> void:
	get_tree().root.size = Vector2i(1280, 720)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	main = MAIN.instantiate()
	main.starting_pack = {"workbench": 2, "chest": 1, "plank": 3}
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	yard = main.get_node("IslandA/Shipyard")
	atmosphere = main.get_node("Atmosphere")
	_run()


func _log(line: String) -> void:
	print("cabin: " + line)


func _physics_process(delta: float) -> void:
	elapsed += delta
	if not done and elapsed > TIMEOUT:
		_finish(false, "超时")


func _run() -> void:
	await _frames(5)
	ship.set_layout(PackedStringArray(CABIN))
	if Array(ship.get_layout()) != CABIN:
		_finish(false, "换不上带舱的布局（%s）" % [ship.get_layout()])
		return
	ship.place_at(yard.get_cradle_contact(), true)  # 飞进坞里那一段 shipyard_e2e 测过
	atmosphere.set_weather_override("rain", 0.0)
	await _frames(5)
	if not main.is_ship_in_yard():
		_finish(false, "船没停进坞里")
		return
	side = signf(ship.to_local(yard.get_gangway_top()).x)
	# 开局的东西进的是背包主区：挪到快捷栏 1–3 格
	if not await _stock_hotbar(["workbench", "chest", "plank"]):
		return
	_log("船换成带舱的布局 %s、停进坞里；跳板在%s舷；下雨" % [ship.get_layout(), "右" if side > 0.0 else "左"])

	# 1. 走上船：跳板 → 舷门甲板（露天，淋雨）
	stage = "上船"
	var bench := yard.get_bench_position()
	if not await _walk_path([bench + yard.get_cradle_contact().basis.x * 6.0, bench, yard.get_gangway_foot(), yard.get_gangway_top(), _at(side * 7.0, 0.0, 0.0)], 0.6):
		return
	if not await _until(func() -> bool: return float(atmosphere.get_look()["indoor"]) < 0.1, 5.0):
		_finish(false, "露天甲板上该淋着雨（indoor %.2f）" % atmosphere.get_look()["indoor"])
		return
	if main.is_in_cabin() or player.is_indoors():
		_finish(false, "甲板上不该算舱里 / 屋里")
		return
	_log("上了船：舷门甲板露天（indoor %.2f）" % atmosphere.get_look()["indoor"])

	# 2. 从驾驶区穿门洞进舱（舵柱在中线上：先贴着一舷绕过去）
	stage = "进舱"
	if not await _walk_path([_at(side * 1.2, 0.0, 2.8), _at(0.0, 0.0, 2.6)], 0.4):
		return
	_face(_at(0.0, 0.0, 7.0))
	await _shot("1_door")
	if not await _walk_path([_at(0.0, 0.0, 4.6), _at(0.0, 0.0, 7.0)], 0.5):
		return
	if not main.is_in_cabin():
		_finish(false, "走进了舱，却不算在舱里（船的局部坐标 %s）" % ship.to_local(player.global_position))
		return
	var arm: SpringArm3D = player.get_node("CameraPivot/SpringArm3D")
	if not await _until(func() -> bool: return player.is_indoors() and arm.spring_length < player.get_indoor_arm() + 0.4 and float(atmosphere.get_look()["indoor"]) > 0.9, 5.0):
		_finish(false, "舱里该算屋里：相机拉近、淋不着雨（屋里 %s，臂长 %.2f，indoor %.2f）" % [player.is_indoors(), arm.spring_length, atmosphere.get_look()["indoor"]])
		return
	if (arm.collision_mask & 2) == 0:
		_finish(false, "相机的弹簧臂该撞船身（第 2 层），不然会穿进舱墙（mask %d）" % arm.collision_mask)
		return
	if not main.get_status_text().contains("在船舱里"):
		_finish(false, "左上该说在船舱里（%s）" % main.get_status_text())
		return
	_log("进了舱：算舱里、屋里，臂长 %.2f，下雨淋不着（indoor %.2f）" % [arm.spring_length, atmosphere.get_look()["indoor"]])
	await _until(func() -> bool: return float(atmosphere.get_look()["indoor"]) > 0.99, 3.0)
	await _shot("2_inside")

	# 3. 舱里摆木箱（靠后墙）和工作台
	stage = "舱里摆东西"
	await _tap("hotbar_2")
	if not await _walk_path([_at(0.0, 0.0, 8.2)], 0.25):
		return
	var chest := await _place(_at(0.0, 0.0, 12.0), "chest")
	if chest.is_empty():
		return
	await _tap("hotbar_1")
	if not await _walk_path([_at(0.8, 0.0, 7.0)], 0.25):
		return
	var cabin_bench := await _place(_at(-5.0, 0.0, 7.0), "workbench")
	if cabin_bench.is_empty():
		return
	if (main.get_stations() & 2) == 0:
		_finish(false, "舱里工作台旁边该算有工作台（设施位 %d）" % main.get_stations())
		return
	await _shot("3_cabin_props")

	# 4. 在船上的工作台旁合一把木锹（手上换成木板：拿着工作台时 E 会先去摆）
	stage = "船上合成"
	await _tap("hotbar_3")
	_face(ship.to_global(Vector3(cabin_bench["x"], 0.0, cabin_bench["z"])))
	await _frames(3)
	if main.get_facing_prop() != int(cabin_bench["id"]):
		_finish(false, "该看着舱里的工作台（看着 %d，工作台 %d）" % [main.get_facing_prop(), cabin_bench["id"]])
		return
	await _tap("interact")
	if not main.is_pack_open():
		_finish(false, "看着工作台按 E 该打开背包（%s）" % main.get_notice())
		return
	var index: int = main.get_recipe_index("wood_shovel")
	await _click(main.get_pack_slot_center(RECIPES, index))
	if main.get_item_count("wood_shovel") != 1 or main.get_item_count("plank") != 0:
		_finish(false, "在船上的工作台旁该合出一把木锹（木锹 %d，木板 %d；%s）" % [main.get_item_count("wood_shovel"), main.get_item_count("plank"), main.get_notice()])
		return
	await _shot("4_crafted")
	await _tap("inventory")
	if main.is_pack_open():
		_finish(false, "Tab 该合上背包")
		return
	_log("在船上的工作台旁合了一把木锹")

	# 5. 出舱到驾驶区：拿着工作台，虚影红、说驾驶区上摆不了
	stage = "驾驶区上摆"
	await _tap("hotbar_1")
	if not await _walk_path([_at(0.0, 0.0, 4.6), _at(0.0, 0.0, 2.6), _at(side * 1.2, 0.0, 2.8), _at(side * 1.2, 0.0, -1.0)], 0.4):
		return
	_face(_at(side * 1.2, 0.0, -10.0))
	await _frames(4)
	var ghost: Dictionary = main.get_ghost()
	if not ghost["on"] or ghost["ok"] or not String(ghost["why"]).contains("驾驶区上摆不了东西"):
		_finish(false, "驾驶区上该摆不了（虚影 %s）" % ghost)
		return
	if not main.get_prompt().contains("驾驶区上摆不了东西"):
		_finish(false, "底下的提示该说驾驶区上摆不了（%s）" % main.get_prompt())
		return
	_log("驾驶区上：%s" % ghost["why"])
	await _shot("5_bridge_refused")

	# 6. 舷门排另一头的空节上再摆一张工作台（露天甲板）
	stage = "甲板上摆"
	if not await _walk_path([_at(-side * 6.0, 0.0, 1.6)], 0.25):
		return
	var deck_bench := await _place(_at(-side * 6.0, 0.0, -10.0), "workbench")
	if deck_bench.is_empty():
		return
	var props: Array = main.get_placed_props()
	if props.size() != 3:
		_finish(false, "船上该摆着三件（%s）" % [props])
		return
	await _shot("6_deck_bench")

	# 7. 下船到画样台改船：摆件压着的改动不让改
	stage = "改船"
	if not await _walk_path([_at(side * 7.0, 0.0, 0.0), yard.get_gangway_top(), yard.get_gangway_foot(), bench], 0.6):
		return
	if main.get_placed_props().size() != 0:
		_finish(false, "下了船，脚下那处地面的账里不该有船上的东西（%s）" % [main.get_placed_props()])
		return
	await _tap("interact")
	if not main.is_refit_open():
		_finish(false, "画样台按 E 该打开改装（%s）" % main.get_notice())
		return
	await _frames(3)
	var deck_id := "%d,0,0:deck" % int(-side)
	if not await _select(Vector3i(int(-side), 0, 0), deck_id):
		return
	await _click(main.get_refit_remove_center())
	if Array(ship.get_layout()) != CABIN or not main.get_notice().contains("工作台放不下了"):
		_finish(false, "拆摆着工作台的那节空节该被拒、说工作台放不下（%s；%s）" % [ship.get_layout(), main.get_notice()])
		return
	_log("拆 %s：%s" % [deck_id, main.get_notice()])
	await _shot("7_refit_remove_blocked")
	if not await _drag_refused("deck", Vector3i(0, 0, 2), "挡着舱门了"):
		return
	await _tap("inventory")
	if main.is_refit_open():
		_finish(false, "Tab 该合上改装")
		return

	# 8. 回船掌舵升空、往前飞：船上的东西跟着船走
	stage = "开船"
	var locals := {}
	for p in props:
		locals[p["id"]] = Vector3(p["x"], p["y"], p["z"])
	if not await _walk_path([yard.get_gangway_foot(), yard.get_gangway_top(), _at(side * 7.0, 0.0, 0.0), _at(side * 1.6, 0.0, 0.0)], 0.6):
		return
	if not await _walk_path([ship.get_helm_position()], main.helm_range - 0.3):
		return
	await _tap("interact")
	if main.get_state() != Expedition.PILOTING:
		_finish(false, "舵旁按 E 该掌舵（%s）" % main.get_notice())
		return
	var start_y := ship.global_position.y
	_hold("ascend", true)
	if not await _until(func() -> bool: return ship.global_position.y > start_y + 8.0, 20.0):
		_finish(false, "20 s 没升起 8 m")
		return
	_hold("ascend", false)
	_hold("move_forward", true)
	await _seconds(3.0)
	_hold("move_forward", false)
	if ship.get_speed() < 3.0:
		_finish(false, "船该往前飞着（%.1f m/s）" % ship.get_speed())
		return
	for id in locals:
		var node: Node3D = ship.get_furniture_node(id)
		if node == null:
			_finish(false, "船上第 %d 件的样子没了" % id)
			return
		var drift: float = ship.to_local(node.global_position).distance_to(locals[id])
		if drift > 0.6:
			_finish(false, "船上第 %d 件没跟着船走（偏了 %.2f m）" % [id, drift])
			return
	_log("掌舵升空、往前飞：船上三件跟着船走（航速 %.1f m/s）" % ship.get_speed())
	await _shot("8_flying")
	_finish(true, "进舱算屋里（淋不着雨、相机拉近）、舱里摆木箱与工作台并在船上合成、驾驶区上摆不了、甲板上摆工作台、改船被摆件挡住、开船摆件跟着走")


## 手上那件朝 look_at 看着摆下：虚影该绿、E 摆下、账里多一件（船的局部坐标）、船上画出了它；返回那一件
func _place(look_at: Vector3, item: String) -> Dictionary:
	_face(look_at)
	await _frames(4)
	var ghost: Dictionary = main.get_ghost()
	if not ghost["on"] or not ghost["ok"]:
		_finish(false, "%s的虚影该是绿的（%s；人在船的局部 %s）" % [item, ghost, ship.to_local(player.global_position)])
		return {}
	var before: int = main.get_placed_props().size()
	await _tap("interact")
	var list: Array = main.get_placed_props()
	if list.size() != before + 1 or list[-1]["item"] != item:
		_finish(false, "按 E 该摆下%s（%s；%s）" % [item, list, main.get_notice()])
		return {}
	var prop: Dictionary = list[-1]
	var node: Node3D = main.get_prop_node(prop["id"])
	if node == null or node.get_parent() != ship.get_node("Visual"):
		_finish(false, "%s该画在船上（%s）" % [item, node])
		return {}
	_log("摆下%s：船的局部 (%.1f, %.1f, %.1f)，朝向 %d" % [item, prop["x"], prop["y"], prop["z"], prop["quarter"]])
	return prop


## 开背包，把 items 依次挪到快捷栏第 1、2、3… 格，再合上
func _stock_hotbar(items: Array) -> bool:
	await _tap("inventory")
	if not main.is_pack_open():
		_finish(false, "Tab 该打开背包")
		return false
	for k in items.size():
		var slot := -1
		for s in range(9, 64):
			var stack: Dictionary = main.get_pack_slot(s)
			if stack.is_empty():
				break
			if stack["item"] == items[k]:
				slot = s
				break
		if slot < 0:
			_finish(false, "背包主区里没有 %s" % items[k])
			return false
		await _click(main.get_pack_slot_center(PACK, slot))
		await _click(main.get_pack_slot_center(PACK, k))
		if main.get_pack_slot(k)["item"] != items[k]:
			_finish(false, "%s 该挪到快捷栏第 %d 格（%s）" % [items[k], k + 1, main.get_pack_slot(k)])
			return false
	await _tap("inventory")
	if main.is_pack_open():
		_finish(false, "Tab 该合上背包")
		return false
	return true


## 拖 kind 到锚点 anchor：那儿不该亮，虚影红、原因里有 why，松手不改
func _drag_refused(kind: String, anchor: Vector3i, why: String) -> bool:
	var card: Vector2 = main.get_refit_card_center(kind)
	await _mouse_at(card)
	await _button(card, true)
	var aim: Vector2 = main.get_refit_aim(anchor, kind)
	if aim.x < 0.0:
		_finish(false, "屏幕上找不到 %s 的落点 %s" % [kind, anchor])
		return false
	await _mouse_at(card.lerp(aim, 0.5), MOUSE_BUTTON_MASK_LEFT)
	await _mouse_at(aim, MOUSE_BUTTON_MASK_LEFT)
	var hover: Dictionary = main.get_refit_hover()
	var glow: Array = main.get_refit_glow()
	if hover["type"] != "spot" or hover["anchor"] != anchor or hover["legal"] or not String(hover["reason"]).contains(why) or glow.has(anchor):
		_finish(false, "拖%s到 %s：该不亮、红色虚影说「%s」（%s；亮格 %s）" % [kind, anchor, why, hover, glow])
		return false
	_log("拖%s到 %s：%s" % [kind, anchor, hover["reason"]])
	await _shot("7_refit_drag_blocked")
	await _button(aim, false)
	await _frames(3)
	if Array(ship.get_layout()) != CABIN:
		_finish(false, "红色虚影松手不该改船（%s）" % [ship.get_layout()])
		return false
	return true


func _select(anchor: Vector3i, want: String) -> bool:
	var aim: Vector2 = main.get_refit_aim(anchor, "")
	if aim.x < 0.0:
		_finish(false, "屏幕上找不到 %s 那个节点" % [anchor])
		return false
	await _click(aim)
	if main.get_refit_selected() != want:
		_finish(false, "点 %s 该选中 %s，实为「%s」" % [anchor, want, main.get_refit_selected()])
		return false
	return true


# ---------------------------------------------------------------- 走、按键、鼠标

## 船的局部坐标 → 全局
func _at(x: float, y: float, z: float) -> Vector3:
	return ship.to_global(Vector3(x, y, z))


func _face(target: Vector3) -> void:
	var to := target - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


## 挨个走到路点（最后一个 last_radius 以内停下）；每个路点 20 s 没到算失败
func _walk_path(points: Array, last_radius: float) -> bool:
	for k in points.size():
		var target: Vector3 = points[k]
		var radius := last_radius if k == points.size() - 1 else 0.6
		var t := 0.0
		while true:
			if done:
				return false
			var to := target - player.global_position
			var rise := to.y
			to.y = 0.0
			if to.length() <= radius and absf(rise) < 1.5:
				break
			_face(target)
			_hold("move_forward", true)
			await get_tree().physics_frame
			t += get_physics_process_delta_time()
			if t > 20.0:
				_hold("move_forward", false)
				_finish(false, "20 s 没走到路点 %d（%s；人在 %s，船的局部 %s）" % [k, target, player.global_position, ship.to_local(player.global_position)])
				return false
	_hold("move_forward", false)
	await _frames(6)  # 停稳
	return true


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


func _seconds(seconds: float) -> void:
	var t := 0.0
	while t < seconds:
		await get_tree().physics_frame
		t += get_physics_process_delta_time()


func _frames(n: int) -> void:
	for i in n:
		await get_tree().physics_frame


func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _tap(action: String) -> void:
	Input.action_press(action)
	await _frames(3)
	Input.action_release(action)
	await _frames(4)  # 松开几帧，下一次 just_pressed 才认


func _mouse_at(at: Vector2, buttons := 0) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = at
	motion.global_position = at
	motion.relative = at - mouse
	motion.button_mask = buttons
	Input.parse_input_event(motion)
	mouse = at
	await _frames(2)


func _button(at: Vector2, pressed: bool, button := MOUSE_BUTTON_LEFT) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = button
	ev.pressed = pressed
	ev.position = at
	ev.global_position = at
	Input.parse_input_event(ev)
	await _frames(2)


func _click(at: Vector2) -> void:
	await _mouse_at(at)
	await _button(at, true)
	await _button(at, false)
	await _frames(2)


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


func _finish(ok: bool, why: String) -> void:
	if done:
		return
	done = true
	for action in MOVES + ["interact", "remove", "inventory"]:
		Input.action_release(action)
	atmosphere.clear_weather_override()
	print("cabin: %s —— %s（%s，模拟 %.1f s；人在船的局部 %s，船 %s，布局 %s，提示「%s」）" % [
		"PASS" if ok else "FAIL", why, stage, elapsed, ship.to_local(player.global_position), ship.global_position, ship.get_layout(), main.get_notice()])
	get_tree().quit(0 if ok else 1)
