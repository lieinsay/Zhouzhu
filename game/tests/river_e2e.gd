extends Node
## 岛群的河（world_2051.tscn）端到端测试：
## 开局（不钉径流，跟着钟与 world_2051 的 calendar_offset）：雨季里水不小，出生点最近的溪沿途 200 m 处处有水、有水深
## → 主码头附近找一条大河 → 瞬移到河滩上、面朝下游 → 脚下有河况、HUD 报河宽水深 → 河心的地面是河床、水面高过河床
## → 径流钉在年均 / 枯水 / 大水三档：水深、水面宽跟着涨落，大水漫出平岸 → 源头的小溪枯水时断流 → 放开钉子后跟着钟走、换日子水情变
## → 到这条河的河口看跌水 → 飞到河的上空往下游看。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/river_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。

const SCENE := preload("res://world_2051.tscn")
const TIMEOUT := 120.0  # 模拟秒
const BIG_RIVER_KM2 := 60.0  # 找汇流至少这么大的河
const SMALL_STREAM_KM2 := 1.0
const OPENING_MIN_DEPTH := 0.3  # 开局出生点附近的溪至少这么深

enum Phase { SETTLE, OPENING, FIND, LAND, MEAN, LOW, FLOOD, BROOK, CLOCK, MOUTH, AERIAL, DONE }

var main: Expedition
var world: World
var player: Player

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var river := {}  # find_river_near 的结果
var bank := Vector3.ZERO
var mean_state := {}
var low_state := {}


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	GameClock.restart(10.0)
	main = SCENE.instantiate()
	add_child(main)
	world = main.get_node("World")
	player = main.get_node("Player")


func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	pending_shot = shot_name
	shot_wait = 4


func _goto(next: Phase) -> void:
	phase = next
	phase_started = elapsed


func _in_phase() -> float:
	return elapsed - phase_started


