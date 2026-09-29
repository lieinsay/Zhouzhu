extends Node
## 开发者工具端到端测试（DevTools autoload），只通过输入操作：
## 甲乙两岛里按 M → 提示去 F2 换地图 → F3 开 god → 空格升起 → Ctrl 降回地面站住 → Shift 前飞超过 100 m/s
## → F3 关、重力回来往下掉 → F3 再开 → F2 开清单、按 2 换到世界 seed42（剧情，开局那群的主码头）：新场景里的人还是 god、脚下有地
## → 按住空格升到离地 50 km（god 的速度随离地高度放大，一分钟内；行星计划 P1），岛群的画面弯曲开着、相机的经纬度就是起飞处的经纬度
## → HUD 的天气行报「高空」、气温照对流层顶（不再按直减率外推到零下几十上百度；行星计划 P2）
## → F7 开轨道相机（接过 god 的相机，行星计划 P2）：滚轮往上十格高度成倍涨、按住空格再升，高空换成整颗行星（云带顶网格淡掉、云层叠画开着）
## → Esc 落回人身上（相机换回人的、输入放开）
## → F2、按 1 换回甲乙两岛。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/dev_tools_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，存 god 飞行与换地图清单两张图。
##
## 驱动器自己不是「当前场景」：它把 main.tscn 挂成当前场景，换地图时被换掉的是那个，驱动器留着。

const MAIN := preload("res://main.tscn")
const TIMEOUT := 240.0
const CLIMB_M := 50000.0
const CLIMB_MAX_S := 60.0

enum Phase { START, PRESS_M, GOD_ON, ASCEND, DESCEND, BOOST, GOD_OFF, FALLING, GOD_AGAIN, OPEN_MENU, PICK_STORY, IN_STORY, CLIMB, HIGH_HUD, ORBIT_ON, ORBIT_WHEEL, ORBIT_CLIMB, ORBIT_OFF, BACK, DONE }

var dev: DevTools
var player: Player
var phase := Phase.START
var elapsed := 0.0
var phase_started := 0.0
var mark := Vector3.ZERO
var new_game_line := ""  # 新开一局查到的（结果行里打出来）
var peak_speed := 0.0
var sent := false
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var orbit_start := 0.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	dev = get_node("/root/DeveloperTools")  # autoload 不能与类同名
	var main := MAIN.instantiate()
	get_tree().root.add_child.call_deferred(main)
	(func(): get_tree().current_scene = main).call_deferred()


func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait <= 0:
		get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
		pending_shot = ""


func _shot(shot_name: String) -> void:
	pending_shot = shot_name
	shot_wait = 3


func _goto(next: Phase) -> void:
	phase = next
	phase_started = elapsed
	sent = false


## 本阶段里只做一次（送出的输入事件下一帧才处理，所以按完要等一会儿再看结果）
func _once() -> bool:
	if sent:
		return false
	sent = true
	return true


func _action(action: String) -> void:
	for pressed in [true, false]:
		var ev := InputEventAction.new()
		ev.action = action
		ev.pressed = pressed
		Input.parse_input_event(ev)


func _key(code: Key) -> void:
	for pressed in [true, false]:
		var ev := InputEventKey.new()
		ev.physical_keycode = code
		ev.keycode = code
		ev.pressed = pressed
		Input.parse_input_event(ev)


func _scene_path() -> String:
	var scene := get_tree().current_scene
	return scene.scene_file_path if scene else ""


func _find_player() -> Player:
	var scene := get_tree().current_scene
	if scene == null or scene == self:
		return null
	var found := scene.find_children("*", "Player", true, false)
	return found[0] if not found.is_empty() else null


