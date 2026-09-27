extends Node
## god 飞到高空、绕着行星转、再落地（world_2051.tscn，剧情模式 seed42，世界存档目录用测试自己的 user://worlds/e2e_orbit，开局清空）。
## 查的是用户报的「上帝模式飞到非常高、绕着行星转，想落地时场景已经乱了」：原来锚只在细版的群之间换，高空巡航一群也进不了细版，
## 锚一直是 2051，人在它的平面里跑出一整圈（平面坐标 4 万公里，远过 AEQD 的对跖点）——回到 2051 上空时 HUD 报「离群 #2051 陆地 40027 km」、
## 画面弯曲的展开式失效（云带顶斜成一块板）、2051 的地在平面里够不着，一路掉到海面；飞到几千公里外落地，脚下那一群排在一长串顺路生成的群后面，迟迟轮不到；
## 另外远处岛群（FarGroups）的低模在高速下一份份重复地建、攒着不挂，一圈下来内存涨到十几 GB、任务池被占满，落地处的地面块迟迟建不出来。
##   A. 主码头站稳 → F3 开 god → 按住空格 + Shift 升到 500 km → 按住前进 + Shift 绕行星一整圈（4 万公里）：
##      一路上人在锚群平面里的坐标不超过对跖点（过了就「换到锚群自己」，平面坐标规范化），锚照旧是 2051；
##      一圈里 Godot 的内存（静态）比起飞前多不过 1 GB；600 km 内有岛群的那些帧里，七成以上挂着至少一份远处岛群的低模（修之前后六成的路上一份也没有）；
##      停下、降到 20 km、飞回主码头上空、落在码头边一块平地上：锚是 2051、人站在它的地上（离起飞处 1 km 内）、脚下的块挂好了。
##   B. 再升到 500 km，朝同一个方向飞 3,000 km，停下、往下落：锚换到了那里最近的一群（远离锚群时换到近的群；没有包就当场生成），
##      人在新锚群平面里的坐标在一千公里以内；飞到它的码头（没有码头就找一块地）上空落下，站在它的地上、脚下的块挂好了。
## 去掉 --headless、末尾加 `-- --shots=<目录>`：A 升到顶、一圈回来、20 km、落地，B 停下、换锚后、落地各一张。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/god_orbit_e2e.tscn

const SCENE := preload("res://world_2051.tscn")
const WORLD_DIR := "user://worlds/e2e_orbit"
const HOME := 2051
const TIMEOUT_REAL_S := 1200.0
const CRUISE_Y := 500000.0
const LAP_M := 2.0 * PI * 6377800.0  # 一整圈（R = 海面半径 + 6.8 km，同 src/planet_frame.h）
const FAR_M := 3000000.0
const HALF_M := PI * 6377800.0  # 对跖点
const SEEK_Y := 20000.0
const MEM_GROWTH_MAX := 1.0e9  # 绕一圈内存（静态）最多多涨这么多（修之前无头 3.8 GB、带窗口 12 GB）

enum Phase { SETTLE, GOD_ON, A_CLIMB, A_LAP, A_DOWN, A_LAND, A_CHECK, B_CLIMB, B_CRUISE, B_DOWN, B_LAND, B_CHECK, DONE }

var main: Expedition
var player: Player
var stream: GroupStream
var provider: GroupProvider
var planet: PlanetView
var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var phase_real := 0
var sent := false
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var start_us := Time.get_ticks_usec()
var dock := Vector3.ZERO
var dock_ll := Vector2.ZERO
var start_yaw := 0.0
var travelled := 0.0
var last_ll := Vector2.ZERO
var max_flat := 0.0
var since_log := 0.0
var target_ll := Vector2.ZERO
var target_y := 0.0
var target_node := -1
var target_local := Vector3.ZERO  # 目标在它那一群自己的平面里（看脚下的块挂好没有）
var target_where := ""
var results: Array[String] = []
var lap_shots: Array[float] = []
var mem_start := 0
var mem_peak := 0
var far_frames := 0  # 绕圈时 600 km 内有岛群的帧
var far_shown := 0  # 其中挂着至少一份低模的帧
var finished := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	_remove_tree(WORLD_DIR)  # 从没生成过起：落地那一群要当场生成
	main = SCENE.instantiate()
	provider = main.get_node("GroupProvider")
	provider.world_dir = WORLD_DIR
	get_tree().root.add_child.call_deferred(main)
	(func(): get_tree().current_scene = main).call_deferred()  # DevTools 找的是当前场景里的 Player


