extends Node
## 跑货（world_2051.tscn），只通过输入（动作、转视角）操作，一路不瞬移：
## 主码头 #1 出生：船上带一节储存区、4 个货位；村031、窑村37（专业聚落也是村）都有船台
## → 走上船掌舵，飞 2.5 km 到村031 的船台，提示「按 E 落上船台」→ 落上去：船舷贴着跳板顶
## → 走下跳板到村仓（跳板脚前）：提示里有托运 → E 接下、打包第一箱：村仓少 5 份，人抱着箱子
## → 抱着箱子走上跳板：慢、跳不起来；到舵旁按 E 被拒（抱着箱子掌不了舵）→ 放上 0 号货位
## → 下跳板打包第二箱、上来放上 2 号货位 → 掌舵：航向行指着托运的目的村 → 飞 7.7 km 到 8 号岛窑村37 的船台落下
## → 从货位抱起一箱走下跳板交货：窑村37 的仓多 5 份、还没给钱 → 再抱一箱交：交齐，背包里多了运费、这单结了；两边村仓一共挪了 10 份。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/freight_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_2051.tscn")
const TIMEOUT := 900.0  # 模拟秒
const SETTLE_TIMEOUT := 30.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const ORIGIN_VILLAGE := 31  # 村031：主码头 2.5 km 外、66 户
const DEST_VILLAGE := 197  # 窑村37：8 号岛上、离村031 最近的别岛村子
const ORIGIN_STORE := 132  # 村031 开账时的土产：66 户 × 2
const CRUISE_CLEARANCE := 60.0
const SLOW_DOWN_AT := 1500.0

enum Phase { SETTLE, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, FLY, MOORING, TO_POST, PACK, CARRY_UP, HELM_REFUSED, STOW,
	CARRY_DOWN, DELIVER, DONE }

var main: Expedition
var world: World
var player: Player
var ship: Ship
var sites: SiteStream
var hinterland: Hinterland

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var step := 0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var held := {}
var waypoints: Array[Vector3] = []
var leg := 0  # 0：飞去起运村，1：飞去目的村
var crate_no := 0  # 这一趟搬的第几箱（0、1）
var item := ""
var order := {}
var origin_before := 0
var dest_before := -1
var climb_from := 0.0
var min_clearance := INF
var helm_tried := false
var carry_top_speed := 0.0
var carry_rise := 0.0
var jump_tapped := false
var centers := {}  # 村号 → 村心
var names := {}  # 村号 → 名字


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
	hinterland = main.get_node("Hinterland")
	for v in world.get_info()["villages"]:
		centers[int(v["id"])] = Vector3(v["pos"][0], 0, v["pos"][1])
		names[int(v["id"])] = v["name"]
	_log("出生 %s；%s 村心 %s，%s 村心 %s" % [player.global_position, names[ORIGIN_VILLAGE], centers[ORIGIN_VILLAGE], names[DEST_VILLAGE], centers[DEST_VILLAGE]])


func _log(line: String) -> void:
	print("freight: " + line)


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


func _store(village_id: int) -> int:
	var summary: Dictionary = hinterland.get_ledger_summary(village_id, false)
	if not summary.get("opened", false):
		return -1
	var store: Dictionary = summary["store"]
	return int(store.get(item, 0))


func _village_id() -> int:
	return ORIGIN_VILLAGE if leg == 0 else DEST_VILLAGE


## 这一程要落的船台所在的聚落（村子还没铺出来、或没有船台就返回 null）
func _berth_settlement(village_id: int) -> Settlement:
	var v := sites.get_village(village_id)
	if v == null or v.get_settlement() == null:
		return null
	var s: Settlement = v.get_settlement()
	return s if s.get_berth_indices().size() > 0 else null


func _berth_contact(village_id: int) -> Transform3D:
	var s := _berth_settlement(village_id)
	return s.get_berth_contact(s.get_berth_indices()[0])


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


## 船上某个货位旁边站的那一点（中线那侧 0.9 m）
func _slot_stand(slot: int) -> Vector3:
	var local := ship.to_local(ship.get_hold_slot_position(slot))
	return _on_ship(-0.9 if local.x < 0.0 else 0.9, local.z)


func _slot_midline(slot: int) -> Vector3:
	return _on_ship(0, ship.to_local(ship.get_hold_slot_position(slot)).z)


## 从舷门（船上）到跳板脚前村仓那一点：舷门 → 跳板顶 → 跳板脚 → 村仓
func _down_gangway(village_id: int) -> Array[Vector3]:
	var contact := _berth_contact(village_id)
	var out: Array[Vector3] = [ship.global_position, contact.origin + contact.basis.x * 0.6]
	out.append(contact.origin + contact.basis.x * 9.0)
	out.append(world.get_freight_post(village_id))
	return out


