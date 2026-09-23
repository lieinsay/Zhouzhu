extends Node
## 村民端到端测试：加载主场景，玩家站着不动，只看村民自己过日子。
## 两座岛招到的人数与职业对得上 → 出工的钟点陆续出门 → 一路上每一帧都贴着地面、不走进房子、不瞬移
## → 农夫开荒、播种、收割、把粮送进仓（仓里的粮比开局多）→ 挑水的到河边打水、挑回家 → 守望在望楼下站班、巡村
## → 闲人去别家串门 → 玩家垦的那块地没人碰 → 拨到傍晚，全村回家进门 → 拨到清早，又有人出门。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/villager_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，会在村里干活时与收工时各存一张截图。

const MAIN := preload("res://main.tscn")
const TIMEOUT := 900.0  # 模拟秒
const WORK_DEADLINE := 420.0  # 这么久里各样活都得干过
const GROW_SECONDS := 12.0  # 测试里庄稼长快些

enum Phase { WORK, EVENING, NIGHT, MORNING, DONE }

var main: Expedition
var player: Player
var island_a: Island
var island_b: Island
var farm: Farm
var folk_a: Villagers
var folk_b: Villagers
var settle_a: Settlement
var settle_b: Settlement

var phase := Phase.WORK
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var player_cell := Vector2i(-1, -1)
var grain_at_start := 0
var grain_max := 0
var last_pos := {}  # [岛, 下标] → 上一帧的全局位置（只记看得见的）
var seen_fetch_near_river := false
var seen_watch_at_tower := false
var work_shot := false
var checked_frames := 0


func _ready() -> void:
	main = MAIN.instantiate()
	farm = main.get_node("IslandA/Farm")
	farm.grow_seconds = GROW_SECONDS
	add_child(main)
	player = main.get_node("Player")
	island_a = main.get_node("IslandA")
	island_b = main.get_node("IslandB")
	folk_a = island_a.get_node("Villagers")
	folk_b = island_b.get_node("Villagers")
	settle_a = island_a.get_node("Settlement")
	settle_b = island_b.get_node("Settlement")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE

	# 人数与职业：出发岛 5 户 × 2 + 堂屋 1 + 守望 1；目的岛 2 户 × 2（没地没河，都闲着）+ 守望 1
	var jobs_a := _jobs(folk_a)
	var jobs_b := _jobs(folk_b)
	print("villager_e2e: 出发岛 %d 人 %s；目的岛 %d 人 %s；钟点 %s" % [folk_a.get_villager_count(), jobs_a, folk_b.get_villager_count(), jobs_b, folk_a.get_clock_text()])
	if jobs_a != {"farmer": 5, "carrier": 5, "watchman": 1, "idler": 1}:
		_finish(false, "出发岛的职业不对：%s" % jobs_a)
		return
	if jobs_b != {"idler": 4, "watchman": 1}:
		_finish(false, "目的岛的职业不对：%s" % jobs_b)
		return
	for k in folk_a.get_villager_count():
		if folk_a.get_villager_phase(k) != Villagers.INDOORS or folk_a.is_villager_visible(k):
			_finish(false, "开局都该在屋里：%s %s" % [folk_a.get_villager_name(k), folk_a.get_villager_activity(k)])
			return

	# 玩家先在路尽头垦一块地：村民不许动它
	var trail := island_a.get_trail_points()
	player_cell = farm.find_nearest_arable(trail[trail.size() - 1])
	farm.perform(player_cell, Farm.ACTION_TILL, "")
	grain_at_start = folk_a.get_store_count("millet") + folk_a.get_store_count("bean")
	grain_max = grain_at_start


