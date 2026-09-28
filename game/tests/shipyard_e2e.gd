extends Node
## 船坞端到端测试：加载主场景，只通过输入（动作与鼠标事件）操作。
## 出生点走到画样台按 E → 船不在坞里，被拒 → 走上空艇掌舵、升空、飞向船坞、E 入坞 → 下船走下跳板到画样台
## → E 打开改装：鼠标放出来，左边是船节点卡片
## → 鼠标指着「储存区」卡片，船上亮起它能放的格子 → 按住拖到舷门后一格松手：放上、选中
## → 拖储存区到帆顶上：红色虚影、说帆只能在最上面，松手不放
## → 在「空节」卡片上点一下（不拖）拿着，再点舷门排左舷：放上
## → 拖「双桅帆」（前后双节）到左舷后面两排、拖「双层货舱」（上下双节）到船尾
## → 拖着帆时按右键放下、点一下拿着帆时按 Esc 放下（改装不合上）
## → 点驾驶区选中、X：拆不得 → 储存区上再叠一节 → 点底下那节、点「拆除」：上面搭着拆不了 → 点上面那节、点「拆除」：拆掉 → Tab 合上
## → 走上跳板、爬梯子到双层货舱顶上、下来走到舵位 → 掌舵升空飞回泊位靠泊。
## 断言每一步的布局、亮格、虚影、选中、提示与船况一路对得上。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/shipyard_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，会在每个阶段存一张截图。

const MAIN := preload("res://main.tscn")
const TIMEOUT := 400.0  # 模拟秒
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const START := ["0,0,-1:sail", "0,0,0:bridge"]
const FINAL := ["0,0,-1:sail", "-1,0,0:deck", "0,0,0:bridge", "-1,0,1:long_sail", "0,0,1:storage", "0,0,2:tall_hold"]

enum Phase {
	WALK_TO_BENCH, REFUSED_NO_SHIP, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, FLY_TO_YARD, DOCKING,
	WALK_DOWN, OPEN_REFIT, REFIT, WALK_UP, CLIMB_LADDER, WALK_TO_HELM, TAKE_HELM_AGAIN, CLIMB_OUT_AGAIN, FLY_TO_BERTH, MOORING, DONE,
}

var main: Expedition
var player: Player
var ship: Ship
var origin: Island
var yard: Shipyard

var phase := Phase.WALK_TO_BENCH
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var waypoints: Array[Vector3] = []
var climb_from := 0.0
var interact_frames := 0
var speed_before_refit := 0.0
var top_deck := Vector3.ZERO
var ladder_index := 0
var highest := -INF
var mouse := Vector2.ZERO
var done := false


func _ready() -> void:
	get_tree().root.size = Vector2i(1280, 720)  # 无头时视口很小：卡片与船要在屏幕上摆得开
	main = MAIN.instantiate()
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	origin = main.get_node("IslandA")
	yard = origin.get_node("Shipyard")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
			_shot("START")
	if not origin.has_shipyard() or not yard.is_present():
		_finish(false, "出发岛上没有船坞")
		return
	if Array(ship.get_layout()) != START:
		_finish(false, "开局的船该是 帆 + 驾驶区（%s）" % [ship.get_layout()])
		return
	if main.is_ship_in_yard():
		_finish(false, "开局船不该在坞里")
		return
	speed_before_refit = ship.get_max_speed()
	# 从停机坪过来先走坪的岛内一侧（坞墩块横在坪上，正对着推会顶住），再折到画样台
	waypoints = [yard.get_bench_position() + _cradle().basis.x * 6.0, yard.get_bench_position()]
	print("shipyard_e2e: 出生点 %s，泊位接触线 %s，坞位接触线 %s（相距 %.0f m），画样台 %s，跳板 %s → %s，开局航速 %.1f m/s" % [
		player.global_position, _berth().origin, _cradle().origin, _berth().origin.distance_to(_cradle().origin),
		yard.get_bench_position(), yard.get_gangway_top(), yard.get_gangway_foot(), speed_before_refit])


