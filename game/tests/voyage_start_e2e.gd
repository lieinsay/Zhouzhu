extends Node
## 开局那群（world_seed42.tscn，开局配置 story_start.h：#6329）里跑一趟船，只通过输入（动作、鼠标、转视角）操作：
## 主码头的停机坪上出生 → 旁边的邑治（#6329 是村001、486 户，离主码头 1.2 km）已经铺出来、房子站在地上、地图上的码头与村子和 3D 里一一对应
## → M 开地图点主岛上离主码头 7–13 km 户数最多的村（#6329 是 12 km 外的村002、298 户）瞬移过去：房子站在地上、村民陆续出门 → 再开地图点回主码头
## → 走过栈桥上船 → 掌舵、升空、R 换到疾档 → 飞到场景配的目的码头（#6329 是岛 #13 上的码头 #28，约 23 km，岛 #13 比主岛低 150 m；途中贴着地形保持高度）
## → 从岸外的进场点（泊位沿栈桥往外 400 m）进港、近了换回泊档 → 按 E 靠泊 → 下船走上目的岛的停机坪 → 码头边的村子房子站在地上、有人出门。
## 村、码头都从开局那群的点位挑（world.get_info()），户数的金标 = 户数 + 公房（邑治另有堂屋、仓，每村一座船台）。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/voyage_start_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 1500.0  # 模拟秒
const SETTLE_TIMEOUT := 30.0
const FOLK_TIMEOUT := 60.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const FAR_MIN_M := 7000.0  # 远处那个村：离主码头 7–13 km（村子 6 km 内才铺简模：出生时它还没铺）、主岛上户数最多的
const FAR_MAX_M := 13000.0
const CRUISE_CLEARANCE := 60.0  # 飞越陆地时离地至少这么高
const SLOW_DOWN_AT := 2000.0  # 离进场点这么近换回泊档
const APPROACH_OUT_M := 400.0  # 进场点：泊位往岸外这么远

enum Phase { SETTLE, CHECK_HOME, MAP_TO_FAR, FAR_VILLAGE, MAP_TO_DOCK, BACK_AT_DOCK, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, GEAR_UP, CRUISE, APPROACH, MOORING, WALK_ASHORE, ASHORE_VILLAGE, VISIT, DONE }

var main: Expedition
var world: World
var player: Player
var ship: Ship
var sites: SiteStream
var map: WorldMap

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var held := {}  # 动作 → 还要按住几帧
var waypoints: Array[Vector3] = []
var climb_from := 0.0
var map_step := 0
var dest_index := -1
var dest_site := {}  # 目的码头边上最近的村 / 散户（world.json 的一行）
var dest_is_hamlet := false
var cruise_top := 0.0
var min_clearance := INF
var cruise_shots := 0
var log_lines: Array[String] = []
var seat_id := -1  # 邑治（world.json 标 seat 的那一村）
var seat_households := 0
var far_id := -1
var far_households := 0
var far_km := 0.0


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)  # 无头时根视口默认很小，地图上的点没处可点
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	sites = main.get_node("SiteStream")
	map = main.get_map()
	dest_index = world.find_dock(main.destination_dock)
	var info: Dictionary = world.get_info()
	# 邑治；主岛上离主码头 FAR_MIN_M–FAR_MAX_M、户数最多的村
	var home := world.get_dock_position(world.find_dock(main.origin_dock))
	for row in info["villages"]:
		if bool(row.get("seat", false)):
			seat_id = int(row["id"])
			seat_households = int(row["households"])
		var d := Vector2(row["pos"][0] - home.x, row["pos"][1] - home.z).length()
		if int(row["island"]) == 0 and not bool(row.get("seat", false)) and d >= FAR_MIN_M and d <= FAR_MAX_M and int(row["households"]) > far_households:
			far_id = int(row["id"])
			far_households = int(row["households"])
			far_km = d / 1000.0
	# 目的码头边上最近的聚落（#6329 的码头 #28 边上是村112）
	var dock := world.get_dock_position(dest_index)
	var best := INF
	for key in ["villages", "hamlets"]:
		for row in info[key]:
			var d := Vector2(row["pos"][0] - dock.x, row["pos"][1] - dock.z).length()
			if int(row["island"]) == int(world.get_island_at(dock.x, dock.z)) and d < best:
				best = d
				dest_site = row
				dest_is_hamlet = key == "hamlets"
	_log("出生 %s，船 %s，目的泊位 %s（%.1f km）；目的码头边最近的是 %s（%d 户，%.0f m）" % [
		player.global_position, ship.global_position, main.get_destination_berth().origin,
		ship.global_position.distance_to(main.get_destination_berth().origin) / 1000.0,
		dest_site.get("name", "?"), int(dest_site.get("households", 0)), best])
	_log("群 #%d：邑治村%03d（%d 户）；远处的村%03d（%d 户，离主码头 %.1f km）" % [StoryStart.get_spawn_node(), seat_id, seat_households, far_id, far_households, far_km])


