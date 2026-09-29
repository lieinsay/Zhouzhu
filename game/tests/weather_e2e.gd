extends Node
## 时间与天气端到端测试（世界 seed42，开局那群 #6329）：
##   开局（开局配置的那一天：一年里的第 85 天，暖季 · 四月初二）上午：天与光读得到这一群的气候（纬度就是 world.json 的群心纬度，北纬 34.23°），
##   太阳高，HUD 第一行带季节与此地天气
##   → F6 开「时间与天气」面板：有读数；→ 键拨一个钟头；3 换小雨、0 换回自动；F6 合上
##   → 拨到夜里：太阳在地平线下、日光熄了 → 往回拨两个钟头：钟点退了、日子没变、钟的秒只往前
##   → 拨到冬天（第 34 天，冷季 · 二月初七：北半球的冬末，一月那场雪在码头已经化了）：雪线下来了，主峰积雪、码头没雪；
##     垦一块地播粟，快进一个钟头一点不长（天冷）
##   → 拨到第二天午后：邑治有人在外面 → 覆盖成风暴：渐入、此地是风暴、下雨、HUD 说风暴，在外面的当场都往家走（还没到收工）→ 恢复自动：渐出、又有人出门干活
##   → 气温偏 −10：雪线下到台面以下 → 覆盖成大雪：新雪积起来、此地下的是雪 → 换到平板岛：覆盖跟过去。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/weather_e2e.tscn
##
## 驱动器自己不是「当前场景」：它把 world_seed42.tscn 挂成当前场景，换地图时被换掉的是那个，驱动器留着。

const SCENE := preload("res://world_seed42.tscn")
const TIMEOUT := 400.0
const SEAT := 1  # 邑治，主码头旁 1.2 km（午后那几段挪到它的村口去看人）
# 开局那群 #6329 的金标（换开局要重记）：开局那天的日子与季名；冬天挑的那一天（冷季、晴、码头 0.6 °C、没积雪，主峰还积着雪）
const START_DATE := "四月初二"
const START_SEASON := "暖季"
const WINTER_DAY := 34
const WINTER_SEASON := "冷季"
const YEAR_DAYS := 336
const FARM_SEARCH_M := 1500.0  # 冬天播粟：离人多远以内找能垦的地

enum Phase { BOOT, PANEL, PANEL_KEYS, NIGHT, BACK, WINTER, FARM, NOON, STORM, STORM_HOME, CALM, COLD, SNOW, SWITCH, DONE }

var dev: DevTools
var main: Expedition
var world: World
var player: Player
var atmosphere: Atmosphere
var phase := Phase.BOOT
var elapsed := 0.0
var phase_started := 0.0
var sent := false
var mark := {}
var cell := Vector2i.ZERO
var farm: Farm


func _ready() -> void:
	get_tree().root.size = Vector2i(1280, 720)
	dev = get_node("/root/DeveloperTools")
	GameClock.restart(10.0)
	main = SCENE.instantiate()
	get_tree().root.add_child.call_deferred(main)
	(func(): get_tree().current_scene = main).call_deferred()


func _goto(next: Phase) -> void:
	phase = next
	phase_started = elapsed
	sent = false
	mark = {}
	print("weather_e2e: → %s（%s）" % [Phase.keys()[next], GameClock.get_clock_text()])


func _once() -> bool:
	if sent:
		return false
	sent = true
	return true


func _key(code: Key) -> void:
	for pressed in [true, false]:
		var ev := InputEventKey.new()
		ev.physical_keycode = code
		ev.keycode = code
		ev.pressed = pressed
		Input.parse_input_event(ev)


func _finish(ok: bool, why: String) -> void:
	GameClock.fast_forward = false
	print("weather_e2e: %s —— %s（阶段 %s，模拟 %.1f s，钟 %s）" % ["PASS" if ok else "FAIL", why, Phase.keys()[phase], elapsed, GameClock.get_clock_text()])
	get_tree().quit(0 if ok else 1)


