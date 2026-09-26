extends Node
## 地貌交界目视检查：按几对地表类（林地 | 可耕地、林地 | 灌丛、灌丛 | 草坡……）在 world_2051 里找离出发码头最近的交界，
## 每处从人眼、低空斜看、正上俯看各拍一张（不做断言，只存图）。看地皮的颜色与树、灌丛、苇是不是一起渐变。
##
##   Godot --path game --fixed-fps 60 res://tools/cover_shots.tscn -- --shots=<目录>
##
## 找交界：先在码头周围 2.5 km 每 50 m 问一次 World.get_cover_at 的主类，相邻两点主类不同就是一处交界；
## 近处没有的那一对再到 25 km 内每 250 m 粗找。找到后沿那两点二分到林地（前一类）份额一半处，方向从前一类指向后一类。

const SCENE := preload("res://world_2051.tscn")
const PAIRS := [
	["林地", "可耕地"], ["林地", "灌丛"], ["灌丛", "草坡"], ["草坡", "可耕地"], ["灌丛", "可耕地"],
	["可耕地", "湿地"], ["崖缘", "林地"], ["裸岩", "灌丛"], ["裸岩", "林地"],
]

var main: Expedition
var world: World
var player: Player
var camera: Camera3D
var shots_dir := ""
var views: Array = []  # [名字, 人的位置, 相机位置, 相机看向]
var index := -1
var wait := 0.0
var busy := false
var elapsed := 0.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
	if shots_dir.is_empty():
		shots_dir = OS.get_user_data_dir().path_join("cover_shots")
	DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1600, 900)
	GameClock.restart(10.0)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	camera = Camera3D.new()
	camera.far = 60000.0
	camera.fov = 60.0
	add_child(camera)


## 在 p_center 周围 p_radius 内每 p_step 米问一次主类，把还没找到的那几对里离 p_center 最近的交界记进 r_found（名字 → [距离, 前一类的点, 后一类的点]）
func _scan(p_center: Vector2, p_radius: float, p_step: float, r_found: Dictionary) -> void:
	var n := int(p_radius * 2.0 / p_step) + 1
	var names: Array = []
	for i in range(n):
		var row: Array = []
		for j in range(n):
			var p := p_center + Vector2(-p_radius + j * p_step, -p_radius + i * p_step)
			var cover: Dictionary = world.get_cover_at(p.x, p.y)
			row.append("" if cover.is_empty() or is_nan(world.get_surface_height(p.x, p.y)) else String(cover["name"]))
		names.append(row)
	for i in range(n):
		for j in range(n):
			var a: String = names[i][j]
			if a.is_empty():
				continue
			for d: Vector2i in [Vector2i(0, 1), Vector2i(1, 0)]:
				if i + d.y >= n or j + d.x >= n:
					continue
				var b: String = names[i + d.y][j + d.x]
				if b.is_empty() or b == a:
					continue
				var pa := p_center + Vector2(-p_radius + j * p_step, -p_radius + i * p_step)
				var pb := pa + Vector2(d) * p_step
				for pair: Array in PAIRS:
					var key := "%s_%s" % [pair[0], pair[1]]
					var first: Vector2
					var second: Vector2
					if a == pair[0] and b == pair[1]:
						first = pa
						second = pb
					elif a == pair[1] and b == pair[0]:
						first = pb
						second = pa
					else:
						continue
					var dist := (pa - p_center).length()
					if not r_found.has(key) or float(r_found[key][0]) > dist:
						r_found[key] = [dist, first, second]


## 沿 p_a（前一类）→ p_b 二分到前一类的份额掉到一半处
func _refine(p_a: Vector2, p_b: Vector2, p_class: int) -> Vector2:
	var lo := p_a
	var hi := p_b
	for k in range(12):
		var mid := (lo + hi) * 0.5
		var w: PackedFloat32Array = world.get_cover_at(mid.x, mid.y)["weights"]
		if w[p_class] >= 0.5:
			lo = mid
		else:
			hi = mid
	return (lo + hi) * 0.5


func _plan() -> void:
	var dock3 := world.get_dock_position(world.find_dock(main.origin_dock))
	var dock := Vector2(dock3.x, dock3.z)
	var found := {}
	_scan(dock, 2500.0, 50.0, found)
	if found.size() < PAIRS.size():
		_scan(dock, 25000.0, 250.0, found)
	var classes: Array = Dictionary(world.get_info().get("landcover", {})).get("classes", [])
	for pair: Array in PAIRS:
		var key := "%s_%s" % [pair[0], pair[1]]
		if not found.has(key):
			print("cover_shots: 没找到 %s" % key)
			continue
		var first: Vector2 = found[key][1]
		var second: Vector2 = found[key][2]
		var edge := _refine(first, second, classes.find(pair[0]))
		var dir2 := (second - first).normalized()
		var dir := Vector3(dir2.x, 0.0, dir2.y)
		var side := Vector3(-dir.z, 0.0, dir.x)
		var p := Vector3(edge.x, world.get_surface_height(edge.x, edge.y), edge.y)
		# 人眼：站在前一类那边 40 m，看过交界 60 m
		var eye := p - dir * 40.0
		eye.y = world.get_surface_height(eye.x, eye.z) + 1.7
		var ahead := p + dir * 60.0
		ahead.y = world.get_surface_height(ahead.x, ahead.z) + 1.0
		# 人站在相机后面 6 m（站在相机处的话，开垦的虚影会挡在镜头前）
		views.append([key + "_eye", eye - dir * 6.0, eye, ahead])
		# 低空斜看：前一类那边 150 m、横错 60 m、高 40 m
		var low := p - dir * 150.0 + side * 60.0
		low.y = maxf(world.get_surface_height(low.x, low.z), p.y) + 40.0
		views.append([key + "_low", p + Vector3(0, 3, 0), low, p + dir * 40.0])
		# 俯看：交界正上 220 m
		views.append([key + "_top", p + Vector3(0, 3, 0), p + Vector3(0, 220, 0), p])
		print("cover_shots: %s 在 (%.0f, %.0f)，离码头 %.0f m" % [key, edge.x, edge.y, (edge - dock).length()])


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > 900.0:
		print("cover_shots: 超时")
		get_tree().quit(1)
		return
	if index < 0:
		if world.is_loaded() and elapsed > 1.0:
			_plan()
			if views.is_empty():
				print("cover_shots: 一处交界也没找到")
				get_tree().quit(1)
				return
			player.set_god_mode(true)
			index = 0
			_enter()
		return
	if busy:
		return
	wait += delta
	if wait > 2.5 and world.get_pending_count() == 0:
		busy = true
		var v: Array = views[index]
		# 拍几帧后的画面（新块、新散布上屏要几帧）
		for k in range(4):
			await get_tree().process_frame
		get_viewport().get_texture().get_image().save_png(shots_dir.path_join(v[0] + ".png"))
		print("cover_shots: %s" % v[0])
		index += 1
		busy = false
		if index >= views.size():
			print("cover_shots: 完成 → %s" % shots_dir)
			get_tree().quit(0)
			return
		_enter()


func _enter() -> void:
	var v: Array = views[index]
	player.global_position = v[1]
	world.prepare_at(v[1])
	camera.global_position = v[2]
	# 正上往下看时 UP 与视线平行，换一个朝北的上方向
	var look: Vector3 = (v[3] - v[2]).normalized()
	camera.look_at(v[3], Vector3.FORWARD if absf(look.y) > 0.99 else Vector3.UP)
	camera.make_current()
	wait = 0.0
