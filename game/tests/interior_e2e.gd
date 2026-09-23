extends Node
## 建筑能进去 + 开发者房屋模板的端到端测试（平板岛 flat_map.tscn）。只通过输入操作人（转视角、按住 W、按键）。
##   ① 民居·三间：从门外坡道脚下走进正门 → 穿过隔墙上的门洞进东间；每到一处查人站在地坪上、在这座的占地里，
##      第三人称相机也在屋里（顶到天花板、墙就停住，不穿出去）
##   ② 仓：高台基，顺台阶走上去、进门，站在仓里的地坪上
##   ③ 望楼：进门 → 面朝梯子按住 W 爬到二层 → 绕开梯口走到第二段梯子 → 爬到三层，站在 8 m 高的楼板上
##   ④ 布置模式 V 换款式：B 开布置模式、光标挪到民居上、V 换一套模板 → 清单里这座写上了款式、屋里换了样子
##   ⑤ 室内编辑器：T 在光标下那座上开编辑 → 挑家具、挪光标、E 放 → 两点画一道隔墙 → X 拆掉 → N 另存为新模板
##      → F5 存盘：文件写到测试目录、读回来与编辑器里的一样、这座改用了新模板 → Tab 合上
##
##   Godot --headless --path game --fixed-fps 60 res://tests/interior_e2e.tscn
##
## 退出码 0 = 通过。去掉 --headless、末尾加 `-- --shots=<目录>`，每一站存一张截图。

const TIMEOUT := 600.0  # 模拟秒
const STEP_TIMEOUT := 30.0
const LAYOUT := "user://interior_e2e_village.txt"
const TEMPLATES := "user://interior_e2e_templates"
const REACH := 0.3
const KEYS := ["interact", "remove", "build_mode", "rotate_left", "rotate_right", "save", "next_style", "prev_style", "edit_interior", "save_as", "inventory", "jump", "fly_down"]

var main: Expedition
var player: Player
var settlement: Settlement
var shots_dir := ""
var elapsed := 0.0
var step_started := 0.0
var steps: Array = []  # 每一步：[种类, 参数…]
var step := 0
var house := -1
var granary := -1
var tower := -1
var taps: Array = []
var tap_frames := 0
var tap_gap := 0
var climb_seen := false
var hops := 0
var stuck_frames := 0
var last_pos := Vector3.ZERO
var waiting := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots_dir = arg.trim_prefix("--shots=")
			DirAccess.make_dir_recursive_absolute(shots_dir)
	get_tree().root.size = Vector2i(1280, 720)
	_cleanup()
	BuildingTemplates.write_dir = TEMPLATES
	BuildingTemplates.reload()
	main = (load("res://flat_map.tscn") as PackedScene).instantiate()
	(main.get_node("Map/Settlement") as Settlement).layout_path = LAYOUT
	add_child(main)
	player = main.get_node("Player")
	settlement = main.get_node("Map/Settlement")
	steps = [["settle"]]


func _cleanup() -> void:
	if FileAccess.file_exists(LAYOUT):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(LAYOUT))
	var dir := ProjectSettings.globalize_path(TEMPLATES)
	for shell in ["house", "hall", "granary", "tower"]:
		var sub := dir.path_join(shell)
		if DirAccess.dir_exists_absolute(sub):
			for f in DirAccess.get_files_at(sub):
				DirAccess.remove_absolute(sub.path_join(f))


func _log(text: String) -> void:
	print("[interior] %6.1fs  %s" % [elapsed, text])


func _finish(ok: bool, why: String) -> void:
	set_physics_process(false)
	for action in ["move_forward", "move_back", "move_left", "move_right", "sprint"]:
		Input.action_release(action)
	_log(("通过：" if ok else "失败：") + why)
	_cleanup()
	get_tree().quit(0 if ok else 1)


func _shot(shot_name: String) -> void:
	if shots_dir.is_empty():
		return
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(shots_dir.path_join(shot_name + ".png"))


# ---------------------------------------------------------------- 摆房子、排路线

## 在村心附近找一处能盖的平地，放一座指定款式的（门朝南 +z）
func _place(kind: String, style: String, near: Vector3) -> int:
	var spot := settlement.find_site(kind, near, 0.0, 80.0)
	if is_nan(spot.y):
		return -1
	var why := settlement.place(kind, spot, 0.0, style)
	if why != "":
		_log("放 %s/%s 不成：%s" % [kind, style, why])
		return -1
	return settlement.get_count() - 1


## 梯子前面、爬梯区域外的一点：面朝梯子一按 W 就是往上爬，所以走到这儿就停
func _approach(foot: Vector3, top: Vector3) -> Vector3:
	var out := foot - top
	out.y = 0
	return foot + out.normalized() * 1.2


