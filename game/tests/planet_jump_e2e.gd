extends Node
## 行星计划 P4：按需生成，任何一个岛群都能去（world_2051.tscn，剧情模式 seed42，世界存档目录用测试自己的 user://worlds/e2e_jump）。
## 2051 主码头出生 → 开发者瞬移（Expedition.request_planet_jump，行星地图点一下走的就是它）去——
##   群 #6449（北纬 36°、中疏、离 2051 约七千公里，没生成过：先清掉缓存，当场调生成器）；
##   群 #7997（北纬 78°，没生成过，一月里是极夜）；
##   再回 #6449（这回有缓存：只载入）；
##   第一回落地后（#6449）试预取：人（god）以 150 m/s 朝 70–140 km 外一群没生成过的飞，几秒内它该按「船头方向」排进生成的队（优先级 2）；
##   密接群 #162（南纬 50°，离 #163 群心 25 km）：落地以后等 #163 预取、载入成细版，人（god）摆到 #163 的陆地上空——
##     换锚的新规矩：脚下是别的群的地、不是锚群的地，当场换过去（原来只在云海上空换，挨着的群落在陆地上空就一直不换）。
## 每一回查：从点下去到能走（人站在那一群的地上、脚下的碰撞挂好）用了多久（真实秒）、期间最慢一帧（真实毫秒），落点是主码头（没有码头是邑治），
## 锚换成了那一群、HUD 第一行报「群 #N」，码头边的村子铺出来了，相机的纬度就是那一群的纬度，季节对（2051 在南半球的夏天，北半球同一天是冬天：白天短于 12 小时）。
## 没生成过的 ≤ 60 s、有缓存的 ≤ 5 s、期间最慢帧 ≤ 50 ms（计划 P4 的验收，带窗口量；无头下只拦 150 ms 以上的大卡）。
## 没有 Python / 生成器（GroupProvider.can_generate 为假）时报 SKIP、退出码 77——不算 PASS。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/planet_jump_e2e.tscn
##   … -- --nodes=4,3588,162,2484,3922 --shots=<目录>      换一串群去（依次），每落一次存一张截图（去掉 --headless）
##   … -- --keep                                         不清缓存（没生成过的就当成有缓存的去）

const SCENE := preload("res://world_2051.tscn")
const WORLD_DIR := "user://worlds/e2e_jump"
const HOME := 2051
const TIMEOUT_REAL_S := 600.0  # 真实秒（生成器在真实时间里跑，无头下模拟时间比它快十几倍）
const UNCACHED_LIMIT_S := 60.0
const CACHED_LIMIT_S := 5.0
const FRAME_LIMIT_MS := 50.0  # 带窗口
const HEADLESS_FRAME_LIMIT_MS := 150.0

enum Phase { SETTLE, JUMP, LANDED, STAY, PREFETCH, DENSE, DENSE_SHIFT, DONE }
const STAY_S := 10.0  # 落地、村子铺出来以后站一会儿（真实秒）再去下一群：落地后村子开账、村民展开（无头下模拟时间跑得快，开账的后台活落后得多，村民要「估着走过去」）的那几帧另记，不算进下一回瞬移

var main: Expedition
var player: Player
var stream: GroupStream
var provider: GroupProvider
var planet: PlanetView
var atmo: Atmosphere

