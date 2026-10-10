extends SceneTree
## M 地图的「当前岛群 → 行星总览 → 点选目标 → 明确跳转」定向验收。
## Godot --headless --path game --fixed-fps 60 --script res://tests/dev_group_jump_e2e.gd
## 已获准可见验收时，去掉 --headless，末尾加 -- --shots-dir=<目录>，保存四张实际渲染截图。
## 截图可加 --preview-only：验收 #3980 的已有粗图与宽/窄布局后退出，不再生成细包或跨群跳转。
## --failure-only 只验收 #4 无细包且禁止生成时的真实失败提示，不重复跨群成功流程。
## 直接实例化剧情场景，并在进树前隔离写目录，不调用会准备正式剧情存档的换图入口。
## 退出码：0 通过，1 失败；总超时及单次跳转超时都按真实时间计算。

const STORY := preload("res://world_seed42.tscn")
const OLD_GROUP_ROW := "DevToolsHud/MapMenu/Rows/GroupJumpRow"
const PLANET_TAB := "Toolbar/ToolbarRow/PlanetTab"
const CLOSE_CONTROL := "Toolbar/ToolbarRow/CloseButton"
const SEARCH_ROW := "TargetCard/Content/SearchRow/"
const JUMP_CONTROL := "TargetCard/Content/TargetBody/TargetInfo/JumpButton"
const TOTAL_TIMEOUT_MS := 600000
const JUMP_TIMEOUT_MS := 180000
const LAND_TIMEOUT_MS := 30000
const PREVIEW_TIMEOUT_MS := 10000

var dev: DevTools
var main: Expedition
var player: Player
var stream: GroupStream
var browser: MapBrowser
var planet: PlanetMap
var planet_tab: Button
var close_button: Button
var node_search: LineEdit
var locate_button: Button
var jump_button: Button
var target_card: Control
var started_ms := 0
var failed := false
var phase := "初始化"
var test_world_dir := ""
var shots_dir := ""
var preview_only := false
var failure_only := false


func _initialize() -> void:
	started_ms = Time.get_ticks_msec()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots-dir="):
			shots_dir = arg.trim_prefix("--shots-dir=")
		elif arg == "--preview-only":
			preview_only = true
		elif arg == "--failure-only":
			failure_only = true
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


