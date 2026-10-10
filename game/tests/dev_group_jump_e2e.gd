extends SceneTree
## F2 岛群编号跳转的定向无头验收。
## Godot --headless --path game --fixed-fps 60 --script res://tests/dev_group_jump_e2e.gd
## 直接实例化剧情场景，并在进树前隔离写目录，不调用会准备正式剧情存档的换图入口。
## 退出码：0 通过，1 失败；总超时及单次跳转超时都按真实时间计算。

const STORY := preload("res://world_seed42.tscn")
const TWO_ISLANDS := preload("res://main.tscn")
const MENU_PATH := "DevToolsHud/MapMenu/Rows/"
const TOTAL_TIMEOUT_MS := 600000
const JUMP_TIMEOUT_MS := 180000
const LAND_TIMEOUT_MS := 30000

var dev: DevTools
var main: Expedition
var player: Player
var stream: GroupStream
var group_node: LineEdit
var jump_button: Button
var hint: Label
var started_ms := 0
var failed := false
var phase := "初始化"
var test_world_dir := ""


func _initialize() -> void:
	started_ms = Time.get_ticks_msec()
	call_deferred("_run")


func _process(_delta: float) -> bool:
	if not failed and Time.get_ticks_msec() - started_ms > TOTAL_TIMEOUT_MS:
		_check(false, "真实时间总超时")
	return false


func _check(ok: bool, why: String) -> bool:
	if ok and not failed:
		return true
	if not failed:
		failed = true
		push_error("dev_group_jump_e2e: FAIL 阶段=%s，%s，真实 %.1f s" % [phase, why, (Time.get_ticks_msec() - started_ms) / 1000.0])
		quit(1)
	return false


func _frames(count: int = 3) -> void:
	for i in range(count):
		await process_frame


func _key(code: int, character: int = 0, shift: bool = false) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.keycode = code
		event.physical_keycode = code
		event.unicode = character
		event.shift_pressed = shift
		event.pressed = pressed
		Input.parse_input_event(event)


func _open_menu() -> bool:
	_key(KEY_F2)
	await _frames()
	return _check(dev.is_menu_open() and player.is_input_blocked(), "F2 没有打开菜单并阻挡玩家输入")


func _find_controls() -> bool:
	group_node = dev.get_node_or_null(MENU_PATH + "GroupJumpRow/GroupNode") as LineEdit
	jump_button = dev.get_node_or_null(MENU_PATH + "GroupJumpRow/GroupJumpButton") as Button
	hint = dev.get_node_or_null(MENU_PATH + "GroupJumpHint") as Label
	return _check(group_node != null and jump_button != null and hint != null, "缺少岛群编号输入框、跳转按钮或状态行")


func _standing() -> bool:
	if not is_instance_valid(stream) or not is_instance_valid(player):
		return false
	var world: GroupWorld = stream.get_group_world(stream.get_anchor_node())
	if world == null:
		return false
	var position: Vector3 = player.global_position
	var ground: float = world.get_surface_height(position.x, position.z)
	return player.is_on_floor() and world.is_ready_at(position) and not is_nan(ground) and absf(position.y - ground) < 2.0


func _wait_for_floor() -> bool:
	var deadline := Time.get_ticks_msec() + LAND_TIMEOUT_MS
	while not _standing() and not failed and Time.get_ticks_msec() < deadline:
		await physics_frame
	return _check(_standing(), "30 秒内没有在实际地面碰撞上站稳")


func _type_number(value: String) -> bool:
	group_node.clear()
	group_node.grab_focus()
	await _frames(1)
	for digit in value:
		if digit == "#":
			_key(KEY_3, digit.unicode_at(0), true)
		else:
			_key(KEY_0 + digit.to_int(), digit.unicode_at(0))
		await _frames(1)
	return _check(group_node.text == value and dev.is_menu_open() and current_scene == main, "输入 %s 触发了旧数字换图快捷键或没有写入输入框（实际 %s）" % [value, group_node.text])


