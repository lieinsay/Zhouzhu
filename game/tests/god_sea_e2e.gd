extends Node
## god 模式飞到海面（world_seed42.tscn，开局那群的主码头，只通过输入动作开关 god、往下飞）：
## 主码头站稳 → F3 开 god → 人挪到主码头外一片开阔的云海上空（2.5 km 内没有地，离地面 200 m 上下）
## → 按住 Ctrl 一路往下：穿过 y = −40 m（不开 god 时算坠入云海、回到上一处站稳的地方）也不回位，
##   往下飞的速度也随离起飞处的距离放大（一分钟内到海面），停在海面之上 2 m（再按也不往下），HUD 报「云底下 · 离海面 2 m」
## → F3 关 god：重力回来、人在坠落线以下，回到上一处站稳的地方（主码头）。
## 去掉 --headless、末尾加 `-- --shots=<目录>`：拍云带顶上、云带里、云底下、海面上四张。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/god_sea_e2e.tscn

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 600.0  # 模拟秒
const SETTLE_TIMEOUT := 150.0  # 墙钟秒
const TOP_Y := -5000.0  # 云带顶、云底、海面：同 src/planet_frame.h 的 VerticalDatum（浮高计划 F4 起整体下移 1200 m）
const BASE_Y := -6200.0
const SEA_Y := -6800.0
const GOD_FLOOR_Y := SEA_Y + 2.0
const DESCEND_MAX_S := 60.0
const SHOT_YS := [TOP_Y + 100.0, 0.5 * (TOP_Y + BASE_Y), BASE_Y - 300.0]  # 云带顶上 100 m、云带当中、云底下 300 m

enum Phase { SETTLE, GOD_ON, MOVE_OUT, DESCEND, HOLD_AT_SEA, GOD_OFF, DONE }

var main: Expedition
var player: Player
var planet: PlanetView
var world: GroupWorld
var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var sent := false
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var dock := Vector3.ZERO
var last_y := 0.0
var min_y := 1e9
var crossed_kill := false
var shots_taken := 0
var reached_at := -1.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	get_tree().root.add_child.call_deferred(main)
	(func(): get_tree().current_scene = main).call_deferred()  # DevTools 找的是当前场景里的 Player


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


# 从 from 起朝 away 走，找一处 2.5 km 内没有地的开阔云海，再往外 3 km（同 sea_descent_e2e）
func _open_sea(from: Vector2, away: Vector2) -> Vector2:
	for k in 400:
		var p := from + away * (float(k) * 500.0)
		var clear := true
		for r in [0.0, 1000.0, 2500.0]:
			for a in 12:
				var q: Vector2 = p + Vector2(cos(TAU * a / 12.0), sin(TAU * a / 12.0)) * float(r)
				if not is_nan(world.get_surface_height(q.x, q.y)):
					clear = false
		if clear:
			return p + away * 3000.0
	return from + away * 200000.0


func _peak() -> Vector2:
	var peak := Vector2.ZERO
	var peak_h := -1e9
	for i in range(-60, 61):
		for j in range(-60, 61):
			var p := Vector2(j, i) * 1000.0
			var h := world.get_surface_height(p.x, p.y)
			if not is_nan(h) and h > peak_h:
				peak_h = h
				peak = p
	return peak