func _remove_tree(dir: String) -> void:
	if not DirAccess.dir_exists_absolute(dir):
		return
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		_remove_tree(dir.path_join(d))
	DirAccess.remove_absolute(dir)


func _log(line: String) -> void:
	print("god_orbit_e2e: " + line)


func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait <= 0:
		get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
		pending_shot = ""


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	pending_shot = shot_name
	shot_wait = 3


func _goto(next: Phase) -> void:
	phase = next
	phase_started = elapsed
	phase_real = Time.get_ticks_usec()
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


func _release_all() -> void:
	for a in ["ascend", "fly_down", "move_forward", "sprint"]:
		Input.action_release(a)


func _hold_altitude() -> void:
	if player.global_position.y < CRUISE_Y:
		Input.action_press("ascend")
	else:
		Input.action_release("ascend")


func _real_s() -> float:
	return (Time.get_ticks_usec() - phase_real) / 1e6


func _gc_m(a: Vector2, b: Vector2) -> float:
	var la1 := deg_to_rad(a.x)
	var la2 := deg_to_rad(b.x)
	var c := sin(la1) * sin(la2) + cos(la1) * cos(la2) * cos(deg_to_rad(b.y - a.y))
	return 6377800.0 * acos(clampf(c, -1.0, 1.0))


func _flat() -> float:
	return Vector2(player.global_position.x, player.global_position.z).length()


func _anchor_world() -> GroupWorld:
	return stream.get_group_world(stream.get_anchor_node())


func _stats() -> Dictionary:
	return stream.get_stats()


func _state_line() -> String:
	var p := player.global_position
	var s := _stats()
	var ps: Dictionary = provider.get_stats()
	return "高 %.0f km、锚 #%d（换锚 %d、规范化 %d、远离换锚 %d）、平面 %.0f km、走了 %.0f km、平着 %.0f m/s；生成 %d 群、排着 %d、撤掉 %d" % [p.y / 1000.0,
		stream.get_anchor_node(), int(s["shifts"]), int(s["rebases"]), int(s["far_shifts"]), _flat() / 1000.0, travelled / 1000.0, float(s["focus_speed"]),
		int(ps["generated"]), int(ps["queued"]), int(ps.get("dropped", 0))]


# 这一群里找一处落脚：第一个能靠的码头的站位（没有码头就离群心最近的一块地）旁边、四周 30 m 都是平地的一点（码头在岸边，差十几米就踩空了）
func _landing_in(node: int) -> bool:
	var w: GroupWorld = stream.get_group_world(node)
	if w == null or not w.is_loaded():
		return false
	var base := Vector3(NAN, NAN, NAN)
	for k in w.get_dock_count():
		if w.is_dock_berthable(k):
			base = (w.get_dock_spawn(k) as Transform3D).origin
			target_where = "码头 #%d 边" % w.get_dock_id(k)
			break
	if is_nan(base.x):
		var best := 1e18
		for i in range(-40, 41):
			for j in range(-40, 41):
				var q := Vector2(j, i) * 2000.0
				var h: float = w.get_surface_height(q.x, q.y)
				if not is_nan(h) and q.length() < best:
					best = q.length()
					base = Vector3(q.x, h, q.y)
		target_where = "离群心 %.0f km 的一块地" % (best / 1000.0)
	if is_nan(base.x):
		return false
	var spot := Vector3(NAN, NAN, NAN)
	for r in range(0, 600, 10):
		for a in (1 if r == 0 else 24):
			var q := Vector2(base.x, base.z) + Vector2(cos(TAU * a / 24.0), sin(TAU * a / 24.0)) * float(r)
			var h: float = w.get_surface_height(q.x, q.y)
			if is_nan(h):
				continue
			var flat := true
			for b in 12:
				var e := q + Vector2(cos(TAU * b / 12.0), sin(TAU * b / 12.0)) * 30.0
				var he: float = w.get_surface_height(e.x, e.y)
				if is_nan(he) or absf(he - h) > 8.0:
					flat = false
					break
			if flat:
				spot = Vector3(q.x, h, q.y)
				break
		if not is_nan(spot.x):
			break
	if is_nan(spot.x):
		return false
	var at := stream.to_anchor(node, spot)
	target_ll = planet.latlon_at(at)
	target_y = spot.y
	target_node = node
	target_local = spot
	return true