func _finish(ok: bool, why: String) -> void:
	set_physics_process(false)
	print("river_e2e: %s —— %s（阶段 %s，模拟 %.1f s，玩家 %s，径流 %.2f，HUD「%s」）" % [
		"PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, player.global_position, world.get_runoff(), main.get_status_text()])
	get_tree().quit(0 if ok else 1)


func _expect(ok: bool, why: String) -> bool:
	if not ok:
		_finish(false, why)
	return ok


func _standing() -> bool:
	return player.is_on_floor() and world.is_ready_at(player.global_position)


## 站在 p 处、面朝 (dir_x, dir_z)
func _stand_at(p: Vector3, dir_x: float, dir_z: float) -> void:
	main.teleport_to(p)
	player.set_look_yaw(atan2(-dir_x, -dir_z))


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时")
		return
	match phase:
		Phase.SETTLE:
			if world.is_loaded() and _standing() and _in_phase() > 1.0:
				_goto(Phase.OPENING)
		Phase.OPENING:
			var spawn := main.get_spawn_point()
			if not _expect(world.get_runoff() > 1.0, "开局径流 %.2f，没对上雨季" % world.get_runoff()):
				return
			var near: Dictionary = world.find_river_near(spawn.x, spawn.z, 3000.0, SMALL_STREAM_KM2)
			if not _expect(not near.is_empty(), "出生点 3 km 内没有河"):
				return
			# 顺着这条溪往上游走 200 m，每 8 m 贴回中线查一次：处处有水
			var shallow := 0
			var least := INF
			var at: Dictionary = near
			for k in range(25):
				var here: Dictionary = world.get_river_at(at["x"], at["z"])
				if here.is_empty() or not bool(here["wet"]) or float(here["depth"]) < OPENING_MIN_DEPTH:
					shallow += 1
				else:
					least = minf(least, float(here["depth"]))
				var next: Dictionary = world.find_river_near(float(at["x"]) - float(at["dir_x"]) * 8.0, float(at["z"]) - float(at["dir_z"]) * 8.0, 6.0, SMALL_STREAM_KM2)
				if next.is_empty():
					break
				at = next
			print("river_e2e: 开局径流 %.2f；出生点最近的河（%.0f m 外，汇流 %.1f km²）往上游 200 m 有 %d 处水浅于 %.1f m，最浅 %.2f m" % [
				world.get_runoff(), Vector2(near["x"] - spawn.x, near["z"] - spawn.z).length(), near["acc_km2"], shallow, OPENING_MIN_DEPTH, least])
			# 河口前后、支流汇口会偏离中线几米：容一两处
			if not _expect(shallow <= 2, "开局出生点附近的河断断续续"):
				return
			_goto(Phase.FIND)
		Phase.FIND:
			var spawn := main.get_spawn_point()
			river = world.find_river_near(spawn.x, spawn.z, 40000.0, BIG_RIVER_KM2)
			if not _expect(not river.is_empty(), "主码头 40 km 内找不到汇流 ≥ %d km² 的河" % BIG_RIVER_KM2):
				return
			print("river_e2e: 大河在 (%.0f, %.0f)，汇流 %.0f km²，平岸宽 %.1f m，离出生点 %.1f km" % [
				river["x"], river["z"], river["acc_km2"], river["width"], Vector2(river["x"] - spawn.x, river["z"] - spawn.z).length() / 1000.0])
			# 河滩：顺水方向的左手边，平岸外一半滩宽处
			var side: float = 0.5 * float(river["width"]) + 0.5 * float(river["floodplain"])
			bank = Vector3(river["x"] + float(river["dir_z"]) * side, 0, river["z"] - float(river["dir_x"]) * side)
			world.set_runoff_override(1.0)
			_stand_at(bank, river["dir_x"], river["dir_z"])
			_goto(Phase.LAND)
		Phase.LAND:
			if _standing() and _in_phase() > 1.0:
				var here: Dictionary = world.get_river_at(player.global_position.x, player.global_position.z)
				if not _expect(not here.is_empty(), "站在河滩上却没有河况"):
					return
				if not _expect(String(main.get_status_text()).contains("河宽"), "HUD 没报河宽"):
					return
				if not _expect(world.get_river_chunk_count() > 0, "没有挂上水面块"):
					return
				if world.get_pending_count() > 0:
					return  # 近处的细地面块到齐了再看
				_goto(Phase.MEAN)
				_shot("1_mean")
		Phase.MEAN:
			if _in_phase() > 0.5:
				mean_state = world.get_river_at(river["x"], river["z"])
				var ground: float = world.get_surface_height(river["x"], river["z"])
				print("river_e2e: 年均：河床 %.2f，地面 %.2f，水位 %.2f，水深 %.2f，水面宽 %.1f（平岸 %.1f）" % [
					mean_state["bed"], ground, mean_state["level"], mean_state["depth"], mean_state["width"], mean_state["bankfull_width"]])
				if not _expect(absf(ground - float(mean_state["bed"])) < 0.1, "河心的地面不是河床（地面 %.2f，河床 %.2f）" % [ground, mean_state["bed"]]):
					return
				if not _expect(bool(mean_state["wet"]) and float(mean_state["level"]) > ground + 0.1, "年均时大河里没水"):
					return
				world.set_runoff_override(0.2)
				_goto(Phase.LOW)
				_shot("2_low")
		Phase.LOW:
			if _in_phase() > 0.5:
				low_state = world.get_river_at(river["x"], river["z"])
				print("river_e2e: 枯水：水深 %.2f，水面宽 %.1f" % [low_state["depth"], low_state["width"]])
				if not _expect(float(low_state["depth"]) < float(mean_state["depth"]) and float(low_state["width"]) < float(mean_state["width"]), "枯水时水没变浅变窄"):
					return
				world.set_runoff_override(6.5)
				_goto(Phase.FLOOD)
				_shot("3_flood")
		Phase.FLOOD:
			if _in_phase() > 0.5:
				var flood: Dictionary = world.get_river_at(river["x"], river["z"])
				print("river_e2e: 大水：水深 %.2f，水面宽 %.1f（平岸 %.1f）" % [flood["depth"], flood["width"], flood["bankfull_width"]])
				if not _expect(float(flood["width"]) > float(flood["bankfull_width"]), "大水没漫出平岸"):
					return
				if not _expect(String(main.get_status_text()).contains("涨水漫滩"), "HUD 没说涨水"):
					return
				_goto(Phase.BROOK)
		Phase.BROOK:
			# 源头的小溪：旱季断流，来水了又有水
			# find_river_near 取最近的一点，在大河边上找常就是大河本身；绕着它换几处找源头附近的小溪
			var probe := {}
			for offset in [Vector2(-3000, 0), Vector2(3000, 0), Vector2(0, -3000), Vector2(0, 3000), Vector2(-2000, 2000), Vector2(2000, -2000)]:
				var cand: Dictionary = world.find_river_near(river["x"] + offset.x, river["z"] + offset.y, 1500.0, SMALL_STREAM_KM2)
				if not cand.is_empty() and float(cand["acc_km2"]) < 2.5:
					probe = cand
					break
			if not _expect(not probe.is_empty(), "大河附近找不到汇流 < 2.5 km² 的小溪"):
				return
			world.set_runoff_override(0.2)
			var dry: Dictionary = world.get_river_at(probe["x"], probe["z"])
			world.set_runoff_override(1.5)
			var wet: Dictionary = world.get_river_at(probe["x"], probe["z"])
			print("river_e2e: 小溪（汇流 %.1f km²）：径流 0.2 时 %s，1.5 时水深 %.2f" % [probe["acc_km2"], "有水" if dry["wet"] else "断流", wet["depth"]])
			if not _expect(not bool(dry["wet"]) and bool(wet["wet"]), "小溪没有随水情断流 / 来水"):
				return
			_goto(Phase.CLOCK)
		Phase.CLOCK:
			# 放开钉子：径流跟着钟与日历走；换一天水情不同
			world.set_runoff_override(-1.0)
			var seen := {}
			for day in [2.0, 30.0, 60.0, 100.0, 150.0, 200.0, 250.0, 300.0]:
				world.set_calendar_offset(day)
				world.set_runoff_override(-1.0)
				seen[snappedf(world.get_runoff(), 0.01)] = true
			if not _expect(seen.size() >= 4, "换了日子径流不变：%s" % [seen.keys()]):
				return
			world.set_calendar_offset(0.0)
			world.set_runoff_override(1.5)
			if not river.get("mouth", false):
				_finish(true, "大河不出岸缘，不看河口")
				return
			# 河口：河槽凿到岸缘；再开 god 模式悬在岸外 35 m、比岸缘低 8 m，回头看水从岸缘跌下去
			var mouth := Vector3(river["mouth_x"], 0, river["mouth_z"])
			var flow := Vector3(river["mouth_dir_x"], 0, river["mouth_dir_z"])
			# 岸线常斜着切过河口：离河口 20 / 40 / 60 m、左右两岸里取第一个还在岛上的岸点来比
			var up := mouth - flow * 20.0
			var mid: float = world.get_surface_height(up.x, up.z)
			var bank_y := NAN
			var back := 20.0
			for d in [20.0, 40.0, 60.0]:
				for sgn in [1.0, -1.0]:
					var at: Vector3 = mouth - flow * float(d)
					var side: Vector3 = at + Vector3(-flow.z, 0, flow.x) * float(sgn) * (float(river["width"]) + 4.0)
					var y: float = world.get_surface_height(side.x, side.z)
					if is_nan(bank_y) and not is_nan(y):
						bank_y = y
						back = float(d)
						up = at
						mid = world.get_surface_height(at.x, at.z)
			print("river_e2e: 河口在 (%.0f, %.0f)，上游 %.0f m 处河心地面 %.2f、岸上 %.2f" % [mouth.x, mouth.z, back, mid, bank_y])
			if not _expect(mid < bank_y - 0.2, "河口前的河槽没凿出来"):
				return
			main.teleport_to(up)
			player.set_god_mode(true)
			player.global_position = mouth + flow * 35.0 + Vector3(0, mid - 8.0, 0)
			player.velocity = Vector3.ZERO
			player.set_look_yaw(atan2(flow.x, flow.z))
			_goto(Phase.MOUTH)
		Phase.MOUTH:
			player.velocity = Vector3.ZERO
			if world.get_pending_count() == 0 and _in_phase() > 1.5:
				_shot("4_mouth")
				# 再飞到大河上空 120 m 往下游看：远处的河也看得见、不浮在地上
				player.global_position = bank + Vector3(0, world.get_surface_height(bank.x, bank.z) + 120.0, 0)
				player.set_look_yaw(atan2(-float(river["dir_x"]), -float(river["dir_z"])))
				world.prepare_at(player.global_position)
				_goto(Phase.AERIAL)
		Phase.AERIAL:
			player.velocity = Vector3.ZERO
			if pending_shot.is_empty() and world.get_pending_count() == 0 and _in_phase() > 2.0:
				_shot("5_aerial")
				_goto(Phase.DONE)
		Phase.DONE:
			if pending_shot.is_empty() and _in_phase() > 0.3:
				_finish(true, "河网、河道、水涨水落与水面都对")