func _click_at(position: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = position
	motion.global_position = position
	Input.parse_input_event(motion)
	for pressed in [true, false]:
		var event := InputEventMouseButton.new()
		event.position = position
		event.global_position = position
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = pressed
		Input.parse_input_event(event)


func _click(control: Control) -> void:
	_click_at(control.get_global_rect().get_center())


func _shot(name: String) -> bool:
	if shots_dir.is_empty() or DisplayServer.get_name() == "headless":
		return true
	# 截图只读取已完成的真实渲染帧；无头检查不创建图片，也不声称做过像素验收。
	await _frames()
	await RenderingServer.frame_post_draw
	var rendered: Image = root.get_texture().get_image()
	if rendered == null or rendered.is_empty():
		print("dev_group_jump_e2e: 跳过截图 %s，当前 viewport 没有实际渲染图像" % name)
		return true
	var error := DirAccess.make_dir_recursive_absolute(shots_dir)
	if not _check(error == OK, "无法创建截图目录：%s" % shots_dir):
		return false
	return _check(rendered.save_png(shots_dir.path_join(name + ".png")) == OK, "无法保存实际渲染截图：%s" % name)


func _find_controls() -> bool:
	browser = main.get_map_browser()
	if not _check(browser != null, "剧情 M 地图缺少 MapBrowser（需要先编译目标）"):
		return false
	planet = browser.get_planet_map()
	planet_tab = browser.get_node_or_null(PLANET_TAB) as Button
	close_button = browser.get_node_or_null(CLOSE_CONTROL) as Button
	node_search = browser.get_node_or_null(SEARCH_ROW + "NodeSearch") as LineEdit
	locate_button = browser.get_node_or_null(SEARCH_ROW + "LocateButton") as Button
	jump_button = browser.get_node_or_null(JUMP_CONTROL) as Button
	target_card = browser.get_node_or_null("TargetCard") as Control
	return _check(planet != null and planet_tab != null and close_button != null and node_search != null and locate_button != null and jump_button != null and target_card != null,
		"M 地图缺少行星图、页签、关闭、编号定位、目标卡或明确跳转按钮")


func _open_map() -> bool:
	_key(KEY_M)
	await _frames()
	if not _check(main.is_map_open() and browser.is_visible_in_tree() and player.is_input_blocked(), "M 没有打开地图并阻挡玩家输入"):
		return false
	return _check(not browser.is_planet_view(), "M 重新打开时没有先显示当前岛群")


func _open_planet_tab() -> bool:
	_click(planet_tab)
	await _frames()
	return _check(main.is_map_open() and browser.is_planet_view() and planet.is_visible_in_tree(), "点击行星页签没有进入 M 内的行星总览")


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
	node_search.clear()
	node_search.grab_focus()
	await _frames(1)
	for digit in value:
		if digit == "#":
			_key(KEY_3, digit.unicode_at(0), true)
		else:
			_key(KEY_0 + digit.to_int(), digit.unicode_at(0))
		await _frames(1)
	return _check(node_search.text == value and main.is_map_open() and current_scene == main,
		"输入 %s 没有写入 M 定位框或触发了数字换图快捷键（实际 %s）" % [value, node_search.text])


func _no_jump(anchor: int, last: Dictionary, why: String) -> bool:
	return _check(main.is_map_open() and browser.is_planet_view() and current_scene == main and stream.get_anchor_node() == anchor
		and int(main.get_jump_state().get("stage", -1)) == 0 and main.get_jump_state().get("last", {}) == last, why)


func _wait_for_preview(node: int) -> bool:
	var deadline := Time.get_ticks_msec() + PREVIEW_TIMEOUT_MS
	while not browser.has_preview() and not failed and Time.get_ticks_msec() < deadline:
		await process_frame
	return _check(target_card.is_visible_in_tree() and browser.get_target_text().contains("#%d" % node) and browser.has_preview(),
		"目标 #%d 没有显示信息与真实 LOD 预览（读数：%s）" % [node, browser.get_target_text()])


func _check_layout(node: int) -> bool:
	phase = "900×700 的地图布局"
	root.size = Vector2i(900, 700)
	await _frames(6)
	var viewport_rect := root.get_visible_rect()
	var map_rect := planet.get_global_rect()
	var local_point := planet.node_screen(node)
	var node_point := planet.get_global_position() + local_point
	if not _check(local_point.x >= 0.0 and local_point.y >= 0.0 and map_rect.has_point(node_point) and viewport_rect.has_point(node_point),
		"缩小窗口后目标球图点移出可点击地图区域"):
		return false
	for control in [planet_tab, close_button, node_search, locate_button, jump_button]:
		var control_rect: Rect2 = control.get_global_rect()
		if not _check(control.is_visible_in_tree() and control_rect.size.x > 0.0 and control_rect.size.y > 0.0 and viewport_rect.encloses(control_rect),
			"900×700 时控件 %s 不完整显示在 viewport 内：%s / %s" % [control.name, control_rect, viewport_rect]):
			return false
		if not _check(not control_rect.has_point(node_point) and not control_rect.intersects(map_rect), "控件 %s 覆盖球图点选区域" % control.name):
			return false
	return _check(not target_card.get_global_rect().has_point(node_point), "目标卡覆盖球图目标点")


func _select_by_actual_click(node: int) -> bool:
	phase = "鼠标点选行星上的 #%d" % node
	var local_point := planet.node_screen(node)
	if not _check(local_point.x >= 0.0 and local_point.y >= 0.0 and planet.pick(local_point) == node, "定位后的球图中心不能准确拾取 #%d" % node):
		return false
	var anchor: int = stream.get_anchor_node()
	var last: Dictionary = main.get_jump_state().get("last", {}).duplicate(true)
	# 定位已预选目标；先清空才能证明鼠标事件本身完成点选，避免预选状态掩盖输入失效。
	planet.select(-1)
	await _frames(1)
	if not _check(browser.get_selected_node() == -1, "清空行星选中状态没有同步目标卡"):
		return false
	_click_at(planet.get_global_position() + local_point)
	await _frames()
	if not _check(planet.get_selected() == node and browser.get_selected_node() == node, "实际鼠标点击没有选中 #%d" % node):
		return false
	return _no_jump(anchor, last, "鼠标单击目标时提前跳转；应等待明确的跳转按钮")


func _jump_selected(node: int) -> bool:
	phase = "M 目标卡跳转 #%d" % node
	if not _check(main.is_map_open() and browser.get_selected_node() == node and not jump_button.disabled and jump_button.text.contains("跳转"),
		"开发者模式的目标卡没有可用、明确的跳转按钮"):
		return false
	_click(jump_button)
	await _frames()
	if not _check(not main.is_map_open(), "点击跳转按钮没有关闭 M 地图"):
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
	if not _check(not main.is_map_open() and not player.is_input_blocked() and player.get_camera().current, "跳转完成没有恢复玩家输入和相机"):
		return false
	if not _check(main.get_status_text().begins_with("群 #%d" % node), "落地 HUD 没有报告目标群号"):
		return false
	print("dev_group_jump_e2e: 群 #%d 落地，位置=%s，耗时=%.1f ms，输入恢复" % [node, player.global_position, float(last.get("total_ms", -1))])
	return true


func _check_walking(node: int = 3980) -> bool:
	phase = "#%d 恢复行走输入" % node
	var position: Vector3 = player.global_position
	Input.action_press("move_forward")
	for i in range(20):
		await physics_frame
	Input.action_release("move_forward")
	return _check(Vector2(player.global_position.x - position.x, player.global_position.z - position.z).length() > 0.25, "跳转落地后前进输入未生效")


func _close_map_with_f2_overlay() -> bool:
	phase = "M 上叠加 F2 后点击地图关闭按钮"
	if not await _open_map():
		return false
	_key(KEY_F2)
	await _frames()
	if not _check(main.is_map_open() and dev.is_menu_open() and player.is_input_blocked(), "F2 没有叠在 M 地图上并保持输入阻挡"):
		return false
	_click(close_button)
	await _frames()
	if not _check(not main.is_map_open() and not browser.is_visible_in_tree() and not dev.is_menu_open() and not player.is_input_blocked(),
		"地图关闭按钮没有同时关闭 M 和叠层 F2、恢复玩家输入"):
		return false
	return await _check_walking(6068)


func _reject_invalid_text(value: String, reason: String) -> bool:
	phase = "M 无效定位编号 %s" % value
	var anchor: int = stream.get_anchor_node()
	var selected: int = browser.get_selected_node()
	var last: Dictionary = main.get_jump_state().get("last", {}).duplicate(true)
	node_search.text = value
	node_search.grab_focus()
	_key(KEY_ENTER)
	await _frames()
	if not _no_jump(anchor, last, "无效定位编号触发了跳转、关闭 M 或换图"):
		return false
	if not _check(browser.get_selected_node() == selected, "无效定位编号改变了之前选中的目标"):
		return false
	return _check(browser.get_target_text().contains(reason), "无效定位编号没有说明原因（提示：%s）" % browser.get_target_text())


func _close_focused_map(key_code: int) -> bool:
	phase = "定位框焦点下 %s 关闭 M 地图" % ("Esc" if key_code == KEY_ESCAPE else "M")
	node_search.grab_focus()
	await _frames(1)
	if not _check(node_search.has_focus(), "编号定位框没有取得焦点"):
		return false
	_key(key_code)
	await _frames()
	return _check(not main.is_map_open() and not browser.is_visible_in_tree() and not player.is_input_blocked(), "焦点下关闭 M 地图没有恢复玩家输入")


func _run_failure_only(provider: GroupProvider) -> bool:
	phase = "M 跳转 #4 失败时保留地图并说明真实原因"
	if not await _open_map() or not await _open_planet_tab():
		return false
	if not _check(not provider.is_available(4) and provider.find_group(4).is_empty(), "#4 已有仓库或测试细包，无法验证禁止生成的失败分支"):
		return false
	if not _check(browser.lookup_node("4"), "无法在 M 行星图中定位 #4"):
		return false
	await _frames()
	if not _check(browser.get_selected_node() == 4 and not jump_button.disabled and stream.get_anchor_node() == 6068,
		"#4 定位后没有保持在 #6068 或缺少可用跳转按钮"):
		return false
	var saved_world_dir: String = provider.get_world_dir()
	provider.world_dir = ""
	var problem: String = provider.get_problem()
	var generation_blocked := not provider.can_generate() and not problem.is_empty()
	if not generation_blocked:
		provider.world_dir = saved_world_dir
		return _check(false, "临时清空测试世界目录没有禁止按需生成并返回真实原因")
	_click(jump_button)
	await _frames()
	var jump: Dictionary = main.get_jump_state()
	var last: Dictionary = jump.get("last", {})
	var target_text: String = browser.get_target_text()
	var failed_as_expected := main.is_map_open() and browser.is_planet_view() and current_scene == main and stream.get_anchor_node() == 6068 \
		and int(jump.get("stage", -1)) == 0 and int(last.get("node", -1)) == 4 and not bool(last.get("ok", true)) and target_text.contains(problem)
	# 不论断言结果如何，都把 provider 恢复为本轮隔离目录。
	provider.world_dir = saved_world_dir
	if not _check(failed_as_expected, "跳转失败后没有保留 M/#6068/idle 或没有显示 provider 真实原因（状态：%s；读数：%s；原因：%s）" % [jump, target_text, problem]):
		return false
	return await _close_focused_map(KEY_ESCAPE)


func _run() -> void:
	dev = root.get_node_or_null("DeveloperTools") as DevTools
	if not _check(dev != null, "缺少 DeveloperTools autoload"):
		return
	root.size = Vector2i(1280, 720)
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
	dev.set_god(false)
	if not _check(provider.get_world_dir() == test_world_dir and provider.can_generate(), "测试世界目录或按需生成配置不可用：%s" % provider.get_problem()):
		return
	if not await _wait_for_floor() or not _find_controls():
		return
	if not _check(stream.get_anchor_node() == 6068, "真实开局不在群 #6068"):
		return
	if failure_only:
		if not await _run_failure_only(provider):
			return
		print("dev_group_jump_e2e: PASS failure-only，M→行星→定位 #4→禁止生成→目标卡按钮失败后保留 M/#6068/idle，显示 provider 真实原因；已恢复隔离目录并关闭地图，未执行跨群成功流程；测试目录=%s；真实 %.1f s" % [test_world_dir, (Time.get_ticks_msec() - started_ms) / 1000.0])
		quit(0)
		return

	phase = "M 默认显示 #6068 当前岛群"
	if not await _open_map() or not await _shot("01_current_group_6068"):
		return
	phase = "M 行星总览"
	if not await _open_planet_tab() or not await _shot("02_planet_overview"):
		return
	phase = "编号 Enter 只定位 #3980"
	var initial_last: Dictionary = main.get_jump_state().get("last", {}).duplicate(true)
	if not await _type_number("3980"):
		return
	_key(KEY_ENTER)
	await _frames()
	if not _no_jump(6068, initial_last, "编号 Enter 应只定位，却离开了 #6068 或关闭 M"):
		return
	if not _check(browser.get_selected_node() == 3980, "编号 Enter 没有选中 #3980") or not await _wait_for_preview(3980):
		return
	if not await _shot("03_selected_3980_wide"):
		return
	if not await _check_layout(3980) or not await _select_by_actual_click(3980) or not await _wait_for_preview(3980):
		return
	if not await _shot("04_selected_3980_compact"):
		return
	if preview_only:
		print("dev_group_jump_e2e: PASS preview-only，M 当前岛群、行星总览、#3980 定位及真实 LOD 预览、1280×720/900×700 布局；锚点仍为 #6068，未执行生成细包或跨群跳转；测试目录=%s；真实 %.1f s" % [test_world_dir, (Time.get_ticks_msec() - started_ms) / 1000.0])
		quit(0)
		return
	if not await _jump_selected(3980) or not await _check_walking():
		return

	phase = "M 返回 #6068"
	if not await _open_map() or not await _open_planet_tab():
		return
	var last: Dictionary = main.get_jump_state().get("last", {}).duplicate(true)
	if not await _type_number("#6068"):
		return
	_click(locate_button)
	await _frames()
	if not _no_jump(3980, last, "定位按钮应只定位，却离开了 #3980 或关闭 M"):
		return
	if not _check(browser.get_selected_node() == 6068, "带 # 编号的定位按钮没有选中 #6068"):
		return
	if not await _select_by_actual_click(6068) or not await _jump_selected(6068):
		return
	if not await _close_map_with_f2_overlay():
		return

	phase = "M 无效与超长编号"
	if not await _open_map() or not await _open_planet_tab() or not await _type_number("123"):
		return
	for entry in [["", "请输入"], ["-1", "非负整数"], ["8000", "有效编号"], ["6068x", "非负整数"], ["1.5", "非负整数"], ["6068 3980", "非负整数"], ["999999999999999999999999999999999999999999999999999999999999999999", "有效编号"]]:
		if not await _reject_invalid_text(entry[0], entry[1]):
			return
	if not await _close_focused_map(KEY_ESCAPE):
		return
	if not await _open_map() or not await _open_planet_tab() or not await _close_focused_map(KEY_M):
		return

	phase = "F2 清单不再承载岛群编号跳转"
	_key(KEY_F2)
	await _frames()
	if not _check(dev.is_menu_open() and dev.get_node_or_null(OLD_GROUP_ROW) == null, "F2 仍保留上一版岛群编号跳转行"):
		return
	_key(KEY_ESCAPE)
	await _frames()
	if not _check(not dev.is_menu_open() and not player.is_input_blocked(), "F2 清单关闭后没有恢复输入"):
		return

	phase = "非开发者仍可用 M 看图、不可跳转"
	main.set_dev_tools(false)
	if not await _open_map() or not await _open_planet_tab():
		return
	last = main.get_jump_state().get("last", {}).duplicate(true)
	if not _check(browser.lookup_node("3980"), "非开发者模式无法查找行星岛群"):
		return
	await _frames()
	if not _check(browser.get_selected_node() == 3980 and jump_button.disabled, "dev_tools=false 时无法选中目标或跳转按钮仍可用"):
		return
	_click(jump_button)
	await _frames()
	if not _no_jump(6068, last, "dev_tools=false 时点击目标卡仍发起跳转"):
		return
	if not _check(not main.request_planet_jump(3980) and stream.get_anchor_node() == 6068 and int(main.get_jump_state().get("stage", -1)) == 0,
		"dev_tools=false 时后端仍接受跳转"):
		return
	if not await _close_focused_map(KEY_ESCAPE):
		return
	_key(KEY_F2)
	await _frames()
	if not _check(not dev.is_menu_open() and not player.is_input_blocked(), "dev_tools=false 时 F2 仍打开菜单或阻挡输入"):
		return
	print("dev_group_jump_e2e: PASS 6068→M 当前岛群→行星总览→3980 Enter 只定位→鼠标点选→目标卡按钮→3980 真实落地及行走→M 返回6068；真实 LOD 预览、900×700 布局、M 叠加 F2 后按钮一起关闭且恢复行走、无效及超长编号、焦点下 M/Esc、F2 无编号跳转行、dev_tools=false 可看图不可跳；测试目录=%s；真实 %.1f s" % [test_world_dir, (Time.get_ticks_msec() - started_ms) / 1000.0])
	quit(0)
