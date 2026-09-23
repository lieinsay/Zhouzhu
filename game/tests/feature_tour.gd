extends Node
## 全部玩法走一遍的端到端测试驱动：只通过输入（动作、鼠标点地图、转视角）操作，与地图无关。
## 哪张地图由子类说（world_features_e2e：岛群 #2051；flat_map_e2e：平板岛），子类只回答几件事：
## 场景、农田与船坞是哪个节点、村子在哪（地面宿主节点、村心、半径）、从哪儿找要垦的地、改动存到哪儿。
## 去远处：地图有岛群地图面板就 M 开图点过去（开发者瞬移），没有就走过去。依次：
##   ① 开垦、播种、收获：去村外的一块熟地 → 按住 E 开垦 → 手上没种子播不了
##   ② 从背包选种：Tab 打开背包、S 挪到菽、E 拿在手上、Tab 合上 → 按住 E 播菽 → 等它熟 → 按住 E 收获（菽 += 单产）
##   ③ 船坞改船：回出发码头 → 走到画样台按 E（船不在坞里，被拒）→ 上船掌舵飞到坞口、E 入坞
##      → 下跳板到画样台 E 打开装配视角 → S、E 在舷门后放一节储存区 → Tab 合上 → 走上跳板掌舵、飞回泊位靠泊
##   ④ 布置模式：回村 → B 在脚下这处村落里开布置模式 → 挪到空地 E 放一座民居 → 挪到自动铺出的一座房子上 X 拆掉
##      → F5 存盘：文件里正好一行「加」一行「拆」→ Tab 合上 → 重新读文件，清单不变
##   ⑤ 村民：村里招到了农夫与挑水的，农夫下地干活、挑水的去打水
## 截图：去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张（目视检查用）。

const TIMEOUT := 1500.0  # 模拟秒
const SETTLE_TIMEOUT := 30.0
const GROW_SECONDS := 12.0  # 测试里庄稼长快些
const STAND_OFF := 2.2  # 停在离格心这么远：视线前方 reach 处正好落在格内
const SEED := "bean"
const OTHER := "millet"
const FOLK_TIMEOUT := 300.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const KEYS := ["interact", "remove", "inventory", "build_mode", "rotate_left", "rotate_right", "kind_1", "kind_2", "kind_3", "kind_4", "save"]

enum Phase {
	SETTLE,
	GO_FIELD, LAND_FIELD, WALK_TO_CELL, TILL, SOW_WITHOUT_SEED, PICK_SEED, SOW, GROW, HARVEST,
	GO_DOCK, LAND_DOCK, WALK_TO_BENCH, REFUSED_NO_SHIP, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, FLY_TO_YARD, DOCKING,
	WALK_DOWN, OPEN_REFIT, REFIT, WALK_UP, TAKE_HELM_AGAIN, CLIMB_OUT_AGAIN, FLY_TO_BERTH, MOORING,
	GO_VILLAGE, LAND_VILLAGE, OPEN_BUILD, TO_SITE, PLACE, TO_OLD, REMOVE, SAVE, CLOSE, RELOAD,
	FOLK, DONE,
}

var main: Expedition
var world: World
var player: Player
var ship: Ship
var farm: Farm
var yard: Shipyard
var map: WorldMap

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var waypoints: Array[Vector3] = []
var map_target := Vector3.ZERO
var map_step := 0
var taps: Array = []
var tap_frames := 0
var tap_gap := 0
var interact_frames := 0
var released_for := 0  # E 已经松开了多少帧
var climb_from := 0.0
var stuck_frames := 0
var jump_frames := 0
var hops := 0  # 走路卡住跳过几次（报告里写出来）

