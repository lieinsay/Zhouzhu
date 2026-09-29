extends Node
## 散布的主线程开销：开局在主码头站 20 s（村子、散布一起往外铺），再开 god 模式顺着一条线飞 20 s，报每帧的耗时（最慢、P99、超过 50 ms 的帧数）。
##
##   Godot --path game res://tools/scatter_perf.tscn            # 不加 --fixed-fps：量的是真帧时

const SCENE := preload("res://world_seed42.tscn")

var main: Expedition
var world: World
var player: Player
var scatter: ScatterStream
var elapsed := 0.0
var last := 0
var frames: Array[float] = []
var phase := 0
var slow: Array = []  # [秒, 毫秒, 散布块, 立着的]


func _ready() -> void:
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	scatter = main.get_node("ScatterStream")
	last = Time.get_ticks_usec()


func _report(name: String) -> void:
	var sorted := frames.duplicate()
	sorted.sort()
	var n := sorted.size()
	var total := 0.0
	for f in sorted:
		total += f
	print("scatter_perf: %s  %d 帧  平均 %.1f ms  P99 %.1f ms  最慢 %.1f ms  >50 ms 的 %d 帧  散布块 %d  立着 %d  树干 %d" % [
		name, n, total / max(1, n), sorted[int(n * 0.99)] if n > 0 else 0.0, sorted[n - 1] if n > 0 else 0.0,
		sorted.filter(func(f): return f > 50.0).size(), scatter.get_chunk_count(), scatter.get_item_count(), scatter.get_solid_count()])
	for s in slow.slice(0, 12):
		print("  慢帧 %.1f s：%.0f ms（块 %d、立着 %d）" % s)
	frames.clear()
	slow.clear()


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var ms := (now - last) / 1000.0
	last = now
	elapsed += _delta
	if not world.is_loaded():
		return
	frames.append(ms)
	if ms > 50.0:
		slow.append([elapsed, ms, scatter.get_chunk_count(), scatter.get_item_count()])
	if phase == 0 and elapsed > 20.0:
		_report("主码头站 20 s")
		phase = 1
		player.set_god_mode(true)
	elif phase == 1:
		# 往岛里飞：30 m/s，离地 40 m
		var p := player.global_position
		var dir := Vector3(-p.x, 0, -p.z).normalized()
		p += dir * 30.0 * _delta
		var h := world.get_surface_height(p.x, p.z)
		if not is_nan(h):
			p.y = h + 40.0
		player.global_position = p
		if elapsed > 40.0:
			_report("往岛里飞 20 s")
			get_tree().quit(0)
