extends Node
## 采集（world_2051.tscn），只通过输入（动作、转视角）操作，一路不瞬移：
## 主码头 #1 出生，找离得最近的浮石崖（露头的石块挡着船；最近的黏土坑是个平坑，船落得下，测不到「挡着」）
## → 走上船掌舵、升过船坞的门架，先飞到近处村里一座民居上空、降低：野泊不了（船底下有房子）→ 再到采掘面正上空：提示「野泊不了：…挡着」
## → 飞到资源点旁 28 m 的平地上空、降到离地二十来米、停稳：提示「按 E 野泊（舷梯放…）」→ E：状态行「野泊中」，
##   船悬停落定、舷梯从舷门斜下到地面（梯脚贴地），人下船
## → 顺着舷梯走下去、到采掘面：提示按住 E → 按住 E 一份份采进背包、存量跟着少；到堆场按 E 打成一箱抱着（背包少 5 份）
## → 抱着箱子走上舷梯、放上货位 0；再下去采 5 份、打一箱，按 E 拆回背包、再打包，抱上去放货位 2
## → 走到舵旁掌舵：舷梯收起、升空。船上两箱原料，地图上标着这处资源点。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/gather_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_2051.tscn")
const TIMEOUT := 900.0  # 模拟秒
const SETTLE_TIMEOUT := 30.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const HOVER_CLEARANCE := 16.0  # 落点上空停在离地这么高（甲板）
const PER_CRATE := 5

enum Phase { SETTLE, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, FLY_VILLAGE, OVER_HOUSE, FLY_FACE, OVER_FACE, FLY_SITE, HOVER, LAND, WALK_DOWN, GATHER, TO_PILE, PACK,
	CARRY_UP, STOW, TO_HELM, LIFT_OFF, DONE }

var main: Expedition
var world: World
var player: Player
var ship: Ship
var sites: SiteStream

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var step := 0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var held := {}
var waypoints: Array[Vector3] = []
var climb_from := 0.0

var site_id := -1
var site := {}
var item := ""
var face := Vector3.ZERO
var pile := Vector3.ZERO
var house := Vector3.ZERO
var village_id := -1
var landed_at := Vector3.ZERO
var landing := Vector3.ZERO  # 落点（地面上的 xz，y = 悬停的甲板高）
var landing_yaw := 0.0
var crate_no := 0
var bag_before := 0
var stock_before := 0.0
var gathered_total := 0
var saw_working := false
var unpacked := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	sites = main.get_node("SiteStream")


func _log(line: String) -> void:
	print("gather: " + line)


# ---------------------------------------------------------------- 输入

func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _release_all() -> void:
	for action in MOVES:
		Input.action_release(action)


func _tap(action: String) -> void:
	if held.has(action):
		return
	Input.action_press(action)
	held[action] = 4


## 地图这类键走 _unhandled_input：要发一个输入事件（按下、松开），光 action_press 不够
func _press_action(action: String) -> void:
	var ev := InputEventAction.new()
	ev.action = action
	ev.pressed = true
	Input.parse_input_event(ev)
	var up := InputEventAction.new()
	up.action = action
	up.pressed = false
	Input.parse_input_event(up)


func _tick_taps() -> void:
	for action in held.keys():
		held[action] -= 1
		if held[action] <= 0:
			Input.action_release(action)
			held.erase(action)


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


# ---------------------------------------------------------------- 小工具

func _standing() -> bool:
	return player.is_on_floor() and world.is_ready_at(player.global_position)


func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	to.y = 0.0
	if to.length() <= radius:
		_hold("move_forward", false)
		return true
	player.set_look_yaw(atan2(-to.x, -to.z))
	_hold("move_forward", true)
	return false


func _follow_waypoints(radius: float = 0.5) -> bool:
	if waypoints.is_empty():
		return true
	if _walk_toward(waypoints[0], radius):
		waypoints.pop_front()
	return waypoints.is_empty()


func _on_ship(x: float, z: float) -> Vector3:
	return ship.to_global(Vector3(x, 0, z))


