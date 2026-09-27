extends Node
## 行星计划 P7：降到海面——云带之下（world_2051.tscn），只通过输入动作开船：
## 2051 主码头出生（钟停在一月初四 10 点、天气覆盖成晴：光与雾只随高度变）→ 走上船、掌舵、升空、换疾档
## → 背着主峰往外飞到一片开阔的云海上空（2.5 km 内没有地）
## → 按住下降一路降到海面：每一帧录光与雾的读数（日光、环境光、雾的浓淡与颜色、天空蒙雾的份、云带的剖面、颠簸）——
##   云带顶上：天与光照旧；云带里（−5.6 km 上下）：浓雾（能见度几十米）、天被雾盖满、日光只剩一点、船在颠、HUD 报「云带里」；
##   穿过云底（−6.5 km 上下）：日光熄了、阴天的漫射光、霾、HUD 报「云底下」；
## → 海面上空：最低高度挡得住（再按下降也停在海面之上 15 m）、HUD 报离海面 15 m、F6 报离海面、气温与 y = 0 一样（D13）、
##   提示「海上无处落脚」、按 E 离不了舵；覆盖成风暴：浪高、暴雨、霾浓、颠得厉害、打闪；恢复晴
## → 按住上升回到云带顶以上：读数恢复成降下去之前的样子；一路上（降、升）相邻两帧的读数都没有跳变
## 读数存成 CSV（`-- --shots=<目录>` 时在那个目录，否则 user://sea_descent.csv），外面画成曲线看。
## 去掉 --headless、末尾加 `-- --shots=<目录>`：一路从船的追尾相机拍几张（云带顶上、云带里、云底下、海面上、风暴里、升回云带顶上）。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/sea_descent_e2e.tscn

const SCENE := preload("res://world_2051.tscn")
const TIMEOUT := 1800.0  # 模拟秒
const SETTLE_TIMEOUT := 150.0
const MOVES := ["move_forward", "move_back", "move_left", "move_right", "ascend", "descend", "sprint", "jump"]
const DAY := 3
const HOUR := 10.0
const CRUISE_Y := 420.0
const TOP_Y := -5000.0  # 云带顶、云底、海面：同 src/planet_frame.h 的 VerticalDatum（浮高计划 F4 起整体下移 1200 m）
const BASE_Y := -6200.0
const SEA_Y := -6800.0
const DECK_Y := TOP_Y + 200.0  # 云带顶上 200 m：降下去之前、升回来之后在这里比读数
const MID_Y := 0.5 * (TOP_Y + BASE_Y)  # 云带当中
const UNDER_Y := BASE_Y - 300.0  # 云底下 300 m
const BAND_MARKS := [TOP_Y - 200.0, MID_Y, BASE_Y + 100.0]  # 顶下 200 m、当中、云底上 100 m
# 相邻两帧最多变多少（船升降二三十米每秒，一帧半米；云带顶、云底的软边各几十米）
const MAX_STEP := {"sun_energy": 0.03, "ambient_energy": 0.03, "fog_density": 0.003, "fog_r": 0.02, "fog_g": 0.02, "fog_b": 0.02, "fog_sky_affect": 0.03,
	"band_inside": 0.03, "band_under": 0.03, "band_below": 0.05, "band_light": 0.02}

enum Phase { SETTLE, WALK_TO_SHIP, TAKE_HELM, CLIMB_OUT, GEAR_UP, FLY_OUT, DESCEND, HOVER, STORM, CALM, ASCEND, DONE }

var main: Expedition
var player: Player
var ship: Ship
var planet: PlanetView
var atmosphere: Atmosphere
var globe: PlanetGlobe
var world: GroupWorld
var dev = null

var phase := Phase.SETTLE
var elapsed := 0.0
var phase_started := 0.0
var shots_dir := ""
var pending_shot := ""
var shot_wait := 0
var held := {}
var waypoints: Array[Vector3] = []
var climb_from := 0.0
var step := 0
var sea_point := Vector3.ZERO