# ① ②
var cell := Vector2i(-1, -1)
var seed_at_start := 0
var other_at_start := 0
# ③
var layout_at_start: Array = []
var speed_at_start := 0.0
# ④
var village: Node3D  # 村子的地面宿主（Settlement、Villagers 挂在它下面）
var settlement: Settlement
var base_count := 0
var site := Vector3.ZERO
var old_index := -1
var old_pos := Vector3.ZERO
var old_line := ""


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)  # 无头时根视口默认很小，地图上的点没处可点
	_cleanup()
	main = _scene().instantiate()
	_setup()
	farm = _farm()
	farm.grow_hours = GROW_SECONDS * 24.0 / GameClock.get_day_seconds()  # 生长按钟点算：12 秒折成游戏小时
	yard = _yard()
	add_child(main)
	world = main.get_map_node() as World  # 不是岛群就是 null
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	map = main.get_map()
	seed_at_start = main.get_item_count(SEED)
	other_at_start = main.get_item_count(OTHER)
	layout_at_start = Array(ship.get_layout())
	speed_at_start = ship.get_max_speed()
	if not yard.is_present():
		_finish(false, "出发码头 #%d 旁该有船坞坪" % main.origin_dock)
		return
	if seed_at_start < 1 or other_at_start < 1 or main.get_held_seed() != "":
		_finish(false, "开局背包里该有 %s 和 %s、手上空着（%s，手持「%s」）" % [SEED, OTHER, main.get_pack_items(), main.get_held_seed()])
		return
	_log("出生 %s，泊位 %s，画样台 %s，坞位 %s（离泊位 %.0f m），开局布局 %s" % [
		player.global_position, _berth().origin, yard.get_bench_position(), _cradle().origin,
		_berth().origin.distance_to(_cradle().origin), layout_at_start])


# ---------------------------------------------------------------- 子类回答的事

func _tag() -> String:
	return "feature_tour"


func _scene() -> PackedScene:
	return null


## 场景实例化后、进树前：把村落改动的存盘处改到 user://，之类
func _setup() -> void:
	pass


## 测试前后都调：删掉存到 user:// 的改动
func _cleanup() -> void:
	pass


func _farm() -> Farm:
	return null


func _yard() -> Shipyard:
	return null


## 村子的地面宿主；还没铺出来返回 null
func _village_host() -> Node3D:
	return null


func _village_name() -> String:
	return "村子"


func _village_center() -> Vector3:
	return village.global_position


func _village_radius() -> float:
	return 40.0


## 从哪儿找要垦的地（离它最近的熟地）：默认村子南边外沿再往外 20 m
func _field_probe() -> Vector3:
	return _village_center() + Vector3(0, 0, _village_radius() + 20.0)


func _log(line: String) -> void:
	print(_tag() + ": " + line)


# ---------------------------------------------------------------- 流程

