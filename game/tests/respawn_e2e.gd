extends Node
## 坠落回位测试：把空艇挪到远处，让玩家沿栈桥一直走、从桥头掉进云海。
## 断言：玩家回到岛上最后一个落脚点附近并重新着地，漂远的空艇被召回出发岛的泊位。
##
##   Godot --headless --path game --fixed-fps 60 res://tests/respawn_e2e.tscn

const MAIN := preload("res://main.tscn")
const TIMEOUT := 40.0

var main: Voyage
var player: Player
var ship: Ship
var origin: Island

var elapsed := 0.0
var berth: Transform3D
var lowest := INF
var fell := false


func _ready() -> void:
	main = MAIN.instantiate()
	add_child(main)
	player = main.get_node("Player")
	ship = main.get_node("Ship")
	origin = main.get_node("IslandA")
	berth = origin.get_berth_contact()
	ship.place_at(Transform3D(Basis(), berth.origin + Vector3(0, 60, 600)))


func _physics_process(delta: float) -> void:
	elapsed += delta
	lowest = minf(lowest, player.global_position.y)
	if elapsed > TIMEOUT:
		_finish(false, "超时")
	elif not fell:
		# 沿栈桥方向一直走，走过桥头
		var pier_end := origin.get_pier_end_position()
		var to := pier_end - origin.get_spawn_transform().origin
		player.set_look_yaw(atan2(-to.x, -to.z))
		Input.action_press("move_forward")
		if player.global_position.y < main.kill_altitude + 40.0:
			fell = true
			Input.action_release("move_forward")
	elif player.is_on_floor():
		var ground := origin.get_surface_height(player.global_position)
		var on_island := not is_nan(ground) and absf(player.global_position.y - ground) < 1.0
		var ship_home := ship.is_at(berth, 0.1)
		var near_dock := player.global_position.distance_to(origin.get_pier_end_position()) < 30.0
		if on_island and ship_home and near_dock:
			_finish(true, "回到岛上，空艇已召回泊位")
		else:
			_finish(false, "着地了但不对：在岛上 %s，船回泊位 %s，在码头附近 %s" % [on_island, ship_home, near_dock])


func _finish(ok: bool, why: String) -> void:
	Input.action_release("move_forward")
	set_physics_process(false)
	print("respawn: %s —— %s（模拟 %.1f s，最低掉到 y = %.0f；玩家 %s，船 %s）" % [
		"PASS" if ok else "FAIL", why, elapsed, lowest, player.global_position, ship.global_position])
	get_tree().quit(0 if ok else 1)