func _log(line: String) -> void:
	print("voyage_start: " + line)


# ---------------------------------------------------------------- 输入

func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _release_all() -> void:
	for action in MOVES:
		Input.action_release(action)


## 按一下：按住几帧再松（当帧按下一帧就松，物理帧里读不到「刚按下」）
func _tap(action: String) -> void:
	if held.has(action):
		return
	Input.action_press(action)
	held[action] = 4


func _tick_taps() -> void:
	for action in held.keys():
		held[action] -= 1
		if held[action] <= 0:
			Input.action_release(action)
			held.erase(action)


func _mouse(button: MouseButton, at: Vector2) -> void:
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = button
		ev.position = at
		ev.global_position = at
		ev.pressed = pressed
		Input.parse_input_event(ev)


func _press_key_action(action: String) -> void:
	var ev := InputEventAction.new()
	ev.action = action
	ev.pressed = true
	Input.parse_input_event(ev)
	var up := InputEventAction.new()
	up.action = action
	up.pressed = false
	Input.parse_input_event(up)


# ---------------------------------------------------------------- 截图

func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _shot(shot_name: String) -> void:
	pending_shot = shot_name
	shot_wait = 3


# ---------------------------------------------------------------- 断言用的小工具

func _standing_on_ground() -> bool:
	var at := player.global_position
	var ground: float = world.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.0 and world.is_ready_at(at)


## 村子里每座房子都站在地上：还站得住（勘察照过）、地坪在地面之上一个台基高左右。返回空串 = 都好
func _houses_stand(v: WorldVillage) -> String:
	if v == null:
		return "村子没铺出来"
	var s: Settlement = v.get_settlement()
	if s == null or s.get_count() == 0:
		return "村子里没有房子"
	for i in s.get_count():
		var why := s.survey_building(i)
		if not why.is_empty():
			return "%s 第 %d 座站不住：%s" % [v.get_site_name(), i, why]
		var at: Vector3 = s.get_building_position(i)
		var ground: float = world.get_surface_height(at.x, at.z)
		if is_nan(ground):
			return "%s 第 %d 座底下没有地" % [v.get_site_name(), i]
		var lift := at.y - ground
		if lift < 0.2 or lift > 4.0:
			return "%s 第 %d 座地坪离地 %.2f m（悬空或埋进地里）" % [v.get_site_name(), i, lift]
	return ""


func _folk_walking(v: WorldVillage) -> int:
	if v == null or v.get_villagers() == null:
		return 0
	var folk: Villagers = v.get_villagers()
	var n := 0
	for i in folk.get_villager_count():
		if folk.get_villager_phase(i) == Villagers.WALKING:
			n += 1
	return n


func _dest_village() -> WorldVillage:
	return sites.get_hamlet(int(dest_site["id"])) if dest_is_hamlet else sites.get_village(int(dest_site["id"]))


# ---------------------------------------------------------------- 走与飞

## 朝目标走；到了返回 true。转视角就是真人转鼠标那一下，其余只按前进。
func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	to.y = 0.0
	if to.length() <= radius:
		_hold("move_forward", false)
		return true
	player.set_look_yaw(atan2(-to.x, -to.z))
	_hold("move_forward", true)
	return false