func _expect(ok: bool, why: String) -> bool:
	if not ok:
		_finish(false, why)
	return ok


func _outdoors() -> int:
	var sites: SiteStream = main.get_node_or_null("SiteStream")
	var v: WorldVillage = sites.get_village(SEAT) if sites else null
	var folk: Villagers = v.get_villagers() if v else null
	if folk == null:
		return -1
	var n := 0
	for i in folk.get_villager_count():
		n += 0 if folk.get_villager_phase(i) == Villagers.INDOORS else 1
	return n


# 在外面、手上不是「回家」的人
func _outdoors_not_going_home() -> int:
	var sites: SiteStream = main.get_node_or_null("SiteStream")
	var v: WorldVillage = sites.get_village(SEAT) if sites else null
	var folk: Villagers = v.get_villagers() if v else null
	if folk == null:
		return -1
	var n := 0
	for i in folk.get_villager_count():
		if folk.get_villager_phase(i) != Villagers.INDOORS and folk.get_villager_task(i) != "go_home":
			n += 1
	return n


## 邑治的村口：从村心往主码头那边 450 m
func _seat_gate() -> Vector3:
	var seat := Vector3.ZERO
	for v in world.get_info()["villages"]:
		if int(v["id"]) == SEAT:
			seat = Vector3(v["pos"][0], 0, v["pos"][1])
	var dock := world.get_dock_position(world.find_dock(main.origin_dock))
	var out := Vector3(dock.x - seat.x, 0, dock.z - seat.z).normalized()
	return seat + out * 450.0


func _peak() -> Vector3:
	var at := Vector2(player.global_position.x, player.global_position.z)
	var best := Vector3(at.x, -1e9, at.y)
	for i in range(-30, 31):
		for j in range(-30, 31):
			var p := at + Vector2(i, j) * 1000.0
			var h := world.get_surface_height(p.x, p.y)
			if not is_nan(h) and h > best.y:
				best = Vector3(p.x, h, p.y)
	return best