## 建筑局部坐标（地坪中心为原点）→ 全局
func _local(index: int, x: float, z: float, level_y := 0.0) -> Vector3:
	return settlement.get_building_transform(index) * Vector3(x, level_y, z)


func _plan() -> bool:
	var flat: FlatMap = main.get_node("Map")
	var center := flat.to_global(Vector3(flat.village_x, 0, flat.village_z + 60.0))
	house = _place("house", "sanjian", center)
	granary = _place("granary", "cang", center + Vector3(30, 0, 0))
	tower = _place("tower", "wanglou", center + Vector3(-30, 0, 0))
	if house < 0 or granary < 0 or tower < 0:
		return false
	_log("三间 #%d %s，仓 #%d %s，望楼 #%d %s" % [house, settlement.get_building_template(house), granary, settlement.get_building_template(granary), tower, settlement.get_building_template(tower)])
	var h_in := settlement.get_building_entry(house)
	var g_in := settlement.get_building_entry(granary)
	var t_in := settlement.get_building_entry(tower)
	var ladders := settlement.get_building_ladders(tower)
	if h_in.size() != 2 or g_in.size() != 2 or t_in.size() != 2 or ladders.size() != 4:
		_log("门 %s / %s / %s，梯 %s" % [h_in, g_in, t_in, ladders])
		return false
	steps += [
		["teleport", h_in[0] + (h_in[0] - h_in[1]).normalized() * 3.0, h_in[1]],
		["shot", "HOUSE_front"],
		["walk", h_in[0]], ["walk", h_in[1]],
		["inside", house, 0, "正门里", false],
		["shot", "HOUSE_hall"],
		["walk", _local(house, 1.0, 0.9)], ["walk", _local(house, 2.8, 0.9)], ["walk", _local(house, 3.6, 0.2)],
		["inside", house, 0, "东间", true],
		["face", _local(house, 3.8, -1.9)],
		["shot", "HOUSE_east_room"],
		["walk", _local(house, 2.8, 0.9)], ["walk", _local(house, 0.0, 1.8)], ["walk", h_in[0]],
		["teleport", g_in[0] + (g_in[0] - g_in[1]).normalized() * 3.0, g_in[1]],
		["shot", "GRANARY_steps"],
		["walk", g_in[0]], ["walk", g_in[1]], ["walk", _local(granary, 0.0, -1.0)],
		["inside", granary, 0, "仓里", true],
		["shot", "GRANARY_inside"],
		["teleport", t_in[0] + (t_in[0] - t_in[1]).normalized() * 3.0, t_in[1]],
		["walk", t_in[0]], ["walk", t_in[1]], ["walk", _approach(ladders[0], ladders[1])],
		["inside", tower, 0, "望楼底层梯前", true],
		["climb", ladders[1]],
		["inside", tower, 1, "望楼二层", true],
		["walk", _local(tower, 0.1, -1.9, 4.0)], ["walk", _local(tower, 0.1, -0.5, 4.0)], ["walk", _approach(ladders[2], ladders[3])],
		["climb", ladders[3]],
		["inside", tower, 2, "望楼三层", true],
		["walk", _local(tower, 0.0, 0.0, 8.0)],
		["face", _local(tower, 0.0, 6.0, 8.0)],
		["shot", "TOWER_top"],
	]
	_plan_dev(h_in)
	return true