func _physics_process(delta: float) -> void:
	if done:
		return
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时")
		return
	if interact_frames > 0:
		interact_frames -= 1
		if interact_frames == 0:
			Input.action_release("interact")

	match phase:
		Phase.WALK_TO_BENCH:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.REFUSED_NO_SHIP)
		Phase.REFUSED_NO_SHIP:
			if main.is_refit_open():
				_finish(false, "船不在坞里也打开了改装")
			elif elapsed - phase_started > 0.5:
				if not main.get_notice().contains("船坞里没有船"):
					_finish(false, "该提示船坞里没有船，实为「%s」" % main.get_notice())
				else:
					# 先回到栈桥正后方再上桥：从船坞直奔桥头会从岸崖边上走下去
					waypoints = [yard.get_bench_position() + _cradle().basis.x * 6.0, origin.get_spawn_transform().origin, origin.get_pier_end_position(), ship.global_position]
					_enter(Phase.WALK_TO_SHIP)
		Phase.WALK_TO_SHIP:
			if _follow_waypoints(0.6):
				_enter(Phase.TAKE_HELM)
		Phase.TAKE_HELM, Phase.TAKE_HELM_AGAIN:
			if main.get_state() == Expedition.PILOTING:
				climb_from = ship.global_position.y
				_enter(Phase.CLIMB_OUT if phase == Phase.TAKE_HELM else Phase.CLIMB_OUT_AGAIN)
			elif _walk_toward(ship.get_helm_position(), main.helm_range - 0.3):
				_tap_interact()
		Phase.CLIMB_OUT, Phase.CLIMB_OUT_AGAIN:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 8.0:
				_enter(Phase.FLY_TO_YARD if phase == Phase.CLIMB_OUT else Phase.FLY_TO_BERTH)
		Phase.FLY_TO_YARD:
			if _hop_toward(_cradle().origin, _berth().origin):
				_release_all()
				_tap_interact()
				_enter(Phase.DOCKING)
		Phase.DOCKING:
			if main.get_state() == Expedition.ON_FOOT:
				if not main.is_ship_in_yard():
					_finish(false, "入坞后船舷没贴在坞位接触线上（船 %s）" % ship.global_position)
				elif not player.is_on_floor():
					pass  # 等落到甲板上
				else:
					waypoints = [yard.get_gangway_top(), yard.get_gangway_foot(), yard.get_bench_position()]
					_enter(Phase.WALK_DOWN)
		Phase.WALK_DOWN:
			if _follow_waypoints(main.helm_range - 0.5):
				_tap_interact()
				_enter(Phase.OPEN_REFIT)
		Phase.OPEN_REFIT:
			if main.is_refit_open():
				_enter(Phase.REFIT)
				_refit()  # 协程：一步步点鼠标，做完自己进下一阶段
			elif elapsed - phase_started > 1.0:
				_finish(false, "在画样台按了 E，改装没打开（%s）" % main.get_notice())
		Phase.REFIT:
			pass
		Phase.WALK_UP:
			if _follow_waypoints(0.5):
				_enter(Phase.CLIMB_LADDER)
		Phase.CLIMB_LADDER:
			# 面朝梯子那面墙一直往前：Player 碰到梯子区域就会往上爬，爬过顶再往前就落在双层货舱顶上
			highest = maxf(highest, player.global_position.y)
			# 正对着梯子往前推：斜着走会从梯子边上滑出去掉下来
			var landing: Vector3 = ship.get_ladder_tops()[ladder_index]
			_face(landing)
			_hold("move_forward", true)
			var flat := Vector2(landing.x - player.global_position.x, landing.z - player.global_position.z).length()
			if flat < 1.2 and absf(player.global_position.y - top_deck.y) < 0.4 and player.is_on_floor():
				_hold("move_forward", false)
				_shot("LADDER_top")
				# 下去：回到梯顶落脚处，朝梯脚走——走进舷栏缺口就顺梯子下去，再走到舵旁
				waypoints = [ship.get_ladder_tops()[ladder_index], ship.get_ladder_feet()[ladder_index], ship.get_helm_position()]
				_enter(Phase.WALK_TO_HELM)
			elif elapsed - phase_started > 20.0:
				_finish(false, "20 s 没爬上第二层（最高到 y=%.1f，第二层甲板 y=%.1f，在梯子上 %s）" % [highest, top_deck.y, player.is_on_ladder()])
		Phase.WALK_TO_HELM:
			# 从第二层朝舵位走：走出梯顶的缺口掉进梯子区域后，面朝墙按后退顺梯子下去，落地再走到舵旁
			if player.is_on_ladder() and not player.is_on_floor():
				_hold("move_forward", false)
				_face(ship.get_ladder_tops()[ladder_index])
				_hold("move_back", true)
			elif _hold_back_released() and _follow_waypoints(main.helm_range - 0.3) and player.is_on_floor() and absf(player.global_position.y - ship.get_helm_position().y) < 1.5:
				_enter(Phase.TAKE_HELM_AGAIN)
			elif elapsed - phase_started > 20.0:
				_finish(false, "20 s 没走到舵旁（玩家 %s，舵 %s）" % [player.global_position, ship.get_helm_position()])
		Phase.FLY_TO_BERTH:
			if _hop_toward(_berth().origin, _cradle().origin):
				_release_all()
				_tap_interact()
				_enter(Phase.MOORING)
		Phase.MOORING:
			if main.get_state() == Expedition.ON_FOOT:
				if not ship.is_at(_berth(), 0.5):
					_finish(false, "靠泊后船舷没贴在泊位接触线上（船 %s）" % ship.global_position)
				elif main.is_ship_in_yard():
					_finish(false, "船已离坞却还说在坞里")
				else:
					_enter(Phase.DONE)
		Phase.DONE:
			_finish(true, "入坞、拖节点改成 %s（%d 节两层）、爬了梯子、飞回泊位靠泊；航速 %.1f → %.1f m/s" % [ship.get_layout(), ship.get_section_count(), speed_before_refit, ship.get_max_speed()])