func _jobs(p_folk: Villagers) -> Dictionary:
	var jobs := {}
	for k in p_folk.get_villager_count():
		var job := p_folk.get_villager_job(k)
		jobs[job] = jobs.get(job, 0) + 1
	return jobs


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时，停在阶段 %s；%s" % [Phase.keys()[phase], _summary()])
		return
	if not _check_everyone(folk_a, settle_a, island_a, delta) or not _check_everyone(folk_b, settle_b, island_b, delta):
		return
	if farm.get_plot(player_cell) != Farm.PLOT_TILLED:
		_finish(false, "玩家垦的地被人动了：地块状态 %d、种着 %s" % [farm.get_plot(player_cell), farm.get_crop(player_cell)])
		return
	grain_max = maxi(grain_max, folk_a.get_store_count("millet") + folk_a.get_store_count("bean"))

	match phase:
		Phase.WORK:
			_watch_work()
			if not work_shot and folk_a.get_tally("harvest") >= 1:
				work_shot = true
				_work_shots()
			var missing := _missing_work()
			if missing.is_empty():
				print("villager_e2e: %.0f s 里各样活都干过了（%s）；%s" % [elapsed, folk_a.get_clock_text(), _summary()])
				folk_a.set_hour(18.45)
				folk_b.set_hour(18.45)
				_enter(Phase.EVENING)
			elif elapsed > WORK_DEADLINE:
				_finish(false, "%.0f s 里还没干过：%s；%s" % [elapsed, missing, _summary()])
		Phase.EVENING:
			# 收工了：半路的掉头、干着的放下，全都走回家进门
			if _all_indoors(folk_a) and _all_indoors(folk_b):
				print("villager_e2e: 收工后 %.0f s 全村进门（%s）" % [elapsed - phase_started, folk_a.get_clock_text()])
				_shot("VILLAGE_night")
				_enter(Phase.NIGHT)
			elif elapsed - phase_started > 200.0:
				_finish(false, "收工 200 s 了还有人在外面：%s" % _outside(folk_a) + _outside(folk_b))
		Phase.NIGHT:
			# 夜里谁也不出门
			if not (_all_indoors(folk_a) and _all_indoors(folk_b)):
				_finish(false, "夜里有人出门：%s" % _outside(folk_a) + _outside(folk_b))
				return
			if elapsed - phase_started > 5.0:
				folk_a.set_hour(5.98)
				folk_b.set_hour(5.98)
				_enter(Phase.MORNING)
		Phase.MORNING:
			var out := 0
			for k in folk_a.get_villager_count():
				out += 1 if folk_a.get_villager_phase(k) != Villagers.INDOORS else 0
			if out >= folk_a.get_villager_count() / 2:
				print("villager_e2e: 清早 %.0f s 里出门 %d 人（%s）" % [elapsed - phase_started, out, folk_a.get_clock_text()])
				_enter(Phase.DONE)
				_finish(true, "")
			elif elapsed - phase_started > 60.0:
				_finish(false, "天亮一分钟了才出门 %d 人" % out)


func _watch_work() -> void:
	var river := island_a.get_river_points()
	for k in folk_a.get_villager_count():
		if folk_a.get_villager_phase(k) != Villagers.WORKING:
			continue
		var at := folk_a.get_villager_position(k)
		match folk_a.get_villager_task(k):
			"fetch_water":
				for p in river:
					if Vector2(p.x - at.x, p.z - at.z).length() < 8.0:
						seen_fetch_near_river = true
						break
			"watch":
				for b in settle_a.get_count():
					if settle_a.get_building_kind(b) == "tower" and Vector2(settle_a.get_building_position(b).x - at.x, settle_a.get_building_position(b).z - at.z).length() < 10.0:
						seen_watch_at_tower = true


func _missing_work() -> Array:
	var missing := []
	var need := {"till": 5, "sow": 3, "harvest": 2, "deliver": 4, "fetch_water": 3, "watch": 1, "patrol": 1, "chat": 1}
	for task in need:
		if folk_a.get_tally(task) < need[task]:
			missing.append("%s %d/%d" % [task, folk_a.get_tally(task), need[task]])
	if grain_max <= grain_at_start:
		missing.append("仓里的粮没多过开局（%d）" % grain_at_start)
	if not seen_fetch_near_river:
		missing.append("在河边打水")
	if not seen_watch_at_tower:
		missing.append("在望楼下站班")
	if folk_b.get_tally("chat") < 1:
		missing.append("目的岛有人串门")
	return missing


