extends Node
## 流式加载的帧时实测：开局在主码头站定，再开 god 模式离地 40 m 顺着一串大村（村004 → 村006 → 村003 → 村002）飞过去——
## 先 150 m/s（god 加速）飞一段，再 30 m/s 飞一段。每段报帧时（平均、P99、最慢、超过 50 / 100 ms 的帧数），
## 列出最慢的十几帧和那一帧主线程上各流式子系统花了多少（World.get_stream_frame：地面块、水面、码头、村子、村民、采场、散布……），
## 以及各子系统的累计与单帧最多。改前改后走同一条路线对比。
## 站定分两段：先等 15 s（载入、村子挂齐），再量 20 s 的稳态；每段末尾报进程内存（工作集、私有提交）与显存。
## `--high=<秒>`：飞完再升到主码头上空 50 km（god），相机斜着往下看、慢慢转一圈，量高空的帧时（行星计划 P1）。
## `--orbit=<秒>`（行星计划 P2，单独跑，不走上面几段）：邑治的村口站定、等行星贴图烘好，拍一张「起飞前」；开发者的轨道相机（OrbitCamera）
##   从离地 2 m 按对数匀速升到 2 万公里（<秒> 秒，默认 60），相机从平视渐渐转到正对着星；顶上停 2 s，再同样落回村口；
##   升、落两段各报帧时，落回来站 3 s 再拍一张「落回后」（--shots=<目录>，两张的差由外面比）。
##
##   Godot --path game res://tools/stream_perf.tscn               # 带窗口：帧时含渲染
##   Godot --headless --path game res://tools/stream_perf.tscn    # 无头：只有主线程的活
##
## `--voyage=<秒>`（行星计划 P3，单独跑）：主码头站定 15 s 报内存；god 模式挪到 2051 西南岸外 4.6 km 的云海上空 1800 m，
##   以 150 m/s 朝 2050 飞 <秒> 秒（默认 240：约 100 s 时过换锚线，再飞到 2050 岸边），再原路飞回同样久（过换锚线回到 2051）；
##   两段各报帧时（换锚那一帧与下一帧另报）与进程内存。
##
## `--sea=<秒>`（行星计划 P7，单独跑）：主码头站定 15 s；一架自由相机摆到主码头往外（背着主峰）的开阔云海上空，钟停在 10 点、天气覆盖成晴——
##   云带顶上 150 m、云带里（−5.6 km）、云底下（−6.5 km）、海面上 20 m、风暴里的海面各停 <秒> 秒（相机慢慢转一圈），
##   再从 −4.7 km 匀速降到海面上 20 m、升回去（各 <秒> 秒）；每段报帧时。
## 不加 --fixed-fps：量的是真帧时（带窗口时加 --disable-vsync，免得被刷新率卡住）。
## 末尾加 `-- --fast=<秒> --slow=<秒> --stand=<秒> --high=<秒>` 改各段多久（默认 60 / 30 / 20 / 0）。

const SCENE := preload("res://world_2051.tscn")
const ROUTE := [Vector2(3050, -26550), Vector2(150, -27450), Vector2(-7750, -28050), Vector2(-9150, -23450), Vector2(-3050, -18000)]
const SETTLE_S := 15.0
const HEIGHT_M := 40.0
const SLOW_MS := 50.0

var main: Expedition
var world: World
var player: Player
var scatter: ScatterStream
var sites: SiteStream
var fast_s := 60.0
var slow_s := 30.0
var stand_s := 20.0
var high_s := 0.0
var high_m := 50000.0
var orbit_s := 0.0
var voyage_s := 0.0
var sea_s := 0.0
var sea_cam: Camera3D = null
var sea_point := Vector2.ZERO
var sea_heading := 0.0
var atmosphere = null
const VOYAGE_FROM := Vector3(-21612.0, 1800.0, 57039.0)  # 2051 平面：西南岸外 4.6 km、朝 2050 主码头的航线上
const VOYAGE_DIR := Vector3(-0.3739, 0.0, 0.9275)
const VOYAGE_SPEED := 150.0
var stream = null  # GroupStream
var shifts_seen := 0
var shift_watch := 0
var shift_ms: Array[float] = []
var shots_dir := ""
var orbit = null  # OrbitCamera
var globe = null  # PlanetGlobe
var gate := Vector3.ZERO
var gate_ll := Vector2.ZERO
var gate_heading := 0.0
const ORBIT_TOP_M := 2.0e7
const SEAT := Vector2(10350, -21850)  # 邑治
var dock := Vector3.ZERO