# 飞到目标上空、边走边落：平着按离目标多远给前进键的力度（离得近就轻按，停得准），差不多在头顶了就往下落；人在地上站稳了返回真
func _land_step() -> bool:
	var at := planet.flat_of_latlon(target_ll.x, target_ll.y)
	var p := player.global_position
	var d := Vector3(at.x - p.x, 0.0, at.z - p.z)
	var dist := d.length()
	var h := maxf(p.y - target_y, 0.0)
	if dist > 0.5:
		player.set_look_yaw(atan2(-d.x, -d.z))
		var fly := player.get_god_speed() * player.get_god_scale()
		var dz := InputMap.action_get_deadzone("move_forward")
		Input.action_press("move_forward", dz + (1.0 - dz) * clampf(dist / maxf(1.0, fly * 0.5), 0.0, 1.0))
	else:
		Input.action_release("move_forward")
	# 离地 1.5 km 以内先等落脚处的块（带碰撞）挂好：无头下模拟比真实快好几倍，建块跟不上 god 往下落
	var w: GroupWorld = stream.get_group_world(target_node)
	var ready := w != null and w.is_ready_at(target_local)
	if dist < maxf(3.0, 0.3 * h) and (ready or h > 1500.0):
		Input.action_press("fly_down")
	else:
		Input.action_release("fly_down")
	return player.is_on_floor() and elapsed - phase_started > 1.0


func _standing_ok(tag: String, node: int, near: Vector3, near_m: float) -> bool:
	var w := _anchor_world()
	var at := player.global_position
	var ground: float = w.get_surface_height(at.x, at.z) if w else NAN
	if stream.get_anchor_node() != node:
		_finish(false, "%s：落地后锚该是 #%d，实为 #%d" % [tag, node, stream.get_anchor_node()])
		return false
	if not player.is_on_floor() or is_nan(ground) or absf(at.y - ground) > 2.0:
		_finish(false, "%s：没站在锚群的地上（在地上 %s，离地 %.1f m）" % [tag, player.is_on_floor(), at.y - ground])
		return false
	if not w.is_ready_at(at):
		_finish(false, "%s：脚下的块没挂好" % tag)
		return false
	if near_m > 0.0 and Vector2(at.x - near.x, at.z - near.z).length() > near_m:
		_finish(false, "%s：落在离目标 %.0f m 处（该 ≤ %.0f m）" % [tag, Vector2(at.x - near.x, at.z - near.z).length(), near_m])
		return false
	if _flat() > 200000.0:
		_finish(false, "%s：人在锚群平面里 %.0f km 处（该在这一群里）" % [tag, _flat() / 1000.0])
		return false
	var hud := main.get_status_text().get_slice("\n", 0)
	if not hud.begins_with("群 #%d" % node):
		_finish(false, "%s：HUD 第一行该以「群 #%d」开头：%s" % [tag, node, hud])
		return false
	return true


