extends Node
## 行星计划 P4：开发机上新建随机世界（生存模式，计划 4.8、D15）——
## F2「新建世界」走的同一条路（DeveloperTools.new_world：调生成器算行星层 → 行星包（没有粗版）→ 出生的岛群 → world.json），
## 只是好了不换场景，这里自己实例化 world_2051.tscn（DeveloperTools.prepare_world 把这个世界交给场景里的 GroupProvider）：
##   从点下去到在出生群的主码头能走 ≤ 60 s（真实秒）；
##   远处是替身（行星包里没有粗版：FarGroups 画的是 planet_surface 的替身，行星贴图里也是）；
##   F2 的清单里有「行星」「新建世界」；打开行星地图（F2「行星」）：读数说得出你在哪一群、别的群没生成过；选中 600 km 内的一群、点瞬移（T）——
##     地图合上、当场生成、落到那一群的主码头；
##   站着等一阵，600 km 内的替身陆续换成粗版（GroupProvider 的粗版那条活，一批批生成、存进存档目录的 lods/）、行星贴图跟着重烘；
## 然后「退出再进」：拆掉场景、再进一回——行星层、出生群、粗版都不重生成（存档目录里有的都认，文件一个没重写），替身比第一回少。
## 没有 Python / 生成器时报 SKIP、退出码 77（不算 PASS）。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/planet_world_e2e.tscn [-- --seed=2027 --shots=<目录>]

const SCENE := preload("res://world_2051.tscn")
const WORLD_NAME := "e2e_newworld"
const WORLD_DIR := "user://worlds/" + WORLD_NAME
const TIMEOUT_REAL_S := 900.0  # 真实秒：生成器在真实时间里跑，无头下模拟时间比它快几十倍，等生成器的都按真实秒算
const WALK_LIMIT_S := 60.0
const WANT_REPLACED := 6  # 至少这么多群的替身换成了粗版

enum Phase { MAKE, ENTER, WALK, FAR_LOOK, MAP, MAP_JUMP, STAND_INS, EXIT, REENTER, DONE }

var devtools: Node
var main: Expedition
var player: Player
var provider: GroupProvider
var far: FarGroups
var globe: PlanetGlobe
var stream: GroupStream

var seed_value := 2027
var phase := Phase.MAKE
var elapsed := 0.0
var phase_started := 0.0
var t0 := 0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var first_stand_ins := 0
var first_lods := 0
var lod_mtimes := {}
var spawn_node := -1
var made_s := 0.0
var walk_s := 0.0
var last_tick := 0
var max_ms := 0.0
var map_target := -1
var map_t0 := 0
var log_at := 0.0
var start_us := 0
var phase_real := 0.0


func _real() -> float:
	return (Time.get_ticks_usec() - start_us) / 1e6


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--seed="):
			seed_value = int(arg.trim_prefix("--seed="))
		elif arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	devtools = get_node("/root/DeveloperTools")
	_remove_tree(WORLD_DIR)
	start_us = Time.get_ticks_usec()
	t0 = Time.get_ticks_usec()
	last_tick = t0
	if not devtools.new_world(seed_value, false, WORLD_NAME):
		print("planet_world: SKIP —— 新建不了世界：%s" % devtools.get_notice())
		get_tree().quit(77)
		return
	_log("新建世界 %s（种子 %d）……" % [WORLD_DIR, seed_value])


func _remove_tree(dir: String) -> void:
	if not DirAccess.dir_exists_absolute(dir):
		return
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		_remove_tree(dir.path_join(d))
	DirAccess.remove_absolute(dir)


func _log(line: String) -> void:
	print("planet_world: " + line)