func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	if elapsed > TIMEOUT:
		_finish(false, "超时")
		return
	if interact_frames > 0:
		interact_frames -= 1
		if interact_frames == 0:
			Input.action_release("interact")
	released_for = 0 if Input.is_action_pressed("interact") else released_for + 1
	if jump_frames > 0:
		jump_frames -= 1
		if jump_frames == 0:
			Input.action_release("jump")
	if not _run_taps():
		return

	match phase:
		Phase.SETTLE:
			village = _village_host()
			if in_phase > 1.0 and _standing() and village != null:
				settlement = village.get_node("Settlement")
				# 村子外沿最近的一块熟地（不压着房子）
				cell = farm.find_nearest_arable(_field_probe())
				if cell == Vector2i(-1, -1) or settlement.find_at(farm.get_cell_center(cell)) >= 0:
					_finish(false, "%s外沿找不到能垦的地（%s）" % [_village_name(), cell])
					return
				_log("%s在 %s（半径 %.0f m，%d 座）；要垦的地 %s 在 %s，墒情 %.2f" % [
					_village_name(), _village_center(), _village_radius(), settlement.get_count(), cell, farm.get_cell_center(cell), farm.get_moisture(cell)])
				_shot("00_spawn")
				_travel(farm.get_cell_center(cell) + Vector3(0, 0, 3.0), Phase.GO_FIELD)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "出生 %.0f s 还没站稳 / %s没铺出来" % [SETTLE_TIMEOUT, _village_name()])

		# ---------------- ① ② 开垦、选种、播种、收获
		Phase.GO_FIELD, Phase.GO_DOCK, Phase.GO_VILLAGE:
			if map != null:
				_run_map_click()
			elif _follow_waypoints(1.0):
				_enter(phase + 1)  # GO_… 的下一个就是 LAND_…
			elif in_phase > 120.0:
				_finish(false, "120 s 没走到 %s" % map_target)
		Phase.LAND_FIELD:
			if in_phase > 1.0 and _standing():
				_enter(Phase.WALK_TO_CELL)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "瞬移到地头 %.0f s 还没站稳（%s）" % [SETTLE_TIMEOUT, player.global_position])
		Phase.WALK_TO_CELL:
			if _walk_toward(farm.get_cell_center(cell), STAND_OFF, true):
				_enter(Phase.TILL)
			elif in_phase > 30.0:
				_finish(false, "30 s 没走到地头")
		Phase.TILL:
			if _work_until(Farm.PLOT_TILLED):
				if main.get_item_count(SEED) != seed_at_start:
					_finish(false, "开垦不该动背包")
				else:
					_enter(Phase.SOW_WITHOUT_SEED)
		Phase.SOW_WITHOUT_SEED:
			_face(farm.get_cell_center(cell))
			if farm.get_plot(cell) != Farm.PLOT_TILLED or main.is_working():
				_finish(false, "没选种子居然种下去了（状态 %d）" % farm.get_plot(cell))
			elif in_phase < 2.0:
				Input.action_press("interact")
			else:
				Input.action_release("interact")
				_queue([["inventory", 1], ["move_back", 1], ["interact", 1], ["inventory", 1]])
				_enter(Phase.PICK_SEED)
		Phase.PICK_SEED:
			if main.is_pack_open():
				_finish(false, "背包没合上")
			elif main.get_held_seed() != SEED:
				_finish(false, "手上该拿着 %s，实为「%s」（光标 %d）" % [SEED, main.get_held_seed(), main.get_pack_cursor()])
			else:
				_enter(Phase.SOW)
		Phase.SOW:
			if _work_until(Farm.PLOT_GROWING):
				if main.get_item_count(SEED) != seed_at_start - 1 or farm.get_crop(cell) != SEED:
					_finish(false, "播种该只耗 1 份 %s、地里种的是它（%s，地里「%s」）" % [SEED, _pack_text(), farm.get_crop(cell)])
				else:
					_enter(Phase.GROW)
		Phase.GROW:
			if farm.get_plot(cell) == Farm.PLOT_RIPE:
				_enter(Phase.HARVEST)
			elif farm.get_plot(cell) != Farm.PLOT_GROWING:
				_finish(false, "长着长着状态变成了 %d" % farm.get_plot(cell))
		Phase.HARVEST:
			if _work_until(Farm.PLOT_TILLED):
				var expected := seed_at_start - 1 + farm.get_crop_yield(SEED)
				if main.get_item_count(SEED) != expected or main.get_item_count(OTHER) != other_at_start:
					_finish(false, "收获后 %s 该是 %d、%s 不动（%s）" % [SEED, expected, OTHER, _pack_text()])
				else:
					_log("① ② 垦、选种、播、收：%s %d → %d" % [SEED, seed_at_start, main.get_item_count(SEED)])
					_travel(main.get_spawn_point(), Phase.GO_DOCK)

		# ---------------- ③ 入坞改船、飞回泊位
		Phase.LAND_DOCK:
			if in_phase > 1.0 and _standing():
				if player.global_position.distance_to(main.get_spawn_point()) > 15.0:
					_finish(false, "回到出发码头落在 %.0f m 外" % player.global_position.distance_to(main.get_spawn_point()))
					return
				# 主码头的停机坪在船坞坪靠海那一侧：先绕到那排坞墩块的一头，再顺着画样台那一线走过去（正对着墩块推会顶住）
				waypoints = _yard_detour()
				waypoints.reverse()
				waypoints.append(yard.get_bench_position())
				_enter(Phase.WALK_TO_BENCH)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "回到出发码头 %.0f s 还没站稳" % SETTLE_TIMEOUT)
		Phase.WALK_TO_BENCH:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.REFUSED_NO_SHIP)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到画样台")
		Phase.REFUSED_NO_SHIP:
			if main.is_refit_open():
				_finish(false, "船不在坞里也打开了改装")
			elif in_phase > 0.5:
				if not main.get_notice().contains("船坞里没有船"):
					_finish(false, "该提示船坞里没有船，实为「%s」" % main.get_notice())
				else:
					var berth := _berth()
					waypoints = _yard_detour()
					waypoints.append_array([main.get_spawn_point(), berth.origin + berth.basis.x * 0.8, ship.global_position])
					_enter(Phase.WALK_TO_SHIP)
		Phase.WALK_TO_SHIP:
			if _follow_waypoints(0.6):
				_enter(Phase.TAKE_HELM)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到船上")
		Phase.TAKE_HELM, Phase.TAKE_HELM_AGAIN:
			if main.get_state() == Expedition.PILOTING:
				climb_from = ship.global_position.y
				_enter(Phase.CLIMB_OUT if phase == Phase.TAKE_HELM else Phase.CLIMB_OUT_AGAIN)
			elif _walk_toward(ship.get_helm_position(), main.helm_range - 0.3, false):
				_tap_interact()
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_OUT, Phase.CLIMB_OUT_AGAIN:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 8.0:
				_enter(Phase.FLY_TO_YARD if phase == Phase.CLIMB_OUT else Phase.FLY_TO_BERTH)
		Phase.FLY_TO_YARD:
			if _hop_toward(_cradle().origin, _berth().origin):
				_release_moves()
				_tap_interact()
				_enter(Phase.DOCKING)
			elif in_phase > 60.0:
				_finish(false, "60 s 没飞到坞口")
		Phase.DOCKING:
			if main.get_state() == Expedition.ON_FOOT and player.is_on_floor():
				if not main.is_ship_in_yard():
					_finish(false, "入坞后船舷没贴在坞位接触线上（船 %s）" % ship.global_position)
				else:
					waypoints = [yard.get_gangway_top(), yard.get_gangway_foot(), yard.get_bench_position()]
					_enter(Phase.WALK_DOWN)
			elif in_phase > 30.0:
				_finish(false, "30 s 没入坞（状态 %d）" % main.get_state())
		Phase.WALK_DOWN:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.OPEN_REFIT)
			elif in_phase > 30.0:
				_finish(false, "30 s 没走下跳板到画样台")
		Phase.OPEN_REFIT:
			if main.is_refit_open():
				if not player.is_input_blocked():
					_finish(false, "开着改装人还能走")
					return
				_shot("REFIT_open")
				_queue([["move_back", 2], ["interact", 2], ["inventory", 2]])
				_enter(Phase.REFIT)
			elif in_phase > 1.0:
				_finish(false, "在画样台按了 E，改装没打开（%s）" % main.get_notice())
		Phase.REFIT:
			var want := layout_at_start + ["0,0,1:storage"]
			want.sort()
			var now := Array(ship.get_layout())
			now.sort()
			if now != want:
				_finish(false, "S、E 之后布局该是 %s，实为 %s（%s）" % [want, now, main.get_notice()])
			elif main.is_refit_open() or player.is_input_blocked():
				_finish(false, "Tab 之后改装没合上或人还动不了")
			elif not main.is_ship_in_yard() or not ship.can_fly():
				_finish(false, "改完船该还坐在坞里、飞得起来")
			else:
				# 先站到跳板脚正前方再上板：从画样台斜着过去会顶在板边上
				var foot := yard.get_gangway_foot()
				var down := foot - yard.get_gangway_top()
				down.y = 0.0
				waypoints = [foot + down.normalized() * 2.0, foot, yard.get_gangway_top(), ship.get_helm_position()]
				_enter(Phase.WALK_UP)
		Phase.WALK_UP:
			if _follow_waypoints(main.helm_range - 0.3):
				_enter(Phase.TAKE_HELM_AGAIN)
			elif in_phase > 30.0:
				_finish(false, "30 s 没走上跳板到舵旁")
		Phase.FLY_TO_BERTH:
			if _hop_toward(_berth().origin, _cradle().origin):
				_release_moves()
				_tap_interact()
				_enter(Phase.MOORING)
			elif in_phase > 60.0:
				_finish(false, "60 s 没飞回泊位")
		Phase.MOORING:
			if main.get_state() == Expedition.ON_FOOT and player.is_on_floor():
				if not ship.is_at(_berth(), 0.5):
					_finish(false, "靠泊后船舷没贴在泊位接触线上（船 %s）" % ship.global_position)
				elif main.is_ship_in_yard():
					_finish(false, "船已离坞却还说在坞里")
				else:
					_log("③ 入坞、改成 %s、飞回泊位；航速 %.1f → %.1f m/s" % [ship.get_layout(), speed_at_start, ship.get_max_speed()])
					_travel(_village_center() + Vector3(0, 0, _village_radius() + 10.0), Phase.GO_VILLAGE)
			elif in_phase > 30.0:
				_finish(false, "30 s 没靠上泊位（状态 %d）" % main.get_state())

		# ---------------- ④ 布置模式改村子
		Phase.LAND_VILLAGE:
			village = _village_host()
			if in_phase > 1.0 and _standing() and village != null:
				settlement = village.get_node("Settlement")
				base_count = settlement.get_base_count()
				if base_count == 0 or settlement.get_count() != base_count or settlement.is_dirty():
					_finish(false, "村子该是自动铺的原样（%d / %d 座，有没存的改动 %s）" % [settlement.get_count(), base_count, settlement.is_dirty()])
					return
				site = _snap(settlement.find_site("house", player.global_position, 0.0, 60.0))
				if is_nan(site.y):
					_finish(false, "脚下 60 m 内找不到能盖民居的地")
					return
				# 要拆的：离人最近的那座自动铺出的房子
				var best := INF
				for k in settlement.get_count():
					var d := settlement.get_building_position(k).distance_to(player.global_position)
					if d < best:
						best = d
						old_index = k
				old_pos = _snap(settlement.get_building_position(old_index))
				old_line = settlement.get_layout()[old_index]
				_log("%s：%d 座；民居盖在 %s；拆 %s" % [_village_name(), base_count, site, old_line])
				_queue([["build_mode", 2]])
				_enter(Phase.OPEN_BUILD)
			elif in_phase > SETTLE_TIMEOUT:
				_finish(false, "瞬移回村 %.0f s 还没站稳" % SETTLE_TIMEOUT)
		Phase.OPEN_BUILD:
			if not main.is_build_open():
				_finish(false, "按了 B 布置模式没打开（%s）" % main.get_notice())
			elif main.get_build_target() != settlement:
				_finish(false, "布置模式改的该是脚下这处村子")
			else:
				_enter(Phase.TO_SITE)
		Phase.TO_SITE:
			if _steer_cursor(site):
				_queue([["interact", 2]])
				_enter(Phase.PLACE)
		Phase.PLACE:
			if settlement.get_count() != base_count + 1:
				_finish(false, "该放下一座民居（%d 座，提示「%s」）" % [settlement.get_count(), main.get_notice()])
			else:
				_shot("BUILD_placed")
				_enter(Phase.TO_OLD)
		Phase.TO_OLD:
			if _steer_cursor(old_pos):
				_queue([["remove", 2]])
				_enter(Phase.REMOVE)
		Phase.REMOVE:
			if settlement.get_count() != base_count or Array(settlement.get_layout()).has(old_line):
				_finish(false, "拆掉 %s 后该回到 %d 座（%d 座，提示「%s」）" % [old_line, base_count, settlement.get_count(), main.get_notice()])
			else:
				_queue([["save", 2]])
				_enter(Phase.SAVE)
		Phase.SAVE:
			var path := settlement.get_layout_path()
			if settlement.is_dirty() or not FileAccess.file_exists(path):
				_finish(false, "F5 之后文件没写出来（%s，%s）" % [path, main.get_notice()])
				return
			var lines: Array = []
			for line in FileAccess.get_file_as_string(path).split("\n"):
				if line.strip_edges() != "" and not line.begins_with("#"):
					lines.append(line.strip_edges())
			var adds := lines.filter(func(l): return not l.begins_with("-"))
			var removes := lines.filter(func(l): return l.begins_with("-"))
			if lines.size() != 2 or adds.size() != 1 or not adds[0].begins_with("house@") or removes.size() != 1 or removes[0] != "-" + old_line:
				_finish(false, "改动文件该是一行加民居、一行「-%s」，实为 %s" % [old_line, lines])
			elif settlement.is_dirty():
				_finish(false, "存过了还说有没存的改动")
			else:
				_log("④ 改动存到 %s：%s" % [path, lines])
				_queue([["inventory", 2]])
				_enter(Phase.CLOSE)
		Phase.CLOSE:
			if main.is_build_open() or player.is_input_blocked():
				_finish(false, "Tab 之后布置模式没合上或人还动不了")
			else:
				_enter(Phase.RELOAD)
		Phase.RELOAD:
			var before: Array = Array(settlement.get_layout())
			if not settlement.load():
				_finish(false, "重新读改动文件失败")
			elif Array(settlement.get_layout()) != before:
				_finish(false, "重新读出来的清单变了：%s → %s" % [before, settlement.get_layout()])
			else:
				_enter(Phase.FOLK)

		# ---------------- ⑤ 村民干活
		Phase.FOLK:
			var folk := village.get_node_or_null("Villagers") as Villagers
			if folk == null or folk.get_villager_count() == 0:
				if in_phase > 10.0:
					_finish(false, "%s没招到村民" % _village_name())
				return
			var jobs := {}
			for k in folk.get_villager_count():
				jobs[folk.get_villager_job(k)] = jobs.get(folk.get_villager_job(k), 0) + 1
			if jobs.get("farmer", 0) < 1 or jobs.get("carrier", 0) < 1:
				_finish(false, "%s该有农夫和挑水的（%s）" % [_village_name(), jobs])
				return
			var farmed := folk.get_tally("till") + folk.get_tally("sow") + folk.get_tally("harvest")
			if farmed >= 1 and folk.get_tally("fetch_water") >= 1:
				_shot("FOLK_work")
				_log("⑤ %s：%s；招来以后累计 开垦 %d、播种 %d、收割 %d、挑水 %d" % [_village_name(), jobs,
					folk.get_tally("till"), folk.get_tally("sow"), folk.get_tally("harvest"), folk.get_tally("fetch_water")])
				_enter(Phase.DONE)
			elif in_phase > FOLK_TIMEOUT:
				_finish(false, "%.0f s 里农夫干活 %d 次、挑水 %d 次" % [FOLK_TIMEOUT, farmed, folk.get_tally("fetch_water")])
		Phase.DONE:
			_finish(true, "垦、选种、播、收；入坞改船飞回泊位；布置模式加拆存读；村民农夫与挑水的都在干活")


