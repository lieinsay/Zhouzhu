extends Node
## 流式加载的帧时实测：开局在主码头站定，再开 god 模式离地 40 m 顺着一串大村（村004 → 村006 → 村003 → 村002）飞过去——
## 先 150 m/s（god 加速）飞一段，再 30 m/s 飞一段。每段报帧时（平均、P99、最慢、超过 50 / 100 ms 的帧数），
## 列出最慢的十几帧和那一帧主线程上各流式子系统花了多少（World.get_stream_frame：地面块、水面、码头、村子、村民、采场、散布……），
## 以及各子系统的累计与单帧最多。改前改后走同一条路线对比。
## 站定分两段：先等 15 s（载入、村子挂齐），再量 20 s 的稳态；每段末尾报进程内存（工作集、私有提交）与显存。
## `--high=<秒>`：飞完再升到主码头上空 50 km（god），相机斜着往下看、慢慢转一圈，量高空的帧时（行星计划 P1）。
##
##   Godot --path game res://tools/stream_perf.tscn               # 带窗口：帧时含渲染
##   Godot --headless --path game res://tools/stream_perf.tscn    # 无头：只有主线程的活
##
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
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	scatter = main.get_node("ScatterStream")
	sites = main.get_node("SiteStream")
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