func _physics_process(delta: float) -> void:
	elapsed += delta
	var t := elapsed - phase_started
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在 %s）" % Phase.keys()[phase])
		return
	match phase:
		Phase.START:
			player = _find_player()
			if player and _scene_path() == "res://main.tscn" and t > 1.0 and player.is_on_floor():
				_goto(Phase.PRESS_M)
		Phase.PRESS_M:
			if _once():
				_action("map")
			if t < 0.2:
				return
			if not dev.get_notice().contains("F2"):
				_finish(false, "甲乙两岛里按 M 没有提示去 F2（提示：%s）" % dev.get_notice())
				return
			_goto(Phase.GOD_ON)
		Phase.GOD_ON:
			if _once():
				_action("god_mode")
			if t < 0.2:
				return
			if not dev.is_god() or not player.is_god_mode():
				_finish(false, "按 F3 god 没开")
				return
			mark = player.global_position
			_goto(Phase.ASCEND)
		Phase.ASCEND:
			Input.action_press("ascend")
			if t > 2.0:
				Input.action_release("ascend")
				var rise := player.global_position.y - mark.y
				if rise < 30.0:
					_finish(false, "按空格 2 s 只升了 %.1f m" % rise)
					return
				print("dev_tools_e2e: god 升起 %.1f m" % rise)
				_goto(Phase.DESCEND)
		Phase.DESCEND:
			Input.action_press("fly_down")
			if player.is_on_floor() and t > 0.5:
				Input.action_release("fly_down")
				print("dev_tools_e2e: Ctrl 降回地面（%.1f s）" % t)
				_goto(Phase.BOOST)
			elif t > 10.0:
				_finish(false, "按 Ctrl 10 s 还没落地：%s" % player.global_position)
		Phase.BOOST:
			Input.action_press("ascend")  # 先离地，免得贴地蹭
			Input.action_press("move_forward")
			Input.action_press("sprint")
			var v := player.velocity
			peak_speed = maxf(peak_speed, Vector2(v.x, v.z).length())
			if t > 2.5:
				for a in ["ascend", "move_forward", "sprint"]:
					Input.action_release(a)
				if peak_speed < 100.0:
					_finish(false, "Shift 前飞最快只有 %.0f m/s" % peak_speed)
					return
				print("dev_tools_e2e: Shift 前飞最快 %.0f m/s" % peak_speed)
				_shot("1_god")
				_goto(Phase.GOD_OFF)
		Phase.GOD_OFF:
			if _once():
				_action("god_mode")
			if t < 0.2:
				return
			if dev.is_god() or player.is_god_mode():
				_finish(false, "再按 F3 god 没关")
				return
			_goto(Phase.FALLING)
		Phase.FALLING:
			if t > 0.5:
				if player.velocity.y > -3.0:
					_finish(false, "关了 god 没往下掉（vy = %.1f）" % player.velocity.y)
					return
				_goto(Phase.GOD_AGAIN)
		Phase.GOD_AGAIN:
			_action("god_mode")
			_goto(Phase.OPEN_MENU)
		Phase.OPEN_MENU:
			if t > 0.3 and _once():
				_action("switch_map")
			if t > 0.6:
				if not dev.is_menu_open():
					_finish(false, "按 F2 清单没开")
					return
				_shot("2_menu")
				_goto(Phase.PICK_STORY)
		Phase.PICK_STORY:
			if t > 0.4:
				_key(KEY_2)
				_goto(Phase.IN_STORY)
		Phase.IN_STORY:
			if _scene_path() != "res://world_seed42.tscn":
				if t > 5.0:
					_finish(false, "按 2 没换到世界 seed42（当前 %s）" % _scene_path())
				return
			player = _find_player()
			if player == null:
				return
			var world: World = get_tree().current_scene.get_node("World")
			var at := player.global_position
			var ground: float = world.get_surface_height(at.x, at.z)
			if t > 1.0 and player.is_god_mode() and not is_nan(ground) and absf(at.y - ground) < 2.0 and world.is_ready_at(at):
				print("dev_tools_e2e: 换到世界 seed42（群 #%d），god 还开着，人在地上（%s）" % [world.get_node_id(), at])
				if not _check_new_game(world, at):
					return
				mark = at
				_goto(Phase.CLIMB)
			elif t > 15.0:
				_finish(false, "换到世界 seed42 后：god %s，离地 %.1f，脚下块 %s" % [player.is_god_mode(), at.y - ground, world.is_ready_at(at)])
		Phase.CLIMB:
			Input.action_press("ascend")
			var rise := player.global_position.y - mark.y
			if rise >= CLIMB_M:
				Input.action_release("ascend")
				var planet = get_tree().current_scene.get_node_or_null("PlanetView")
				if planet == null or not planet.is_active():
					_finish(false, "世界 seed42 里没有 PlanetView，或没锚定")
					return
				# 相机就在起飞处的正上方：经纬度与起飞处（开局那群的主码头，锚群平面换回经纬度）差不到 0.3°
				var ll: Vector2 = planet.get_camera_latlon()
				var at_mark: Vector2 = planet.latlon_at(Vector3(mark.x, 0.0, mark.z))
				if at_mark == Vector2.ZERO or absf(ll.x - at_mark.x) > 0.3 or absf(ll.y - at_mark.y) > 0.3:
					_finish(false, "高空相机的经纬度不对：%s（起飞处 %s）" % [ll, at_mark])
					return
				print("dev_tools_e2e: 按住空格 %.1f s 升到离地 %.0f km（god ×%.0f），相机在 %s %.2f° %s %.2f°（起飞处 %.2f° %.2f°）" % [t, rise / 1000.0, player.get_god_scale(),
					"北纬" if ll.x >= 0.0 else "南纬", absf(ll.x), "东经" if ll.y >= 0.0 else "西经", absf(ll.y), at_mark.x, at_mark.y])
				_goto(Phase.HIGH_HUD)
			elif t > CLIMB_MAX_S:
				_finish(false, "按住空格 %d s 只升了 %.1f km" % [int(CLIMB_MAX_S), rise / 1000.0])
		Phase.HIGH_HUD:
			var atmosphere = get_tree().current_scene.get_node("Atmosphere")
			var line: String = atmosphere.hud_text(player.global_position)
			var w: Dictionary = atmosphere.get_weather()
			var re := RegEx.create_from_string("(-?\\d+)°C")
			var m := re.search(line)
			var temp := float(m.get_string(1)) if m else -999.0
			if not line.contains("高空") or temp < -70.0 or temp > 10.0 or int(w["altitude_band"]) != 1:
				_finish(false, "50 km 高 HUD 的天气行不对：「%s」（层 %s）" % [line, w["altitude_band"]])
				return
			print("dev_tools_e2e: 50 km 高 HUD：「%s」" % line)
			_goto(Phase.ORBIT_ON)
		Phase.ORBIT_ON:
			if _once():
				_action("orbit_camera")
			if t < 0.3:
				return
			var cam := get_viewport().get_camera_3d()
			if not dev.is_orbit() or cam == null or cam.get_class() != "OrbitCamera" or not player.is_input_blocked():
				_finish(false, "按 F7 没换成轨道相机（%s，当前相机 %s）" % [dev.is_orbit(), cam.get_class() if cam else "无"])
				return
			orbit_start = cam.get_clearance()
			if absf(orbit_start - CLIMB_M) > 2000.0:
				_finish(false, "轨道相机没接过原来相机的位置：离地 %.0f m" % orbit_start)
				return
			print("dev_tools_e2e: F7 轨道相机接过来，离地 %.1f km" % (orbit_start / 1000.0))
			_goto(Phase.ORBIT_WHEEL)
		Phase.ORBIT_WHEEL:
			if _once():
				for k in 10:
					var ev := InputEventMouseButton.new()
					ev.button_index = MOUSE_BUTTON_WHEEL_UP
					ev.pressed = true
					Input.parse_input_event(ev)
			if t < 2.5:
				return
			var cam = get_viewport().get_camera_3d()
			var gain: float = cam.get_clearance() / orbit_start
			if gain < 8.0:
				_finish(false, "滚轮往上十格只升到 %.1f 倍" % gain)
				return
			print("dev_tools_e2e: 滚轮往上十格，离地 %.0f km（×%.1f）" % [cam.get_clearance() / 1000.0, gain])
			orbit_start = cam.get_clearance()
			_goto(Phase.ORBIT_CLIMB)
		Phase.ORBIT_CLIMB:
			Input.action_press("ascend")
			if t < 3.0:
				return
			Input.action_release("ascend")
			var cam = get_viewport().get_camera_3d()
			var globe = get_tree().current_scene.get_node("PlanetGlobe")
			var gs: Dictionary = globe.get_state()
			if cam.get_clearance() < orbit_start * 2.5:
				_finish(false, "轨道相机按住空格 3 s 只从 %.0f km 升到 %.0f km" % [orbit_start / 1000.0, cam.get_clearance() / 1000.0])
				return
			if gs["blend_planet"] < 0.99 or gs["deck_alpha"] > 0.01 or not gs["overlay"]:
				_finish(false, "高空没换成整颗行星：%s" % gs)
				return
			print("dev_tools_e2e: 按住空格 3 s 升到离地 %.0f km；行星 %.2f、云带顶网格 %.2f、云层叠画 %s、贴图烘好 %s" % [cam.get_clearance() / 1000.0, gs["blend_planet"], gs["deck_alpha"],
				gs["overlay"], gs["surface_ready"]])
			_shot("3_orbit")
			_goto(Phase.ORBIT_OFF)
		Phase.ORBIT_OFF:
			if _once():
				var ev := InputEventAction.new()
				ev.action = "ui_cancel"
				ev.pressed = true
				Input.parse_input_event(ev)
				var up := InputEventAction.new()
				up.action = "ui_cancel"
				Input.parse_input_event(up)
			if t < 0.5:
				return
			var cam := get_viewport().get_camera_3d()
			if dev.is_orbit() or cam != player.get_camera() or player.is_input_blocked():
				_finish(false, "Esc 没落回人身上（轨道 %s、当前相机 %s、输入挡着 %s）" % [dev.is_orbit(), cam, player.is_input_blocked()])
				return
			print("dev_tools_e2e: Esc 落回人身上")
			_action("switch_map")
			_goto(Phase.BACK)
		Phase.BACK:
			if t > 0.3 and _once():
				_key(KEY_1)
			if _scene_path() == "res://main.tscn" and _find_player() != null:
				_finish(true, "M 提示、god 升降与加速、关 god 恢复重力、F2 换到世界 seed42 且 god 跟着、一分钟内升到 50 km、高空 HUD、F7 轨道相机（滚轮、空格、整颗行星、Esc 落回）、换回甲乙两岛；" + new_game_line)
			elif t > 5.0:
				_finish(false, "按 1 没换回甲乙两岛（当前 %s）" % _scene_path())