# ---------------------------------------------------------------- 去远处

## 进 next（GO_…）：有地图面板就开图、对着目标滚轮放大几格、左键点它；没有就走过去。到了进下一阶段（LAND_…）
func _travel(target: Vector3, next: Phase) -> void:
	map_target = target
	_enter(next)
	map_step = 0
	waypoints = [target]


func _run_map_click() -> void:
	match map_step:
		0:
			_press_key_action("map")
			map_step = 1
		1, 2, 3, 4, 5, 6, 7, 8:
			# 地图沿用上次的缩放：先在屏幕中间缩小回去，目标才在屏幕里
			if main.is_map_open():
				_mouse(MOUSE_BUTTON_WHEEL_DOWN, Vector2(640, 360))
				map_step += 1
		9, 10, 11, 12, 13, 14, 15, 16:
			_mouse(MOUSE_BUTTON_WHEEL_UP, map.world_to_screen(map_target.x, map_target.z))
			map_step += 1
		17:
			_mouse(MOUSE_BUTTON_LEFT, map.world_to_screen(map_target.x, map_target.z))
			map_step = 18
		_:
			if not main.is_map_open():
				_enter(phase + 1)  # GO_… 的下一个就是 LAND_…
			elif elapsed - phase_started > 5.0:
				_finish(false, "点了地图没瞬移（%s）" % main.get_notice())