func _follow_waypoints() -> bool:
	if waypoints.is_empty():
		return true
	if _walk_toward(waypoints[0], 0.6):
		waypoints.pop_front()
	return waypoints.is_empty()


## 船头前方 2 km 内（沿着去泊位的方向）最高的地面
func _terrain_ahead(berth: Vector3) -> float:
	var from := ship.global_position
	var dir := Vector3(berth.x - from.x, 0, berth.z - from.z)
	var flat := dir.length()
	dir = dir.normalized()
	var top := -INF
	var d := 0.0
	while d <= minf(2000.0, flat):
		for side in [-60.0, 0.0, 60.0]:
			var p: Vector3 = from + dir * d + Vector3(-dir.z, 0, dir.x) * side
			var h: float = world.get_surface_height(p.x, p.z)
			if not is_nan(h):
				top = maxf(top, h)
		d += 50.0
	return top


## 朝泊位飞：舵对准方位；高度在陆地上空贴着地形留足余量，近了对准泊位略高处；远了进车近了滑行。进入可靠泊的距离返回 true。
## p_hug：离目标这么远以外才贴着地形留余量（朝岸外的进场点飞时一路都留）
func _fly_toward(berth: Vector3, p_hug := 600.0) -> bool:
	var to := berth - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))  # 正 = 目标在右舷
	var flat := Vector2(to.x, to.z).length()
	var want_y := berth.y + 8.0
	if flat > p_hug:
		var ground := _terrain_ahead(berth)
		if ground > -INF:
			want_y = maxf(want_y, ground + CRUISE_CLEARANCE)
	var climb := want_y - ship.global_position.y

	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 2.0)
	_hold("descend", climb < -2.0)
	# 高度差太多先别往前冲，免得撞上山
	_hold("move_forward", flat > 30.0 and absf(bearing) < 60.0 and climb < 40.0)
	var below: float = world.get_surface_height(ship.global_position.x, ship.global_position.z)
	if not is_nan(below):
		min_clearance = minf(min_clearance, ship.global_position.y - below)
	cruise_top = maxf(cruise_top, ship.global_position.y)
	return to.length() < main.get_dock_range() - 5.0


# ---------------------------------------------------------------- 流程