## 从村仓走上跳板、到货位旁
func _up_gangway(village_id: int, slot: int) -> Array[Vector3]:
	var contact := _berth_contact(village_id)
	var out: Array[Vector3] = [contact.origin + contact.basis.x * 9.0, contact.origin + contact.basis.x * 0.4, ship.global_position]
	out.append(_slot_midline(slot))
	out.append(_slot_stand(slot))
	return out


func _terrain_ahead(target: Vector3) -> float:
	var from := ship.global_position
	var dir := Vector3(target.x - from.x, 0, target.z - from.z)
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


func _fly_toward(target: Vector3) -> bool:
	var to := target - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))
	var flat := Vector2(to.x, to.z).length()
	var want_y := target.y + 8.0
	if flat > 300.0:
		var ground := _terrain_ahead(target)
		if ground > -INF:
			want_y = maxf(want_y, ground + CRUISE_CLEARANCE)
	var climb := want_y - ship.global_position.y
	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 2.0)
	_hold("descend", climb < -2.0)
	_hold("move_forward", flat > 30.0 and absf(bearing) < 60.0 and climb < 40.0)
	var below: float = world.get_surface_height(ship.global_position.x, ship.global_position.z)
	if not is_nan(below):
		min_clearance = minf(min_clearance, ship.global_position.y - below)
	return to.length() < main.get_dock_range() - 5.0


# ---------------------------------------------------------------- 流程