func _press_key_action(action: String) -> void:
	for pressed in [true, false]:
		var ev := InputEventAction.new()
		ev.action = action
		ev.pressed = pressed
		Input.parse_input_event(ev)


func _mouse(button: MouseButton, at: Vector2) -> void:
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = button
		ev.position = at
		ev.global_position = at
		ev.pressed = pressed
		Input.parse_input_event(ev)


# ---------------------------------------------------------------- 走、干活、飞、挪光标

## 站稳了：着地；流式地图还要脚下的地面块已经建好
func _standing() -> bool:
	return player.is_on_floor() and (world == null or world.is_ready_at(player.global_position))


func _face(target: Vector3) -> void:
	var to := target - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


## 朝目标走；到了返回 true。settle 为真时还要站稳（农事要瞄准）
func _walk_toward(target: Vector3, radius: float, settle: bool) -> bool:
	var to := target - player.global_position
	var rise := to.y
	to.y = 0.0
	_face(target)
	if to.length() <= radius and absf(rise) < 1.5:
		_hold("move_forward", false)
		return not settle or player.get_real_velocity().length() < 0.05
	_hold("move_forward", true)
	# 按着走却原地不动（地块接缝、坪沿的折棱会卡住胶囊）：像真人一样跳一下；跳了几次还不动就算失败
	var flat_speed := Vector2(player.get_real_velocity().x, player.get_real_velocity().z).length()
	stuck_frames = stuck_frames + 1 if player.is_on_floor() and flat_speed < 0.05 else 0
	if stuck_frames > 30:
		stuck_frames = 0
		hops += 1
		_log("走不动了，跳一下（%s，阶段 %s）" % [player.global_position, Phase.keys()[phase]])
		if hops > 3:
			_finish(false, "走路卡死了：%s" % _blocker_report())
			return false
		Input.action_press("jump")
		jump_frames = 4
	return false


