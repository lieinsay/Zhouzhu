extends SceneTree
## 旧开局包：真实玩家从岸上走过接岸坡道，检查网格与碰撞吻合。

func _initialize() -> void:
	call_deferred("_run")

func _fail(message: String) -> void:
	Input.action_release("move_forward")
	printerr("dock_approach: FAIL — " + message)
	quit(1)

func _run() -> void:
	var main = load("res://world_seed42.tscn").instantiate()
	root.add_child(main)
	var player: Player = main.get_node("Player")
	var ramp: MeshInstance3D = null
	for frame in 7200:
		await physics_frame
		if not player.is_on_floor():
			continue
		var best := INF
		for candidate in main.find_children("PierApproach", "MeshInstance3D", true, false):
			var distance: float = candidate.global_position.distance_to(player.global_position)
			if distance < best:
				best = distance
				ramp = candidate
		if ramp != null and best < 100.0:
			break
	if ramp == null:
		_fail("开局码头或地面没有就绪")
		return
	var collider: CollisionShape3D = ramp.get_parent().get_node("PierApproachShape")
	var box: BoxMesh = ramp.mesh
	var shape: BoxShape3D = collider.shape
	if not box.size.is_equal_approx(shape.size) or not ramp.transform.is_equal_approx(collider.transform):
		_fail("坡道网格和碰撞不一致")
		return
	var foot := ramp.global_transform * Vector3(-box.size.x * 0.5, box.size.y * 0.5, 0)
	var top := ramp.global_transform * Vector3(box.size.x * 0.5, box.size.y * 0.5, 0)
	var out := Vector3(top.x - foot.x, 0, top.z - foot.z).normalized()
	player.global_position = foot - out * 0.7 + Vector3.UP * 0.3
	player.velocity = Vector3.ZERO
	for frame in 120:
		await physics_frame
	var target := top + out * 1.0
	for frame in 1200:
		var delta := target - player.global_position
		delta.y = 0
		if delta.length() < 0.4 and player.is_on_floor():
			Input.action_release("move_forward")
			print("dock_approach: PASS — 岸上步行越过坡道登上码头；网格与碰撞一致")
			quit(0)
			return
		player.set_look_yaw(atan2(-delta.x, -delta.z))
		Input.action_press("move_forward")
		await physics_frame
	_fail("步行未能登上码头：" + str(player.global_position) + "，目标 " + str(target))
