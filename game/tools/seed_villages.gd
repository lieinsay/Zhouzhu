extends SceneTree
## 开发者工具：给两座岛各铺一个起步村落，存到各自 Settlement 的 layout_path（会覆盖原有清单）。
## 只是个样板——真正摆村子还是进游戏按 B 用布置模式；这个脚本在地形改了、村落整体站不住时重新铺一遍。
##
##   Godot --headless --path game --script res://tools/seed_villages.gd

var main: Node
var frames := 0


func _initialize() -> void:
	main = load("res://main.tscn").instantiate()
	root.add_child(main)


func _process(_delta: float) -> bool:
	frames += 1
	if frames < 3:
		return false  # 等岛生成完、Settlement 读完
	_seed_a(main.get_node("IslandA"), main.get_node("IslandA/Settlement"))
	_seed_b(main.get_node("IslandB"), main.get_node("IslandB/Settlement"))
	return true


## 门朝 dir（岛内水平方向）该用的 yaw：建筑自己的 +z 转 yaw 后指向 (sin yaw, cos yaw)
func _facing(dir: Vector3) -> float:
	return atan2(dir.x, dir.z)


func _flat(v: Vector3) -> Vector3:
	v.y = 0.0
	return v.normalized()


func _put(settlement: Settlement, kind: String, near: Vector3, yaw: float, radius: float) -> bool:
	var site: Vector3 = settlement.find_site(kind, near, yaw, radius)
	if is_nan(site.y):
		print("  %s：%s 附近 %.0f m 内没地方" % [kind, near, radius])
		return false
	var why: String = settlement.place(kind, site, yaw)
	if why != "":
		print("  %s：%s" % [kind, why])
		return false
	return true


## 出发岛：路尽头这一岸只有窄窄一条滩，村子在河对岸的坡地上（河浅，蹚过去）——民居沿河排成一溜、门朝河，
## 堂屋在后排正中，仓在堂屋旁边，望楼再往后、往高处
func _seed_a(island: Island, settlement: Settlement) -> void:
	settlement.set_layout(PackedStringArray())
	var trail := island.get_trail_points()
	var end := trail[trail.size() - 1]
	var river := island.get_river_points()
	var nearest := 0
	for k in river.size():
		if river[k].distance_to(end) < river[nearest].distance_to(end):
			nearest = k
	var along := _flat(river[mini(river.size() - 1, nearest + 3)] - river[maxi(0, nearest - 3)])  # 顺河
	var across := -_flat(end - river[nearest])  # 从路尽头指向对岸
	var placed := 0
	for k in [-2, -1, 0, 1, 2]:
		placed += int(_put(settlement, "house", end + across * 17.0 + along * (k * 11.0), _facing(-across), 9.0))
	placed += int(_put(settlement, "hall", end + across * 31.0 + along * 2.0, _facing(-across), 14.0))
	placed += int(_put(settlement, "granary", end + across * 31.0 + along * 16.0, _facing(-along), 12.0))
	placed += int(_put(settlement, "tower", end + across * 46.0 - along * 10.0, _facing(-across), 20.0))
	print("IslandA：铺了 %d 座 → %s（%s）" % [placed, settlement.layout_path, "已存" if settlement.save() else "没存上"])


## 目的岛：没有路，就在停机坪后面的坡上放一处小小的落脚点
func _seed_b(island: Island, settlement: Settlement) -> void:
	settlement.set_layout(PackedStringArray())
	var spawn := island.get_spawn_transform()
	var inland := _flat(spawn.basis.z)  # 出生位姿面朝栈桥（岛外），-basis.z 是它看的方向，所以 +basis.z 指向岛内
	var side := Vector3(-inland.z, 0, inland.x)
	var placed := 0
	placed += int(_put(settlement, "house", spawn.origin + inland * 26.0 + side * 8.0, _facing(-inland), 30.0))
	placed += int(_put(settlement, "house", spawn.origin + inland * 30.0 - side * 9.0, _facing(-inland), 30.0))
	placed += int(_put(settlement, "tower", spawn.origin + inland * 45.0, _facing(-inland), 40.0))
	print("IslandB：铺了 %d 座 → %s（%s）" % [placed, settlement.layout_path, "已存" if settlement.save() else "没存上"])
