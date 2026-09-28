extends Node
## Esc 暂停菜单（flat_map.tscn，拓展）：只经输入事件操作（Esc、↓、回车、F3、Tab、F2 都走 Input.parse_input_event）。
## 平板岛站稳 → Esc：暂停菜单开着、整棵树暂停（世界钟不走、按住前进人不动、F3 不开 god）
## → ↓ 回车：进「开发者工具说明」——列出每一样开发者工具（B / F1、F2、F3、F4、F6、F7、F8、T、地图瞬移），说明页 ↓ 能往下翻
## → Esc 回到菜单 → Esc 合上：取消暂停，钟接着走、前进人会动
## → 再 Esc 开、回车（焦点在「返回游戏」）也合上
## → Tab 开背包、Esc：只合上背包，不出暂停菜单；F2 开换地图清单、Esc：只合上清单
##
##   Godot --headless --path game --fixed-fps 60 res://tests/pause_menu_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，菜单与说明页各存一张截图。

const SCENE := preload("res://flat_map.tscn")
const TIMEOUT := 120.0  # 真实秒（树暂停时模拟时间不走）
const MOVES := ["move_forward", "inventory"]
const TOOLS := ["B / F1", "F2", "F3", "F4", "F6", "F7", "F8", "T", "M 地图里左键"]

var main: Expedition
var player: Player
var menu: PauseMenu
var dev: DevTools
var clock: WorldClock

var stage := "开局"
var shots_dir := ""
var done := false
var started := 0


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS  # 树暂停时测试自己得接着跑
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	started = Time.get_ticks_msec()
	menu = get_node("/root/GameMenu")
	dev = get_node("/root/DeveloperTools")
	clock = get_node("/root/GameClock")
	main = SCENE.instantiate()
	main.process_mode = Node.PROCESS_MODE_PAUSABLE  # 挂在测试节点（ALWAYS）下面：照游戏里（挂在 root 下）那样跟着暂停
	add_child(main)
	player = main.get_node("Player")
	_run()


func _log(line: String) -> void:
	print("pause_menu: " + line)


func _process(_delta: float) -> void:
	if not done and Time.get_ticks_msec() - started > TIMEOUT * 1000.0:
		_finish(false, "超时")


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _action(action: String) -> void:
	for pressed in [true, false]:
		var ev := InputEventAction.new()
		ev.action = action
		ev.pressed = pressed
		Input.parse_input_event(ev)
		await _frames(2)
	await _frames(2)


func _key(code: Key) -> void:
	for pressed in [true, false]:
		var ev := InputEventKey.new()
		ev.physical_keycode = code
		ev.keycode = code
		ev.pressed = pressed
		Input.parse_input_event(ev)
		await _frames(2)
	await _frames(2)


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await _frames(6)
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