## 新开一局（PLAN-LAND P0 的验收）：F2 进世界 seed42 走的是剧情世界那条路（WorldSession.story_config → GroupProvider），
## 出生在开局那群（StoryStart）主码头的停机坪旁、HUD 报它的群号与地方时、日历是开局那天、天与光读它的纬度、钟是它的地方时
func _check_new_game(world: World, at: Vector3) -> bool:
	var scene: Expedition = get_tree().current_scene
	var planet = scene.get_node("PlanetView")
	var atmosphere = scene.get_node("Atmosphere")
	var spawn_node := StoryStart.get_spawn_node()
	if world.get_node_id() != spawn_node:
		_finish(false, "新开一局该出生在开局那群 #%d，实为 #%d" % [spawn_node, world.get_node_id()])
		return false
	# 主码头的停机坪旁：离出生点（出发码头 = 开局那群的主码头）几十米内
	var spawn: Vector3 = scene.get_spawn_point()
	var off := Vector2(at.x - spawn.x, at.z - spawn.z).length()
	var main_dock := -1
	for d in world.get_info()["docks"]:
		if bool(d.get("main", false)) and main_dock < 0:
			main_dock = int(d["id"])
	if off > 50.0 or scene.origin_dock != main_dock:
		_finish(false, "新开一局该站在主码头的停机坪旁：离出生点 %.0f m，出发码头 #%d（主码头 #%d）" % [off, scene.origin_dock, main_dock])
		return false
	# HUD：群号、地方时
	var status: String = scene.get_status_text()
	if not status.begins_with("群 #%d" % spawn_node) or not status.get_slice("\n", 0).contains("地方时"):
		_finish(false, "HUD 第一行该以「群 #%d」开头、带地方时：%s" % [spawn_node, status.get_slice("\n", 0)])
		return false
	# 日历：开局那天（开局配置的 calendar_start）加钟走过的整日（前半段在甲乙两岛跑了几十秒，还是第 0 日）
	var date: Dictionary = GameClock.get_date()
	var expect_day := StoryStart.get_calendar_start() + int(date["day"])
	if int(floor(float(date["calendar_day"]))) != expect_day or GameClock.get_calendar_offset() != StoryStart.get_calendar_start():
		_finish(false, "日历该是开局那天（第 %d 天 + 钟走过 %d 日），实为 %s（偏移 %d）" % [StoryStart.get_calendar_start(), int(date["day"]), date, GameClock.get_calendar_offset()])
		return false
	# 天与光：开局那群的纬度；钟的那一群就是开局那群，地方时差只是码头离群心的那点经度差
	var src: Dictionary = world.get_info()["source"]
	var w: Dictionary = atmosphere.get_weather()
	var cam_ll: Vector2 = planet.get_camera_latlon()
	var expect_off: float = (cam_ll.y - float(planet.get_clock_longitude())) / 15.0
	if absf(float(w["latitude"]) - float(src["lat"])) > 0.01 or planet.get_clock_node() != StoryStart.get_clock_node() \
			or absf(planet.get_clock_longitude() - float(planet.node_latlon(StoryStart.get_clock_node()).y)) > 0.01 \
			or absf(float(w["local_offset_h"]) - expect_off) > 0.01 or absf(float(w["local_offset_h"])) > 0.1:
		_finish(false, "天与光该读开局那群：纬度 %.3f°（群 %.3f°）、钟的群 #%d（该是 #%d）、钟的经度 %.3f°、地方时差 %.3f h（按相机经度该是 %.3f h）" % [float(w["latitude"]),
			float(src["lat"]), planet.get_clock_node(), StoryStart.get_clock_node(), planet.get_clock_longitude(), float(w["local_offset_h"]), expect_off])
		return false
	new_game_line = "新开一局：群 #%d 主码头 #%d 停机坪旁 %.0f m；HUD「%s」；%s（历日第 %d 天，一年里第 %d 天）；纬度 %.2f°、钟的群 #%d 经度 %.2f°、地方时差 %.1f 分钟；此地%s %s" % [spawn_node,
		scene.origin_dock, off, status.get_slice("\n", 0), date["text"], expect_day, int(date["day_of_year"]), float(w["latitude"]), planet.get_clock_node(),
		planet.get_clock_longitude(), float(w["local_offset_h"]) * 60.0, w["season_name"], w["kind_name"]]
	print("dev_tools_e2e: " + new_game_line)
	return true


func _finish(ok: bool, why: String) -> void:
	for a in ["ascend", "fly_down", "move_forward", "sprint"]:
		Input.action_release(a)
	set_physics_process(false)
	print("dev_tools: %s —— %s（模拟 %.1f s）" % ["PASS" if ok else "FAIL", why, elapsed])
	get_tree().quit(0 if ok else 1)