# ---------------------------------------------------------------- 改装：一步步点鼠标

func _refit() -> void:
	await _frames(3)
	if not player.is_input_blocked():
		_finish(false, "开着改装人还能走")
		return
	if main.get_refit_drag() != "" or main.get_refit_selected() != "" or not main.get_refit_glow().is_empty():
		_finish(false, "刚打开时不该拖着、选着、亮着（%s，%s，%s）" % [main.get_refit_drag(), main.get_refit_selected(), main.get_refit_glow()])
		return
	var card: Vector2 = main.get_refit_card_center("storage")
	if card.x < 0.0:
		_finish(false, "面板上找不到储存区的卡片")
		return
	await _shot("REFIT_open")

	# 指着储存区卡片（不按）：船上亮起它能放的格子
	await _mouse_at(card)
	var glow: Array = main.get_refit_glow()
	if not glow.has(Vector3i(0, 0, 1)) or glow.has(Vector3i(0, 1, -1)) or glow.has(Vector3i(0, 1, 0)):
		_finish(false, "指着储存区卡片：舷门后一格该亮、帆与驾驶区顶上不该亮（%s）" % [glow])
		return
	print("shipyard_e2e:   指着储存区卡片：亮 %d 格" % glow.size())

	# 1. 按住拖到舷门后一格松手
	if not await _drag("storage", Vector3i(0, 0, 1), true, "", "REFIT_drag"):
		return
	if not await _expect(START + ["0,0,1:storage"], "拖储存区到舷门后一格", "放了储存区"):
		return
	if main.get_refit_selected() != "0,0,1:storage":
		_finish(false, "刚放的该选中（%s）" % main.get_refit_selected())
		return

	# 2. 拖到帆顶上：红色虚影、说为什么，松手不放
	if not await _drag("storage", Vector3i(0, 1, -1), false, "帆只能在最上面", "REFIT_refused"):
		return
	if not await _expect(START + ["0,0,1:storage"], "拖储存区到帆顶上被拒", "帆只能在最上面"):
		return

	# 3. 点一下空节卡片（不拖）拿着，再点舷门排左舷
	await _click(main.get_refit_card_center("deck"))
	if main.get_refit_drag() != "deck":
		_finish(false, "点一下空节卡片该拿着它（%s）" % main.get_refit_drag())
		return
	var aim: Vector2 = main.get_refit_aim(Vector3i(-1, 0, 0), "deck")
	if aim.x < 0.0:
		_finish(false, "屏幕上找不到舷门排左舷那一格")
		return
	await _click(aim)
	if not await _expect(["0,0,-1:sail", "-1,0,0:deck", "0,0,0:bridge", "0,0,1:storage"], "点一下拿着空节、点舷门排左舷", "空节"):
		return
	if main.get_refit_drag() != "":
		_finish(false, "放下后手上不该还拿着（%s）" % main.get_refit_drag())
		return

	# 4. 复合节：双桅帆（前后双节）到左舷后面两排；双层货舱（上下双节）到船尾
	if not await _drag("long_sail", Vector3i(-1, 0, 1), true, "", "REFIT_long_sail"):
		return
	if not await _expect(["0,0,-1:sail", "-1,0,0:deck", "0,0,0:bridge", "-1,0,1:long_sail", "0,0,1:storage"], "拖双桅帆到左舷后面两排", "双桅帆"):
		return
	if not await _drag("tall_hold", Vector3i(0, 0, 2), true, "", ""):
		return
	if not await _expect(FINAL, "拖双层货舱到船尾", "双层货舱"):
		return
	if ship.get_levels() != 2 or ship.get_section_count() != 8:
		_finish(false, "双层货舱放上后该两层、8 节（%d 层 %d 节）" % [ship.get_levels(), ship.get_section_count()])
		return

	# 5. 拖着帆按右键放下；点一下拿着帆按 Esc 放下（改装不合上）
	card = main.get_refit_card_center("sail")
	await _mouse_at(card)
	await _button(card, true)
	await _mouse_at(Vector2(640, 360), MOUSE_BUTTON_MASK_LEFT)
	if main.get_refit_drag() != "sail":
		_finish(false, "按住帆卡片拖出来，该拖着帆（%s）" % main.get_refit_drag())
		return
	await _button(Vector2(640, 360), true, MOUSE_BUTTON_RIGHT)
	await _button(Vector2(640, 360), false, MOUSE_BUTTON_RIGHT)
	await _button(Vector2(640, 360), false)
	if main.get_refit_drag() != "" or Array(ship.get_layout()) != FINAL:
		_finish(false, "右键该放下拖着的帆、船不变（%s，%s）" % [main.get_refit_drag(), ship.get_layout()])
		return
	await _click(card)
	if main.get_refit_drag() != "sail":
		_finish(false, "点一下帆卡片该拿着（%s）" % main.get_refit_drag())
		return
	await _action("ui_cancel")
	if main.get_refit_drag() != "" or not main.is_refit_open():
		_finish(false, "Esc 该只放下拿着的帆、改装还开着（%s，开着 %s）" % [main.get_refit_drag(), main.is_refit_open()])
		return
	print("shipyard_e2e:   右键、Esc 都放下了拿着的帆，船没变")

	# 6. 点驾驶区选中、X：拆不得
	if not await _select(Vector3i(0, 0, 0), "0,0,0:bridge"):
		return
	await _tap("remove")
	if not await _expect(FINAL, "选中驾驶区按 X", "驾驶区拆不得"):
		return

	# 7. 储存区上再叠一节；点底下那节、点「拆除」：上面搭着拆不了；点上面那节、点「拆除」：拆掉
	if not await _drag("storage", Vector3i(0, 1, 1), true, "", ""):
		return
	var stacked := FINAL + ["0,1,1:storage"]
	if not await _expect(stacked, "储存区上再叠一节", "放了储存区"):
		return
	if not await _select(Vector3i(0, 0, 1), "0,0,1:storage"):
		return
	await _shot("REFIT_selected")
	await _click(main.get_refit_remove_center())
	if not await _expect(stacked, "点底下那节储存区的「拆除」", "上面还搭着节"):
		return
	if not await _select(Vector3i(0, 1, 1), "0,1,1:storage"):
		return
	await _click(main.get_refit_remove_center())
	if not await _expect(FINAL, "点上面那节储存区的「拆除」", "拆掉了储存区"):
		return
	if main.get_refit_selected() != "":
		_finish(false, "拆掉后不该还选着（%s）" % main.get_refit_selected())
		return
	await _shot("REFIT_done")

	# 8. Tab 合上
	await _tap("inventory")
	if main.is_refit_open() or player.is_input_blocked():
		_finish(false, "Tab 之后改装没合上或人还动不了")
		return
	var stats: Dictionary = ship.get_stats()
	if not ship.can_fly() or stats["hold"] != 12 or stats["bay"] != 42 or stats["sections"] != 8 or ship.get_levels() != 2:
		_finish(false, "改完该是 8 节、两层、货位 12、船舱 42 格、能飞（%s）" % stats)
		return
	if ship.get_max_speed() >= speed_before_refit:
		_finish(false, "多了货，航速该比开局慢（%.1f ≥ %.1f）" % [ship.get_max_speed(), speed_before_refit])
		return
	if ship.get_ladder_feet().size() < 1:
		_finish(false, "两层的船该有梯子")
		return

	# 梯脚里离舷门那一列的甲板最近的那架，爬上双层货舱顶上
	var deck0: Vector3 = ship.get_deck_position(0, 0)
	ladder_index = 0
	for k in ship.get_ladder_feet().size():
		if ship.get_ladder_feet()[k].distance_to(deck0) < ship.get_ladder_feet()[ladder_index].distance_to(deck0):
			ladder_index = k
	top_deck = ship.get_deck_position(0, 2)
	# 从跳板上来贴着舷门那一侧走：舵柱在中线上，两舷有舵楼的柱子
	var side := signf(ship.to_local(yard.get_gangway_top()).x)
	waypoints = [yard.get_gangway_foot(), yard.get_gangway_top(), ship.to_global(Vector3(side * 2.0, 0.0, 0.0)), ship.to_global(Vector3(side * 2.0, 0.0, 6.0)),
		ship.get_ladder_feet()[ladder_index]]
	_enter(Phase.WALK_UP)