var elapsed := 0.0
var phase_started := 0.0
var phase := 0  # 0 等载入 1 站定量稳态 2 快飞 3 慢飞 4 高空
var last := 0
var frames: Array[float] = []
var slow: Array = []  # [秒, 毫秒, 分项]
var leg := 0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--fast="):
			fast_s = float(arg.trim_prefix("--fast="))
		elif arg.begins_with("--slow="):
			slow_s = float(arg.trim_prefix("--slow="))
		elif arg.begins_with("--stand="):
			stand_s = float(arg.trim_prefix("--stand="))
		elif arg.begins_with("--high="):
			high_s = float(arg.trim_prefix("--high="))
		elif arg.begins_with("--orbit="):
			orbit_s = float(arg.trim_prefix("--orbit="))
		elif arg.begins_with("--voyage="):
			voyage_s = float(arg.trim_prefix("--voyage="))
		elif arg.begins_with("--sea="):
			sea_s = float(arg.trim_prefix("--sea="))
		elif arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	scatter = main.get_node("ScatterStream")
	sites = main.get_node("SiteStream")
	globe = main.get_node_or_null("PlanetGlobe")
	stream = main.get_node_or_null("GroupStream")
	if orbit_s > 0.0 or sea_s > 0.0:
		GameClock.restart(10.0)
		GameClock.paused = true
	atmosphere = main.get_node_or_null("Atmosphere")
	last = Time.get_ticks_usec()


func _breakdown(frame: int) -> Dictionary:
	# 这一帧的慢可能记在上一帧号或这一帧号上（物理帧与处理帧的编号先后）：两帧的分项合起来看
	var out := {}
	for f in [frame - 1, frame]:
		var d: Dictionary = world.get_stream_frame(f)
		for k in d:
			out[k] = out.get(k, 0.0) + d[k]
	return out


func _format(parts: Dictionary) -> String:
	var keys := parts.keys()
	keys.sort_custom(func(a, b): return parts[a] > parts[b])
	var s := ""
	for k in keys:
		if parts[k] >= 0.5:
			s += "  %s %.1f" % [k, parts[k]]
	return s


func _report(name: String) -> void:
	var sorted := frames.duplicate()
	sorted.sort()
	var n := sorted.size()
	var total := 0.0
	for f in sorted:
		total += f
	print("stream_perf: %s  %d 帧  平均 %.1f ms  P99 %.1f ms  最慢 %.1f ms  >50 ms %d 帧  >100 ms %d 帧" % [
		name, n, total / max(1, n), sorted[int(n * 0.99)] if n > 0 else 0.0, sorted[n - 1] if n > 0 else 0.0,
		sorted.filter(func(f): return f > 50.0).size(), sorted.filter(func(f): return f > 100.0).size()])
	print("  此刻：地面块 %d、散布块 %d、立着 %d、树干 %d、村 %d + 散户 %d" % [world.get_tile_count(), scatter.get_chunk_count(),
		scatter.get_item_count(), scatter.get_solid_count(), sites.get_built_villages().size(), sites.get_built_hamlet_count()])
	var worst := slow.duplicate()
	worst.sort_custom(func(a, b): return a[1] > b[1])
	for s in worst.slice(0, 14):
		print("  慢帧 %.1f s：%.0f ms ←%s" % [s[0], s[1], _format(s[2])])
	var totals: Dictionary = world.get_stream_totals()
	var line := "  累计（毫秒 / 单帧最多）："
	for k in totals:
		var row: Dictionary = totals[k]
		if row["total_ms"] > 0.0:
			line += "  %s %.0f / %.1f" % [k, row["total_ms"], row["max_ms"]]
	print(line)
	_memory()
	frames.clear()
	slow.clear()
	world.reset_stream_meter()