## 人的胶囊往视线前方挪 0.3 m 会碰上什么（卡住时报告用：细柱子、矮台阶射线常常打不着）
func _blocker_report() -> String:
	var query := PhysicsShapeQueryParameters3D.new()
	for child in player.get_children():
		if child is CollisionShape3D:
			query.shape = child.shape
			query.transform = child.global_transform.translated(Basis(Vector3.UP, player.get_look_yaw()) * Vector3(0, 0, -0.3))
	query.exclude = [player.get_rid()]
	query.collision_mask = player.collision_mask
	var names: PackedStringArray = []
	for hit in get_viewport().world_3d.direct_space_state.intersect_shape(query, 8):
		var node := hit["collider"] as Node
		names.append("%s 的第 %d 个形状" % [node.get_path() if node else hit["collider"], hit["shape"]])
	return "、".join(names) if not names.is_empty() else "（什么也没碰上）"


func _follow_waypoints(last_radius: float) -> bool:
	if waypoints.is_empty():
		return true
	var radius := last_radius if waypoints.size() == 1 else 0.6
	if _walk_toward(waypoints[0], radius, false):
		waypoints.pop_front()
	return waypoints.is_empty()


func _aim_point() -> Vector3:
	return player.global_position + Basis(Vector3.UP, player.get_look_yaw()) * Vector3(0, 0, -main.reach)