# 读数
var rows: Array = []  # 每帧一个 Dictionary
var worst := {}
var worst_at := {}
var before := {}  # 降下去之前（云带顶上 200 m 附近）的读数
var seen := {}  # 各层查过了没有
var hover_min_y := 1e9
var storm_flash := 0.0
var storm_checked := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	GameClock.restart(HOUR)
	GameClock.set_date(DAY, HOUR)
	GameClock.paused = true
	main = SCENE.instantiate()
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	planet = main.get_node("PlanetView")
	atmosphere = main.get_node("Atmosphere")
	globe = main.get_node("PlanetGlobe")
	world = main.get_node("World")
	dev = get_node_or_null("/root/DeveloperTools")
	atmosphere.override_fade = 0.05
	atmosphere.set_weather_override("clear", 0.0)
	for key in MAX_STEP.keys():
		worst[key] = 0.0


func _finish(ok: bool, why: String) -> void:
	_release_all()
	_write_csv()
	print("sea_descent: %s —— %s（阶段 %s，模拟 %.1f s）" % ["PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed])
	get_tree().quit(0 if ok else 1)


func _log(line: String) -> void:
	print("sea_descent: " + line)


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


func _goto(next: Phase) -> void:
	_log("%-12s 用时 %6.1f s（船 y = %.0f）" % [Phase.keys()[phase], elapsed - phase_started, ship.global_position.y])
	phase = next
	phase_started = elapsed
	step = 0


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	pending_shot = shot_name
	shot_wait = 3


func _process(_delta: float) -> void:
	if pending_shot.is_empty() or shots_dir.is_empty():
		return
	shot_wait -= 1
	if shot_wait > 0:
		return
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(pending_shot + ".png"))
	_log("截图 %s（船 y = %.0f）" % [pending_shot, ship.global_position.y])
	pending_shot = ""


# ---------------------------------------------------------------- 小工具

func _standing_on_ground() -> bool:
	var at := player.global_position
	var ground: float = world.get_surface_height(at.x, at.z)
	return player.is_on_floor() and not is_nan(ground) and absf(at.y - ground) < 1.0 and world.is_ready_at(at)


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


func _fly_toward(target: Vector3, want_y: float, arrive: float) -> bool:
	var to := target - ship.global_position
	var local := ship.global_transform.basis.inverse() * to
	var bearing := rad_to_deg(atan2(local.x, -local.z))
	var flat := Vector2(to.x, to.z).length()
	var climb := want_y - ship.global_position.y
	_hold("move_right", bearing > 3.0)
	_hold("move_left", bearing < -3.0)
	_hold("ascend", climb > 2.0)
	_hold("descend", climb < -2.0)
	_hold("move_forward", flat > 30.0 and absf(bearing) < 60.0 and climb < 60.0)
	return flat < arrive


# 从 from 起朝 away 走，找一处 2.5 km 内没有地的开阔云海，再往外 3 km（同 planet_shots）
func _open_sea(from: Vector2, away: Vector2) -> Vector2:
	for k in 400:
		var p := from + away * (float(k) * 500.0)
		var clear := true
		for r in [0.0, 1000.0, 2500.0]:
			for a in 12:
				var q: Vector2 = p + Vector2(cos(TAU * a / 12.0), sin(TAU * a / 12.0)) * float(r)
				if not is_nan(world.get_surface_height(q.x, q.y)):
					clear = false
		if clear:
			return p + away * 3000.0
	return from + away * 200000.0


func _peak() -> Vector2:
	var peak := Vector2.ZERO
	var peak_h := -1e9
	for i in range(-60, 61):
		for j in range(-60, 61):
			var p := Vector2(j, i) * 1000.0
			var h := world.get_surface_height(p.x, p.y)
			if not is_nan(h) and h > peak_h:
				peak_h = h
				peak = p
	return peak


# 这一帧的读数
func _reading() -> Dictionary:
	var look: Dictionary = atmosphere.get_look()
	var cam := get_viewport().get_camera_3d()
	var r := {"t": elapsed, "phase": Phase.keys()[phase], "ship_y": ship.global_position.y, "cam_y": cam.global_position.y if cam != null else NAN,
		"turbulence": ship.get_turbulence()}
	for key in ["sun_energy", "moon_energy", "ambient_energy", "fog_density", "fog_r", "fog_g", "fog_b", "fog_sky_affect", "band_inside", "band_under", "band_below",
			"band_depth", "band_sun", "band_diffuse", "band_light", "sea_height", "precip", "storm", "flash", "rain_ratio"]:
		r[key] = float(look.get(key, NAN))
	return r


func _record() -> void:
	var r := _reading()
	# 查跳变只在升降的时候（悬停时覆盖天气、打闪是天气变了，不是升降带来的）
	if not rows.is_empty() and rows.back()["phase"] == r["phase"] and (phase == Phase.DESCEND or phase == Phase.ASCEND):
		var prev: Dictionary = rows.back()
		for key in MAX_STEP.keys():
			var d := absf(float(r[key]) - float(prev[key]))
			if d > float(worst[key]):
				worst[key] = d
				worst_at[key] = "y %.0f（%s）" % [float(r["cam_y"]), r["phase"]]
	rows.append(r)


func _write_csv() -> void:
	if rows.is_empty():
		return
	var keys := ["t", "phase", "ship_y", "cam_y", "sun_energy", "moon_energy", "ambient_energy", "fog_density", "fog_r", "fog_g", "fog_b", "fog_sky_affect", "band_inside",
		"band_under", "band_below", "band_depth", "band_sun", "band_diffuse", "band_light", "sea_height", "turbulence", "precip", "storm", "flash", "rain_ratio"]
	var lines: Array[String] = [",".join(keys)]
	for r in rows:
		var cells: Array[String] = []
		for k in keys:
			cells.append(str(r[k]))
		lines.append(",".join(cells))
	var path := shots_dir.path_join("sea_descent.csv") if not shots_dir.is_empty() else "user://sea_descent.csv"
	var fa := FileAccess.open(path, FileAccess.WRITE)
	if fa != null:
		fa.store_string("\n".join(lines) + "\n")
		fa.close()
		_log("读数 %d 帧存到 %s" % [rows.size(), ProjectSettings.globalize_path(path)])


# 数字按有效数字印（GDScript 的 % 没有 %g）
func _num(v: float) -> String:
	if v == 0.0:
		return "0"
	var digits := clampi(3 - int(floor(log(absf(v)) / log(10.0))), 0, 9)
	return String.num(v, digits)


func _check_steps() -> bool:
	for key in MAX_STEP.keys():
		if float(worst[key]) > float(MAX_STEP[key]):
			_finish(false, "读数跳变：%s 相邻两帧差 %.5f（上限 %.5f，在 %s）" % [key, float(worst[key]), float(MAX_STEP[key]), worst_at.get(key, "?")])
			return false
	return true


# ---------------------------------------------------------------- 流程

func _physics_process(delta: float) -> void:
	elapsed += delta
	var in_phase := elapsed - phase_started
	_tick_taps()
	if elapsed > TIMEOUT:
		_finish(false, "超时（卡在阶段 %s，船 y = %.0f）" % [Phase.keys()[phase], ship.global_position.y])
		return
	match phase:
		Phase.SETTLE:
			# 大气表、白礁的疏密在后台做（墙钟一两秒）；按墙钟等（无头下模拟时间比墙钟快得多）
			var gs: Dictionary = globe.get_state()
			if not world.is_loaded() or not planet.is_active() or not bool(gs.get("luts_ready", false)) or not bool(gs.get("reefs_ready", false)) or not _standing_on_ground():
				if Time.get_ticks_msec() / 1000.0 > SETTLE_TIMEOUT:
					_finish(false, "墙钟 %.0f s 还没就绪：PlanetView %s、大气表 %s、白礁 %s、站稳 %s" % [SETTLE_TIMEOUT, planet.is_active(), gs.get("luts_ready", false),
						gs.get("reefs_ready", false), _standing_on_ground()])
				return
			if not is_equal_approx(ship.get_floor_y(), SEA_Y + 15.0):
				_finish(false, "有行星数据的地图，船的最低高度该是海面之上 15 m（%.0f），实为 %.1f" % [SEA_Y + 15.0, ship.get_floor_y()])
				return
			var peak := _peak()
			var berth: Transform3D = main.get_origin_berth()
			var dock := Vector2(berth.origin.x, berth.origin.z)
			var sea := _open_sea(dock, (dock - peak).normalized())
			sea_point = Vector3(sea.x, 0.0, sea.y)
			_log("白礁烘好 %.0f ms；开阔云海 %s（离主码头 %.1f km）" % [float(gs["reef_ms"]), sea, (sea - dock).length() / 1000.0])
			waypoints = [berth.origin + berth.basis.x * 0.8, ship.global_position]
			_goto(Phase.WALK_TO_SHIP)
		Phase.WALK_TO_SHIP:
			if _follow_waypoints():
				_goto(Phase.TAKE_HELM)
			elif in_phase > 60.0:
				_finish(false, "60 s 没走到船上")
		Phase.TAKE_HELM:
			if main.get_state() == 1:
				climb_from = ship.global_position.y
				_goto(Phase.CLIMB_OUT)
			elif _walk_toward(ship.get_helm_position(), 1.9):
				_tap("interact")
			elif in_phase > 30.0:
				_finish(false, "30 s 没上舵（%s）" % main.get_prompt())
		Phase.CLIMB_OUT:
			_hold("ascend", true)
			if ship.global_position.y > climb_from + 8.0:
				_hold("ascend", false)
				_goto(Phase.GEAR_UP)
		Phase.GEAR_UP:
			if ship.get_gear() >= 2:
				_goto(Phase.FLY_OUT)
			elif not held.has("gear_up"):
				_tap("gear_up")
		Phase.FLY_OUT:
			if _fly_toward(sea_point, CRUISE_Y, 150.0):
				_release_all()
				if ship.get_speed() < 3.0:
					var ground: float = world.get_surface_height(ship.global_position.x, ship.global_position.z)
					if not is_nan(ground):
						_finish(false, "开阔云海底下有地（%.0f m）" % ground)
						return
					_log("到了开阔云海上空：船 y = %.0f，下面没有地；开始往下降" % ship.global_position.y)
					_goto(Phase.DESCEND)
			elif in_phase > 400.0:
				_finish(false, "400 s 没飞到开阔云海上空")
		Phase.DESCEND:
			_descend()
		Phase.HOVER:
			_hover(in_phase)
		Phase.STORM:
			_storm(in_phase)
		Phase.CALM:
			_record()
			if in_phase > 4.0:
				_goto(Phase.ASCEND)
		Phase.ASCEND:
			_ascend()
		Phase.DONE:
			if pending_shot.is_empty() and in_phase > 0.2:  # 等最后一张截图存好
				_finish(true, "降到海面、悬停、风暴、升回云带顶以上都对，升降一路读数连续")


## 一路降到海面：每一帧录读数，经过各层时查一回
func _descend() -> void:
	_hold("descend", true)
	_record()
	var y := ship.global_position.y
	var cam_y: float = rows.back()["cam_y"]
	var look: Dictionary = rows.back()
	var status := main.get_status_text()
	if not seen.has("before") and cam_y < DECK_Y:
		seen["before"] = true
		before = look.duplicate()
		_log("云带顶上（相机 %.0f）：日光 %.3f、环境光 %.3f、雾 %.7f、天空蒙雾 %.2f、颠 %.2f；HUD「%s」" % [cam_y, float(look["sun_energy"]), float(look["ambient_energy"]),
			float(look["fog_density"]), float(look["fog_sky_affect"]), float(look["turbulence"]), status.get_slice("\n", 0)])
		if float(look["band_inside"]) > 0.0 or float(look["band_under"]) > 0.0 or float(look["turbulence"]) > 0.0:
			_finish(false, "云带顶上 200 m 不该有云带的雾、不该颠：%s" % [look])
			return
		_shot("descent_deck_top")
	if not seen.has("band") and cam_y < MID_Y:
		seen["band"] = true
		_log("云带里（相机 %.0f）：日光 %.3f、环境光 %.3f、雾 %.4f（能见度 %.0f m）、天空蒙雾 %.2f、雾色 %.2f、颠 %.2f；HUD「%s」；提示「%s」" % [cam_y,
			float(look["sun_energy"]), float(look["ambient_energy"]), float(look["fog_density"]), 3.0 / maxf(1e-9, float(look["fog_density"])),
			float(look["fog_sky_affect"]), float(look["fog_r"]), float(look["turbulence"]), status.get_slice("\n", 0), main.get_prompt().replace("\n", " / ")])
		if float(look["band_inside"]) < 0.999 or float(look["fog_density"]) < 0.05 or float(look["fog_sky_affect"]) < 0.99:
			_finish(false, "云带里该是浓雾（能见度几十米）、天被雾盖满：%s" % [look])
			return
		# 直射只剩一点；日光 + 环境光合起来比云带顶上暗（环境光本身会亮一点：直射进了云都成了散射光）
		var light_now := float(look["sun_energy"]) + float(look["ambient_energy"])
		var light_top := float(before["sun_energy"]) + float(before["ambient_energy"])
		if float(look["sun_energy"]) > 0.2 * float(before["sun_energy"]) or light_now > 0.6 * light_top:
			_finish(false, "云带里该越深越暗：日光 %.3f（顶上 %.3f）、日光 + 环境光 %.3f（顶上 %.3f）" % [float(look["sun_energy"]), float(before["sun_energy"]),
				light_now, light_top])
			return
		if ship.get_turbulence() < 0.8:
			_finish(false, "云带里船该颠：颠簸 %.2f" % ship.get_turbulence())
			return
		if status.find("云带里") < 0:
			_finish(false, "云带里 HUD 该报「云带里」：%s" % status)
			return
		if main.get_prompt().find("海上无处落脚") < 0:
			_finish(false, "降到云海以下提示该说「海上无处落脚」：%s" % main.get_prompt())
			return
		_shot("descent_in_band")
	# 云带里越深越暗：顶下 200 m、当中、云底上 100 m 三处的雾色（透进来的光）一处比一处暗
	for mark in BAND_MARKS:
		if not seen.has(mark) and cam_y < mark:
			seen[mark] = float(look["band_light"])
			if mark == BAND_MARKS[2]:
				_log("云带里雾色的亮（透进来的光）：顶下 200 m %.3f → 当中 %.3f → 云底上 100 m %.3f" % [float(seen[BAND_MARKS[0]]), float(seen[BAND_MARKS[1]]), float(seen[BAND_MARKS[2]])])
				if not (float(seen[BAND_MARKS[0]]) > float(seen[BAND_MARKS[1]]) and float(seen[BAND_MARKS[1]]) > float(seen[BAND_MARKS[2]])):
					_finish(false, "云带里该越深越暗：%.3f / %.3f / %.3f" % [float(seen[BAND_MARKS[0]]), float(seen[BAND_MARKS[1]]), float(seen[BAND_MARKS[2]])])
					return
	if not seen.has("under") and cam_y < UNDER_Y:
		seen["under"] = true
		var w: Dictionary = atmosphere.get_weather()
		_log("云底下（相机 %.0f）：日光 %.4f、环境光 %.3f、雾 %.6f、雾色 %.3f %.3f %.3f、颠 %.2f、层 %d；HUD「%s」" % [cam_y, float(look["sun_energy"]),
			float(look["ambient_energy"]), float(look["fog_density"]), float(look["fog_r"]), float(look["fog_g"]), float(look["fog_b"]), float(look["turbulence"]),
			int(w["altitude_band"]), status.get_slice("\n", 0)])
		if float(look["band_under"]) < 0.999 or float(look["band_inside"]) > 0.001 or float(look["sun_energy"]) > 0.02:
			_finish(false, "云底下该出了云、日面看不见：%s" % [look])
			return
		if float(look["fog_density"]) > 0.001 or float(look["ambient_energy"]) > 0.8 * float(before["ambient_energy"]):
			_finish(false, "云底下该是霾（不是浓雾）、阴天式的暗：雾 %.6f、环境光 %.3f（顶上 %.3f）" % [float(look["fog_density"]), float(look["ambient_energy"]),
				float(before["ambient_energy"])])
			return
		if int(w["altitude_band"]) != 4 or status.find("云底下") < 0:
			_finish(false, "云底下 HUD 该报「云底下」、层 4：%s（层 %d）" % [status, int(w["altitude_band"])])
			return
		_shot("descent_under_base")
	if y <= ship.get_floor_y() + 0.5 and ship.get_velocity().y > -0.5:
		_log("降到海面上空：船 y = %.2f（最低 %.0f），用时 %.0f s" % [y, ship.get_floor_y(), elapsed - phase_started])
		hover_min_y = y
		_goto(Phase.HOVER)


## 海面上空：最低高度挡得住、HUD 与 F6 报离海面、气温同 y = 0、离不了舵
func _hover(in_phase: float) -> void:
	_record()
	var y := ship.global_position.y
	hover_min_y = minf(hover_min_y, y)
	if in_phase < 5.0:
		_hold("descend", true)  # 一直按着下降：最低高度挡着
		if in_phase > 1.0 and in_phase < 1.1 and dev != null:
			dev.set_weather_open(true)
		return
	if step == 0:
		_hold("descend", false)
		var floor_y := ship.get_floor_y()
		if hover_min_y < floor_y - 0.5 or absf(y - (SEA_Y + 15.0)) > 1.0:
			_finish(false, "最低高度没挡住：按着下降 5 s，船最低 %.2f（海面之上 15 m = %.0f）" % [hover_min_y, SEA_Y + 15.0])
			return
		var status := main.get_status_text()
		var w: Dictionary = atmosphere.get_weather()
		var f6: String = dev.get_weather_text() if dev != null else ""
		if dev != null:
			dev.set_weather_open(false)
		var sea_h := float(w["sea_height"])
		_log("海面上空：船 y = %.2f（按着下降 5 s 最低 %.2f），相机离海面 %.1f m；HUD「%s」；F6「%s」" % [y, hover_min_y, sea_h,
			status.get_slice("\n", 0), f6.get_slice("\n", 2)])
		if status.find("离海面 15 m") < 0:
			_finish(false, "HUD 该报船离海面 15 m：%s" % status)
			return
		if f6.find("离海面") < 0 or f6.find("云底下") < 0:
			_finish(false, "F6 该报离海面多高（云底下）：%s" % f6)
			return
		# 气温：y < 0 一律取 y = 0 处的（D13）
		var at := ship.global_position
		var t_sea := float(atmosphere.weather_at(at)["temp_c"])
		var t_zero := float(atmosphere.weather_at(Vector3(at.x, 0.0, at.z))["temp_c"])
		var t_high := float(atmosphere.weather_at(Vector3(at.x, 1000.0, at.z))["temp_c"])
		_log("气温：海面上 %.2f °C、同一处 y = 0 %.2f °C、y = 1000 %.2f °C" % [t_sea, t_zero, t_high])
		if absf(t_sea - t_zero) > 1e-6 or t_high >= t_zero:
			_finish(false, "海面上的气温该同 y = 0 处的（D13）：%.3f / %.3f" % [t_sea, t_zero])
			return
		if main.get_prompt().find("海上无处落脚") < 0:
			_finish(false, "海面上空提示该说「海上无处落脚」：%s" % main.get_prompt())
			return
		var sea: Dictionary = globe.get_state()["sea"]
		if not bool(sea.get("shown", false)):
			_finish(false, "海面网格该画出来了：%s" % [sea])
			return
		_log("海面：浪高 %.2f m、波长 %.0f m（风 %.1f m/s）" % [float(sea["wave_height"]), float(sea["wave_length"]), float(sea["wind_ms"])])
		_shot("descent_sea")
		step = 1
		return
	if step == 1 and in_phase > 5.5:
		_tap("interact")
		step = 2
		return
	if step == 2 and in_phase > 6.2:
		if main.get_state() != 1:
			_finish(false, "海上按 E 不该离舵（状态 %d）" % main.get_state())
			return
		if main.get_notice().find("海上无处落脚") < 0:
			_finish(false, "海上按 E 该说「海上无处落脚」：%s" % main.get_notice())
			return
		_log("海上按 E：还在掌舵，说「%s」" % main.get_notice())
		atmosphere.set_weather_override("storm", 0.0)
		storm_flash = 0.0
		_goto(Phase.STORM)


## 风暴里的海面：浪大、暴雨、霾浓、颠、打闪
func _storm(in_phase: float) -> void:
	_record()
	storm_flash = maxf(storm_flash, float(rows.back()["flash"]))
	if in_phase > 7.0 and not seen.has("storm_shot"):
		seen["storm_shot"] = true
		_shot("descent_sea_storm")  # 换回晴之前拍
	if in_phase < 8.0:
		return
	var sea: Dictionary = globe.get_state()["sea"]
	var look: Dictionary = rows.back()
	_log("风暴里：浪高 %.2f m、波长 %.0f m、白浪 %.2f、碎浪 %.2f；雨丝 %.2f、雾 %.5f、颠 %.2f、打闪最亮 %.2f；HUD「%s」" % [float(sea["wave_height"]), float(sea["wave_length"]),
		float(sea["foam"]), float(sea["chop"]), float(look["rain_ratio"]), float(look["fog_density"]), float(look["turbulence"]), storm_flash, main.get_status_text().get_slice("\n", 0)])
	if float(sea["wave_height"]) < 6.0 or float(sea["chop"]) < 0.5:
		_finish(false, "风暴里浪该大：%s" % [sea])
		return
	if float(look["rain_ratio"]) < 0.3 or float(look["fog_density"]) < 0.001:
		_finish(false, "风暴里该下暴雨、霾浓：雨丝 %.2f、雾 %.5f" % [float(look["rain_ratio"]), float(look["fog_density"])])
		return
	if float(look["turbulence"]) < 0.5:
		_finish(false, "风暴里船该颠得厉害：%.2f" % float(look["turbulence"]))
		return
	if storm_flash < 0.5:
		_finish(false, "风暴里 8 s 该打过闪：最亮 %.2f" % storm_flash)
		return
	atmosphere.set_weather_override("clear", 0.0)
	_goto(Phase.CALM)


## 升回云带顶以上：读数恢复
func _ascend() -> void:
	_hold("ascend", true)
	_record()
	var cam_y: float = rows.back()["cam_y"]
	if not seen.has("band_up") and cam_y > MID_Y:
		seen["band_up"] = true
		_shot("ascent_in_band")
	if cam_y > DECK_Y:
		_hold("ascend", false)
		var look: Dictionary = rows.back()
		var diffs := []
		for key in ["sun_energy", "ambient_energy", "fog_density", "fog_sky_affect", "fog_r", "band_inside", "band_under", "band_below", "turbulence"]:
			diffs.append("%s %s→%s" % [key, _num(float(before[key])), _num(float(look[key]))])
		_log("升回云带顶上（相机 %.0f）：%s" % [cam_y, ", ".join(diffs)])
		for key in ["sun_energy", "ambient_energy", "fog_sky_affect", "fog_r", "fog_g", "fog_b"]:
			if absf(float(look[key]) - float(before[key])) > 0.02:
				_finish(false, "升回云带顶以上 %s 没恢复：%.4f（降下去之前 %.4f）" % [key, float(look[key]), float(before[key])])
				return
		if absf(float(look["fog_density"]) - float(before["fog_density"])) > 1e-6 or float(look["band_inside"]) > 0.0 or float(look["band_under"]) > 0.0 or float(look["turbulence"]) > 0.0:
			_finish(false, "升回云带顶以上雾、云带、颠簸该恢复：%s" % [look])
			return
		_shot("ascent_deck_top")
		var parts := []
		for key in MAX_STEP.keys():
			parts.append("%s %s（%s）" % [key, _num(float(worst[key])), worst_at.get(key, "")])
		_log("一路 %d 帧，相邻两帧最多变：%s" % [rows.size(), "；".join(parts)])
		if not _check_steps():
			return
		_goto(Phase.DONE)