# 进程内存：工作集（tasklist）、私有提交（PowerShell），外加显存。要几百毫秒，量完把帧计时重新起头
func _memory() -> void:
	var pid := OS.get_process_id()
	var out := []
	var ws := "?"
	if OS.execute("tasklist", ["/FI", "PID eq %d" % pid, "/FO", "CSV", "/NH"], out) == 0 and out.size() > 0:
		var cols: PackedStringArray = String(out[0]).strip_edges().split("\",\"")
		if cols.size() >= 5:
			ws = cols[4].replace("\"", "")
	out = []
	var priv := "?"
	if OS.execute("powershell", ["-NoProfile", "-Command", "(Get-Process -Id %d).PrivateMemorySize64" % pid], out) == 0 and out.size() > 0:
		priv = "%.0f MB" % (float(String(out[0]).strip_edges()) / 1048576.0)
	var video := Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0
	var stat := Performance.get_monitor(Performance.MEMORY_STATIC) / 1048576.0
	print("  内存：工作集 %s、私有提交 %s、Godot 静态 %.0f MB、显存 %.0f MB" % [ws, priv, stat, video])
	last = Time.get_ticks_usec()


func _fly(speed: float, delta: float) -> void:
	var p := player.global_position
	var target: Vector2 = ROUTE[min(leg, ROUTE.size() - 1)]
	var to := Vector2(target.x - p.x, target.y - p.z)
	if to.length() < speed * delta * 2.0 and leg < ROUTE.size() - 1:
		leg += 1
	var dir := to.normalized()
	p.x += dir.x * speed * delta
	p.z += dir.y * speed * delta
	var h := world.get_surface_height(p.x, p.z)
	if not is_nan(h):
		p.y = h + HEIGHT_M
	player.global_position = p


func _process(delta: float) -> void:
	var now := Time.get_ticks_usec()
	var ms := (now - last) / 1000.0
	last = now
	elapsed += delta
	if not world.is_loaded():
		return
	if orbit_s > 0.0:
		_orbit_process(delta, ms)
		return
	if voyage_s > 0.0:
		_voyage_process(delta, ms)
		return
	if sea_s > 0.0:
		_sea_process(delta, ms)
		return
	frames.append(ms)
	if ms > SLOW_MS:
		slow.append([elapsed, ms, _breakdown(Engine.get_process_frames() - 1)])
	if phase == 0 and elapsed > SETTLE_S:
		_report("主码头等载入 %d s" % int(SETTLE_S))
		phase = 1
		phase_started = elapsed
		dock = player.global_position
	elif phase == 1:
		if elapsed - phase_started > stand_s:
			_report("主码头站定（稳态）%d s" % int(stand_s))
			phase = 2
			phase_started = elapsed
			player.set_god_mode(true)
	elif phase == 2:
		_fly(150.0, delta)
		if elapsed - phase_started > fast_s:
			_report("150 m/s 飞 %d s" % int(fast_s))
			phase = 3
			phase_started = elapsed
	elif phase == 3:
		_fly(30.0, delta)
		if elapsed - phase_started > slow_s:
			_report("30 m/s 飞 %d s" % int(slow_s))
			if high_s <= 0.0:
				get_tree().quit(0)
				return
			phase = 4
			phase_started = elapsed
			player.global_position = dock + Vector3(0, high_m, 0)
			world.prepare_at(player.global_position)
			frames.clear()
			last = Time.get_ticks_usec()
	elif phase == 4:
		# 高空：人钉在主码头上空，相机往下斜 35°、一圈 60 s 慢慢转；前 5 s 不计（远景在后台建）
		player.global_position = dock + Vector3(0, high_m, 0)
		player.set_look_yaw(TAU * (elapsed - phase_started) / 60.0)
		var cam := player.get_camera()
		if cam:
			cam.get_parent().get_parent().rotation.x = -0.6
		if elapsed - phase_started < 5.0:
			frames.clear()
		if elapsed - phase_started > high_s + 5.0:
			_report("主码头上空 %.0f km %d s" % [high_m / 1000.0, int(high_s)])
			get_tree().quit(0)