func _physics_process(delta: float) -> void:
	elapsed += delta
	var t := elapsed - phase_started
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在 %s，人在 %s）" % [Phase.keys()[phase], player.global_position if player else Vector3.ZERO])
		return
	match phase:
		Phase.SETTLE:
			if main == null or not main.is_inside_tree():
				return
			player = main.get_node("Player")
			planet = main.get_node("PlanetView")
			world = main.get_node("World")
			if not world.is_loaded() or not planet.is_active() or not player.is_on_floor() or t < 1.0:
				if Time.get_ticks_msec() / 1000.0 > SETTLE_TIMEOUT:
					_finish(false, "墙钟 %.0f s 还没就绪：世界 %s、PlanetView %s、站稳 %s" % [SETTLE_TIMEOUT, world.is_loaded(), planet.is_active(), player.is_on_floor()])
				return
			dock = player.global_position
			_goto(Phase.GOD_ON)
		Phase.GOD_ON:
			if _once():
				_action("god_mode")
			if t < 0.3:
				return
			if not player.is_god_mode():
				_finish(false, "按 F3 god 没开")
				return
			var peak := _peak()
			var here := Vector2(dock.x, dock.z)
			var sea := _open_sea(here, (here - peak).normalized())
			player.global_position = Vector3(sea.x, dock.y + 200.0, sea.y)
			player.velocity = Vector3.ZERO
			print("god_sea_e2e: 主码头 %s 站稳、god 开了；挪到开阔云海上空 %s（离主码头 %.1f km）" % [dock, player.global_position, (sea - here).length() / 1000.0])
			_goto(Phase.MOVE_OUT)
		Phase.MOVE_OUT:
			if t < 0.5:
				return
			if player.global_position.y < dock.y + 150.0:
				_finish(false, "挪到云海上空后没悬停住（y = %.1f）" % player.global_position.y)
				return
			last_y = player.global_position.y
			Input.action_press("fly_down")
			_goto(Phase.DESCEND)
		Phase.DESCEND:
			Input.action_press("fly_down")  # 每帧按：窗口失焦时 Godot 会松开按住的动作
			var y := player.global_position.y
			if y > last_y + 50.0:
				_finish(false, "往下飞时一帧里升了 %.0f m（%.0f → %.0f）：被当成坠落回位了" % [y - last_y, last_y, y])
				return
			last_y = y
			min_y = minf(min_y, y)
			if y < main.kill_altitude - 100.0:
				crossed_kill = true
			if shots_taken < SHOT_YS.size() and y < SHOT_YS[shots_taken]:
				_shot("%d_y%d" % [shots_taken + 1, int(SHOT_YS[shots_taken])])
				shots_taken += 1
			if y < GOD_FLOOR_Y + 0.5:
				reached_at = t
				print("god_sea_e2e: 按住 Ctrl %.1f s 从 %.0f m 降到海面之上 %.1f m（god ×%.0f）" % [t, dock.y + 200.0, y - SEA_Y, player.get_god_scale()])
				if t > DESCEND_MAX_S:
					_finish(false, "往下飞到海面用了 %.0f s（该在 %.0f s 内：往下飞的速度也要随离起飞处的距离放大）" % [t, DESCEND_MAX_S])
					return
				_goto(Phase.HOLD_AT_SEA)
		Phase.HOLD_AT_SEA:
			# 接着按住 Ctrl 两秒：停在海面之上 2 m
			if t < 2.0:
				Input.action_press("fly_down")
			var y := player.global_position.y
			min_y = minf(min_y, y)
			if y > last_y + 50.0:
				_finish(false, "停在海面上时被回位了（%.0f → %.0f）" % [last_y, y])
				return
			last_y = y
			if t < 2.0:
				return
			if min_y < GOD_FLOOR_Y - 0.05:
				_finish(false, "穿到海面之下了：最低 y = %.2f（该停在 %.1f）" % [min_y, GOD_FLOOR_Y])
				return
			if not crossed_kill:
				_finish(false, "没穿过坠落线（kill_altitude %.0f）" % main.kill_altitude)
				return
			var hud := main.get_status_text()
			if not hud.contains("云底下") or not hud.contains("离海面 2 m"):
				_finish(false, "停在海面上时 HUD 没报云底下、离海面 2 m：%s" % hud.get_slice("\n", 0))
				return
			_shot("4_sea")
			Input.action_release("fly_down")
			print("god_sea_e2e: 停在海面之上 %.2f m（最低 %.2f m）" % [y - SEA_Y, min_y - SEA_Y])
			_goto(Phase.GOD_OFF)
		Phase.GOD_OFF:
			if t < 0.3:
				return  # 等海面那张拍完
			if _once():
				_action("god_mode")
			if t < 1.5:
				return
			if player.is_god_mode():
				_finish(false, "再按 F3 god 没关")
				return
			var at := player.global_position
			var back := Vector2(at.x - dock.x, at.z - dock.z).length()
			if at.y < main.kill_altitude or back > 50.0:
				_finish(false, "关了 god 在坠落线以下，没回到上一处站稳的地方：人在 %s，离主码头 %.0f m" % [at, back])
				return
			_finish(true, "god 飞穿坠落线不回位、%.1f s 降到海面之上 2 m 停住；关 god 后坠落回位到主码头（%.0f m）" % [reached_at, back])


func _finish(ok: bool, message: String) -> void:
	if phase == Phase.DONE:
		return
	phase = Phase.DONE
	Input.action_release("fly_down")
	print("god_sea_e2e: %s —— %s" % ["PASS" if ok else "FAIL", message])
	get_tree().quit(0 if ok else 1)
