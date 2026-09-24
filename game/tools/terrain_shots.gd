extends Node
## 岛群地形目视检查：从几个固定的机位拍 world_2051 的岛体、崖壁、岛底、河谷与山地（不做断言，只存图）。
##
##   Godot --path game --fixed-fps 60 res://tools/terrain_shots.tscn -- --shots=<目录>
##
## 机位全按主码头（#7）与岛心推：人开 god 模式悬在机位附近（地面块按人的位置细分），另放一台相机拍。

const SCENE := preload("res://world_2051.tscn")
const MAIN_DOCK := 7

var main: Expedition
var world: World
var player: Player
var camera: Camera3D
var shots_dir := ""
var views: Array = []  # [名字, 人的位置, 相机位置, 相机看向]
var index := -1
var wait := 0.0
var busy := false  # 正在等帧拍图（await 期间 _physics_process 还会进来）
var elapsed := 0.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
	if shots_dir.is_empty():
		shots_dir = OS.get_user_data_dir().path_join("terrain_shots")
	DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1600, 900)
	GameClock.restart(10.0)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	camera = Camera3D.new()
	camera.far = 20000.0
	camera.fov = 60.0
	add_child(camera)


func _plan() -> void:
	var dock := world.get_dock_position(world.find_dock(MAIN_DOCK))
	# 离岸方向：从岛心（原点附近）指向码头
	var out := Vector3(dock.x, 0, dock.z).normalized()
	var side := Vector3(-out.z, 0, out.x)
	var rim := dock.y
	# 1 远看：离岸 2.5 km、比岸缘低 100 m，斜着看过去，崖壁连着倒锥的岛底悬在云海上
	views.append(["1_floating", dock + out * 800.0, dock + out * 2500.0 + side * 900.0 + Vector3(0, -100.0, 0), dock - out * 600.0 + side * -300.0 + Vector3(0, -350.0, 0)])
	# 2 从下往上：岸外 1.2 km、云带顶以下 900 m，仰看崖脚与倒锥岛底
	views.append(["2_underside", dock + out * 600.0, dock + out * 1200.0 + Vector3(0, -rim - 900.0, 0), dock - out * 2500.0 + Vector3(0, -rim - 300.0, 0)])
	# 3 崖壁近看：岸外 90 m、岸缘下 60 m，看岩层
	views.append(["3_cliff", dock + out * 60.0, dock + out * 90.0 + side * 40.0 + Vector3(0, -60.0, 0), dock + side * -60.0 + Vector3(0, -70.0, 0)])
	# 4 河谷：主码头 6 km 内最大的一条河，上空 120 m 顺流看
	var river: Dictionary = world.find_river_near(dock.x, dock.z, 6000.0, 20.0)
	if not river.is_empty():
		var p := Vector3(river["x"], 0, river["z"])
		p.y = world.get_surface_height(p.x, p.z)
		views.append(["4_valley", p, p + Vector3(-250, 140, -250), p + Vector3(150, -10, 150)])
	# 5 山地：主岛上最高的一块（抽样找），离地 250 m 斜看
	var best := Vector3.ZERO
	for i in range(60):
		for j in range(60):
			var x := -30000.0 + j * 1000.0
			var z := -30000.0 + i * 1000.0
			var h := world.get_surface_height(x, z)
			if h > best.y:
				best = Vector3(x, h, z)
	views.append(["5_mountain", best, best + Vector3(-900, 250, -900), best + Vector3(200, -60, 200)])
	# 6 小岛：12 号岛（14.7 km²，岛心 (-21920, 16486)），离岛心 4 km 平视，整座岛悬在云海上
	var islet := Vector3(-21920.0, 0.0, 16486.0)
	views.append(["6_islet", islet + Vector3(0, 400, -2600), islet + Vector3(-2800, 120, -2800), islet + Vector3(0, -80, 0)])


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > 600.0:
		print("terrain_shots: 超时")
		get_tree().quit(1)
		return
	if index < 0:
		if world.is_loaded() and elapsed > 1.0:
			_plan()
			player.set_god_mode(true)
			index = 0
			_enter()
		return
	if busy:
		return
	wait += delta
	if wait > 2.0 and world.get_pending_count() == 0:
		busy = true
		var v: Array = views[index]
		# 拍三帧后的画面（新块上屏要几帧）
		await get_tree().process_frame
		await get_tree().process_frame
		await get_tree().process_frame
		get_viewport().get_texture().get_image().save_png(shots_dir.path_join(v[0] + ".png"))
		print("terrain_shots: %s  人 %s  相机 %s" % [v[0], v[1], v[2]])
		index += 1
		busy = false
		if index >= views.size():
			print("terrain_shots: 完成 → %s" % shots_dir)
			get_tree().quit(0)
			return
		_enter()


func _enter() -> void:
	var v: Array = views[index]
	player.global_position = v[1]
	world.prepare_at(v[1])
	camera.global_position = v[2]
	camera.look_at(v[3], Vector3.UP)
	camera.make_current()
	wait = 0.0
