extends "res://tests/feature_tour.gd"
## 平板岛（flat_map.tscn）里把全部玩法走一遍（见 feature_tour.gd）。
## 平板岛（FlatMap）只实现了两份宿主接口（GroundHost、MapHost），一行玩法都没写——这条测试证明这就够了：
## 垦、选种、播、收，入坞改船，布置模式改自动铺的村子，村民下地、挑水，全都照常。
## 没有岛群地图面板，去远处就走过去。村落改动存到 user:// 的临时文件。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/flat_map_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图。

const LAYOUT := "user://flat_map_e2e_village.txt"


func _tag() -> String:
	return "flat_map"


func _scene() -> PackedScene:
	return load("res://flat_map.tscn")


func _setup() -> void:
	(main.get_node("Map/Settlement") as Settlement).layout_path = LAYOUT


func _cleanup() -> void:
	if FileAccess.file_exists(LAYOUT):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(LAYOUT))


func _farm() -> Farm:
	return main.get_node("Map/Farm")


func _yard() -> Shipyard:
	return main.get_node("Map/Shipyard")


func _village_host() -> Node3D:
	return main.get_node("Map")


func _village_name() -> String:
	return "平板岛的村子"


func _village_center() -> Vector3:
	var flat: FlatMap = main.get_node("Map")
	return flat.to_global(Vector3(flat.village_x, 0, flat.village_z))


## 最远那座房子的中心离村心多远，再加半座房子
func _village_radius() -> float:
	var s: Settlement = main.get_node("Map/Settlement")
	var far := 0.0
	for k in s.get_count():
		far = maxf(far, Vector2(s.get_building_position(k).x - _village_center().x, s.get_building_position(k).z - _village_center().z).length())
	return far + 6.0


## 溪边：村子与溪之间、溪的东岸
func _field_probe() -> Vector3:
	var flat: FlatMap = main.get_node("Map")
	return flat.to_global(Vector3(-50, 0, 30))