func _click_jump_button() -> void:
	var center: Vector2 = jump_button.get_global_rect().get_center()
	var motion := InputEventMouseMotion.new()
	motion.position = center
	motion.global_position = center
	Input.parse_input_event(motion)
	for pressed in [true, false]:
		var event := InputEventMouseButton.new()
		event.position = center
		event.global_position = center
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = pressed
		Input.parse_input_event(event)


func _jump_by_control(node: int, use_button: bool = false) -> bool:
	phase = "F2 编号跳转 #%d（%s）" % [node, "按钮" if use_button else "Enter"]
	if not await _open_menu():
		return false
	if not _check(group_node.editable and not jump_button.disabled, "有行星数据时跳转控件被禁用"):
		return false
	if not await _type_number("#" + str(node) if use_button else str(node)):
		return false
	if use_button:
		_click_jump_button()
	else:
		_key(KEY_ENTER)
	await _frames()
	if not _check(not dev.is_menu_open(), "提交编号没有关闭 F2 菜单"):
		return false
	var deadline := Time.get_ticks_msec() + JUMP_TIMEOUT_MS
	while int(main.get_jump_state().get("stage", -1)) != 0 and not failed and Time.get_ticks_msec() < deadline:
		await process_frame
	var jump: Dictionary = main.get_jump_state()
	var last: Dictionary = jump.get("last", {})
	if not _check(int(jump.get("stage", -1)) == 0 and bool(last.get("ok", false)) and int(last.get("node", -1)) == node, "跳转超时或失败：%s" % jump):
		return false
	if not _check(stream.get_anchor_node() == node and current_scene == main, "跳转没有更新正确锚点，或误换了场景"):
		return false
	if not await _wait_for_floor():
		return false
	if not _check(not main.is_map_open() and not player.is_input_blocked() and player.get_camera().current, "跳转完成没有关闭地图或恢复玩家输入和相机"):
		return false
	if not _check(main.get_status_text().begins_with("群 #%d" % node), "落地 HUD 没有报告目标群号"):
		return false
	print("dev_group_jump_e2e: 群 #%d 落地，位置=%s，耗时=%.1f ms，输入恢复" % [node, player.global_position, float(last.get("total_ms", -1))])
	return true


func _reject_invalid_text(value: String, reason: String) -> bool:
	phase = "无效编号 %s" % value
	var anchor: int = stream.get_anchor_node()
	var last: Dictionary = main.get_jump_state().get("last", {}).duplicate(true)
	group_node.text = value
	group_node.grab_focus()
	_key(KEY_ENTER)
	await _frames()
	if not _check(dev.is_menu_open() and current_scene == main and stream.get_anchor_node() == anchor and int(main.get_jump_state().get("stage", -1)) == 0 and main.get_jump_state().get("last", {}) == last, "无效编号触发了跳转或换图"):
		return false
	return _check(hint.text.contains(reason), "无效编号没有解释拒绝原因（提示：%s）" % hint.text)


func _close_focused_menu(key_code: int) -> bool:
	phase = "输入框焦点下 %s 关闭菜单" % ("Esc" if key_code == KEY_ESCAPE else "F2")
	group_node.grab_focus()
	await _frames(1)
	if not _check(group_node.has_focus(), "编号输入框没有取得焦点"):
		return false
	_key(key_code)
	await _frames()
	return _check(not dev.is_menu_open() and not player.is_input_blocked(), "焦点下关闭菜单没有恢复玩家输入")