var targets: Array[int] = [6449, 7997, 6449, 162]
const DENSE_A := 162
const DENSE_B := 163
var dense_check := true
var prefetch_check := true
var prefetch_node := -1
var prefetch_dir := Vector3()
var prefetch_t0 := 0
var dense_shifts := 0
var dense_t0 := 0
var expect_cached: Array[bool] = []
var index := 0
var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var keep := false
var pending_shot := ""
var shot_wait := 0
var last_tick := 0
var jump_max_ms := 0.0
var jump_frames := 0
var jump_t0 := 0
var home_day_length := 0.0
var results: Array[String] = []
var worst_ms := 0.0
var stay_max_ms := 0.0
var stay_t0 := 0
var start_us := Time.get_ticks_usec()


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
		elif arg.begins_with("--nodes="):
			dense_check = false
			prefetch_check = false
			targets.clear()
			for s in arg.trim_prefix("--nodes=").split(",", false):
				targets.append(int(s))
		elif arg == "--keep":
			keep = true
	get_tree().root.size = Vector2i(1280, 720)
	main = SCENE.instantiate()
	provider = main.get_node("GroupProvider")
	provider.world_dir = WORLD_DIR
	# 没生成过的：清掉这个测试世界里它们的缓存（仓库里的不动）；同一群第二回去就是「有缓存」
	_remove_tree(WORLD_DIR.path_join("memory"))  # 上一回留下的 2051 的账：别让这一回开局补回
	# 这个测试世界里生成过的群全清掉（仓库里的不动）：要去的、预取的都从「没生成过」起；同一群第二回去就是「有缓存」
	if not keep:
		_remove_tree(WORLD_DIR.path_join("groups"))
		_remove_tree(WORLD_DIR.path_join("logs"))
	var seen := {}
	for n in targets:
		expect_cached.append(seen.has(n) or keep or DirAccess.dir_exists_absolute("res://worlds/%d" % n))
		seen[n] = true
	add_child(main)
	player = main.get_node("Player")
	stream = main.get_node("GroupStream")
	planet = main.get_node("PlanetView")
	atmo = main.get_node("Atmosphere")
	last_tick = Time.get_ticks_usec()
	if not provider.can_generate():
		print("planet_jump: SKIP —— 调不了生成器：%s" % provider.get_problem())
		get_tree().quit(77)


func _remove_tree(dir: String) -> void:
	if not DirAccess.dir_exists_absolute(dir):
		return
	for f in DirAccess.get_files_at(dir):
		DirAccess.remove_absolute(dir.path_join(f))
	for d in DirAccess.get_directories_at(dir):
		_remove_tree(dir.path_join(d))
	DirAccess.remove_absolute(dir)


func _log(line: String) -> void:
	print("planet_jump: " + line)


