extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func require(ok: bool, message: String) -> void:
	if not ok:
		push_error("seed42_rivers_e2e: " + message)
		quit(1)
		assert(ok, message)

func _run() -> void:
	var main = load("res://world_seed42.tscn").instantiate()
	root.add_child(main)
	var world = main.get_node("World")
	var player = main.get_node("Player")
	var clock = root.get_node("GameClock")
	clock.set_paused(true)
	require(int(clock.get_date().day_of_year) == 85, "初始日期不是第85天")
	for i in range(240):
		await physics_frame
		if player.is_on_floor() and world.is_ready_at(player.global_position): break
	require(world.is_loaded() and world.get_node_id() == 6068, "新开局没有进入6068")
	require(player.is_on_floor(), "出生位置缺少有效碰撞")
	var spawn: Vector3 = player.global_position
	require(Vector2(spawn.x-24790,spawn.z+32330).length() < 1.0, "真实玩家出生坐标不符")
	require(abs(spawn.y-world.get_surface_height(spawn.x,spawn.z)) < 3.0, "出生高程与地面不符")
	require(player.get_camera().current, "玩家摄像机未启用")
	var river: Dictionary = world.find_river_near(spawn.x,spawn.z,1000,30)
	require(not river.is_empty(), "出生附近缺少主干河")
	var samples: Array = []
	for day in [10,85,100,200,300]:
		clock.set_date(day,12.0)
		await process_frame
		await process_frame
		var state: Dictionary = world.get_river_at(river.x,river.z)
		require(state.get("source_rivers",false), "水情没有读取源河网")
		samples.append([floori(clock.get_calendar_day()),state.width,state.depth,state.discharge_m3s,state.basin])
	require(samples[0][1] != samples[2][1], "不同日期水情没有变化")
	var saved_day: float = clock.get_calendar_day()
	clock.shift_days(20)
	await process_frame
	clock.set_date(int(saved_day),12)
	await process_frame
	var restored: Dictionary = world.get_river_at(river.x,river.z)
	require(abs(restored.width-samples[-1][1])<0.01, "恢复日期没有刷新水面")
	main.set_dev_tools(true)
	var stream = main.get_node("GroupStream")
	for node in [6067,6068]:
		require(main.request_planet_jump(node), "跨岛请求失败")
		var deadline: int = Time.get_ticks_msec()+120000
		while int(main.get_jump_state().stage) != 0 and Time.get_ticks_msec()<deadline:
			await process_frame
		var jump: Dictionary = main.get_jump_state()
		require(int(jump.stage)==0 and bool(jump.last.get("ok",false)), "跨岛加载超时或失败")
		require(stream.get_anchor_node()==node, "跨岛锚点不符")
		world=stream.get_group_world(node)
		var nearby: Dictionary = world.find_river_near(player.global_position.x,player.global_position.z,100000,1)
		require(not nearby.is_empty() and world.get_river_at(nearby.x,nearby.z).get("source_rivers",false), "跨岛后未加载源河网")
		print("seed42_rivers_e2e: jump=%d ms=%s" % [node,jump.last.total_ms])
	var returned: Dictionary = world.get_river_at(river.x,river.z)
	require(abs(returned.width-restored.width)<0.01, "跨岛及细版重载后日期水情改变")
	main.queue_free()
	await process_frame
	main=load("res://world_seed42.tscn").instantiate()
	root.add_child(main)
	world=main.get_node("World")
	await process_frame
	await process_frame
	var reloaded: Dictionary = world.get_river_at(river.x,river.z)
	require(abs(reloaded.width-restored.width)<0.01, "重新进入场景后水情不符")
	player=main.get_node("Player")
	print("seed42_rivers_e2e: PASS node=6068 spawn=%s camera=%s date_start=%d river=%s samples=%s restored=%.6f" % [spawn,player.get_camera().current,clock.get_calendar_start(),river,samples,restored.width])
	quit(0)

