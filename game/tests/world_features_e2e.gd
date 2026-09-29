extends "res://tests/feature_tour.gd"
## 开局那群（world_seed42.tscn，开局配置 story_start.h：#6329）里把全部玩法走一遍（见 feature_tour.gd）：
## 地图重构前这些玩法只在甲乙两岛里有，这里证明它们在岛群里一样跑得起来。
## 出发码头是主码头（邑治旁，船坞坪凿在它旁边）；村子用主码头 2–6 km、主岛上户数最多的村（#6329 是 2.5 km 外的村003，271 户，有熟地、有水）；
## 去远处 M 开图点过去。村落改动存到 user:// 的临时目录（不动仓库里的 settlements/<节点>）。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/world_features_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图。

const VILLAGE_MIN_M := 2000.0  # 村子：离主码头 2–6 km、主岛上户数最多的（不算邑治）
const VILLAGE_MAX_M := 6000.0
const LAYOUT_DIR := "user://world_features_e2e"

var village_id := -1


func _tag() -> String:
	return "world_features"


func _scene() -> PackedScene:
	return load("res://world_seed42.tscn")


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
	if village_id < 0:
		# 世界读完了才挑（_setup 时还没读）
		var w := main.get_node("World") as World
		var home := w.get_dock_position(w.find_dock(main.origin_dock))
		var best := 0
		for v in w.get_info()["villages"]:
			var d := Vector2(v["pos"][0] - home.x, v["pos"][1] - home.z).length()
			if int(v["island"]) == 0 and not bool(v.get("seat", false)) and d >= VILLAGE_MIN_M and d <= VILLAGE_MAX_M and int(v["households"]) > best:
				best = int(v["households"])
				village_id = int(v["id"])
		_log("群 #%d：村子用村%03d（%d 户）" % [StoryStart.get_spawn_node(), village_id, best])
	return (main.get_node("SiteStream") as SiteStream).get_village(village_id)


func _village_name() -> String:
	return "村%03d" % village_id


func _village_radius() -> float:
	return (village as WorldVillage).get_radius()