func _run() -> void:
	dev = root.get_node_or_null("DeveloperTools") as DevTools
	if not _check(dev != null and dev.has_method("jump_to_group"), "DeveloperTools 没有编号跳转接口（需要先编译目标）"):
		return
	root.size = Vector2i(1280, 720)
	main = TWO_ISLANDS.instantiate()
	root.add_child(main)
	current_scene = main
	player = main.get_node("Player")
	await _frames()
	dev.set_god(false)
	phase = "无行星数据禁用"
	if not await _open_menu() or not _find_controls():
		return
	if not _check(not group_node.editable and jump_button.disabled and not hint.text.is_empty(), "无行星数据时没有禁用编号跳转并说明原因"):
		return
	if not _check(not dev.jump_to_group(6068) and current_scene == main, "无行星数据时仍接受了跳转"):
		return
	dev.set_menu_open_for_tests(false)
	main.queue_free()
	await _frames()

	phase = "隔离故事世界初始化"
	test_world_dir = "user://worlds/e2e_dev_group_jump_%d" % Time.get_ticks_usec()
	main = STORY.instantiate()
	var provider: GroupProvider = main.get_node("GroupProvider")
	provider.world_dir = test_world_dir
	main.get_node("PlanetView").extra_lods_dir = test_world_dir.path_join("lods")
	main.get_node("GroupStream").settlements_dir = test_world_dir.path_join("settlements")
	main.get_node("SiteStream").layout_dir = test_world_dir.path_join("settlements/%d" % StoryStart.get_spawn_node())
	root.add_child(main)
	current_scene = main
	player = main.get_node("Player")
	stream = main.get_node("GroupStream")
	root.get_node("GameClock").set_paused(true)
	await _frames()
	if not _check(provider.get_world_dir() == test_world_dir and provider.can_generate(), "测试世界目录或按需生成配置不可用：%s" % provider.get_problem()):
		return
	if not await _wait_for_floor() or not _find_controls():
		return

	if not _check(stream.get_anchor_node() == 6068, "真实开局不在群 #6068"):
		return
	phase = "M 岛群地图叠加 F2"
	_key(KEY_M)
	await _frames()
	if not _check(main.is_map_open() and player.is_input_blocked(), "M 没有打开岛群地图并阻挡玩家输入"):
		return
	if not await _jump_by_control(3980) or not await _jump_by_control(6068, true):
		return
	phase = "恢复行走输入"
	var position: Vector3 = player.global_position
	Input.action_press("move_forward")
	for i in range(20):
		await physics_frame
	Input.action_release("move_forward")
	if not _check(Vector2(player.global_position.x - position.x, player.global_position.z - position.z).length() > 0.25, "跳转落地后前进输入未生效"):
		return
	if not await _open_menu():
		return
	if not await _type_number("123"):
		return
	for entry in [["", "请输入"], ["-1", "非负整数"], ["8000", "有效编号"], ["6068x", "非负整数"], ["1.5", "非负整数"], ["6068 3980", "非负整数"], ["999999999999999999999999999999999999999999999999999999999999999999", "有效编号"]]:
		if not await _reject_invalid_text(entry[0], entry[1]):
			return
	var anchor: int = stream.get_anchor_node()
	if not _check(not dev.jump_to_group(-1) and not dev.jump_to_group(8000) and stream.get_anchor_node() == anchor and int(main.get_jump_state().get("stage", -1)) == 0, "后端接受了越界编号"):
		return
	if not await _close_focused_menu(KEY_ESCAPE):
		return
	if not await _open_menu() or not await _close_focused_menu(KEY_F2):
		return
	phase = "开发者工具关闭时拒绝"
	main.set_dev_tools(false)
	if not _check(not dev.jump_to_group(3980) and stream.get_anchor_node() == anchor and int(main.get_jump_state().get("stage", -1)) == 0, "dev_tools=false 时后端仍接受跳转"):
		return
	_key(KEY_F2)
	await _frames()
	if not _check(not dev.is_menu_open() and not player.is_input_blocked(), "dev_tools=false 时 F2 仍打开菜单或阻挡输入"):
		return
	print("dev_group_jump_e2e: PASS 6068→3980（M+F2+Enter）→6068（# 编号+按钮），真实地面、锚点、行走恢复、输入数字、无效及超长编号、焦点下 Esc/F2、无行星数据、dev_tools=false；测试目录=%s；真实 %.1f s" % [test_world_dir, (Time.get_ticks_msec() - started_ms) / 1000.0])
	quit(0)