func _run() -> void:
	await _frames(30)
	var t := 0
	while not player.is_on_floor():
		await _frames(1)
		t += 1
		if t > 1200:
			_finish(false, "平板岛站不稳")
			return
	await _frames(20)

	stage = "Esc 暂停"
	await _action("ui_cancel")
	if not menu.is_open() or menu.get_page() != "main" or not get_tree().paused:
		_finish(false, "Esc 该开暂停菜单、树暂停（开着 %s，页 %s，暂停 %s）" % [menu.is_open(), menu.get_page(), get_tree().paused])
		return
	if Input.get_mouse_mode() != Input.MOUSE_MODE_VISIBLE:
		_finish(false, "暂停时鼠标该放出来")
		return
	var s0: float = clock.get_seconds()
	var p0 := player.global_position
	Input.action_press("move_forward")
	await _frames(40)
	Input.action_release("move_forward")
	if clock.get_seconds() != s0 or player.global_position.distance_to(p0) > 0.01:
		_finish(false, "暂停时钟不该走、人不该动（钟 %.3f → %.3f，人挪了 %.3f m）" % [s0, clock.get_seconds(), player.global_position.distance_to(p0)])
		return
	await _action("god_mode")
	if dev.is_god():
		_finish(false, "暂停时 F3 不该开 god")
		return
	_log("Esc：菜单开着、树暂停，钟不走、人不动、F3 不理")
	await _shot("PAUSE_main")

	stage = "说明页"
	await _key(KEY_DOWN)  # 焦点从「返回游戏」挪到「开发者工具说明」
	await _key(KEY_ENTER)
	if menu.get_page() != "dev_help":
		_finish(false, "↓ 回车该进开发者工具说明（页 %s）" % menu.get_page())
		return
	var titles: PackedStringArray = menu.get_dev_help_titles()
	var text: String = menu.get_dev_help_text()
	for k in TOOLS:
		var found := false
		for title in titles:
			if title.begins_with(k + " "):
				found = true
		if not found or not text.contains(k):
			_finish(false, "说明页该列出「%s」（%s）" % [k, titles])
			return
	for word in ["布置模式", "换地图", "god 模式", "时间快进", "时间与天气", "轨道相机", "开发者营造", "室内编辑", "地图瞬移"]:
		if not text.contains(word):
			_finish(false, "说明页里该有「%s」" % word)
			return
	await _shot("PAUSE_dev_help")
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	if menu.get_help_scroll() <= 0:
		_finish(false, "说明页 ↓ 该往下翻（%d）" % menu.get_help_scroll())
		return
	_log("说明页：%d 样工具（%s），↓ 翻到 %d px" % [titles.size(), ", ".join(titles), menu.get_help_scroll()])

	stage = "回去"
	await _action("ui_cancel")
	if not menu.is_open() or menu.get_page() != "main":
		_finish(false, "说明页 Esc 该回到菜单（页 %s）" % menu.get_page())
		return
	await _action("ui_cancel")
	if menu.is_open() or get_tree().paused:
		_finish(false, "菜单 Esc 该合上、取消暂停")
		return
	s0 = clock.get_seconds()
	p0 = player.global_position
	Input.action_press("move_forward")
	await _frames(40)
	Input.action_release("move_forward")
	await _frames(10)
	if clock.get_seconds() <= s0 or player.global_position.distance_to(p0) < 0.5:
		_finish(false, "合上以后钟该走、人该动（钟 %.3f → %.3f，人挪了 %.2f m）" % [s0, clock.get_seconds(), player.global_position.distance_to(p0)])
		return
	await _action("ui_cancel")
	await _key(KEY_ENTER)  # 焦点在「返回游戏」
	if menu.is_open() or get_tree().paused:
		_finish(false, "回车「返回游戏」该合上")
		return
	_log("说明页 Esc → 菜单 → Esc 合上，钟走人动；回车「返回游戏」也合上")

	stage = "先合别的"
	await _action("inventory")
	if not main.is_pack_open():
		_finish(false, "Tab 没开背包")
		return
	await _action("ui_cancel")
	if main.is_pack_open() or menu.is_open():
		_finish(false, "背包开着时 Esc 只合背包（背包 %s，菜单 %s）" % [main.is_pack_open(), menu.is_open()])
		return
	await _action("switch_map")
	if not dev.is_menu_open():
		_finish(false, "F2 没开换地图清单")
		return
	await _action("ui_cancel")
	if dev.is_menu_open() or menu.is_open():
		_finish(false, "换地图清单开着时 Esc 只合清单（清单 %s，菜单 %s）" % [dev.is_menu_open(), menu.is_open()])
		return
	_finish(true, "Esc 暂停（钟不走、人不动、F3 不理）→ 开发者工具说明列出 %d 样、能翻 → Esc 回菜单、Esc / 回车返回游戏 → 背包、换地图清单开着时 Esc 先合它们" % titles.size())


func _finish(ok: bool, why: String) -> void:
	if done:
		return
	done = true
	for action in MOVES:
		Input.action_release(action)
	if menu and menu.is_open():
		menu.close_menu()
	print("pause_menu: %s —— %s（阶段 %s）" % ["PASS" if ok else "FAIL", why, stage])
	get_tree().quit(0 if ok else 1)