# ---- 行星计划 P2：从村口升到两万公里、再落回村口
func _orbit_place(t: float) -> void:
	# t ∈ [0, 1]：离地高按对数匀速（2 m → 2 万公里），俯角跟着高度从平视（−3°）转到正对着星（−90°）
	var lg := lerpf(log(2.0), log(ORBIT_TOP_M), t)
	var clearance := exp(lg)
	var k := clampf((lg - log(2000.0)) / (log(2.0e6) - log(2000.0)), 0.0, 1.0)
	var pitch := lerpf(-3.0, -90.0, k * k * (3.0 - 2.0 * k))
	orbit.set_orbit_state(gate_ll.x, gate_ll.y, gate.y - 1.0 + clearance, gate_heading, pitch)


func _orbit_shot(name: String) -> void:
	if shots_dir.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(shots_dir)
	var path := shots_dir.path_join(name + ".png")
	get_viewport().get_texture().get_image().save_png(path)
	print("stream_perf: %s（离地 %.1f m）" % [path, orbit.get_clearance()])


func _orbit_process(delta: float, ms: float) -> void:
	var planet = main.get_node_or_null("PlanetView")
	if phase == 0:
		if planet == null or not planet.is_active():
			return
		if orbit == null:
			var g := SEAT + Vector2(0.35, 1.0).normalized() * 350.0
			var h := world.get_surface_height(g.x, g.y)
			gate = Vector3(g.x, h + 1.0, g.y)
			gate_ll = planet.latlon_at(Vector3(g.x, 0.0, g.y))
			var to_seat := SEAT - g
			gate_heading = rad_to_deg(atan2(to_seat.x, -to_seat.y))
			var behind := g - to_seat.normalized() * 25.0
			player.global_position = Vector3(behind.x, world.get_surface_height(behind.x, behind.y) + 1.0, behind.y)
			world.prepare_at(player.global_position)
			orbit = ClassDB.instantiate("OrbitCamera")
			main.add_child(orbit)
			orbit.fov = 70.0
			_orbit_place(0.0)
			orbit.current = true
			main.get_node("Atmosphere").set_weather_override("clear", 0.0)
		_orbit_place(0.0)
		if elapsed > SETTLE_S and (globe == null or globe.is_ready()):
			if globe != null:
				var gs: Dictionary = globe.get_state()
				print("stream_perf: 行星贴图 %s（后台 %.0f ms、墙钟 %.0f ms、建纹理 %.0f ms、大气表 %.0f ms），等到 %.1f s" % [globe.is_ready(), gs["bake_cpu_ms"], gs["bake_wall_ms"],
					gs["upload_ms"], gs["lut_ms"], elapsed])
			_orbit_shot("orbit_before")
			_memory()  # 存图与量内存要几百毫秒：量完把帧计时重新起头
			phase = 1
			phase_started = elapsed
			frames.clear()
			slow.clear()
			world.reset_stream_meter()
		return
	frames.append(ms)
	if ms > SLOW_MS:
		slow.append([elapsed, ms, _breakdown(Engine.get_process_frames() - 1)])
	var t := (elapsed - phase_started) / orbit_s
	if phase == 1:
		_orbit_place(minf(t, 1.0))
		if t >= 1.0:
			_report("升到两万公里 %d s" % int(orbit_s))
			phase = 2
			phase_started = elapsed
	elif phase == 2:
		_orbit_place(1.0)
		if elapsed - phase_started > 2.0:
			frames.clear()
			slow.clear()
			phase = 3
			phase_started = elapsed
	elif phase == 3:
		_orbit_place(maxf(0.0, 1.0 - t))
		if t >= 1.0:
			_report("落回村口 %d s" % int(orbit_s))
			phase = 4
			phase_started = elapsed
	elif phase == 4:
		_orbit_place(0.0)
		if elapsed - phase_started > 3.0:
			_orbit_shot("orbit_after")
			get_tree().quit(0)