## 拖一种节点到锚点 anchor 那个落点：按住卡片、拖到船上，指着时查虚影，松手。
## legal：该能放（松手放上）还是不能（虚影是红的、原因里有 why）
func _drag(kind: String, anchor: Vector3i, legal: bool, why: String, shot: String) -> bool:
	var card: Vector2 = main.get_refit_card_center(kind)
	await _mouse_at(card)
	await _button(card, true)
	if main.get_refit_drag() != kind:
		_finish(false, "按住%s卡片该拖着它（%s）" % [kind, main.get_refit_drag()])
		return false
	var aim: Vector2 = main.get_refit_aim(anchor, kind)
	if aim.x < 0.0:
		_finish(false, "屏幕上找不到 %s 放在 %s 的落点（亮格 %s）" % [kind, anchor, main.get_refit_glow()])
		return false
	await _mouse_at(card.lerp(aim, 0.5), MOUSE_BUTTON_MASK_LEFT)
	await _mouse_at(aim, MOUSE_BUTTON_MASK_LEFT)
	var hover: Dictionary = main.get_refit_hover()
	if hover["type"] != "spot" or hover["anchor"] != anchor or hover["legal"] != legal or (why != "" and not String(hover["reason"]).contains(why)):
		_finish(false, "拖着%s指着 %s：虚影该是 %s（能放 %s），实为 %s" % [kind, anchor, anchor, legal, hover])
		return false
	var glow: Array = main.get_refit_glow()
	if legal != glow.has(anchor):
		_finish(false, "拖着%s：落点 %s 亮不亮该是 %s（亮格 %s）" % [kind, anchor, legal, glow])
		return false
	if shot != "":
		await _shot(shot)
	await _button(aim, false)
	return true