## ④ ⑤：布置模式里换款式，室内编辑器里改模板、另存、存盘
func _plan_dev(h_in: PackedVector3Array) -> void:
	var saved := ProjectSettings.globalize_path(TEMPLATES).path_join("house/sanjian2.txt")
	steps += [
		["teleport", h_in[0] + (h_in[0] - h_in[1]).normalized() * 6.0, h_in[1]],
		["tap", "build_mode"],
		["expect", func(): return main.is_build_open(), "B 开得了布置模式"],
		["build_to", house],
		["tap", "next_style"],
		["expect", func(): return settlement.get_building_style(house) == "zhihu" and settlement.get_building_template(house) == "house/zhihu", "V 把三间换成下一套（织户）"],
		["shot", "BUILD_style"],
		["tap", "prev_style"],
		["expect", func(): return settlement.get_building_style(house) == "sanjian" and settlement.is_dirty(), "Z 换回三间，清单有改动"],
		["tap", "edit_interior"],
		["expect", func(): return main.is_interior_open() and not main.is_build_open() and main.get_interior_building() == house and main.get_interior_template() == "house/sanjian", "T 在光标下那座上开室内编辑"],
		["shot", "EDIT_open"],
		["tool", "水缸"],
		["cursor_to", _local(house, 1.0, -0.25)],
		["tap", "interact"],
		["expect", func(): return _lines("house/sanjian").has("item vat 1,-0.25 0"), "E 放了一口水缸"],
		["tool", "隔墙"],
		["cursor_to", _local(house, 3.0, 1.5)],
		["tap", "interact"],
		["cursor_to", _local(house, 3.0, 2.75)],
		["tap", "interact"],
		["expect", func(): return _lines("house/sanjian").has("wall 3,1.5 3,2.95"), "两点画了一道隔墙（终点接到前墙上）"],
		["shot", "EDIT_wall"],
		["cursor_to", _local(house, 1.0, -0.25)],
		["tap", "remove"],
		["expect", func(): return not _lines("house/sanjian").has("item vat 1,-0.25 0"), "X 拆掉水缸"],
		["tap", "save_as"],
		["expect", func(): return main.get_interior_template() == "house/sanjian2" and settlement.get_building_style(house) == "sanjian2" and _lines("house/sanjian2").has("auto no") and _lines("house/sanjian2").has("wall 3,1.5 3,2.95"), "N 另存为 house/sanjian2（不参与自动挑），这座改用它"],
		["tap", "save"],
		["expect", func(): return _file_lines(saved) == Array(_lines("house/sanjian2")), "F5 把新模板存到测试目录，读回来一样"],
		["expect", func(): return FileAccess.get_file_as_string(LAYOUT).contains("house/sanjian2@"), "F5 顺带把清单存了（这座写着新款式）"],
		["expect", func(): return not FileAccess.file_exists("res://buildings/house/sanjian2.txt"), "仓库里的模板目录没被写"],
		["tap", "inventory"],
		["expect", func(): return not main.is_interior_open() and player.get_camera().current, "Tab 合上，回到人身上"],
	]


func _lines(id: String) -> PackedStringArray:
	return BuildingTemplates.get_lines(id)


func _file_lines(path: String) -> Array:
	var out := []
	for line in FileAccess.get_file_as_string(path).split("\n"):
		if line.strip_edges() != "" and not line.begins_with("#"):
			out.append(line.strip_edges())
	return out


# ---------------------------------------------------------------- 每帧

func _physics_process(delta: float) -> void:
	elapsed += delta
	if elapsed > TIMEOUT:
		_finish(false, "超时（第 %d 步 %s）" % [step, steps[step] if step < steps.size() else "?"])
		return
	if _feed_taps():
		return
	if step >= steps.size():
		_finish(true, "进屋、上仓、爬望楼都走通了（跳过 %d 次）；布置模式换款式、室内编辑放 / 画 / 拆 / 另存 / 存盘都对" % hops)
		return
	if elapsed - step_started > STEP_TIMEOUT:
		_finish(false, "第 %d 步 %s 超时：人在 %s" % [step, steps[step], player.global_position])
		return
	var s: Array = steps[step]
	match s[0]:
		"settle":
			if elapsed > 1.5 and player.is_on_floor():
				if not _plan():
					_finish(false, "找不到放三间 / 仓 / 望楼的平地，或门、梯子没盖出来")
					return
				_next()
		"teleport":
			_release()
			if not main.teleport_to(s[1]):
				_finish(false, "瞬移不到 %s" % s[1])
				return
			_face(s[2])
			_next()
		"face":
			_face(s[1])
			_next()
		"walk":
			if _walk_to(s[1]):
				_next()
		"climb":
			if _climb_to(s[1]):
				_next()
		"inside":
			_release()
			if not player.is_on_floor() or elapsed - step_started < 0.8:  # 站稳、相机收近了再查
				return
			var why := _check_inside(s[1], s[2], s[4])
			if why != "":
				_finish(false, "%s：%s" % [s[3], why])
				return
			_log("%s：人 %s，相机 %s" % [s[3], player.global_position, player.get_camera().global_position])
			_next()
		"shot":
			_release()
			if elapsed - step_started > 0.25:  # 等几帧，免得截到上一帧
				_shot(s[1])
				_next()
		"tap":
			_release()
			_tap(s[1])
			_next()
		"expect":
			if elapsed - step_started < 0.1:  # 按键的事下一帧才办完
				return
			if not (s[1] as Callable).call():
				_finish(false, s[2])
				return
			_log("✓ " + s[2])
			_next()
		"build_to":
			if main.get_build_selected() == s[1]:
				_release()
				_next()
			else:
				_steer(main.get_build_cursor(), main.get_build_camera_yaw(), settlement.get_building_position(s[1]), 0.3)
		"cursor_to":
			var target: Vector3 = s[1]
			var at := main.get_interior_cursor()
			if Vector2(at.x - target.x, at.z - target.z).length() < 0.05:
				_release()
				_next()
			else:
				_steer(at, main.get_interior_camera_yaw(), target, 0.05)
		"tool":
			if main.get_interior_tool() == s[1]:
				_next()
			else:
				_tap("next_style")


