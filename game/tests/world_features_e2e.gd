extends "res://tests/feature_tour.gd"
## 岛群 #2051（world_2051.tscn）里把全部玩法走一遍（见 feature_tour.gd）：
## 地图重构前这些玩法只在甲乙两岛里有，这里证明它们在岛群里一样跑得起来。
## 出发码头是主码头 #1（邑治旁，船坞坪凿在它旁边）；村子用 2.5 km 外的村031（有熟地、有水）；去远处 M 开图点过去。
## 村落改动存到 user:// 的临时目录（不动仓库里的 settlements/2051）。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/world_features_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图。

const VILLAGE_ID := 31
const LAYOUT_DIR := "user://world_features_e2e"


func _tag() -> String:
	return "world_features"


func _scene() -> PackedScene:
	return load("res://world_2051.tscn")


func _setup() -> void:
	(main.get_node("SiteStream") as SiteStream).layout_dir = LAYOUT_DIR


func _cleanup() -> void:
	var dir := DirAccess.open(LAYOUT_DIR)
	if dir == null:
		return
	for file in dir.get_files():
		dir.remove(file)


func _farm() -> Farm:
	return main.get_node("World/Farm")


func _yard() -> Shipyard:
	return main.get_node("World/Shipyard")


func _village_host() -> Node3D:
	return (main.get_node("SiteStream") as SiteStream).get_village(VILLAGE_ID)


func _village_name() -> String:
	return "村%03d" % VILLAGE_ID


func _village_radius() -> float:
	return (village as WorldVillage).get_radius()