func _goto(next: Phase) -> void:
	_log("%-14s 用时 %6.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	phase = next
	phase_started = elapsed
	map_step = 0


func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s）" % Phase.keys()[phase])
		return
	match phase:
		Phase.SETTLE:
			if in_phase > 1.0 and _standing_on_ground():
				if world.get_island_at(player.global_position.x, player.global_position.z) != 0:
					_finish(false, "出生点不在主岛")
					return
				if sites.get_dock(main.origin_dock) == null or sites.get_dock(main.destination_dock) != null:
					_finish(false, "主码头的栈桥该铺出来、23 km 外的目的码头该还没铺（%s）" % [sites.get_built_docks()])
					return
				_shot("1_spawn_dock")
				_goto(Phase.CHECK_HOME)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "出生 %.0f s 还没站稳：%s" % [SETTLE_TIMEOUT, player.global_position])
		Phase.CHECK_HOME:
			# 等旁边的邑治铺出来（每帧最多铺一处，由近及远）
			if seat_id < 0 or far_id < 0:
				_finish(false, "开局那群里找不到邑治（%d）或离主码头 %.0f–%.0f km 的村（%d）" % [seat_id, FAR_MIN_M / 1000.0, FAR_MAX_M / 1000.0, far_id])
				return
			var near := sites.get_village(seat_id)
			if near == null:
				if in_phase > 10.0:
					_finish(false, "主码头旁的邑治没铺出来（已铺 %s）" % [sites.get_built_villages()])
				return
			var why := _houses_stand(near)
			if not why.is_empty():
				_finish(false, why)
				return
			var count: int = near.get_settlement().get_count()
			if count != seat_households + 3:
				_finish(false, "邑治 %d 户该有 %d 座民居 + 堂屋 + 仓 + 船台，实为 %d 座" % [seat_households, seat_households, count])
				return
			if sites.get_village(far_id) != null:
				_finish(false, "%.1f km 外的村%03d 不该铺出来" % [far_km, far_id])
				return
			_log("主码头边的%s：%d 座房子都站在地上；已铺村 %s、散户 %d 处、码头 %s" % [
				near.get_site_name(), count, sites.get_built_villages(), sites.get_built_hamlet_count(), sites.get_built_docks()])
			_goto(Phase.MAP_TO_FAR)
		Phase.MAP_TO_FAR:
			match map_step:
				0:
					_press_key_action("map")
					map_step = 1
				1:
					if main.is_map_open():
						# 地图上的码头、村子与 3D 里一一对应
						var dock_node := sites.get_dock(main.origin_dock)
						var marker := map.get_dock_marker(main.origin_dock)
						if Vector2(marker.x - dock_node.global_position.x, marker.z - dock_node.global_position.z).length() > 1.0:
							_finish(false, "地图上的主码头 %s 与 3D 里的栈桥根 %s 对不上" % [marker, dock_node.global_position])
							return
						if not map.is_dock_marked_berthable(main.origin_dock) or not map.is_dock_marked_berthable(main.destination_dock):
							_finish(false, "出发、目的码头在地图上该标成能靠泊")
							return
						if not map.is_village_marked_built(seat_id) or map.is_village_marked_built(far_id):
							_finish(false, "地图上该标出已铺的邑治、不标还没铺的村%03d" % far_id)
							return
						map_step = 2
				2, 3, 4, 5, 6, 7, 8, 9, 10, 11:
					# 滚轮缩小（一次一格），直到远处那个村在屏幕里
					var far := _village_pos(far_id) + Vector3(0, 0, 250)
					var screen := get_viewport().get_visible_rect().size
					if Rect2(Vector2(40, 90), screen - Vector2(80, 180)).has_point(map.world_to_screen(far.x, far.z)):
						_log("地图缩小了 %d 格，村%03d 在屏幕 %s" % [map_step - 2, far_id, map.world_to_screen(far.x, far.z)])
						map_step = 20
					elif map_step == 11:
						_finish(false, "地图缩小 9 格，村%03d 还不在屏幕里（%s）" % [far_id, map.world_to_screen(far.x, far.z)])
					elif in_phase > 0.1 * map_step:
						_mouse(MOUSE_BUTTON_WHEEL_DOWN, Vector2(640, 360))
						map_step += 1
				20:
					_shot("2_map_home")
					map_step = 21
				21:
					if in_phase > 1.5:
						# 点远处那个村南边 250 m（村心一圈是房子，别落在房顶上）
						var far := _village_pos(far_id) + Vector3(0, 0, 250)
						_mouse(MOUSE_BUTTON_LEFT, map.world_to_screen(far.x, far.z))
						_goto(Phase.FAR_VILLAGE)
		Phase.FAR_VILLAGE:
			var far_village := sites.get_village(far_id)
			if in_phase > 1.0 and _standing_on_ground() and far_village != null:
				if map_step == 0:
					var why := _houses_stand(far_village)
					if not why.is_empty():
						_finish(false, why)
						return
					var count: int = far_village.get_settlement().get_count()
					if count != far_households + 1:
						_finish(false, "%s %d 户该有 %d 座民居 + 船台，实为 %d 座" % [far_village.get_site_name(), far_households, far_households, count])
						return
					_log("%s：%d 座房子都站在地上，村民 %d 人" % [far_village.get_site_name(), count, far_village.get_villagers().get_villager_count() if far_village.get_villagers() else 0])
					player.set_look_yaw(0.0)  # 望着村子：落点在村心南边 250 m，look_yaw 0 朝北
					map_step = 1
				elif _folk_walking(far_village) > 0:
					_log("%s：%.1f s 后有 %d 人出了门" % [far_village.get_site_name(), in_phase, _folk_walking(far_village)])
					_shot("3_far_village")
					_goto(Phase.MAP_TO_DOCK)
				elif in_phase > FOLK_TIMEOUT:
					_finish(false, "%s %.0f s 里没人出门" % [far_village.get_site_name(), FOLK_TIMEOUT])
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "瞬移到村%03d %.0f s 还没站稳 / 没铺出来：%s" % [far_id, SETTLE_TIMEOUT, player.global_position])
		Phase.MAP_TO_DOCK:
			match map_step:
				0:
					_press_key_action("map")
					map_step = 1
				1, 2, 3, 4, 5, 6:
					if main.is_map_open():
						# 对着出生点滚轮放大几格，点得准
						var spawn := main.get_spawn_point()
						_mouse(MOUSE_BUTTON_WHEEL_UP, map.world_to_screen(spawn.x, spawn.z))
						map_step += 1
				7:
					var spawn := main.get_spawn_point()
					_mouse(MOUSE_BUTTON_LEFT, map.world_to_screen(spawn.x, spawn.z))
					_goto(Phase.BACK_AT_DOCK)
		Phase.BACK_AT_DOCK:
			if in_phase > 1.0 and _standing_on_ground():
				var off := player.global_position.distance_to(main.get_spawn_point())
				if off > 15.0:
					_finish(false, "点回主码头落在 %.0f m 外" % off)
					return
				if sites.get_village(far_id) != null and sites.get_village(far_id).has_folk():
					_finish(false, "离开村%03d 后它的村民该散掉" % far_id)
					return
				var berth: Transform3D = main.get_origin_berth()
				waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
				_goto(Phase.WALK_TO_SHIP)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "点回主码头 %.0f s 还没站稳" % SETTLE_TIMEOUT)
		Phase.WALK_TO_SHIP:
			if _follow_waypoints():
				_goto(Phase.TAKE_HELM)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到船上")
		Phase.TAKE_HELM:
			if main.get_state() == 1:
				climb_from = ship.global_position.y
				_shot("4_helm")
				_goto(Phase.CLIMB_OUT)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_OUT:
			# 先升起来再转向：船身平行于岸，原地转会把船尾扫到栈桥上
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 8.0:
				_hold("ascend", false)
				_goto(Phase.GEAR_UP)
		Phase.GEAR_UP:
			if ship.get_gear() >= 2:
				_log("换到%s档，极速 %.0f m/s" % [ship.get_gear_name(), ship.get_max_speed()])
				_goto(Phase.CRUISE)
			elif not held.has("gear_up"):
				_tap("gear_up")
		Phase.CRUISE:
			# 先飞到泊位外 APPROACH_OUT_M 的进场点（从岸外进港：目的码头不一定朝着来的方向，别从岛上空斜插下来撞坪）
			var berth: Vector3 = _approach_point()
			_fly_toward(berth, 0.0)
			var flat := Vector2(berth.x - ship.global_position.x, berth.z - ship.global_position.z).length()
			if cruise_shots == 0 and in_phase > 20.0:
				cruise_shots = 1
				_shot("5_cruise")
			elif cruise_shots == 1 and flat < 12000.0:
				cruise_shots = 2
				_log("离进场点 12 km：航速 %.0f m/s，高 %.0f m，途中最低离地 %.0f m" % [ship.get_speed(), ship.global_position.y, min_clearance])
				_shot("6_cruise_far")
			if flat < SLOW_DOWN_AT:
				_goto(Phase.APPROACH)
		Phase.APPROACH:
			if ship.get_gear() > 0 and not held.has("gear_down"):
				_tap("gear_down")
			if map_step == 0:
				# 泊档到进场点，再对着泊位进去
				var p := _approach_point()
				_fly_toward(p, 0.0)
				if Vector2(p.x - ship.global_position.x, p.z - ship.global_position.z).length() < 40.0:
					_log("到了进场点（泊位外 %.0f m），高 %.0f m；对着泊位进港" % [APPROACH_OUT_M, ship.global_position.y])
					map_step = 1
			elif _fly_toward(main.get_destination_berth().origin):
				_release_all()
				_shot("7_approach")
				_tap("interact")
				_goto(Phase.MOORING)
		Phase.MOORING:
			if main.get_state() == 0:
				var berth: Transform3D = main.get_destination_berth()
				waypoints = [ship.global_position, berth.origin + berth.basis.x * 0.8, world.get_dock_spawn(dest_index).origin]
				_shot("8_moored")
				_goto(Phase.WALK_ASHORE)
			elif in_phase > 5.0 and main.get_state() == 1:
				_tap("interact")  # 没叫上靠泊（离得还远）：再按一下
			elif in_phase > 40.0:
				_finish(false, "40 s 没靠上泊位（状态 %d，%s）" % [main.get_state(), main.get_prompt()])
		Phase.WALK_ASHORE:
			if _follow_waypoints():
				_release_all()
				if main.has_arrived() and _standing_on_ground():
					var island: int = world.get_island_at(player.global_position.x, player.global_position.z)
					if island != int(dest_site["island"]):
						_finish(false, "下了船站在岛 #%d 上，不是目的码头那座岛" % island)
						return
					_log("到了：站在岛 #%d 的停机坪上 %s" % [island, player.global_position])
					_shot("9_ashore")
					_goto(Phase.ASHORE_VILLAGE)
				elif in_phase > 20.0:
					_finish(false, "走完了路点，但没判定抵达（站稳 %s）" % _standing_on_ground())
			elif in_phase > 60.0:
				_finish(false, "60 s 没走下船")
		Phase.ASHORE_VILLAGE:
			var v := _dest_village()
			if v == null:
				if in_phase > 10.0:
					_finish(false, "目的码头边的%s没铺出来" % dest_site["name"])
				return
			if map_step == 0:
				var why := _houses_stand(v)
				if not why.is_empty():
					_finish(false, why)
					return
				# 面朝那户人家
				var to := v.global_position - player.global_position
				player.set_look_yaw(atan2(-to.x, -to.z))
				map_step = 1
			elif _folk_walking(v) > 0:
				_log("%s：%d 座房子站在地上，%.1f s 后有人出门" % [v.get_site_name(), v.get_settlement().get_count(), in_phase])
				_goto(Phase.VISIT)
			elif in_phase > FOLK_TIMEOUT:
				_finish(false, "%s %.0f s 里没人出门（村民 %d）" % [v.get_site_name(), FOLK_TIMEOUT, v.get_villagers().get_villager_count() if v.get_villagers() else 0])
		Phase.VISIT:
			# 只为截图：疾跑到那户人家门前 35 m 看一眼（走不到也不算失败，断言上面都做过了）
			var v := _dest_village()
			var house: Vector3 = v.get_settlement().get_building_position(0)
			_hold("sprint", true)
			if _walk_toward(house, 35.0) or in_phase > 150.0:
				_release_all()
				var to := house - player.global_position
				player.set_look_yaw(atan2(-to.x, -to.z))
				_log("走到%s跟前 %.0f m" % [v.get_site_name(), Vector2(to.x, to.z).length()])
				_shot("10_dest_village")
				_goto(Phase.DONE)
		Phase.DONE:
			if in_phase > 0.3:
				_finish(true, "群 #%d 主码头出生（邑治村%03d %d 户在旁）→ 村%03d（%d 户，%.1f km）→ 回主码头 → 上舵换档飞 %.1f km → 码头 #%d 靠泊下船；途中最高 %.0f m、最低离地 %.0f m" % [
					StoryStart.get_spawn_node(), seat_id, seat_households, far_id, far_households, far_km,
					main.get_origin_berth().origin.distance_to(main.get_destination_berth().origin) / 1000.0, main.destination_dock, cruise_top, min_clearance])


## 进场点：目的泊位沿栈桥往岸外 APPROACH_OUT_M（岸缘点 → 泊位的方向），高同泊位
func _approach_point() -> Vector3:
	var berth: Vector3 = main.get_destination_berth().origin
	var root := world.get_dock_position(dest_index)
	var out := Vector3(berth.x - root.x, 0, berth.z - root.z).normalized()
	return berth + out * APPROACH_OUT_M


func _village_pos(id: int) -> Vector3:
	for v in world.get_info()["villages"]:
		if int(v["id"]) == id:
			return Vector3(v["pos"][0], 0, v["pos"][1])
	return Vector3.ZERO


func _finish(ok: bool, why: String) -> void:
	_release_all()
	for action in held.keys():
		Input.action_release(action)
	set_physics_process(false)
	print("voyage_start: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，船 %s，状态 %d，提示「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, ship.global_position, main.get_state(), main.get_prompt()])
	# 截图还在排队就等它存完
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