func _physics_process(delta: float) -> void:
	if finished:
		return
	elapsed += delta
	var t := elapsed - phase_started
	if (Time.get_ticks_usec() - start_us) / 1e6 > TIMEOUT_REAL_S:
		_finish(false, "超时（真实 %.0f s，卡在 %s；%s）" % [TIMEOUT_REAL_S, Phase.keys()[phase], _state_line() if player else ""])
		return
	if phase > Phase.GOD_ON and phase < Phase.DONE:
		# 带窗口跑时真鼠标一动就转了视线（一圈 4 万公里，差 3° 就偏出一千公里）：飞的时候不接鼠标
		if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		var ll := planet.get_camera_latlon()
		travelled += _gc_m(last_ll, ll)
		last_ll = ll
		max_flat = maxf(max_flat, _flat())
		since_log += delta
		if since_log >= 10.0:
			since_log = 0.0
			_log("  [%s] %s" % [Phase.keys()[phase], _state_line()])
	match phase:
		Phase.SETTLE:
			if main == null or not main.is_inside_tree():
				return
			player = main.get_node("Player")
			stream = main.get_node("GroupStream")
			planet = main.get_node("PlanetView")
			var world: GroupWorld = main.get_node("World")
			if not world.is_loaded() or not planet.is_active() or not stream.is_ready() or not player.is_on_floor() or t < 1.0:
				if t > 120.0:
					_finish(false, "120 s 没在 2051 站稳")
				return
			if not provider.can_generate():
				print("god_orbit_e2e: SKIP —— 生成不了岛群：%s" % provider.get_problem())
				finished = true
				get_tree().quit(77)
				return
			dock = player.global_position
			dock_ll = planet.latlon_at(dock)
			start_yaw = player.get_look_yaw()
			_goto(Phase.GOD_ON)
		Phase.GOD_ON:
			if _once():
				_action("god_mode")
			if t < 0.5:
				return
			if not player.is_god_mode():
				_finish(false, "按 F3 god 没开")
				return
			last_ll = planet.get_camera_latlon()
			_log("主码头站稳（%s）、god 开了；升到 %.0f km" % [dock, CRUISE_Y / 1000.0])
			_goto(Phase.A_CLIMB)
		Phase.A_CLIMB, Phase.B_CLIMB:
			Input.action_press("ascend")
			Input.action_press("sprint")
			if player.global_position.y >= CRUISE_Y:
				Input.action_release("ascend")
				_shot("a1_top" if phase == Phase.A_CLIMB else "b1_top")
				mem_start = OS.get_static_memory_usage()
				mem_peak = mem_start
				travelled = 0.0
				player.set_look_yaw(start_yaw)
				_goto(Phase.A_LAP if phase == Phase.A_CLIMB else Phase.B_CRUISE)
		Phase.A_LAP:
			Input.action_press("move_forward")
			Input.action_press("sprint")
			_hold_altitude()
			mem_peak = maxi(mem_peak, OS.get_static_memory_usage())
			if mem_peak - mem_start > MEM_GROWTH_MAX:
				_finish(false, "绕行星时内存涨了 %.0f MB（起飞前 %.0f MB）：远处岛群的低模又在攒了？" % [(mem_peak - mem_start) / 1e6, mem_start / 1e6])
				return
			var far: FarGroups = main.get_node("FarGroups")
			if far.get_wanted_count() > 0:
				far_frames += 1
				far_shown += 1 if far.get_group_count() > 0 else 0
			if _flat() > HALF_M + 10000.0:
				_finish(false, "绕行星时人在锚群平面里到了 %.0f km（过了对跖点 %.0f km 还没规范化）" % [_flat() / 1000.0, HALF_M / 1000.0])
				return
			for mark in [10000000.0, 19500000.0, 30000000.0]:
				if travelled >= mark and travelled - 300000.0 < mark and lap_shots.find(mark) < 0:
					lap_shots.append(mark)
					_shot("a15_%dkm" % int(mark / 1000.0))
			if travelled > 1.1 * LAP_M:
				_finish(false, "绕了 %.0f km 还没回到主码头 100 km 以内（%s）" % [travelled / 1000.0, _state_line()])
				return
			# 一圈快完了、回到主码头 100 km 以内就停（松手后还要滑出四五十公里）
			if travelled > 0.75 * LAP_M and _gc_m(planet.get_camera_latlon(), dock_ll) < 100000.0:
				_release_all()
				var s := _stats()
				var line := "A：%.0f km 高绕了一圈（%.0f km）：锚照旧 #%d，过对跖点规范化 %d 回，平面坐标最远 %.0f km（对跖点 %.0f km）；内存起飞前 %.0f MB、最多多了 %.0f MB；600 km 内有岛群的 %d 帧里 %.0f%% 挂着低模" % [CRUISE_Y / 1000.0,
					travelled / 1000.0, stream.get_anchor_node(), int(s["rebases"]), max_flat / 1000.0, HALF_M / 1000.0, mem_start / 1e6, (mem_peak - mem_start) / 1e6, far_frames, 100.0 * far_shown / maxf(1.0, far_frames)]
				results.append(line)
				_log(line)
				if far_frames > 0 and float(far_shown) / far_frames < 0.7:
					_finish(false, "绕行星时 600 km 内有岛群的 %d 帧里只有 %.0f%% 挂着低模（该 ≥ 70%%）" % [far_frames, 100.0 * far_shown / far_frames])
					return
				if int(s["rebases"]) < 1 or stream.get_anchor_node() != HOME:
					_finish(false, "绕一圈该规范化过（%d 回）、锚照旧是 #%d（实为 #%d）" % [int(s["rebases"]), HOME, stream.get_anchor_node()])
					return
				_shot("a2_lap")
				_goto(Phase.A_DOWN)
		Phase.A_DOWN, Phase.B_DOWN:
			if t < 2.0:
				return  # 先停下
			if player.global_position.y > SEEK_Y:
				Input.action_press("fly_down")
				return
			Input.action_release("fly_down")
			if phase == Phase.A_DOWN:
				# 一圈回来：就在 2051 上空，离主码头没多远（平面坐标规范化过）
				var off := _gc_m(planet.get_camera_latlon(), dock_ll)
				_log("A：降到 %.0f km，离主码头 %.1f km、人在平面 %.1f km 处；%s" % [player.global_position.y / 1000.0, off / 1000.0, _flat() / 1000.0, _state_line()])
				if off > 300000.0 or _flat() > 300000.0:
					_finish(false, "一圈回来离主码头 %.0f km、平面坐标 %.0f km：没回到 2051 上空" % [off / 1000.0, _flat() / 1000.0])
					return
				if not _landing_in(HOME):
					_finish(false, "A：2051 找不到落脚的地方")
					return
				_shot("a3_20km")
				_goto(Phase.A_LAND)
				return
			# B：停下往下落——远离锚群、慢下来了：换到最近的一群（没有包就当场生成）
			if stream.get_anchor_node() == HOME or stream.is_shift_pending():
				if _real_s() > 180.0:
					_finish(false, "B：停在 %.0f km 外的高空 180 s（真实）没换锚（%s）" % [_gc_m(planet.get_camera_latlon(), dock_ll) / 1000.0, _state_line()])
				return
			var node := stream.get_anchor_node()
			var s := _stats()
			var st: Dictionary = stream.get_group_state(node)
			var line := "B：飞到 %.0f km 外停下往下落，%.1f s（真实）后锚换到了 #%d（远离锚群换锚 %d 回；它的陆地离脚下 %.0f km）；人在它的平面里 %.0f km 处" % [
				_gc_m(planet.get_camera_latlon(), dock_ll) / 1000.0, _real_s(), node, int(s["far_shifts"]), float(st.get("land_km", -1.0)), _flat() / 1000.0]
			results.append(line)
			_log(line)
			if int(s["far_shifts"]) < 1:
				_finish(false, "B：锚换了，可不是「远离锚群换到近的群」换的（%s）" % s)
				return
			if _flat() > 1000000.0:
				_finish(false, "B：换锚后人在新锚群平面里 %.0f km 处（该在一千公里以内）" % (_flat() / 1000.0))
				return
			if not _landing_in(node):
				if _real_s() > 60.0:
					_finish(false, "B：群 #%d 找不到落脚的地方" % node)
				return
			_log("B：落到群 #%d 的%s" % [node, target_where])
			_shot("b3_reanchored")
			_goto(Phase.B_LAND)
		Phase.A_LAND, Phase.B_LAND:
			if _land_step():
				_release_all()
				_goto(Phase.A_CHECK if phase == Phase.A_LAND else Phase.B_CHECK)
			elif t > 400.0:
				_finish(false, "%s：400 s 没落到%s（%s）" % ["A" if phase == Phase.A_LAND else "B", target_where, _state_line()])
		Phase.A_CHECK, Phase.B_CHECK:
			if t < 3.0:
				return
			var tag := "A" if phase == Phase.A_CHECK else "B"
			var near := planet.flat_of_latlon(target_ll.x, target_ll.y)
			if not _standing_ok(tag, target_node, near, 30.0):
				return
			var line := "%s：站在群 #%d 的%s上（离目标 %.0f m），人在锚群平面 %.1f km 处；%s" % [tag, target_node, target_where,
				Vector2(player.global_position.x - near.x, player.global_position.z - near.z).length(), _flat() / 1000.0, _state_line()]
			results.append(line)
			_log(line)
			_shot("%s4_landed" % tag.to_lower())
			if phase == Phase.A_CHECK:
				var back := Vector2(player.global_position.x - dock.x, player.global_position.z - dock.z).length()
				if back > 1000.0:
					_finish(false, "A：落在离起飞的主码头 %.0f m 处（该在 1 km 内）" % back)
					return
				_goto(Phase.B_CLIMB)
			else:
				_goto(Phase.DONE)
		Phase.B_CRUISE:
			Input.action_press("move_forward")
			Input.action_press("sprint")
			_hold_altitude()
			if travelled >= FAR_M:
				_release_all()
				_log("B：飞了 %.0f km：%s" % [travelled / 1000.0, _state_line()])
				_shot("b2_stop")
				_goto(Phase.B_DOWN)
		Phase.DONE:
			if pending_shot.is_empty():
				_finish(true, "绕行星一圈回来落在主码头、飞到 3,000 km 外停下换锚到近的群并落地")


func _finish(ok: bool, message: String) -> void:
	if finished:
		return
	finished = true
	_release_all()
	for r in results:
		print("god_orbit_e2e: " + r)
	print("god_orbit_e2e: %s —— %s（模拟 %.0f s，真实 %.0f s）" % ["PASS" if ok else "FAIL", message, elapsed, (Time.get_ticks_usec() - start_us) / 1e6])
	get_tree().quit(0 if ok else 1)