func _finish(ok: bool, why: String) -> void:
	for r in results:
		_log(r)
	print("planet_jump: %s —— %s（阶段 %s，模拟 %.1f s）" % ["PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed])
	get_tree().quit(0 if ok else 1)


func _goto(next: Phase) -> void:
	phase = next
	phase_started = elapsed


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	pending_shot = shot_name
	shot_wait = 3


func _process(_delta: float) -> void:
	var now := Time.get_ticks_usec()
	var ms := (now - last_tick) / 1000.0
	last_tick = now
	if phase == Phase.LANDED or phase == Phase.STAY:
		stay_max_ms = maxf(stay_max_ms, ms)
		if ms > 35.0 and OS.has_environment("ZHOUZHU_STREAM_TRACE"):
			_log("  落地后慢帧 %.1f ms" % ms)
	if phase == Phase.JUMP:
		jump_max_ms = maxf(jump_max_ms, ms)
		jump_frames += 1
		if ms > 35.0:
			var js: Dictionary = main.get_jump_state()
			_log("  慢帧 %.1f ms（%s · %d%%）" % [ms, js["text"], int(float(js["progress"]) * 100.0)])
	if pending_shot.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	pending_shot = ""


func _anchor_world() -> GroupWorld:
	return stream.get_group_world(stream.get_anchor_node())


func _standing() -> bool:
	var w := _anchor_world()
	if w == null:
		return false
	var at := player.global_position
	var ground: float = w.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.5 and w.is_ready_at(at)


func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	if (Time.get_ticks_usec() - start_us) / 1e6 > TIMEOUT_REAL_S:
		_finish(false, "超时（真实 %.0f s）" % ((Time.get_ticks_usec() - start_us) / 1e6))
		return
	match phase:
		Phase.SETTLE:
			if stream.is_ready() and _standing() and in_phase > 1.0:
				home_day_length = float(atmo.get_weather()["day_length"])
				_log("2051 主码头站稳；此地白天 %.1f h（南半球的夏天）；生成器可用" % home_day_length)
				_start_jump()
			elif in_phase > 60.0:
				_finish(false, "60 s 没在 2051 站稳")
		Phase.JUMP:
			var js: Dictionary = main.get_jump_state()
			if int(js["stage"]) != 0:
				if (Time.get_ticks_usec() - jump_t0) / 1e6 > 300.0:
					_finish(false, "瞬移 300 s（真实）没好（%s）" % js)
				return
			var last: Dictionary = js["last"]
			var node: int = targets[index]
			if int(last.get("node", -1)) != node:
				return
			if not bool(last.get("ok", false)):
				_finish(false, "去群 #%d 失败：%s" % [node, last.get("why", "?")])
				return
			var real_s := (Time.get_ticks_usec() - jump_t0) / 1e6
			var cached: bool = expect_cached[index]
			var limit := CACHED_LIMIT_S if cached else UNCACHED_LIMIT_S
			var line := "群 #%d（%s）：%s，从点下去到能走 %.1f s（其中备好 %.1f s）、%d 帧里最慢 %.1f ms；落在%s" % [node, "有缓存" if cached else "没生成过",
				"ok", real_s, float(last["prepare_ms"]) / 1000.0, jump_frames, jump_max_ms, last["where"]]
			results.append(line)
			_log(line)
			worst_ms = maxf(worst_ms, jump_max_ms)
			if real_s > limit:
				_finish(false, "群 #%d %s 该 ≤ %.0f s 能走，用了 %.1f s" % [node, "有缓存" if cached else "没生成过", limit, real_s])
				return
			# 计划的预算（≤ 50 ms）按 Debug、带窗口量；无头下模拟时间比真实快十几倍，村子开账、村民「估着走过去」那几帧也挤进来，只拦大卡（≤ 150 ms）
			var frame_limit := FRAME_LIMIT_MS if DisplayServer.get_name() != "headless" else HEADLESS_FRAME_LIMIT_MS
			if jump_max_ms > frame_limit:
				_finish(false, "去群 #%d 期间最慢一帧 %.1f ms（该 ≤ %.0f ms）" % [node, jump_max_ms, frame_limit])
				return
			_goto(Phase.LANDED)
		Phase.LANDED:
			var node: int = targets[index]
			if stream.get_anchor_node() != node:
				_finish(false, "落地后锚该是群 #%d，实为 #%d" % [node, stream.get_anchor_node()])
				return
			var status := main.get_status_text()
			if not status.begins_with("群 #%d" % node):
				_finish(false, "HUD 第一行该以「群 #%d」开头：%s" % [node, status.get_slice("\n", 0)])
				return
			# 人站在地上；码头边的村子铺出来（给村子 60 s）
			var sites: SiteStream = stream.get_group_sites(node)
			var villages: int = sites.get_built_villages().size() if sites != null else 0
			if not _standing() or villages == 0:
				if in_phase > 60.0:
					_finish(false, "群 #%d 落地 60 s：站稳 %s、铺出来的村 %d" % [node, _standing(), villages])
				return
			# 纬度：相机就在那一群（落点离群心几十公里，差不到 1°）
			var src: Dictionary = _anchor_world().get_info()["source"]
			var ll := planet.get_camera_latlon()
			if absf(ll.x - float(src["lat"])) > 1.0:
				_finish(false, "群 #%d 的纬度 %.2f°，相机在 %.2f°" % [node, float(src["lat"]), ll.x])
				return
			var w: Dictionary = atmo.get_weather()
			if absf(float(w["latitude"]) - float(src["lat"])) > 0.01:
				_finish(false, "天气模型的纬度 %.2f° 该是这一群的 %.2f°" % [float(w["latitude"]), float(src["lat"])])
				return
			# 季节：与 2051（南半球，此刻夏天）隔着赤道就反过来——白天短于 12 h；同在南半球就也长于 12 h；赤道附近不查
			var dl := float(w["day_length"])
			if absf(float(src["lat"])) > 10.0 and (dl - 12.0) * (home_day_length - 12.0) * (-1.0 if float(src["lat"]) > 0.0 else 1.0) <= 0.0:
				_finish(false, "群 #%d（纬度 %.1f°）此刻白天 %.1f h，季节不对（2051 此刻 %.1f h）" % [node, float(src["lat"]), dl, home_day_length])
				return
			var look: Dictionary = atmo.get_look()
			var line := "  群 #%d：%s · 相机 %.2f° %.2f° · 此地「%s」%s（降水 %.2f、雨丝 %.2f）、白天 %.1f h · 铺出来的村 %d 个 · 地方时比钟点%s %.0f 分钟" % [node, status.get_slice("\n", 0).get_slice("    ", 0),
				ll.x, ll.y, w["season_name"], w["kind_name"], float(w["precip"]), float(look["rain_ratio"]), dl, villages, "早" if float(w["local_offset_h"]) >= 0.0 else "晚", absf(float(w["local_offset_h"])) * 60.0]
			results.append(line)
			_log(line)
			_shot("%d_%d" % [index + 1, node])
			stay_t0 = Time.get_ticks_usec()
			_goto(Phase.STAY)
		Phase.STAY:
			if (Time.get_ticks_usec() - stay_t0) / 1e6 < STAY_S or not pending_shot.is_empty():
				return
			var node: int = targets[index]
			_log("  群 #%d：落地到站够 %.0f s 期间最慢一帧 %.1f ms（村子铺出来、村民展开）" % [node, STAY_S, stay_max_ms])
			stay_max_ms = 0.0
			if prefetch_check and index == 0 and _start_prefetch():
				return
			index += 1
			if index >= targets.size():
				if dense_check and node == DENSE_A:
					dense_t0 = Time.get_ticks_usec()
					_goto(Phase.DENSE)
				else:
					_goto(Phase.DONE)
			else:
				_start_jump()
		Phase.PREFETCH:
			# 以 150 m/s 朝那一群飞（锚群平面里按方位走直线，云海上空 1500 m）；GroupProvider 按焦点近 3 s 的挪动定船头
			player.velocity = Vector3()
			player.global_position = Vector3(player.global_position.x, 1500.0, player.global_position.z) + prefetch_dir * 150.0 * delta
			var r: Dictionary = provider.get_request(prefetch_node)
			var st := int(r["state"])
			if st >= GroupProvider.QUEUED and st != GroupProvider.FAILED and int(r.get("priority", -1)) == GroupProvider.PREFETCH:
				var line := "预取：朝 %.0f km 外的 #%d 以 150 m/s 飞了 %.1f s（真实），它按船头方向排进了生成的队（状态 %d）；预取排了 %d 群" % [planet.distance_to_node(prefetch_node) / 1000.0, prefetch_node,
					(Time.get_ticks_usec() - prefetch_t0) / 1e6, st, int(provider.get_stats()["prefetched"])]
				results.append(line)
				_log(line)
				player.set_god_mode(false)
				index += 1
				_start_jump()
			elif (Time.get_ticks_usec() - prefetch_t0) / 1e6 > 15.0:
				_finish(false, "预取：朝 #%d 飞了 15 s 它还没按船头方向排进队（%s）" % [prefetch_node, r])
		Phase.DENSE:
			# 挨着的 #163：预取（焦点 60 km 内）生成、载入成细版
			var st: Dictionary = stream.get_group_state(DENSE_B)
			var real_s := (Time.get_ticks_usec() - dense_t0) / 1e6
			if st.is_empty() or int(st["tier"]) == GroupStream.TIER_FAR or not bool(st["loaded"]):
				if real_s > 120.0:
					_finish(false, "密接群：#%d 120 s（真实）没载入（%s；预取 %s）" % [DENSE_B, st, provider.get_request(DENSE_B)])
				return
			var bw: GroupWorld = stream.get_group_world(DENSE_B)
			var spot: Vector3 = stream.to_anchor(DENSE_B, bw.get_dock_spawn(0).origin)
			var aw := _anchor_world()
			if not is_nan(aw.get_surface_height(spot.x, spot.z)):
				_finish(false, "密接群：#%d 的码头那一点在 #%d 的地上（两群的地叠着），挑错了点" % [DENSE_B, DENSE_A])
				return
			_log("密接群：#%d 载入了（档 %d，离陆地 %.1f km，%.1f s）；人摆到 #%d 码头上空（锚群 #%d 平面 %s，脚下没有 #%d 的地）" % [DENSE_B, int(st["tier"]), float(st["land_km"]), real_s, DENSE_B, DENSE_A, spot, DENSE_A])
			dense_shifts = main.get_shift_count()
			player.set_god_mode(true)
			player.global_position = spot + Vector3(0, 120, 0)
			player.velocity = Vector3()
			_goto(Phase.DENSE_SHIFT)
		Phase.DENSE_SHIFT:
			if main.get_shift_count() == dense_shifts:
				player.velocity = Vector3()
				if in_phase > 3.0:
					_finish(false, "密接群：人在 #%d 的地上空 3 s 没换锚（锚 #%d）" % [DENSE_B, stream.get_anchor_node()])
				return
			if stream.get_anchor_node() != DENSE_B:
				_finish(false, "密接群：换锚换到了 #%d，该是 #%d" % [stream.get_anchor_node(), DENSE_B])
				return
			var w2 := _anchor_world()
			var at := player.global_position
			var ground: float = w2.get_surface_height(at.x, at.z)
			var line := "密接群：人在 #%d 的陆地上空 %.2f s 就换锚到它（不等云海上空）；换过去脚下离地 %.0f m" % [DENSE_B, in_phase, at.y - ground]
			results.append(line)
			_log(line)
			player.set_god_mode(false)
			_goto(Phase.DONE)
		Phase.DONE:
			if pending_shot.is_empty():
				var stats: Dictionary = provider.get_stats()
				_finish(true, "瞬移 %d 回都落地（生成了 %d 群、预取排了 %d 群、失败 %d；最慢一帧 %.1f ms）" % [targets.size(), int(stats["generated"]), int(stats["prefetched"]), int(stats["failed"]), worst_ms])


func _start_prefetch() -> bool:
	# 找 70–140 km 外、没生成过的一群（远处的岛群里挑）
	var far: FarGroups = main.get_node("FarGroups")
	for n in far.get_nodes():
		var d: float = planet.distance_to_node(n)
		if d > 70000.0 and d < 140000.0 and not provider.is_available(n) and int(provider.get_request(n)["state"]) == GroupProvider.NONE:
			prefetch_node = n
			break
	if prefetch_node < 0:
		_log("预取：70–140 km 内没有没生成过的群，跳过")
		return false
	var b := deg_to_rad(planet.bearing_to_node(prefetch_node))
	prefetch_dir = Vector3(sin(b), 0.0, -cos(b))
	player.set_god_mode(true)
	prefetch_t0 = Time.get_ticks_usec()
	_log("预取：朝 #%d（%.0f km、方位 %.0f°）飞" % [prefetch_node, planet.distance_to_node(prefetch_node) / 1000.0, planet.bearing_to_node(prefetch_node)])
	_goto(Phase.PREFETCH)
	return true


func _start_jump() -> void:
	var node: int = targets[index]
	jump_t0 = Time.get_ticks_usec()
	jump_max_ms = 0.0
	jump_frames = 0
	last_tick = jump_t0
	if not main.request_planet_jump(node):
		_finish(false, "去群 #%d 起不了：%s" % [node, main.get_jump_state()["last"]])
		return
	_log("去群 #%d（%s）……" % [node, "有缓存" if expect_cached[index] else "没生成过"])
	_goto(Phase.JUMP)