func _finish(ok: bool, why: String) -> void:
	print("planet_world: %s —— %s（阶段 %s，模拟 %.1f s）" % ["PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed])
	get_tree().quit(0 if ok else 1)


func _goto(next: Phase) -> void:
	phase = next
	phase_started = elapsed
	phase_real = _real()
	log_at = 0.0


func _shot(shot_name: String) -> void:
	if not shots_dir.is_empty():
		pending_shot = shot_name
		shot_wait = 3


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	if phase == Phase.MAKE or phase == Phase.ENTER or phase == Phase.WALK:
		max_ms = maxf(max_ms, (now - last_tick) / 1000.0)
	last_tick = now
	if pending_shot.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _enter() -> void:
	if not devtools.prepare_world(WORLD_DIR):
		_finish(false, "世界 %s 进不去：%s" % [WORLD_DIR, devtools.get_notice()])
		return
	main = SCENE.instantiate()
	add_child(main)
	player = main.get_node("Player")
	provider = main.get_node("GroupProvider")
	far = main.get_node("FarGroups")
	globe = main.get_node("PlanetGlobe")
	stream = main.get_node("GroupStream")


func _standing() -> bool:
	var w: GroupWorld = stream.get_group_world(stream.get_anchor_node()) if stream.is_ready() else main.get_node_or_null("World")
	if w == null or not w.is_loaded():
		return false
	var at := player.global_position
	var ground: float = w.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.5 and w.is_ready_at(at)


func _lod_files() -> Dictionary:
	var out := {}
	var dir := WORLD_DIR.path_join("lods")
	if DirAccess.dir_exists_absolute(dir):
		for f in DirAccess.get_files_at(dir):
			if f.ends_with(".bin"):
				out[f] = FileAccess.get_modified_time(dir.path_join(f))
	return out


func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	var real_in_phase := _real() - phase_real
	if _real() > TIMEOUT_REAL_S:
		_finish(false, "超时（真实 %.0f s）" % _real())
		return
	match phase:
		Phase.MAKE:
			var st: Dictionary = devtools.get_new_world_state()
			if bool(st["making"]):
				if real_in_phase - log_at > 1.0:
					log_at = real_in_phase
					_log("  %.0f s：%s · %d%%" % [real_in_phase, st["text"], int(float(st["progress"]) * 100.0)])
				return
			if bool(st["failed"]) or not bool(st["done"]):
				if real_in_phase < 0.5:
					return
				_finish(false, "新建世界失败：%s" % st["error"])
				return
			made_s = (Time.get_ticks_usec() - t0) / 1e6
			var info: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(WORLD_DIR.path_join("world.json")))
			spawn_node = int(info["spawn"])
			_log("世界建好：%.1f s（行星层 %.1f s、导出行星包 %.1f s、出生群 #%d %.1f s——%s）" % [made_s, float(info["seconds"]["planet"]), float(info["seconds"]["export_planet"]),
				spawn_node, float(info["seconds"]["spawn_group"]), info["spawn_reason"]])
			_enter()
			_goto(Phase.WALK)
		Phase.WALK:
			if not _standing():
				if real_in_phase > 60.0:
					_finish(false, "进了世界 60 s（真实）还没站稳")
				return
			walk_s = (Time.get_ticks_usec() - t0) / 1e6
			var status := main.get_status_text()
			if not status.begins_with("群 #%d" % spawn_node):
				_finish(false, "HUD 第一行该以「群 #%d」开头：%s" % [spawn_node, status.get_slice("\n", 0)])
				return
			_log("从点下去到能走 %.1f s（建世界 %.1f s、进场景站稳 %.1f s）；这一段最慢一帧 %.1f ms；%s" % [walk_s, made_s, walk_s - made_s, max_ms, status.get_slice("\n", 0).get_slice("    ", 0)])
			if walk_s > WALK_LIMIT_S:
				_finish(false, "新建世界到能走该 ≤ %.0f s，用了 %.1f s" % [WALK_LIMIT_S, walk_s])
				return
			_goto(Phase.FAR_LOOK)
		Phase.FAR_LOOK:
			# 刚进来：远处的岛群先是替身（行星包里没有粗版）；粗版那条活由近及远补，一批几秒
			if far.get_group_count() < 3 and real_in_phase < 20.0:
				return
			first_stand_ins = far.get_stand_in_count()
			_log("刚进来：远处的岛群 %d 群里 %d 群是替身（粗版已经加了 %d 份）；行星贴图烘好了没有：%s" % [far.get_group_count(), first_stand_ins, int(provider.get_stats()["lods_added"]),
				globe.get_state()["surface_ready"]])
			if first_stand_ins == 0:
				_finish(false, "新世界里远处该先是替身（行星包里没有粗版）")
				return
			_shot("1_stand_ins")
			_goto(Phase.MAP)
		Phase.MAP:
			# F2 的清单、行星地图（开发者）：读数、选中、点瞬移
			if real_in_phase < 1.0 or not pending_shot.is_empty():
				return
			var entries: PackedStringArray = devtools.get_menu_entries()
			var has_planet := false
			var has_new := false
			for e in entries:
				has_planet = has_planet or (e.begins_with("行星") and not e.contains("没有行星数据"))
				has_new = has_new or e.begins_with("新建世界")
			if not has_planet or not has_new:
				_finish(false, "F2 的清单里该有「行星」与「新建世界」：%s" % [entries])
				return
			devtools.open_planet_map()
			var pm: Control = devtools.get_planet_map()
			if not devtools.is_planet_open() or not pm.visible:
				_finish(false, "行星地图没开")
				return
			var here: String = pm.get_readout(spawn_node)
			var near: PackedInt32Array = far.get_nodes()
			for n in near:
				if n != spawn_node and not provider.is_available(n):
					map_target = n
					break
			if map_target < 0:
				_finish(false, "600 km 内没有别的群（%s）" % [near])
				return
			var there: String = pm.get_readout(map_target)
			if not here.contains("你在这一群") or not here.contains("群 #%d" % spawn_node) or not there.contains("没生成过"):
				_finish(false, "行星地图的读数不对：\n%s\n——\n%s" % [here, there])
				return
			_log("行星地图：你在 #%d；选中 #%d：%s" % [spawn_node, map_target, there.replace("\n", " / ")])
			pm.look_at_node(map_target)
			pm.select(map_target)
			_shot("0_planet_map")
			map_t0 = Time.get_ticks_usec()
			_goto(Phase.MAP_JUMP)
		Phase.MAP_JUMP:
			if not pending_shot.is_empty():
				return
			var pm: Control = devtools.get_planet_map()
			if devtools.is_planet_open():
				pm.request_jump()  # 同按 T：发 jump_requested → 开发者工具让编排瞬移、地图合上
				if devtools.is_planet_open():
					_finish(false, "点了瞬移，行星地图该合上")
				return
			var js: Dictionary = main.get_jump_state()
			if int(js["stage"]) != 0:
				if real_in_phase > 120.0:
					_finish(false, "从行星地图瞬移 120 s（真实）没好（%s）" % js)
				return
			var last: Dictionary = js["last"]
			if int(last.get("node", -1)) != map_target or not bool(last.get("ok", false)):
				_finish(false, "从行星地图瞬移去 #%d 没成：%s" % [map_target, last])
				return
			if not _standing() or stream.get_anchor_node() != map_target:
				if real_in_phase > 120.0:
					_finish(false, "瞬移到 #%d 以后没站稳（锚 #%d）" % [map_target, stream.get_anchor_node()])
				return
			_log("从行星地图点瞬移去 #%d（没生成过）：%.1f s 能走（其中备好 %.1f s），落在%s" % [map_target, (Time.get_ticks_usec() - map_t0) / 1e6, float(last["prepare_ms"]) / 1000.0, last["where"]])
			_goto(Phase.STAND_INS)
		Phase.STAND_INS:
			# 远处先是替身；GroupProvider 由近及远补粗版，FarGroups 换成粗版，行星贴图重烘
			if real_in_phase < 3.0:
				return
			var ps: Dictionary = provider.get_stats()
			var replaced: int = far.get_replaced_count()
			var gs: Dictionary = globe.get_state()
			if real_in_phase - log_at > 5.0:
				log_at = real_in_phase
				_log("  %.0f s（真实）：粗版加了 %d 份（失败 %d）、替身换成粗版 %d 群、还剩替身 %d 群；行星贴图重烘 %d 回" % [real_in_phase, int(ps["lods_added"]), int(ps["lod_failed"]), replaced,
					far.get_stand_in_count(), int(gs["rebakes"])])
			if replaced >= WANT_REPLACED and int(gs["rebakes"]) >= 1 and not bool(gs["rebaking"]):
				_log("站了 %.0f s（真实）：粗版加了 %d 份、%d 群的替身换成了粗版（还剩 %d 群替身）、行星贴图重烘了 %d 回（替身 %d 群次）" % [real_in_phase, int(ps["lods_added"]), replaced,
					far.get_stand_in_count(), int(gs["rebakes"]), int(gs["stand_ins"])])
				_shot("2_lods")
				first_lods = int(ps["lods_added"])
				_goto(Phase.EXIT)
			elif real_in_phase > 300.0:
				_finish(false, "300 s（真实）里替身换成粗版的只有 %d 群（粗版加了 %d 份、失败 %d；重烘 %d 回）" % [replaced, int(ps["lods_added"]), int(ps["lod_failed"]), int(gs["rebakes"])])
		Phase.EXIT:
			if not pending_shot.is_empty():
				return
			if in_phase < 0.1:
				return
			# 等手上这一批粗版做完再退（进程在跑时退出就杀掉它，下回照样补）
			if bool(provider.get_stats()["lod_running"]) and real_in_phase < 60.0:
				return
			lod_mtimes = _lod_files()
			main.get_node("GroupStream").save_world()
			main.queue_free()
			main = null
			_log("退出：存档目录里 %d 份粗版" % lod_mtimes.size())
			_goto(Phase.REENTER)
		Phase.REENTER:
			if main == null:
				if in_phase < 0.5:
					return
				t0 = Time.get_ticks_usec()
				_enter()
				return
			if not _standing():
				if real_in_phase > 60.0:
					_finish(false, "再进 60 s（真实）还没站稳")
				return
			if real_in_phase < 5.0:
				return
			var again_s := (Time.get_ticks_usec() - t0) / 1e6
			var ps: Dictionary = provider.get_stats()
			if int(ps["generated"]) != 0:
				_finish(false, "再进不该再生成岛群（生成了 %d 群）" % int(ps["generated"]))
				return
			var now_files := _lod_files()
			for f in lod_mtimes.keys():
				if not now_files.has(f):
					_finish(false, "粗版 %s 不见了" % f)
					return
				if int(now_files[f]) != int(lod_mtimes[f]):
					_finish(false, "粗版 %s 重生成了（改动时刻变了）" % f)
					return
			var info: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(WORLD_DIR.path_join("world.json")))
			var stand_now: int = far.get_stand_in_count()
			_log("再进：%.1f s 站稳，没重生成（生成 0 群；已有的 %d 份粗版一个没重写）；远处替身 %d 群（第一回 %d 群）；出生群 #%d" % [again_s, lod_mtimes.size(), stand_now, first_stand_ins, int(info["spawn"])])
			if stand_now >= first_stand_ins:
				_finish(false, "再进时远处的替身该比第一回少（%d / %d）" % [stand_now, first_stand_ins])
				return
			_shot("3_reentered")
			_goto(Phase.DONE)
		Phase.DONE:
			if pending_shot.is_empty():
				_finish(true, "新建世界（种子 %d，出生群 #%d）%.1f s 能走；替身陆续换成粗版（%d 份）；退出再进不重生成" % [seed_value, spawn_node, walk_s, first_lods])