## 点船上锚点在 anchor 的节点：该选中 want
func _select(anchor: Vector3i, want: String) -> bool:
	var aim: Vector2 = main.get_refit_aim(anchor, "")
	if aim.x < 0.0:
		_finish(false, "屏幕上找不到 %s 那个节点" % [anchor])
		return false
	await _click(aim)
	if main.get_refit_selected() != want:
		_finish(false, "点 %s 该选中 %s，实为「%s」" % [anchor, want, main.get_refit_selected()])
		return false
	return true


## 这一步之后布局该是 want、提示里该有 notice、船还坐在坞里
func _expect(want: Array, what: String, notice: String) -> bool:
	await _frames(3)
	if Array(ship.get_layout()) != want:
		_finish(false, "%s 之后布局该是 %s，实为 %s（提示「%s」）" % [what, want, ship.get_layout(), main.get_notice()])
		return false
	if notice != "" and not main.get_notice().contains(notice):
		_finish(false, "%s：提示里该有「%s」，实为「%s」" % [what, notice, main.get_notice()])
		return false
	if not main.is_ship_in_yard():
		_finish(false, "%s 之后船舷没贴着坞位" % what)
		return false
	print("shipyard_e2e:   %s → %s（%s）" % [what, ship.get_layout(), main.get_notice()])
	return true


