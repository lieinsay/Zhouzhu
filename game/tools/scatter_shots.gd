extends Node
## 散布目视检查：在主码头附近挑几样散布物、一处采场、一处地标，从几个机位拍 world_2051（不做断言，只存图）。
##
##   Godot --path game --fixed-fps 60 res://tools/scatter_shots.tscn -- --shots=<目录>
##
## 机位按主码头推：人开 god 模式悬在机位附近（地面块、散布块都按人的位置铺），另放一台相机拍。

const SCENE := preload("res://world_2051.tscn")

var main: Expedition
var world: World
var player: Player
var scatter: ScatterStream
var sites: SiteStream
var camera: Camera3D
var shots_dir := ""
var views: Array = []  # [名字, 人的位置, 相机位置, 相机看向]
var index := -1
var wait := 0.0
var busy := false
var elapsed := 0.0
var last_chunks := -1


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
	if shots_dir.is_empty():
		shots_dir = OS.get_user_data_dir().path_join("scatter_shots")
	DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1600, 900)
	GameClock.restart(10.0)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	scatter = main.get_node("ScatterStream")
	sites = main.get_node("SiteStream")
	camera = Camera3D.new()
	camera.far = 60000.0
	camera.fov = 60.0
	add_child(camera)


func _ground(p: Vector3) -> Vector3:
	var h := world.get_surface_height(p.x, p.z)
	return Vector3(p.x, h if not is_nan(h) else p.y, p.z)


## 看一株：从它 p_back 米外、p_up 米高看它根上 p_look_up 米
func _look_at_item(name: String, item: Dictionary, back: float, up: float, look_up: float) -> void:
	if item.is_empty():
		print("scatter_shots: 没找到 %s" % name)
		return
	var p: Vector3 = item["position"]
	var dir := Vector3(0.6, 0, 0.8)
	var cam := _ground(p + dir * back) + Vector3(0, up, 0)
	views.append([name, cam, cam, p + Vector3(0, look_up, 0)])


func _plan() -> void:
	var dock := world.get_dock_position(world.find_dock(main.origin_dock))
	for kind in ["broadleaf", "conifer", "shrub", "boulder", "floatstone", "reeds", "clay_bank", "gravel_heap"]:
		var item: Dictionary = world.find_scatter_near(dock.x, dock.z, 4000.0, kind)
		print("scatter_shots: 离主码头最近的 %s：%s" % [kind, item])
	# 1 林缘：离主码头最近的阔叶树，40 m 外、离地 18 m
	_look_at_item("1_forest_edge", world.find_scatter_near(dock.x, dock.z, 4000.0, "broadleaf"), 40.0, 18.0, 4.0)
	# 2 林下：人眼高，从那棵树往林里看
	var tree: Dictionary = world.find_scatter_near(dock.x, dock.z, 4000.0, "broadleaf")
	if not tree.is_empty():
		var t: Vector3 = tree["position"]
		var deep: Dictionary = world.find_scatter_near(t.x + 150.0, t.z + 150.0, 400.0, "broadleaf")
		if not deep.is_empty():
			var d: Vector3 = deep["position"]
			var eye := _ground(d + Vector3(2.5, 0, 2.5)) + Vector3(0, 1.7, 0)
			views.append(["2_in_forest", eye, eye, eye + Vector3(-30, 1, -10)])
	# 3 灌丛
	_look_at_item("3_shrubs", world.find_scatter_near(dock.x, dock.z, 4000.0, "shrub"), 14.0, 5.0, 0.5)
	# 4 采石场：主码头最近的一处
	var sid: int = world.find_resource_near(dock.x, dock.z, 20000.0, "stone")
	if sid >= 0:
		var site: Dictionary = world.get_resource_site(sid)
		var face: Vector3 = site["face"]
		var cam := _ground(face + Vector3(14, 0, 10)) + Vector3(0, 7, 0)
		views.append(["4_quarry", cam, cam, face + Vector3(0, 1, 0)])
	# 5 石块
	_look_at_item("5_boulders", world.find_scatter_near(dock.x, dock.z, 6000.0, "boulder"), 12.0, 4.0, 0.3)
	# 6 地标：最近的泉眼、洞穴
	for kind in ["spring", "cave"]:
		var lid: int = world.find_landmark_near(dock.x, dock.z, 30000.0, kind)
		if lid >= 0:
			var mark: Dictionary = world.get_landmark(lid)
			var at: Vector3 = mark["position"]
			var cam := _ground(at + Vector3(8, 0, 9)) + Vector3(0, 4, 0)
			views.append(["6_" + kind, cam, cam, at + Vector3(0, 1, 0)])
	# 7 苇丛、砂砾堆、浮石块
	_look_at_item("7_reeds", world.find_scatter_near(dock.x, dock.z, 20000.0, "reeds"), 10.0, 3.0, 0.8)
	_look_at_item("7_gravel", world.find_scatter_near(dock.x, dock.z, 8000.0, "gravel_heap"), 10.0, 4.0, 0.2)
	_look_at_item("7_floatstone", world.find_scatter_near(dock.x, dock.z, 8000.0, "floatstone"), 10.0, 4.0, 0.3)
	# 8 俯瞰：主码头往岛里 1 km、离地 250 m，看林子铺多远
	var inland := _ground(dock * 0.93)
	views.append(["8_overview", inland + Vector3(0, 250, 0), inland + Vector3(0, 250, 0), inland + Vector3(-600, 0, -600)])


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > 900.0:
		print("scatter_shots: 超时")
		get_tree().quit(1)
		return
	if index < 0:
		if world.is_loaded() and elapsed > 1.0:
			_plan()
			player.set_god_mode(true)
			index = 0
			if views.is_empty():
				get_tree().quit(1)
				return
			_enter()
		return
	if busy:
		return
	wait += delta
	var chunks := scatter.get_chunk_count()
	var settled := chunks == last_chunks
	last_chunks = chunks
	if wait > 3.0 and world.get_pending_count() == 0 and settled:
		busy = true
		var v: Array = views[index]
		await get_tree().process_frame
		await get_tree().process_frame
		await get_tree().process_frame
		get_viewport().get_texture().get_image().save_png(shots_dir.path_join(v[0] + ".png"))
		print("scatter_shots: %s  相机 %s  散布块 %d、立着 %d、树干 %d" % [v[0], v[2], scatter.get_chunk_count(), scatter.get_item_count(), scatter.get_solid_count()])
		index += 1
		busy = false
		if index >= views.size():
			print("scatter_shots: 完成 → %s" % shots_dir)
			get_tree().quit(0)
			return
		_enter()


func _enter() -> void:
	var v: Array = views[index]
	player.global_position = v[1]
	world.prepare_at(v[1])
	sites.ensure_near(v[1])
	scatter.ensure_near(v[1])
	camera.global_position = v[2]
	camera.look_at(v[3], Vector3.UP)
	camera.make_current()
	wait = 0.0
	last_chunks = -1