# ---- 行星计划 P3：150 m/s 从 2051 飞过换锚线到 2050，再飞回来
func _voyage_process(delta: float, ms: float) -> void:
	if phase == 0:
		if stream == null or not stream.is_ready():
			return
		frames.append(ms)
		if elapsed > SETTLE_S:
			_report("主码头站定 %d s" % int(SETTLE_S))
			player.set_god_mode(true)
			player.global_position = VOYAGE_FROM
			phase = 1
			phase_started = elapsed
			last = Time.get_ticks_usec()
		return
	frames.append(ms)
	if ms > SLOW_MS:
		slow.append([elapsed, ms, {}])
	# 换锚：Expedition 在物理帧开头换；这一帧、下一帧的帧时（两次 _process 之间）另记
	if main.get_shift_count() > shifts_seen:
		shifts_seen = main.get_shift_count()
		shift_watch = 3
		shift_ms.clear()
		print("stream_perf: 换锚 #%d 在 %.1f s（锚 → %d，编排那一步 %.2f ms）" % [shifts_seen, elapsed, stream.get_anchor_node(), main.get_last_shift_ms()])
	elif shift_watch > 0:
		shift_ms.append(ms)
		shift_watch -= 1
		if shift_watch == 0:
			print("stream_perf: 换锚之后三帧的帧时 %s ms" % [shift_ms])
	# 目标点按 2051 的平面定，每帧换到此刻的锚群平面（换锚之后照样朝同一个真方向飞）
	var sign := 1.0 if phase == 1 else -1.0
	var target_2051: Vector3 = VOYAGE_FROM + VOYAGE_DIR * (sign * 400000.0)
	var target: Vector3 = stream.to_anchor(2051, target_2051)
	var p := player.global_position
	var to := Vector3(target.x - p.x, 0.0, target.z - p.z).normalized()
	player.global_position = Vector3(p.x + to.x * VOYAGE_SPEED * delta, VOYAGE_FROM.y, p.z + to.z * VOYAGE_SPEED * delta)
	player.velocity = Vector3.ZERO
	if elapsed - phase_started > voyage_s:
		var names := {1: "去程（2051 → 2050）", 2: "回程（2050 → 2051）"}
		_voyage_report("150 m/s %s %d s" % [names[phase], int(voyage_s)])
		if phase == 2:
			get_tree().quit(0)
			return
		phase = 2
		phase_started = elapsed
		last = Time.get_ticks_usec()


func _voyage_report(name: String) -> void:
	var sorted := frames.duplicate()
	sorted.sort()
	var n := sorted.size()
	var total := 0.0
	for f in sorted:
		total += f
	print("stream_perf: %s  %d 帧  平均 %.1f ms  P99 %.1f ms  最慢 %.1f ms  >50 ms %d 帧" % [
		name, n, total / max(1, n), sorted[int(n * 0.99)] if n > 0 else 0.0, sorted[n - 1] if n > 0 else 0.0, sorted.filter(func(f): return f > 50.0).size()])
	var worst := slow.duplicate()
	worst.sort_custom(func(a, b): return a[1] > b[1])
	for s in worst.slice(0, 8):
		print("  慢帧 %.1f s：%.0f ms" % [s[0], s[1]])
	var line := "  各群："
	for node in stream.get_group_nodes():
		var st: Dictionary = stream.get_group_state(node)
		line += "  %d 档 %d 离陆地 %.1f km 地面块 %d" % [node, int(st["tier"]), float(st["land_km"]), int(st["tiles"])]
	print(line + "  锚 %d" % stream.get_anchor_node())
	_memory()
	frames.clear()
	slow.clear()