## 面朝目标格按住 E，直到那一格变成 want；E 先松开几帧再按，编排才读得到「刚按下」
func _work_until(want: int) -> bool:
	if farm.get_plot(cell) == want:
		Input.action_release("interact")
		return true
	_face(farm.get_cell_center(cell))
	if farm.cell_at(_aim_point()) != cell:
		_finish(false, "瞄着的不是目标格：%s ≠ %s" % [farm.cell_at(_aim_point()), cell])
	elif released_for >= 3:
		Input.action_press("interact")
	elif elapsed - phase_started > 10.0:
		_finish(false, "按了 10 秒 E，地块还是状态 %d（%s）" % [farm.get_plot(cell), main.get_notice()])
	return false


## 短途跳一下：舵对准目标、高度保持在目标之上 5 m、离得远就进车。
## 目标比 other 更近、且在可靠泊的距离里时返回 true（这时按 E，编排会挑最近的那个）。
func _hop_toward(target: Vector3, other: Vector3) -> bool:
	var to := target - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))
	var flat := Vector2(to.x, to.z).length()
	var climb := to.y + 5.0
	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 1.5)
	_hold("descend", climb < -1.5)
	_hold("move_forward", flat > 6.0 and absf(bearing) < 30.0)
	return to.length() < ship.global_position.distance_to(other) - 2.0 and to.length() < main.dock_range - 5.0


