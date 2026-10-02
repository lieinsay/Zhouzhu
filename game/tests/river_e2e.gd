extends Node
## 岛群的河（world_seed42.tscn，开局那群）端到端测试：
## 开局（不钉径流，跟着全局钟的日历：#6329 是第 85 天、四月初二开春）：全群背景径流不枯，
## 出生点最近的临时流水路径沿途 200 m 的供水类型、实际流量、干湿和宽深一致，允许局部断流
## → 主码头附近找一条大河 → 瞬移到河滩上、面朝下游 → 脚下有河况、HUD 报河宽水深 → 河心的地面是河床、水面高过河床
## → 径流钉在年均 / 枯水 / 大水三档：水深、水面宽跟着涨落，大水漫出平岸 → 零径流边界隐藏水面 → 放开钉子后跟着钟走、换日子水情变
## → 到这条河的河口看跌水 → 飞到河的上空往下游看。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/river_e2e.tscn
##
## 去掉 --headless、末尾加 `-- --shots=<目录>`，每个阶段存一张截图（目视检查用）。
## 开局细溪的实际 GPU 像素验收另见 river_opening_render.tscn；无头查询不能证明水面可见。

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 120.0  # 模拟秒
const BIG_RIVER_KM2 := 60.0  # 找汇流至少这么大的河
const SMALL_STREAM_KM2 := 1.0
const OPENING_MIN_RUNOFF := 0.5  # 开局的径流指数（年均 = 1）不低于这个：开春不是枯水期（#6329 开局那天 0.87）

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
			if not _expect(world.get_runoff() > OPENING_MIN_RUNOFF, "开局径流 %.2f，开局那天（一年里第 %d 天）不该是枯水" % [world.get_runoff(), GameClock.get_calendar_start()]):
				return
			var near: Dictionary = world.find_river_near(spawn.x, spawn.z, 3000.0, SMALL_STREAM_KM2)
			if not _expect(not near.is_empty(), "出生点 3 km 内没有河"):
				return
			# 从源头附近向下游走；必须实际查足样本，不能走一步就 break 后误判通过。
			var inconsistent := 0
			var checked := 0
			var least := INF
			var at: Dictionary = near
			for k in range(25):
				var here: Dictionary = world.get_river_at(at["x"], at["z"])
				checked += 1
				if here.is_empty() or not here.get("temporary_flow", false) or bool(here["wet"]) != (float(here["discharge_m3_s"]) > 0) or (not bool(here["wet"]) and (float(here["width"]) != 0 or float(here["depth"]) != 0)):
					inconsistent += 1
				else:
					least = minf(least, float(here["depth"]))
				var next: Dictionary = world.find_river_near(float(at["x"]) + float(at["dir_x"]) * 8.0, float(at["z"]) + float(at["dir_z"]) * 8.0, 12.0, SMALL_STREAM_KM2)
				if next.is_empty() or Vector2(next.x - at.x, next.z - at.z).length() < 4.0:
					break
				at = next
			print("river_e2e: 开局最近的临时流路 %.0f m 外：检查 %d 处，数据不一致 %d 处，最浅 %.3f m" % [
				Vector2(near["x"] - spawn.x, near["z"] - spawn.z).length(), checked, inconsistent, least])
			if not _expect(checked >= 20 and inconsistent == 0, "开局临时流路必须查足样本，供水类型和实际干湿一致"):
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
			var side: float = 0.5 * float(river["width"]) + 0.5 * float(river["floodplain_left"])
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
				var q: float = float(mean_state["speed_m_s"]) * float(mean_state["wet_area_m2"])
				if not _expect(absf(q - float(mean_state["discharge_m3_s"])) < 0.001, "流速与流量、断面不一致"):
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
				# Bankfull / mean flow is supplied per reach by the generator.
				world.set_runoff_override(2.0 * float(mean_state["bankfull_discharge_m3_s"]) / maxf(0.000001, float(mean_state["mean_discharge_m3_s"])))
				_goto(Phase.FLOOD)
				_shot("3_flood")
		Phase.FLOOD:
			if _in_phase() > 0.5:
				var flood: Dictionary = world.get_river_at(river["x"], river["z"])
				print("river_e2e: 大水：水深 %.2f，水面宽 %.1f（平岸 %.1f）" % [flood["depth"], flood["width"], flood["bankfull_width"]])
				if not _expect(float(flood["depth"]) > float(flood["bankfull_depth"]) and float(flood["width"]) >= float(flood["bankfull_width"]), "大水没到平岸以上"):
					return
				if not _expect(String(main.get_status_text()).contains("涨水漫滩"), "HUD 没说涨水"):
					return
				_goto(Phase.BROOK)
		Phase.BROOK:
			# 只验证零径流边界；源数据尚无逐日干湿掩膜，不能称作自然旱季断流验收。
			# find_river_near 取最近的一点，在大河边上找常就是大河本身；绕着它换几处找源头附近的小溪
			var probe := {}
			for offset in [Vector2(-3000, 0), Vector2(3000, 0), Vector2(0, -3000), Vector2(0, 3000), Vector2(-2000, 2000), Vector2(2000, -2000)]:
				var cand: Dictionary = world.find_river_near(river["x"] + offset.x, river["z"] + offset.y, 1500.0, SMALL_STREAM_KM2)
				if not cand.is_empty() and float(cand["acc_km2"]) < 2.5:
					probe = cand
					break
			if not _expect(not probe.is_empty(), "大河附近找不到汇流 < 2.5 km² 的小溪"):
				return
			world.set_runoff_override(0.0)
			var dry: Dictionary = world.get_river_at(probe["x"], probe["z"])
			world.set_runoff_override(1.5)
			var wet: Dictionary = world.get_river_at(probe["x"], probe["z"])
			print("river_e2e: 小溪（汇流 %.1f km²）：零径流时 %s，1.5 时水深 %.2f" % [probe["acc_km2"], "有水" if dry["wet"] else "断流", wet["depth"]])
			if not _expect(not bool(dry["wet"]) and bool(wet["wet"]), "小溪没有随水情断流 / 来水"):
				return
			_goto(Phase.CLOCK)
		Phase.CLOCK:
			# 放开钉子：径流跟着钟与日历走；换一天水情不同
			world.set_runoff_override(-1.0)
			var seen := {}
			var shift0: int = GameClock.get_calendar_shift()
			for day in [2, 30, 60, 100, 150, 200, 250, 300]:
				GameClock.set_calendar_shift(day - GameClock.get_calendar_start())  # 拨日历（钟不走）
				world.set_runoff_override(-1.0)
				seen[snappedf(float(world.get_river_at(river["x"], river["z"])["runoff_index"]), 0.01)] = true
			if not _expect(seen.size() >= 4, "换了日子径流不变：%s" % [seen.keys()]):
				return
			GameClock.set_calendar_shift(shift0)
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
				_finish(true, "河道查询、水情和水面块挂载通过；GPU 像素另见 river_opening_render")