func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时")
		return
	var in_phase := elapsed - phase_started
	match phase:
		Phase.BOOT:
			world = main.get_node_or_null("World")
			player = main.get_node_or_null("Player")
			atmosphere = main.get_node_or_null("Atmosphere")
			if world == null or not world.is_loaded() or not world.is_ready_at(player.global_position) or in_phase < 1.0:
				return
			var w: Dictionary = atmosphere.get_weather()
			print("weather_e2e: 开局 %s %s · %s · 太阳高 %.0f° · 日出 %.2f 日落 %.2f · 雪线 %.0f" % [w["season_name"], w["date"], w["kind_name"], w["sun_altitude"], w["sunrise"], w["sunset"], w["snow_line"]])
			var group_lat := float(world.get_info()["source"]["lat"])
			if not _expect(bool(w["has_data"]) and absf(float(w["latitude"]) - group_lat) < 0.1, "天与光没读到开局那群的气候（群心纬度 %.2f°）：%s" % [group_lat, w]):
				return
			if not _expect(int(GameClock.get_date()["day_of_year"]) == StoryStart.get_calendar_start(), "开局不是开局配置的那一天（第 %d 天）：%s" % [StoryStart.get_calendar_start(), GameClock.get_date()]):
				return
			if not _expect(String(w["date"]) == START_DATE and String(w["season_name"]) == START_SEASON, "开局不是 %s · %s：%s %s" % [START_SEASON, START_DATE, w["season_name"], w["date"]]):
				return
			if not _expect(float(w["sun_altitude"]) > 30.0 and not atmosphere.is_night(), "上午十点太阳不高：%.1f°" % float(w["sun_altitude"])):
				return
			var status: String = main.get_status_text()
			if not _expect(status.contains(GameClock.get_clock_text()) and status.contains(START_SEASON), "HUD 第一行没有钟点或季节天气：%s" % status):
				return
			_goto(Phase.PANEL)
		Phase.PANEL:
			if _once():
				_key(KEY_F6)
				return
			if in_phase < 0.2:
				return
			var text := dev.get_weather_text()
			if not _expect(dev.is_weather_open() and text.contains("此地") and text.contains("日出") and text.contains("雪线"), "F6 面板没开或没有读数：%s" % text):
				return
			mark["hour"] = GameClock.get_hour()
			_key(KEY_RIGHT)
			_goto(Phase.PANEL_KEYS)
		Phase.PANEL_KEYS:
			if in_phase < 0.2:
				return
			if not mark.has("step"):
				if not _expect(absf(GameClock.get_hour() - 11.0) < 0.05, "→ 没拨一个钟头：%.2f" % GameClock.get_hour()):
					return
				mark["step"] = 1
				_key(KEY_3)
				return
			if mark["step"] == 1:
				if not _expect(dev.get_weather() == "rain", "按 3 没换成小雨：%s" % dev.get_weather()):
					return
				mark["step"] = 2
				_key(KEY_0)
				return
			if mark["step"] == 2:
				if not _expect(dev.get_weather() == "", "按 0 没换回自动：%s" % dev.get_weather()):
					return
				mark["step"] = 3
				_key(KEY_F6)
				return
			if not _expect(not dev.is_weather_open(), "F6 没合上面板"):
				return
			_goto(Phase.NIGHT)
		Phase.NIGHT:
			if _once():
				dev.step_hours(12.0)  # 11 → 23 点
				return
			if in_phase < 3.0:
				return
			var look: Dictionary = atmosphere.get_look()
			if not _expect(atmosphere.is_night() and atmosphere.get_sun_altitude() < -6.0, "23 点太阳还在天上：%.1f°" % atmosphere.get_sun_altitude()):
				return
			if not _expect(float(look["sun_energy"]) < 0.02, "夜里日光没熄：%.3f" % float(look["sun_energy"])):
				return
			print("weather_e2e: 夜里 %s · 太阳 %.0f° · 月光 %.3f" % [GameClock.get_clock_text(), atmosphere.get_sun_altitude(), float(look["moon_energy"])])
			_goto(Phase.BACK)
		Phase.BACK:
			if _once():
				mark["date"] = GameClock.get_date()["text"]
				mark["seconds"] = GameClock.get_seconds()
				dev.step_hours(-2.0)
				return
			if in_phase < 0.2:
				return
			if not _expect(absf(GameClock.get_hour() - 21.0) < 0.05 and GameClock.get_date()["text"] == mark["date"], "往回拨两个钟头：%s（原来 %s）" % [GameClock.get_clock_text(), mark["date"]]):
				return
			if not _expect(GameClock.get_seconds() > float(mark["seconds"]), "往回拨时钟的秒倒流了"):
				return
			_goto(Phase.WINTER)
		Phase.WINTER:
			if _once():
				dev.shift_days((WINTER_DAY - StoryStart.get_calendar_start() + YEAR_DAYS) % YEAR_DAYS)  # 开局那天 → 下一个冬天的第 WINTER_DAY 天
				return
			if in_phase < 0.5:
				return
			var w: Dictionary = atmosphere.get_weather()
			var peak := _peak()
			var top: Dictionary = world.get_weather_at(peak.x, peak.z, peak.y)
			var here: Dictionary = world.get_weather_at(player.global_position.x, player.global_position.z)
			print("weather_e2e: 冬天 %s %s · 雪线 %.0f m · 主峰 %.0f m 积雪 %.0f mm · 码头积雪 %.1f mm · 此地 %.1f °C" % [w["season_name"], w["date"], world.get_snow_line(), peak.y, top["snow_mm"], here["snow_mm"], here["temp_c"]])
			if not _expect(String(w["season_name"]) == WINTER_SEASON and int(GameClock.get_date()["day_of_year"]) == WINTER_DAY, "没拨到%s的第 %d 天：%s" % [WINTER_SEASON, WINTER_DAY, GameClock.get_date()]):
				return
			if not _expect(world.get_snow_line() < 1500.0 and float(top["snow_mm"]) > 20.0 and float(here["snow_mm"]) < 1.0, "冬天雪线不对：雪线 %.0f、主峰 %.0f mm、码头 %.1f mm" % [world.get_snow_line(), top["snow_mm"], here["snow_mm"]]):
				return
			_goto(Phase.FARM)
		Phase.FARM:
			if _once():
				farm = world.get_node("Farm")
				# 开局那群 #6329 的主码头在岸边的平原上，120 m（Farm 缺省的找地范围）内没有离水近的地也没有熟地：放宽到 1.5 km 找
				farm.set_search_radius(FARM_SEARCH_M)
				cell = farm.find_nearest_arable(player.global_position)
				if not _expect(cell != Vector2i(-1, -1), "主码头 %.1f km 内没有能垦的地" % (FARM_SEARCH_M / 1000.0)):
					return
				farm.perform(cell, Farm.ACTION_TILL, "")
				farm.perform(cell, Farm.ACTION_SOW, "millet")
				mark["growth"] = farm.get_growth(cell)
				mark["factor"] = farm.get_season_factor(cell)
				GameClock.fast_forward = true
				return
			if in_phase < 3.0:
				return
			GameClock.fast_forward = false
			var g: float = farm.get_growth(cell)
			print("weather_e2e: 冬天播粟：时令倍数 %.2f，长势 %.3f → %.3f" % [mark["factor"], mark["growth"], g])
			if not _expect(farm.get_plot(cell) == Farm.PLOT_GROWING and float(mark["factor"]) < 0.05 and g == float(mark["growth"]), "冬天台面的地还在长：倍数 %.2f、长势 %.3f → %.3f" % [mark["factor"], mark["growth"], g]):
				return
			_goto(Phase.NOON)
		Phase.NOON:
			if _once():
				# 邑治离主码头 1.2 km（开局那群 #6329），村民只在人 900 m 内才铺出来：先挪到村口（从村心往主码头那边 450 m）
				main.teleport_to(_seat_gate())
				dev.step_hours(24.0 - GameClock.get_hour() + 13.0)  # 第二天午后（歇晌完）
				return
			var out := _outdoors()
			if out >= 3:
				print("weather_e2e: 午后 %s · 邑治 %d 人在外面" % [GameClock.get_clock_text(), out])
				_goto(Phase.STORM)
			elif in_phase > 60.0:
				_finish(false, "午后一分钟里邑治没人出门（%d）" % out)
		Phase.STORM:
			if _once():
				dev.set_weather("storm")
				return
			if in_phase < 9.0:
				return
			var o: Dictionary = atmosphere.get_override()
			var w: Dictionary = atmosphere.get_weather()
			var look: Dictionary = atmosphere.get_look()
			print("weather_e2e: 风暴 渐入 %.2f · 此地 %s · 降水 %.2f（雨 %.2f 雪 %.2f）· 闪 %.2f" % [o["blend"], w["kind_name"], look["precip"], look["rain_ratio"], look["snow_ratio"], look["flash"]])
			if not _expect(float(o["blend"]) >= 0.999 and String(w["kind"]) == "storm" and float(look["precip"]) > 0.3 and float(look["rain_ratio"]) > 0.2, "覆盖成风暴没生效：%s / %s" % [o, look]):
				return
			if not _expect(main.get_status_text().contains("风暴"), "HUD 没说风暴：%s" % main.get_status_text()):
				return
			GameClock.fast_forward = true
			_goto(Phase.STORM_HOME)
		Phase.STORM_HOME:
			# 在外面的全都往家走（邑治大、打水的在河边，走到家要一阵）；还没到收工
			var going := _outdoors_not_going_home()
			if going == 0:
				GameClock.fast_forward = false
				print("weather_e2e: 风暴里在外面的 %d 人全都往家走（%s，%.1f s）" % [_outdoors(), GameClock.get_clock_text(), in_phase])
				if not _expect(GameClock.get_hour() < 15.0, "都往家走时已经快收工了，说明不了是避雨：%s" % GameClock.get_clock_text()):
					return
				dev.set_weather("")
				_goto(Phase.CALM)
			elif in_phase > 20.0:
				_finish(false, "风暴里还有 %d 人在外面干活（%s）" % [going, GameClock.get_clock_text()])
		Phase.CALM:
			if in_phase < 7.0:
				return
			if not mark.has("checked"):
				mark["checked"] = true
				if not _expect(float(atmosphere.get_override()["blend"]) <= 0.001, "恢复自动没渐出"):
					return
				GameClock.fast_forward = true
			var out := _outdoors_not_going_home()
			if out >= 1:
				GameClock.fast_forward = false
				print("weather_e2e: 风停了又有 %d 人出门干活（%s）" % [out, GameClock.get_clock_text()])
				_goto(Phase.COLD)
			elif in_phase > 40.0 or GameClock.get_hour() > 16.0:
				_finish(false, "风停了快进里没人出门（%s）" % GameClock.get_clock_text())
		Phase.SNOW:
			if _once():
				dev.set_weather("heavy_snow")
				return
			if in_phase < 16.0:
				return
			var o: Dictionary = atmosphere.get_override()
			var w: Dictionary = atmosphere.get_weather()
			var look: Dictionary = atmosphere.get_look()
			print("weather_e2e: 大雪 · 此地 %s %.1f °C · 雪 %.2f · 新雪 %.1f mm · 雪粒子 %.2f" % [w["kind_name"], w["temp_c"], w["snow_frac"], o["fresh_snow_mm"], look["snow_ratio"]])
			if not _expect(float(o["fresh_snow_mm"]) > 10.0 and float(w["snow_frac"]) > 0.9 and float(look["snow_ratio"]) > 0.2 and float(w["snow_mm"]) > 5.0, "覆盖成大雪没下雪 / 没积雪：%s / %s" % [o, w]):
				return
			dev.set_weather("rain")
			_goto(Phase.SWITCH)
		Phase.COLD:
			if _once():
				mark["line"] = world.get_snow_line()
				dev.set_weather("")
				dev.set_weather_temp(-10.0)
				return
			if in_phase < 7.0:
				return
			var line := world.get_snow_line()
			print("weather_e2e: 气温偏 −10 °C：雪线 %.0f → %.0f m" % [mark["line"], line])
			if not _expect(line < 300.0 and line < float(mark["line"]) - 300.0, "偏冷十度雪线没下来：%.0f → %.0f" % [mark["line"], line]):
				return
			dev.set_weather_temp(0.0)
			_goto(Phase.SNOW)
		Phase.SWITCH:
			if _once():
				dev.switch_to("res://flat_map.tscn")
				return
			if in_phase < 2.0:
				return
			var next: Atmosphere = get_tree().get_first_node_in_group("atmosphere")
			if not _expect(next != null and get_tree().current_scene.scene_file_path == "res://flat_map.tscn", "换到平板岛后找不到它的天与光"):
				return
			var o: Dictionary = next.get_override()
			var w: Dictionary = next.get_weather()
			print("weather_e2e: 平板岛 · 覆盖 %s（渐入 %.2f）· 此地 %s · 没有气候数据 %s" % [o["kind_name"], o["blend"], w["kind_name"], not bool(w["has_data"])])
			if not _expect(bool(o["active"]) and String(o["kind"]) == "rain" and not bool(w["has_data"]), "换了地图覆盖没跟过去：%s" % [o]):
				return
			dev.set_weather("")
			_goto(Phase.DONE)
			_finish(true, "季节、昼夜、拨钟拨日子、雪线、冬天不长、风暴歇工、大雪、偏冷、换地图都对")
