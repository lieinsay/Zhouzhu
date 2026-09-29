extends Node
## 村子的目视检查：开局那群的邑治（world.json 里 seat 的那一村）与离主码头 25 km 内户数最多的另一个村（#6329 是村006）各从村里、350 m、1.8 km、高空四个距离各拍一张（不做断言，只存图）。
## 近处看房子的细模（门窗、屋顶、灯），远处看简模接得上接不上、远处的村子画没画出来。人跟着相机挪（流式焦点就是人）。
##
##   Godot --path game --fixed-fps 60 res://tools/village_shots.tscn -- --shots=<目录>

const SCENE := preload("res://world_seed42.tscn")
var villages: Array = []  # [村号, 村心]：_plan 里从 world.json 挑
# [名字, 相机离村心（水平，米）, 相机离地（米）, 看向离地（米）]
const DISTANCES := [["near", 45.0, 6.0, 2.0], ["mid", 350.0, 60.0, 0.0], ["far", 1800.0, 260.0, 0.0], ["aerial", 3500.0, 1600.0, 0.0]]
const WAIT_S := 8.0

var main: Expedition
var world: World
var player: Player
var camera: Camera3D
var shots_dir := ""
var views: Array = []  # [名字, 相机位置, 看向]
var index := -1
var wait := 0.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
	if shots_dir.is_empty():
		shots_dir = OS.get_user_data_dir().path_join("village_shots")
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


func _ground(p: Vector2) -> float:
	var h := world.get_surface_height(p.x, p.y)
	return 0.0 if is_nan(h) else h


func _plan() -> void:
	var dock3 := world.get_dock_position(world.find_dock(main.origin_dock))
	var dock := Vector2(dock3.x, dock3.z)
	var big := []
	for v in world.get_info()["villages"]:
		var p := Vector2(float(v["pos"][0]), float(v["pos"][1]))
		if bool(v.get("seat", false)):
			villages.push_front([int(v["id"]), p])
		elif p.distance_to(dock) < 25000.0 and (big.is_empty() or int(v["households"]) > int(big[2])):
			big = [int(v["id"]), p, int(v["households"])]
	if not big.is_empty():
		villages.append([big[0], big[1]])
	print("village_shots: 群 #%d，拍村 %s" % [StoryStart.get_spawn_node(), villages.map(func(v): return v[0])])
	for v in villages:
		var center: Vector2 = v[1]
		for d in DISTANCES:
			var at := center + Vector2(0.35, 1.0).normalized() * float(d[1])
			var cam := Vector3(at.x, _ground(at) + float(d[2]), at.y)
			var look := Vector3(center.x, _ground(center) + float(d[3]), center.y)
			views.append(["village%03d_%s" % [v[0], d[0]], cam, look])


func _process(delta: float) -> void:
	if not world.is_loaded():
		return
	if views.is_empty():
		_plan()
		player.set_god_mode(true)
	wait -= delta
	if index >= 0 and wait > 0.0:
		player.global_position = views[index][1]
		camera.global_position = views[index][1]
		camera.look_at(views[index][2])
		return
	if index >= 0:
		var path := shots_dir.path_join(views[index][0] + ".png")
		get_viewport().get_texture().get_image().save_png(path)
		print("village_shots: ", path)
	index += 1
	if index >= views.size():
		get_tree().quit(0)
		return
	player.global_position = views[index][1]
	world.prepare_at(views[index][1])
	camera.global_position = views[index][1]
	camera.look_at(views[index][2])
	camera.current = true
	wait = WAIT_S