func _goto(next: Phase) -> void:
	_log("%-13s 用时 %6.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	phase = next
	phase_started = elapsed
	step = 0


func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s）" % Phase.keys()[phase])
		return
	match phase:
		Phase.SETTLE:
			if in_phase > 1.0 and _standing() and _berth_settlement(ORIGIN_VILLAGE) != null:
				if ship.get_hold_slot_count() != 4 or int(ship.get_stats()["hold"]) != 4:
					_finish(false, "开局的船该有一节储存区、4 个货位（%d）" % ship.get_hold_slot_count())
					return
				_log("船 %s：货位 4 个，航速 %.1f m/s；%s 的船台接触线在 %s" % [ship.get_layout(), ship.get_max_speed(), names[ORIGIN_VILLAGE], _berth_contact(ORIGIN_VILLAGE).origin])
				_shot("1_spawn")
				var berth: Transform3D = main.get_origin_berth()
				waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
				_goto(Phase.WALK_TO_SHIP)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "出生 %.0f s 还没站稳 / %s 没铺出来或没有船台" % [SETTLE_TIMEOUT, names[ORIGIN_VILLAGE]])
		Phase.WALK_TO_SHIP:
			if _follow_waypoints():
				_goto(Phase.TAKE_HELM)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到船上")
		Phase.TAKE_HELM:
			if main.get_state() == Expedition.PILOTING:
				climb_from = ship.global_position.y
				if leg == 1:
					var course := main.get_status_text()
					if not course.contains("托运") or not course.contains(names[DEST_VILLAGE]):
						_finish(false, "掌舵时航向行该指着托运的目的村%s，实为「%s」" % [names[DEST_VILLAGE], course])
						return
					_log("航向：%s" % course.replace("\n", " / "))
				_goto(Phase.CLIMB_OUT)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_OUT:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 12.0:
				_hold("ascend", false)
				_goto(Phase.FLY)
		Phase.FLY:
			var vid := _village_id()
			var s := _berth_settlement(vid)
			var center: Vector3 = centers[vid]  # 村子还没铺出来时先朝村心飞
			var target := Vector3(center.x, world.get_surface_height(center.x, center.z), center.z)
			if s != null:
				target = _berth_contact(vid).origin
			var flat := Vector2(target.x - ship.global_position.x, target.z - ship.global_position.z).length()
			if flat > SLOW_DOWN_AT and ship.get_gear() < 2 and not held.has("gear_up"):
				_tap("gear_up")
			elif flat < SLOW_DOWN_AT and ship.get_gear() > 0 and not held.has("gear_down"):
				_tap("gear_down")
			if in_phase > 10.0 and step == 0 and leg == 1:
				step = 1
				_shot("6_cruise")
			var arrived := _fly_toward(target)
			if s != null and arrived:
				_release_all()
				if not main.get_prompt().contains("落上船台"):
					_finish(false, "船台跟前该提示「按 E 落上船台」，实为「%s」" % main.get_prompt())
					return
				_shot("%d_over_berth" % (2 + leg * 5))
				_tap("interact")
				_goto(Phase.MOORING)
			elif in_phase > 300.0:
				_finish(false, "300 s 没飞到%s 的船台（离 %.0f m）" % [names[vid], flat])
		Phase.MOORING:
			if main.get_state() == Expedition.ON_FOOT:
				var contact := _berth_contact(_village_id())
				if not ship.is_at(contact, 0.5):
					_finish(false, "落上船台后船舷该贴着跳板顶（船 %s，接触线 %s）" % [ship.global_position, contact.origin])
					return
				_log("落上%s 的船台；途中最低离地 %.0f m" % [names[_village_id()], min_clearance])
				_shot("%d_landed" % (3 + leg * 5))
				crate_no = 0
				if leg == 0:
					waypoints = _down_gangway(ORIGIN_VILLAGE)
					_goto(Phase.TO_POST)
				else:
					waypoints = [_slot_midline(0), _slot_stand(0)]
					_goto(Phase.CARRY_DOWN)
			elif in_phase > 5.0 and main.get_state() == Expedition.PILOTING:
				_tap("interact")
			elif in_phase > 40.0:
				_finish(false, "40 s 没落上船台（%s；%s）" % [main.get_prompt(), main.get_notice()])
		Phase.TO_POST:
			if _follow_waypoints(0.5):
				_release_all()
				_goto(Phase.PACK)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走下跳板到村仓（%s）" % player.global_position)
		Phase.PACK:
			match step:
				0:
					var prompt := main.get_prompt()
					var want := "按 E 接下" if crate_no == 0 else "再打包一箱"
					if not prompt.contains(want):
						if in_phase > 3.0:
							_finish(false, "村仓前该提示「%s」，实为「%s」" % [want, prompt])
						return
					if crate_no == 0:
						_log("村仓前：%s" % prompt)
						_shot("4_post")
					_tap("interact")
					step = 1
				1:
					if in_phase > 0.5:
						if not main.is_carrying() or not main.has_freight_order():
							_finish(false, "按 E 之后该抱着一箱（%s）" % main.get_notice())
							return
						order = main.get_freight_order()
						item = order["item"]
						if crate_no == 0:
							if int(order["from"]) != ORIGIN_VILLAGE or int(order["to"]) != DEST_VILLAGE or int(order["crates"]) != 2:
								_finish(false, "托运单该是%s → %s 两箱，实为 %s" % [names[ORIGIN_VILLAGE], names[DEST_VILLAGE], order])
								return
							origin_before = _store(ORIGIN_VILLAGE) + int(order["per_crate"])
							if origin_before != ORIGIN_STORE:
								_finish(false, "%s 的仓开账时该有 %d 份%s（打包前推算得 %d）" % [names[ORIGIN_VILLAGE], ORIGIN_STORE, item, origin_before])
								return
							_log("接下托运 %s；%s" % [order, main.get_notice()])
						elif _store(ORIGIN_VILLAGE) != origin_before - 10:
							_finish(false, "第二箱该再扣 5 份（仓里 %d）" % _store(ORIGIN_VILLAGE))
							return
						waypoints = _up_gangway(ORIGIN_VILLAGE, 0 if crate_no == 0 else 2)
						if crate_no == 0:
							waypoints.insert(3, ship.get_helm_position())  # 上了船先到舵旁试一下
						carry_top_speed = 0.0
						carry_rise = 0.0
						jump_tapped = false
						_goto(Phase.CARRY_UP)
		Phase.CARRY_UP:
			var v := player.velocity
			carry_top_speed = maxf(carry_top_speed, Vector2(v.x, v.z).length())
			carry_rise = maxf(carry_rise, v.y)
			_hold("sprint", true)  # 按着疾跑也快不了
			if not jump_tapped and in_phase > 0.5 and player.is_on_floor():
				jump_tapped = true
				_tap("jump")
			if crate_no == 0 and not helm_tried and waypoints.size() == 3:
				# 下一个路点是舵：走到舵旁按 E
				if _walk_toward(waypoints[0], 1.9):
					helm_tried = true
					_tap("interact")
					_goto(Phase.HELM_REFUSED)
				return
			if _follow_waypoints(0.4):
				_release_all()
				if carry_top_speed > player.get_carry_speed() + 0.3 or carry_rise > 5.5:
					_finish(false, "抱着箱子该慢走、不能跳（最快 %.1f m/s，最大上升 %.1f m/s）" % [carry_top_speed, carry_rise])
					return
				_goto(Phase.STOW)
			elif in_phase > 90.0:
				_finish(false, "90 s 没抱着箱子走到货位旁（%s）" % player.global_position)
		Phase.HELM_REFUSED:
			if in_phase > 0.3:
				if main.get_state() != Expedition.ON_FOOT or not main.get_notice().contains("抱着货箱"):
					_finish(false, "抱着箱子按 E 该掌不了舵（状态 %d，「%s」）" % [main.get_state(), main.get_notice()])
					return
				_log("舵旁：%s" % main.get_notice())
				waypoints.pop_front()
				phase = Phase.CARRY_UP
				phase_started = elapsed
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
				1:
					if in_phase > 0.5:
						if main.is_carrying() or ship.get_cargo(slot) != item:
							_finish(false, "该放上货位 %d（货位上是「%s」，%s）" % [slot, ship.get_cargo(slot), main.get_notice()])
							return
						_log("放上货位 %d：船上 %d 箱（这一箱抱着最快 %.1f m/s）" % [slot, ship.get_cargo_count(), carry_top_speed])
						_shot("5_stowed_%d" % slot)
						if crate_no == 0:
							crate_no = 1
							waypoints = [_slot_midline(0)]
							waypoints.append_array(_down_gangway(ORIGIN_VILLAGE))
							_goto(Phase.TO_POST)
						else:
							leg = 1
							_goto(Phase.TAKE_HELM)
		Phase.CARRY_DOWN:
			# 目的村：走到货位旁抱起一箱，下跳板到村仓
			var slot := 0 if crate_no == 0 else 2
			match step:
				0:
					if _follow_waypoints(0.4):
						_release_all()
						step = 1
						phase_started = elapsed
					elif in_phase > 60.0:
						_finish(false, "60 s 没走到货位 %d 旁" % slot)
				1:
					# HUD 在编排的物理帧里才刷新（比测试晚一步）：等提示跟上
					if not main.get_prompt().contains("抱起"):
						if in_phase > 2.0:
							_finish(false, "货位 %d 旁该提示抱起，实为「%s」" % [slot, main.get_prompt()])
						return
					_tap("interact")
					step = 2
				2:
					if in_phase > 0.3 and main.is_carrying():
						waypoints = [_slot_midline(slot)]
						waypoints.append_array(_down_gangway(DEST_VILLAGE))
						step = 3
				3:
					if _follow_waypoints(0.5):
						_release_all()
						if dest_before < 0:
							dest_before = _store(DEST_VILLAGE)
						step = 4
						phase_started = elapsed
					elif in_phase > 120.0:
						_finish(false, "120 s 没抱着箱子走下跳板到村仓")
				4:
					if not main.get_prompt().contains("按 E 交货"):
						if in_phase > 2.0:
							_finish(false, "%s 的村仓前该提示交货，实为「%s」" % [names[DEST_VILLAGE], main.get_prompt()])
						return
					_tap("interact")
					_goto(Phase.DELIVER)
		Phase.DELIVER:
			if in_phase > 0.5:
				var got := _store(DEST_VILLAGE) - dest_before
				var want := (crate_no + 1) * int(order["per_crate"])
				if main.is_carrying() or got != want:
					_finish(false, "交第 %d 箱：%s 的仓该多 %d 份，实多 %d（%s）" % [crate_no + 1, names[DEST_VILLAGE], want, got, main.get_notice()])
					return
				_log("交第 %d 箱：%s" % [crate_no + 1, main.get_notice()])
				if crate_no == 0:
					if main.get_item_count("coin") != 0 or main.get_freight_delivered() != 1:
						_finish(false, "交了一箱还不该给钱")
						return
					_shot("9_delivered_1")
					crate_no = 1
					var contact := _berth_contact(DEST_VILLAGE)
					waypoints = [contact.origin + contact.basis.x * 9.0, contact.origin + contact.basis.x * 0.4, ship.global_position, _slot_midline(2), _slot_stand(2)]
					_goto(Phase.CARRY_DOWN)
				else:
					var fee := int(order["fee"])
					if main.has_freight_order() or main.get_item_count("coin") != fee:
						_finish(false, "交齐了该结单、拿到运费 %d 文（背包 %d，单子 %s）" % [fee, main.get_item_count("coin"), main.get_freight_order()])
						return
					if _store(ORIGIN_VILLAGE) != origin_before - 10 or ship.get_cargo_count() != 0:
						_finish(false, "起运村该少 10 份（%d → %d）、船该空了（%d 箱）" % [origin_before, _store(ORIGIN_VILLAGE), ship.get_cargo_count()])
						return
					_shot("10_paid")
					_goto(Phase.DONE)
		Phase.DONE:
			if in_phase > 0.3:
				_finish(true, "主码头 → %s 船台接托运打包两箱、走上跳板装船 → 飞到%s 船台卸下交齐：%s 各 %d 份挪了 10 份，得运费 %d 文；途中最低离地 %.0f m" % [
					names[ORIGIN_VILLAGE], names[DEST_VILLAGE], item, int(order["per_crate"]), int(order["fee"]), min_clearance])


func _finish(ok: bool, why: String) -> void:
	_release_all()
	for action in held.keys():
		Input.action_release(action)
	set_physics_process(false)
	print("freight: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，船 %s，状态 %d，提示「%s」，通知「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, ship.global_position, main.get_state(), main.get_prompt(), main.get_notice()])
	if not pending_shot.is_empty() and not shots_dir.is_empty():
		for i in 6:
			await get_tree().process_frame
	get_tree().quit(0 if ok else 1)