func _slot_stand(slot: int) -> Vector3:
	var local := ship.to_local(ship.get_hold_slot_position(slot))
	return _on_ship(-0.9 if local.x < 0.0 else 0.9, local.z)


func _slot_midline(slot: int) -> Vector3:
	return _on_ship(0, ship.to_local(ship.get_hold_slot_position(slot)).z)


func _gangway_out() -> Vector3:
	var d := ship.get_gangway_foot() - ship.get_gangway_top()
	d.y = 0.0
	return d.normalized()


## 从舷门下舷梯、到堆场（再到采掘面）
func _down_to(target: Vector3) -> Array[Vector3]:
	var top := ship.get_gangway_top()
	var foot := ship.get_gangway_foot()
	var out := _gangway_out()
	var path: Array[Vector3] = [ship.global_position, top - out * 0.6, top + out * 1.0, foot + out * 1.5, pile]
	if target != pile:
		path.append(target)
	return path


## 从堆场上舷梯、到货位旁
func _up_to_slot(slot: int) -> Array[Vector3]:
	var top := ship.get_gangway_top()
	var foot := ship.get_gangway_foot()
	var out := _gangway_out()
	return [foot + out * 1.5, foot - out * 0.3, top + out * 0.6, top - out * 0.8, ship.global_position, _slot_midline(slot), _slot_stand(slot)]


func _bag() -> int:
	return main.get_item_count(item)


func _stock() -> float:
	return float(world.get_resource_site(site_id)["stock"])


## 飞向 target（y 是想停的甲板高）：远了全速，近了收油、倒车刹住；到了（水平 p_radius 内、慢下来、高度差不多）返回 true
func _fly_to(target: Vector3, p_radius: float) -> bool:
	var to := target - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))
	var flat := Vector2(to.x, to.z).length()
	var climb := to.y
	var speed := ship.velocity
	var forward_speed := -(ship.global_transform.basis.inverse() * speed).z
	_hold("move_right", flat > p_radius and bearing > 3.0)
	_hold("move_left", flat > p_radius and bearing < -3.0)
	_hold("ascend", climb > 1.5)
	_hold("descend", climb < -1.5)
	# 想要的前进速度：离得越近越慢
	var want := clampf(0.35 * (flat - 0.5 * p_radius), 0.0, 40.0) if absf(bearing) < 50.0 else 0.0
	_hold("move_forward", forward_speed < want - 0.5)
	_hold("move_back", forward_speed > want + 1.5)
	return flat <= p_radius and Vector2(speed.x, speed.z).length() < 1.2 and absf(climb) < 3.0


# ---------------------------------------------------------------- 流程