func _frames(n: int) -> void:
	for i in n:
		await get_tree().physics_frame


func _mouse_at(at: Vector2, buttons := 0) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = at
	motion.global_position = at
	motion.relative = at - mouse
	motion.button_mask = buttons
	Input.parse_input_event(motion)
	mouse = at
	await _frames(2)


func _button(at: Vector2, pressed: bool, button := MOUSE_BUTTON_LEFT) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = button
	ev.pressed = pressed
	ev.position = at
	ev.global_position = at
	Input.parse_input_event(ev)
	await _frames(2)


func _click(at: Vector2) -> void:
	await _mouse_at(at)
	await _button(at, true)
	await _button(at, false)
	await _frames(2)


func _tap(action: String) -> void:
	Input.action_press(action)
	await _frames(3)
	Input.action_release(action)
	await _frames(4)  # 松开几帧，下一次 just_pressed 才认


func _action(action: String) -> void:
	for pressed in [true, false]:
		var ev := InputEventAction.new()
		ev.action = action
		ev.pressed = pressed
		Input.parse_input_event(ev)
		await _frames(2)
	await _frames(2)


# ---------------------------------------------------------------- 走与飞

func _cradle() -> Transform3D:
	return yard.get_cradle_contact()


func _berth() -> Transform3D:
	return origin.get_berth_contact()


func _enter(next: Phase) -> void:
	print("shipyard_e2e: %-18s 用时 %5.1f s" % [Phase.keys()[phase], elapsed - phase_started])
	_shot("%02d_%s_end" % [phase, Phase.keys()[phase]])
	phase = next
	phase_started = elapsed


func _hold(action: String, pressed: bool) -> void:
	if pressed:
		Input.action_press(action)
	else:
		Input.action_release(action)


func _release_all() -> void:
	for action in MOVES:
		Input.action_release(action)


## 按一下 E，按住几帧再松
func _tap_interact() -> void:
	if interact_frames == 0:
		Input.action_press("interact")
		interact_frames = 4


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


func _hold_back_released() -> bool:
	_hold("move_back", false)
	return true


func _face(target: Vector3) -> void:
	var to := target - player.global_position
	player.set_look_yaw(atan2(-to.x, -to.z))


func _walk_toward(target: Vector3, radius: float) -> bool:
	var to := target - player.global_position
	var rise := to.y
	to.y = 0.0
	# 高度也得到了：挂在梯子半空时脚下的路点还不算到
	if to.length() <= radius and absf(rise) < 1.5:
		_hold("move_forward", false)
		return true
	_face(target)
	_hold("move_forward", true)
	return false


func _follow_waypoints(last_radius: float) -> bool:
	if waypoints.is_empty():
		return true
	var radius := last_radius if waypoints.size() == 1 else 0.6
	if _walk_toward(waypoints[0], radius):
		waypoints.pop_front()
	return waypoints.is_empty()


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


func _finish(ok: bool, why: String) -> void:
	if done:
		return
	done = true
	_release_all()
	for action in ["interact", "remove", "inventory"]:
		Input.action_release(action)
	set_physics_process(false)
	print("shipyard_e2e: %s —— %s（阶段 %s，模拟 %.1f s；玩家 %s，船 %s，编排状态 %d，布局 %s，提示「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, ship.global_position,
		main.get_state(), ship.get_layout(), main.get_notice()])
	if not ok:
		print("shipyard_e2e: 玩家在船的局部坐标 %s，在梯子上 %s，着地 %s，剩余路点 %s，梯脚 %s，舷门边 %.1f / %.1f，改装开着 %s，拖着「%s」，选中「%s」，指着 %s" % [
			ship.to_local(player.global_position), player.is_on_ladder(), player.is_on_floor(), waypoints, ship.get_ladder_feet(),
			ship.get_port_edge_x(), ship.get_starboard_edge_x(), main.is_refit_open(), main.get_refit_drag(), main.get_refit_selected(), main.get_refit_hover()])
	get_tree().quit(0 if ok else 1)
