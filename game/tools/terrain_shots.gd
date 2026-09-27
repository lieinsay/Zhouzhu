extends Node
## 岛群地形目视检查：从几个固定的机位拍 world_2051 的岛体、崖壁、岛底、河谷与山地（不做断言，只存图）。
##
##   Godot --path game --fixed-fps 60 res://tools/terrain_shots.tscn -- --shots=<目录>
##
## 机位全按出发码头（主码头）与岛心推：人开 god 模式悬在机位附近（地面块按人的位置细分），另放一台相机拍。
## 浮高计划 Z2 起多几张看同群的岛高低错落（11–16）：机位按岛表里各岛的岛心与岸缘推，岛号是 2051 的。

const SCENE := preload("res://world_2051.tscn")

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
	camera.far = 60000.0
	camera.fov = 60.0
	add_child(camera)


func _plan() -> void:
	var dock := world.get_dock_position(world.find_dock(main.origin_dock))
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
	# 7 从出发码头望山：人眼的高度，朝着上面找到的最高处（远山靠远平面与淡雾才看得见）
	var eye := world.get_dock_spawn(world.find_dock(main.origin_dock)).origin + Vector3(0, 1.7, 0)
	views.append(["7_dock_view", eye, eye, Vector3(best.x, eye.y + (best.y - eye.y) * 0.6, best.z)])
	# 6 小岛：12 号岛（14.7 km²，岛心 (-21920, 16486)），离岛心 4 km 平视，整座岛悬在云海上（高度按它的岸缘：浮高以后它比主岛低 150 m）
	var islet := _island_center(12)
	var islet_rim := _island_rim(12)
	views.append(["6_islet", islet + Vector3(0, islet_rim + 124.0, -2600), islet + Vector3(-2800, islet_rim - 156.0, -2800), islet + Vector3(0, islet_rim - 356.0, 0)])
	# 浮高计划 Z1：云带顶在 −5000 m，岛底（连岩棱）至少高出它 1000 m
	# 8 岛边往下看：岸外 120 m、岸缘上 30 m，朝外往下约 50° 望——脚下几千米是云带顶
	views.append(["8_edge_down", dock + out * 60.0, dock + out * 120.0 + Vector3(0, 30.0, 0), dock + out * 2200.0 + Vector3(0, -2600.0, 0)])
	# 9 岛底与云带顶之间的空：岸外 8 km、云带顶上 400 m（y = −4600），朝岛心略往上看——倒锥的岛底悬在头顶，底下是云带顶，中间一千多米是天
	views.append(["9_gap_side", dock + out * 600.0, Vector3(dock.x, -4600.0, dock.z) + out * 8000.0 + side * 1500.0, Vector3(dock.x, -3300.0, dock.z) - out * 6000.0])
	# 10 远看整座主岛：岸外 25 km、y = −4500，平看——一整座浮山悬在云带顶上
	views.append(["10_gap_far", dock + out * 600.0, Vector3(dock.x, -4500.0, dock.z) + out * 25000.0, Vector3(dock.x, -3900.0, dock.z) - out * 8000.0])
	# 浮高计划 Z2：同群的岛按岛龄上下错开（主岛不动，−223…+1325 m）
	# 11 从主码头望东：码头岸外 30 m、坪上 3 m（背着船坞门架），朝东（方位 85°）平望——8 号（岸缘 910 m、11 km）比码头高 670 m，
	#    9 号（116 m、8.6 km）、26 号（258 m、15 km）在左手边、与码头差不多高或更低
	var i19 := _island_center(19)
	var dock_eye := dock + out * 30.0 + Vector3(0, 3.0, 0)
	var east := Vector3(sin(deg_to_rad(85.0)), 0.0, -cos(deg_to_rad(85.0)))
	views.append(["11_dock_east", dock_eye, dock_eye, dock_eye + east * 10000.0 + Vector3(0, 250.0, 0)])
	# 12 站在 19 号（岸缘 1237 m）北岸往下看：14 号（134 m、北边 5 km）在脚下一千一百米，8 号（910 m）远一点、也低，左手是主岛
	var i14 := _island_center(14)
	var to14 := Vector3(i14.x - i19.x, 0.0, i14.z - i19.z).normalized()
	var edge := i19
	for k in 200:
		var q := i19 + to14 * (50.0 * k)
		var h := world.get_surface_height(q.x, q.z)
		if is_nan(h):
			break
		edge = Vector3(q.x, h, q.z)
	# 相机悬在崖边外 40 m、比岸上高 25 m（在岸里头往下看只看得见自己脚下的地）；人站在崖边里头 30 m（别拍到人的帽檐）
	var high_eye19 := edge + to14 * 40.0 + Vector3(0, 25.0, 0)
	var stand19 := edge - to14 * 30.0
	stand19.y = world.get_surface_height(stand19.x, stand19.z) + 1.0
	views.append(["12_high_down", stand19, high_eye19, Vector3(i14.x, _island_rim(14), i14.z)])
	# 13 从主峰望东：峰上 40 m，朝 19 号、16 号、2 号那一片略往下看——群里的岛一层层落在不同的高度上
	var summit := best + Vector3(0, 40.0, 0)
	var i16 := _island_center(16)
	views.append(["13_peak_view", summit, summit, Vector3(i16.x, summit.y - 900.0, i16.z)])
	# 14 群外东边 15 km、y = 700 平看整群的东半边：29 号（1166 m）、19 号、8 号高高在上，2 号（121 m）、14 号低低在下，底下远远的是云带顶
	var i29 := _island_center(29)
	var side_eye := i29 + Vector3(15000.0, 700.0, 6000.0)
	views.append(["14_group_side", side_eye, side_eye, Vector3(i19.x, 500.0, i19.z)])
	# 15 19 号的岛底：岸外 3 km、比它的岸缘低 1600 m 仰看——浮在 1.2 km 高的小岛，倒锥的底还在主岛岸缘之上，远处是主岛的崖
	var rim19 := _island_rim(19)
	var out19 := (i19 - dock).normalized()
	var under_eye := i19 + out19 * 3500.0 + Vector3(0, rim19 - 1600.0, 0)
	views.append(["15_high_underside", under_eye, under_eye, i19 + Vector3(0, rim19 - 700.0, 0)])
	# 16 god 在高处斜看整群：群心东南 45 km、y = 6000 m，朝群心往下约 7°——一群浮山各在各的高度上，底下是云带顶
	var high_eye := Vector3(32000.0, 6000.0, 32000.0)
	views.append(["16_group_high", high_eye, high_eye, Vector3(0.0, 500.0, 0.0)])


func _island_center(p_id: int) -> Vector3:
	for row in world.get_info()["islands"]:
		if int(row["id"]) == p_id:
			return Vector3(float(row["center"][0]), 0.0, float(row["center"][1]))
	return Vector3.ZERO


func _island_rim(p_id: int) -> float:
	for row in world.get_info()["islands"]:
		if int(row["id"]) == p_id:
			return float(row["rim_m"])
	return 0.0


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