func _goto(next: Phase) -> void:
	_log("%-12s 用时 %6.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	phase = next
	phase_started = elapsed
	step = 0


## 资源点旁挑一个落点：让采掘面在船的一舷、离 28 m 左右；与掌舵时同一套判定（Expedition.probe_wild）
func _pick_landing() -> bool:
	var from := ship.global_position
	var d := Vector3(face.x - from.x, 0, face.z - from.z).normalized()
	var perp := Vector3(-d.z, 0, d.x)
	for r in [28.0, 34.0, 22.0, 40.0]:
		for s in [1.0, -1.0]:
			var p: Vector3 = face + perp * (r * s)
			var ground: float = world.get_surface_height(p.x, p.z)
			if is_nan(ground):
				continue
			var yaw := atan2(-d.x, -d.z)
			var probe: Dictionary = main.probe_wild(Vector3(p.x, ground + HOVER_CLEARANCE, p.z), yaw)
			if probe["ok"]:
				landing = Vector3(p.x, ground + HOVER_CLEARANCE, p.z)
				landing_yaw = yaw
				_log("落点：采掘面旁 %.0f m（%s），舷梯放%s、长 %.1f m" % [r, landing, "右舷" if int(probe["side"]) > 0 else "左舷", float(probe["run"])])
				return true
	return false


func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s）" % Phase.keys()[phase])
		return
	match phase:
		Phase.SETTLE:
			if in_phase > 1.0 and _standing():
				var at := player.global_position
				site_id = world.find_resource_near(at.x, at.z, 3000.0, "floatstone")
				if site_id < 0:
					_finish(false, "主码头 3 km 内该有浮石崖")
					return
				site = world.get_resource_site(site_id)
				item = site["item"]
				face = site["face"]
				pile = site["pile"]
				if not main.get_map().is_resource_marked(site_id):
					_finish(false, "地图上该标着资源点 #%d" % site_id)
					return
				# 已铺出来的村里挑一座离本村船台最远的民居
				var farthest := -1.0
				for vid in sites.get_built_villages():
					var s: Settlement = sites.get_village(vid).get_settlement()
					if s == null or s.get_berth_indices().is_empty():
						continue
					var berth := s.get_building_position(s.get_berth_indices()[0])
					for k in s.get_count():
						var p := s.get_building_position(k)
						var d := Vector2(p.x - berth.x, p.z - berth.z).length()
						if s.get_building_kind(k) == "house" and d > farthest:
							house = p
							farthest = d
							village_id = vid
				if farthest < 0.0:
					if in_phase > 5.0:
						_finish(false, "近处该有铺出来的村")
					return
				_log("资源点 #%d %s（%s）采掘面 %s、堆场 %s，存 %.0f / %d；村%03d 挑的民居 %s（离船台 %.0f m）" % [site_id, site["name"], item, face, pile, float(site["stock"]), int(site["cap"]), village_id, house, farthest])
				_shot("1_spawn")
				var dock: Transform3D = main.get_origin_berth()
				waypoints = [dock.origin + dock.basis.x * 0.8, ship.global_position]
				_goto(Phase.WALK_TO_SHIP)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "出生 %.0f s 还没站稳" % SETTLE_TIMEOUT)
		Phase.WALK_TO_SHIP:
			if _follow_waypoints():
				_goto(Phase.TAKE_HELM)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到船上")
		Phase.TAKE_HELM:
			if main.get_state() == Expedition.PILOTING:
				climb_from = ship.global_position.y
				_goto(Phase.CLIMB_OUT)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_OUT:
			# 先升过旁边船坞的门架（20 m）再转向：船头离原点二十来米，原地转会扫到门架柱
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 24.0:
				_hold("ascend", false)
				_goto(Phase.FLY_VILLAGE)
		Phase.FLY_VILLAGE:
			# 村里一座民居上空，甲板离地 12 m；路上离地 30 m（邑治一大片房子，贴着房顶飞会挂住）
			var far := Vector2(house.x - ship.global_position.x, house.z - ship.global_position.z).length() > 15.0
			var target := Vector3(house.x, house.y + (30.0 if far else 12.0), house.z)
			if _fly_to(target, 4.0):
				_release_all()
				_goto(Phase.OVER_HOUSE)
			elif in_phase > 120.0:
				_finish(false, "120 s 没飞到村%03d 的民居上空（离 %.0f m）" % [village_id, Vector2(house.x - ship.global_position.x, house.z - ship.global_position.z).length()])
		Phase.OVER_HOUSE:
			if in_phase < 0.5:
				return
			# 这村的船台离得近时提示里是「按 E 落上船台」（先靠船台），野泊的判定照样说有房子
			var note := main.get_wild_note()
			if main.has_wild_berth() or not note.contains("房子"):
				_finish(false, "民居上空不该野泊得下（note「%s」，提示「%s」）" % [note, main.get_prompt()])
				return
			_log("民居上空：野泊不了——%s；提示「%s」" % [note, main.get_prompt().replace("\n", " / ")])
			_shot("2_over_house")
			_goto(Phase.FLY_FACE)
		Phase.FLY_FACE:
			# 采掘面正上空：石头顶着船底
			if _fly_to(Vector3(face.x, face.y + HOVER_CLEARANCE - 6.0, face.z), 3.0):
				_release_all()
				_goto(Phase.OVER_FACE)
			elif in_phase > 200.0:
				_finish(false, "200 s 没飞到采掘面上空（离 %.0f m）" % Vector2(face.x - ship.global_position.x, face.z - ship.global_position.z).length())
		Phase.OVER_FACE:
			_fly_to(Vector3(face.x, face.y + HOVER_CLEARANCE - 6.0, face.z), 3.0)
			if in_phase < 0.5:
				return
			if main.has_wild_berth() or not main.get_prompt().contains("野泊不了") or not main.get_wild_note().contains("挡着"):
				if in_phase > 5.0:
					_finish(false, "采掘面上空该野泊不了（东西挡着），实为 note「%s」，提示「%s」" % [main.get_wild_note(), main.get_prompt()])
				return
			_log("采掘面上空：%s" % main.get_prompt().replace("\n", " / "))
			_shot("3_over_face")
			_goto(Phase.FLY_SITE)
		Phase.FLY_SITE:
			if step == 0:
				if not _pick_landing():
					_finish(false, "资源点 #%d 旁找不到野泊得下的落点" % site_id)
					return
				step = 1
			if _fly_to(landing, 6.0):
				_release_all()
				_goto(Phase.HOVER)
			elif in_phase > 200.0:
				_finish(false, "200 s 没飞到资源点旁的落点（离 %.0f m）" % Vector2(landing.x - ship.global_position.x, landing.z - ship.global_position.z).length())
		Phase.HOVER:
			# 停稳在落点上空：野泊的提示要跟上（HUD 比测试晚一帧）
			_fly_to(landing, 6.0)
			if main.has_wild_berth() and main.get_prompt().contains("按 E 野泊"):
				_release_all()
				_log("落点上空：%s（船 %s）" % [main.get_prompt(), ship.global_position])
				_shot("4_hover")
				_tap("interact")
				_goto(Phase.LAND)
			elif in_phase > 20.0:
				_finish(false, "落点上空 20 s 没提示野泊（note「%s」，提示「%s」）" % [main.get_wild_note(), main.get_prompt()])
		Phase.LAND:
			if main.get_state() == Expedition.MOORING and step == 0:
				step = 1
				if not main.get_status_text().contains("野泊中"):
					_finish(false, "野泊时状态行该写「野泊中」（%s）" % main.get_status_text())
					return
			if main.get_state() == Expedition.ON_FOOT:
				if not ship.has_gangway():
					_finish(false, "野泊落定后该放下舷梯（%s）" % main.get_notice())
					return
				var foot := ship.get_gangway_foot()
				var ground: float = world.get_surface_height(foot.x, foot.z)
				var below: float = world.get_surface_height(ship.global_position.x, ship.global_position.z)
				if absf(foot.y - ground) > 0.8 or ship.global_position.y - below < 4.0 or ship.get_speed() > 0.01:
					_finish(false, "舷梯脚该贴地（梯脚 %.2f，地面 %.2f）、船悬停在地面上方（甲板离地 %.1f m，速度 %.2f）" % [foot.y, ground, ship.global_position.y - below, ship.get_speed()])
					return
				if not main.get_notice().contains("舷梯放在"):
					_finish(false, "落定时该通知舷梯放在哪一舷（%s）" % main.get_notice())
					return
				landed_at = ship.global_position
				_log("野泊落定：船 %s，甲板离地 %.1f m；%s，梯脚 %s" % [ship.global_position, ship.global_position.y - below, main.get_notice(), foot])
				_shot("5_landed")
				waypoints = _down_to(face)
				_goto(Phase.WALK_DOWN)
			elif in_phase > 40.0:
				_finish(false, "40 s 没野泊落定（%s；%s）" % [main.get_prompt(), main.get_notice()])
		Phase.WALK_DOWN:
			if _follow_waypoints(0.6):
				_release_all()
				_goto(Phase.GATHER)
			elif in_phase > 90.0:
				_finish(false, "90 s 没走下舷梯到采掘面（%s，还剩 %d 个路点）" % [player.global_position, waypoints.size()])
		Phase.GATHER:
			match step:
				0:
					var prompt := main.get_prompt()
					if not prompt.contains("按住 E"):
						if in_phase > 3.0:
							_finish(false, "采掘面前该提示按住 E 采，实为「%s」" % prompt)
						return
					if crate_no == 0:
						var status := main.get_status_text()
						if not status.contains(str(site["name"]).substr(0, 3)) or not status.contains("存"):
							_finish(false, "走到资源点跟前 HUD 该报它的名字与存量（%s）" % status)
							return
						_log("采掘面前：%s" % prompt)
						_shot("6_face")
					bag_before = _bag()
					stock_before = _stock()
					saw_working = false
					Input.action_press("interact")  # 按住
					step = 1
				1:
					if main.is_gathering() and main.get_prompt().contains("中 "):
						saw_working = true
					if _bag() >= bag_before + PER_CRATE:
						Input.action_release("interact")
						var got := _bag() - bag_before
						var used := stock_before - _stock()
						if not saw_working or used < got - 0.6 or used > got + 0.01:
							_finish(false, "按住 E 该一份份采：背包 +%d，存量少了 %.2f（干活中提示 %s）" % [got, used, saw_working])
							return
						gathered_total += got
						_log("采了 %d 份%s：背包 %d，存量 %.1f → %.1f（%s）" % [got, item, _bag(), stock_before, _stock(), main.get_notice()])
						waypoints = [pile]
						_goto(Phase.TO_PILE)
					elif in_phase > 60.0:
						Input.action_release("interact")
						_finish(false, "60 s 没采满 %d 份（背包 %d → %d，提示「%s」）" % [PER_CRATE, bag_before, _bag(), main.get_prompt()])
		Phase.TO_PILE:
			if _follow_waypoints(0.6):
				_release_all()
				if crate_no == 0:
					var back := ship.global_position - player.global_position
					player.set_look_yaw(atan2(-back.x, -back.z))
					_shot("6b_ship_from_pile")
				_goto(Phase.PACK)
			elif in_phase > 30.0:
				_finish(false, "30 s 没走到堆场")
		Phase.PACK:
			match step:
				0:
					if in_phase < 0.3 or not main.get_prompt().contains("打成一箱"):
						if in_phase > 3.0:
							_finish(false, "堆场前该提示打包，实为「%s」" % main.get_prompt())
						return
					bag_before = _bag()
					_tap("interact")
					step = 1
					phase_started = elapsed
				1:
					if in_phase > 0.5:
						if not main.is_carrying() or not main.is_holding_goods() or _bag() != bag_before - PER_CRATE:
							_finish(false, "堆场按 E 该从背包打一箱抱着（背包 %d → %d，%s）" % [bag_before, _bag(), main.get_notice()])
							return
						_log("打包：%s" % main.get_notice())
						if crate_no == 1 and not unpacked:
							step = 2  # 第二箱：先拆回背包试一下
							phase_started = elapsed
							return
						_shot("7_packed_%d" % crate_no)
						waypoints = _up_to_slot(0 if crate_no == 0 else 2)
						_goto(Phase.CARRY_UP)
				2:
					if not main.get_prompt().contains("拆回背包"):
						if in_phase > 2.0:
							_finish(false, "抱着自家的箱子在堆场该提示拆回背包，实为「%s」" % main.get_prompt())
						return
					_tap("interact")
					step = 3
					phase_started = elapsed
				3:
					if in_phase > 0.5:
						if main.is_carrying() or _bag() != bag_before:
							_finish(false, "拆箱该放回背包（背包 %d，应 %d；%s）" % [_bag(), bag_before, main.get_notice()])
							return
						_log("拆箱：%s" % main.get_notice())
						unpacked = true
						step = 0
						phase_started = elapsed
		Phase.CARRY_UP:
			if _follow_waypoints(0.4):
				_release_all()
				_goto(Phase.STOW)
			elif in_phase > 120.0:
				_finish(false, "120 s 没抱着箱子走上舷梯到货位旁（%s，还剩 %d 个路点）" % [player.global_position, waypoints.size()])
		Phase.STOW:
			var slot := 0 if crate_no == 0 else 2
			match step:
				0:
					if not main.get_prompt().contains("放上货位"):
						if in_phase > 3.0:
							_finish(false, "货位 %d 旁该提示放上货位，实为「%s」" % [slot, main.get_prompt()])
						return
					_tap("interact")
					step = 1
					phase_started = elapsed
				1:
					if in_phase > 0.5:
						if main.is_carrying() or ship.get_cargo(slot) != item or main.get_cargo_items()[slot] != item:
							_finish(false, "该放上货位 %d（货位上是「%s」，%s）" % [slot, ship.get_cargo(slot), main.get_notice()])
							return
						_log("放上货位 %d：船上 %d 箱；%s" % [slot, ship.get_cargo_count(), main.get_status_text().replace("\n", " / ")])
						_shot("8_stowed_%d" % slot)
						if crate_no == 0:
							crate_no = 1
							waypoints = [_slot_midline(0)]
							waypoints.append_array(_down_to(face))
							_goto(Phase.WALK_DOWN)
						else:
							waypoints = [_slot_midline(2), ship.get_helm_position()]
							_goto(Phase.TO_HELM)
		Phase.TO_HELM:
			if main.get_state() == Expedition.PILOTING:
				if ship.global_position.distance_to(landed_at) > 0.01:
					_finish(false, "野泊着的船该一动不动（挪了 %.3f m）" % ship.global_position.distance_to(landed_at))
					return
				if ship.has_gangway():
					_finish(false, "上舵后舷梯该收起")
					return
				climb_from = ship.global_position.y
				_goto(Phase.LIFT_OFF)
			elif waypoints.size() > 1:
				_follow_waypoints(0.5)
			elif _walk_toward(waypoints[0], 1.9):
				_tap("interact")
			elif in_phase > 60.0:
				_finish(false, "60 s 没回到舵旁掌舵（%s）" % main.get_prompt())
		Phase.LIFT_OFF:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 10.0:
				_hold("ascend", false)
				_shot("9_lift_off")
				_goto(Phase.DONE)
			elif in_phase > 20.0:
				_finish(false, "20 s 没升空")
		Phase.DONE:
			if step == 0:
				_press_action("map")
				step = 1
				return
			if step == 1:
				if main.is_map_open() and in_phase > 0.5:
					main.get_map().look_at_world(face.x, face.z, 3000.0)
					_shot("10_map")
					step = 2
					phase_started = elapsed
				elif in_phase > 3.0:
					_finish(false, "按 M 该开地图")
				return
			if step == 2 and in_phase > 0.3:
				_press_action("map")
				step = 3
				phase_started = elapsed
				return
			if step == 3 and in_phase > 0.3:
				var cargo := main.get_cargo_items()
				if ship.get_cargo_count() != 2 or cargo[0] != item or cargo[2] != item or gathered_total != 2 * PER_CRATE:
					_finish(false, "船上该有两箱%s（%s），共采 %d 份" % [item, cargo, gathered_total])
					return
				_finish(true, "主码头 → 村%03d 民居上空野泊被拒 → 资源点 #%d %s 旁野泊、舷梯落地 → 采 %d 份%s、打两箱（拆过一次）、走上舷梯装上货位 0 / 2 → 上舵收梯升空；存量还剩 %.1f / %d" % [
					village_id, site_id, site["name"], gathered_total, item, _stock(), int(site["cap"])])


func _finish(ok: bool, why: String) -> void:
	_release_all()
	Input.action_release("interact")
	for action in held.keys():
		Input.action_release(action)
	set_physics_process(false)
	print("gather: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，船 %s，状态 %d，提示「%s」，通知「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, ship.global_position, main.get_state(), main.get_prompt(), main.get_notice()])
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