# 每一帧、每个看得见的村民：脚下有地、不在任何一座房子的占地里、离上一帧不超过走路的步长
func _check_everyone(p_folk: Villagers, p_settlement: Settlement, p_island: Island, p_delta: float) -> bool:
	for k in p_folk.get_villager_count():
		var key := [p_island.name, k]
		if not p_folk.is_villager_visible(k):
			last_pos.erase(key)
			continue
		var at := p_folk.get_villager_position(k)
		var ground := p_island.get_surface_height(at)
		if is_nan(ground) or absf(ground - at.y) > 0.6:
			_finish(false, "%s 脚下没地（%s，地面 %s）" % [p_folk.get_villager_name(k), at, ground])
			return false
		var inside := p_settlement.find_at(at)
		if inside >= 0:
			_finish(false, "%s 走进了房子 %d（%s）：%s" % [p_folk.get_villager_name(k), inside, p_settlement.get_building_kind(inside), p_folk.get_villager_activity(k)])
			return false
		if last_pos.has(key):
			var step := Vector2(at.x - last_pos[key].x, at.z - last_pos[key].z).length()
			if step > p_folk.walk_speed * 1.2 * p_delta + 0.01:
				_finish(false, "%s 一帧挪了 %.2f m：%s" % [p_folk.get_villager_name(k), step, p_folk.get_villager_activity(k)])
				return false
		last_pos[key] = at
		checked_frames += 1
	return true


func _all_indoors(p_folk: Villagers) -> bool:
	for k in p_folk.get_villager_count():
		if p_folk.get_villager_phase(k) != Villagers.INDOORS or p_folk.is_villager_visible(k):
			return false
	return true


func _outside(p_folk: Villagers) -> String:
	var names := []
	for k in p_folk.get_villager_count():
		if p_folk.get_villager_phase(k) != Villagers.INDOORS:
			names.append("%s（%s）" % [p_folk.get_villager_name(k), p_folk.get_villager_activity(k)])
	return ", ".join(names)


func _summary() -> String:
	var tallies := []
	for task in ["till", "sow", "harvest", "tend", "deliver", "fetch_water", "watch", "patrol", "chat", "wait"]:
		tallies.append("%s %d" % [task, folk_a.get_tally(task)])
	return "出发岛 %s；认领 %d 块；仓 粟 %d 菽 %d；找不到路 %d/%d 次；检查了 %d 人帧" % [
		", ".join(tallies), folk_a.get_claimed_cells().size(), folk_a.get_store_count("millet"), folk_a.get_store_count("bean"),
		folk_a.get_path_failures(), folk_b.get_path_failures(), checked_frames]


func _enter(p_phase: Phase) -> void:
	phase = p_phase
	phase_started = elapsed


func _village_center() -> Vector3:
	var center := Vector3.ZERO
	for b in settle_a.get_count():
		center += settle_a.get_building_position(b)
	return center / maxf(1.0, settle_a.get_count())


# 干活时三张：从河那边看全村、一个正在地里干活的农夫、一个挑水的（近景，看得见头顶的字）
func _work_shots() -> void:
	var center := _village_center()
	await _shot("VILLAGE_work", center, Vector3(40, 24, 14))
	for job in ["farmer", "carrier"]:
		var pick := -1
		for k in folk_a.get_villager_count():
			if folk_a.get_villager_job(k) == job and folk_a.is_villager_visible(k):
				if pick < 0 or folk_a.get_villager_phase(k) == Villagers.WORKING:
					pick = k
		if pick >= 0:
			await _shot("VILLAGE_" + job, folk_a.get_villager_position(pick) + Vector3(0, 1, 0), Vector3(6, 3.5, 5))


func _shot(p_name: String, p_focus: Vector3 = Vector3.ZERO, p_offset: Vector3 = Vector3(40, 24, 14)) -> void:
	if shots_dir.is_empty():
		return
	var cam := Camera3D.new()
	add_child(cam)
	var focus := p_focus if p_focus != Vector3.ZERO else _village_center()
	cam.global_position = focus + p_offset
	cam.look_at(focus)
	cam.make_current()
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(p_name + ".png"))
	print("villager_e2e: 截图 %s" % p_name)
	player.get_camera().make_current()
	cam.queue_free()


func _finish(p_ok: bool, p_why: String) -> void:
	set_physics_process(false)
	if p_ok:
		print("villager_e2e: PASS（%s）" % _summary())
		get_tree().quit(0)
	else:
		push_error("villager_e2e: FAIL — " + p_why)
		print("villager_e2e: FAIL — " + p_why)
		get_tree().quit(1)