## 村子局部坐标取整到 0.5 m 格（布置光标就是这么取整的），再回到全局
func _snap(global: Vector3) -> Vector3:
	if is_nan(global.y):
		return global
	var local := village.to_local(global)
	local.x = roundf(local.x * 2.0) / 2.0
	local.z = roundf(local.z * 2.0) / 2.0
	return village.to_global(local)


## 把布置光标挪到 target：按相机方位选 WASD，远了按 Shift
func _steer_cursor(target: Vector3) -> bool:
	var cursor: Vector3 = main.get_build_cursor()
	var to := target - cursor
	to.y = 0.0
	if to.length() < 0.01:
		_release_moves()
		return true
	var yaw: float = main.get_build_camera_yaw()
	var forward := Vector3(-sin(yaw), 0, -cos(yaw))
	var right := Vector3(cos(yaw), 0, -sin(yaw))
	var f := forward.dot(to)
	var r := right.dot(to)
	_hold("move_forward", f > 0.1)
	_hold("move_back", f < -0.1)
	_hold("move_right", r > 0.1)
	_hold("move_left", r < -0.1)
	_hold("sprint", to.length() > 8.0)
	if elapsed - phase_started > 40.0:
		_finish(false, "光标 40 s 没挪到 %s（在 %s）" % [target, cursor])
	return false


# ---------------------------------------------------------------- 按键

func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _release_moves() -> void:
	for action in MOVES:
		Input.action_release(action)


func _tap_interact() -> void:
	if interact_frames == 0:
		Input.action_press("interact")
		interact_frames = 4


func _queue(script: Array) -> void:
	taps = script.duplicate()
	tap_frames = 0
	tap_gap = 4


## 按脚本一步步敲键；全敲完（并且松开后又过了几帧）返回 true。步与步之间松开几帧，编排才读得到「刚按下」
func _run_taps() -> bool:
	if tap_gap > 0:
		tap_gap -= 1
		return false
	if taps.is_empty():
		return true
	var tap: Array = taps[0]
	if tap_frames < int(tap[1]):
		Input.action_press(tap[0])
		tap_frames += 1
		return false
	Input.action_release(tap[0])
	taps.pop_front()
	tap_frames = 0
	tap_gap = 4
	return false


# ---------------------------------------------------------------- 杂项

func _cradle() -> Transform3D:
	return yard.get_cradle_contact()


## 画样台与停机坪之间绕开坞墩与门架的路（自画样台一侧起）：先顺着画样台那一线走出坞墩那一排的头
## （停在最末一块墩与门架柱之间），再往海那边走过门架柱，之后就能直奔停机坪。
## 坞位接触线的 x 轴指向岛内（跳板那边），z 轴顺着那一排墩块。
func _yard_detour() -> Array[Vector3]:
	var along := _cradle().basis.z
	var side := signf(along.dot(main.get_spawn_point() - yard.get_bench_position()))
	var out_of_row := yard.get_bench_position() + along * side * 20.5
	return [out_of_row, out_of_row - _cradle().basis.x * 20.0]


func _berth() -> Transform3D:
	return main.get_origin_berth()


func _pack_text() -> String:
	var parts: PackedStringArray = []
	for item in main.get_pack_items():
		parts.append("%s %d" % [item, main.get_item_count(item)])
	return "、".join(parts)


func _enter(next: Phase) -> void:
	_log("%-16s 用时 %5.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	_shot("%02d_%s_end" % [phase, Phase.keys()[phase]])
	phase = next
	phase_started = elapsed


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


func _finish(ok: bool, why: String) -> void:
	set_physics_process(false)
	_log("%s —— %s（阶段 %s，模拟 %.1f s；走路卡住跳了 %d 次；玩家 %s，船 %s，状态 %d，背包 %s，提示「%s」，通知「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, hops, player.global_position, ship.global_position,
		main.get_state(), _pack_text(), main.get_prompt(), main.get_notice()])
	if not ok and player.is_on_floor() and not waypoints.is_empty():
		_log("身前挡着的：%s；剩余路点 %s" % [_blocker_report(), waypoints])
	for action in MOVES + KEYS:
		Input.action_release(action)
	_cleanup()
	get_tree().quit(0 if ok else 1)