## 按相机方位按 WASD，把光标往 target 挪（布置模式与室内编辑一样：W 往 (−sin, 0, −cos)）
func _steer(at: Vector3, yaw: float, target: Vector3, tol: float) -> void:
	var d := target - at
	d.y = 0
	var fwd := Vector3(-sin(yaw), 0, -cos(yaw))
	var right := Vector3(cos(yaw), 0, -sin(yaw))
	var f := d.dot(fwd)
	var r := d.dot(right)
	for pair in [["move_forward", f > tol], ["move_back", f < -tol], ["move_right", r > tol], ["move_left", r < -tol]]:
		if pair[1]:
			Input.action_press(pair[0])
		else:
			Input.action_release(pair[0])


func _next() -> void:
	step += 1
	step_started = elapsed
	stuck_frames = 0
	climb_seen = false


func _release() -> void:
	for action in ["move_forward", "move_back", "move_left", "move_right", "sprint", "jump"]:
		Input.action_release(action)


func _face(target: Vector3) -> void:
	var d := target - player.global_position
	d.y = 0
	if d.length() > 0.01:
		var yaw := atan2(-d.x, -d.z)  # 按前走的方向是 (−sin, 0, −cos)
		player.set_look_yaw(yaw)
		player.set_facing_yaw(yaw)


func _walk_to(target: Vector3) -> bool:
	var d := target - player.global_position
	d.y = 0
	if d.length() < REACH and abs(target.y - player.global_position.y) < 1.0:
		_release()
		return true
	_face(target)
	Input.action_press("move_forward")
	# 卡住就跳一下（台阶口、门槛）
	if player.global_position.distance_to(last_pos) < 0.005 and player.is_on_floor():
		stuck_frames += 1
		if stuck_frames > 30:
			stuck_frames = 0
			hops += 1
			Input.action_press("jump")
	else:
		Input.action_release("jump")
	last_pos = player.global_position
	return false


## 面朝梯子按住 W：爬到顶、翻过梯板落到楼板上，走到梯顶落脚处
func _climb_to(top: Vector3) -> bool:
	climb_seen = climb_seen or player.is_on_ladder()
	var d := top - player.global_position
	d.y = 0
	if climb_seen and d.length() < 0.45 and player.is_on_floor() and player.global_position.y > top.y - 0.1:
		_release()
		return true
	_face(top)
	Input.action_press("move_forward")
	return false


## 人站在第 level 层的楼面上、在这座的占地里；camera 时相机也得在这座里、低于这一层的顶
## （刚进门那一步不查相机：人背对着门，相机顺着门洞留在门外往里看，这是对的）
func _check_inside(index: int, level: int, camera: bool) -> String:
	var floor_y := settlement.get_building_position(index).y + level * 4.0 * (1 if settlement.get_building_kind(index) == "tower" else 0)
	var at := player.global_position
	if settlement.find_at(at) != index:
		return "人不在这座的占地里（%s）" % at
	if abs(at.y - floor_y) > 0.15:
		return "人没站在楼面上：y %.2f，楼面 %.2f" % [at.y, floor_y]
	if not camera:
		return ""
	var cam := player.get_camera().global_position
	var top := floor_y + (4.0 if settlement.get_building_kind(index) == "tower" else _wall(index))
	if settlement.find_at(cam) != index or cam.y > top - 0.1:
		return "第三人称相机跑到屋外去了：相机 %s，这层顶 %.2f" % [cam, top]
	return ""


func _wall(index: int) -> float:
	match settlement.get_building_kind(index):
		"house":
			return 3.3
		"hall":
			return 4.6
		"granary":
			return 3.4
	return 4.0


# ---------------------------------------------------------------- 按键（按一下、隔几帧再按下一个）

func _tap(action: String) -> void:
	taps.append(action)


func _feed_taps() -> bool:
	if taps.is_empty():
		return false
	if tap_gap > 0:
		tap_gap -= 1
		return true
	if tap_frames == 0:
		Input.action_press(taps[0])
		tap_frames = 3
		return true
	tap_frames -= 1
	if tap_frames == 0:
		Input.action_release(taps[0])
		taps.pop_front()
		tap_gap = 4
	return true