# ---------------------------------------------------------------- 云带之下（行星计划 P7）

# 从 from 起朝 away 走，找一处 2.5 km 内没有地的开阔云海，再往外 3 km（同 planet_shots、sea_descent_e2e）
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


# [名字, 起始 y, 结束 y, 天气]
# 高度同 src/planet_frame.h 的 VerticalDatum：云带顶 −5000、云底 −6200、海面 −6800（浮高计划 F4 起整体下移 1200 m）
const SEA_LEGS := [["云带顶上 150 m", -4850.0, -4850.0, "clear"], ["云带里 −5.6 km", -5600.0, -5600.0, "clear"], ["云底下 −6.5 km", -6500.0, -6500.0, "clear"],
	["海面上 20 m", -6780.0, -6780.0, "clear"], ["风暴里的海面 20 m", -6780.0, -6780.0, "storm"], ["从 −4.7 km 降到海面", -4700.0, -6780.0, "clear"],
	["从海面升回 −4.7 km", -6780.0, -4700.0, "clear"]]


func _sea_process(delta: float, ms: float) -> void:
	if phase == 0:
		if elapsed < SETTLE_S:
			return
		# 开阔云海：主码头背着主峰往外
		var berth: Transform3D = main.get_origin_berth()
		var dock2 := Vector2(berth.origin.x, berth.origin.z)
		var peak := Vector2.ZERO
		var peak_h := -1e9
		for i in range(-60, 61):
			for j in range(-60, 61):
				var q := Vector2(j, i) * 1000.0
				var h := world.get_surface_height(q.x, q.y)
				if not is_nan(h) and h > peak_h:
					peak_h = h
					peak = q
		var away := (dock2 - peak).normalized()
		sea_point = _open_sea(dock2, away)
		sea_heading = atan2(-away.x, -away.y)
		sea_cam = Camera3D.new()
		sea_cam.fov = 60.0
		sea_cam.far = 60000.0
		add_child(sea_cam)
		sea_cam.current = true
		if atmosphere != null:
			atmosphere.override_fade = 0.05
		print("stream_perf: 云带之下——开阔云海 %s（离主码头 %.1f km）" % [sea_point, (sea_point - dock2).length() / 1000.0])
		phase = 1
		leg = 0
		phase_started = elapsed
		frames.clear()
		slow.clear()
		_sea_leg_start()
		return
	if leg >= SEA_LEGS.size():
		get_tree().quit(0)
		return
	var in_leg := elapsed - phase_started
	var l: Array = SEA_LEGS[leg]
	var k := clampf(in_leg / sea_s, 0.0, 1.0)
	var y: float = lerpf(float(l[1]), float(l[2]), k)
	sea_cam.global_position = Vector3(sea_point.x, y, sea_point.y)
	sea_cam.rotation = Vector3(deg_to_rad(-6.0), sea_heading + in_leg * TAU / maxf(1.0, sea_s), 0.0)
	if in_leg > 1.0:  # 换段那一秒不算（天气覆盖渐入、相机挪过去）
		frames.append(ms)
		if ms > SLOW_MS:
			slow.append([elapsed, ms, _breakdown(Engine.get_process_frames() - 1)])
	if in_leg >= sea_s + 1.0:
		_report(str(l[0]))
		leg += 1
		phase_started = elapsed
		if leg < SEA_LEGS.size():
			_sea_leg_start()


func _sea_leg_start() -> void:
	var l: Array = SEA_LEGS[leg]
	if atmosphere != null:
		atmosphere.set_weather_override(str(l[3]), 0.0)
	sea_cam.global_position = Vector3(sea_point.x, float(l[1]), sea_point.y)
