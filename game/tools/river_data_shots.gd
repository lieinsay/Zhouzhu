extends "res://tools/start_shots.gd"
## Same camera, actual basin's driest/wettest exported days; numerical check before every screenshot.
## --shots=<absolute directory> [--clear]. Uses the generator's points and daily series without a second runoff model.

var river_probe := Vector2.ZERO
var series := PackedFloat32Array()
var q_mean := 0.0
var q_bf := 0.0
var w_bf := 0.0
var d_bf := 0.0
var bed := 0.0


func _plan() -> void:
	var data_dir := "res://planet/seed42/groups/%d" % world.get_node_id()
	var f := FileAccess.open(data_dir.path_join("terrain.bin"), FileAccess.READ)
	f.seek(8)
	var rows := f.get_32()
	var cols := f.get_32()
	var res := f.get_double()
	var x0 := f.get_double()
	var z0 := f.get_double()
	f.seek(40 + rows * cols * 66)
	var count := f.get_32()
	var dock: Vector3 = world.get_dock_position(world.find_dock(main.origin_dock))
	var best := INF
	var point := PackedFloat32Array()
	var basin := 0
	var flow_dir := Vector3.FORWARD
	for si in count:
		f.get_32() # island
		f.get_32() # down
		f.get_32() # join
		f.get_32() # exit
		var b := int(f.get_32())
		var n := f.get_32()
		var pts := f.get_buffer(n * 80).to_float32_array()
		for i in range(2, n - 2):
			var p := pts.slice(i * 20, (i + 1) * 20)
			var pos := Vector2(x0 + p[1] * res, z0 + p[0] * res)
			var distance := pos.distance_to(Vector2(dock.x, dock.z))
			if p[3] < 100.0 or p[8] < 5.0 or p[12] > 0.015:
				continue
			var score := distance + 200000.0 * p[12]
			if score >= best:
				continue
			var here: Dictionary = world.get_river_at(pos.x, pos.y)
			if here.is_empty() or absf(float(here["bed"]) - p[11]) > 0.001:
				continue
			best = score
			point = p
			river_probe = pos
			basin = b
			flow_dir = Vector3(pts[(i + 1) * 20 + 1] - p[1], 0, pts[(i + 1) * 20] - p[0]).normalized()
	var basins := f.get_32()
	for b in basins:
		var days := f.get_32()
		var values := f.get_buffer(days * 4).to_float32_array()
		if b == basin or (basin == 4294967295 and b == basins - 1):
			series = values
	if point.is_empty() or series.is_empty():
		push_error("river_data_shots: no suitable exported river")
		get_tree().quit(1)
		return
	q_mean = point[4]
	q_bf = point[5]
	w_bf = point[6]
	d_bf = point[7]
	bed = point[11]
	var low := 0
	var high := 0
	for i in series.size():
		if series[i] < series[low]: low = i
		if series[i] > series[high]: high = i
	var p := Vector3(river_probe.x, bed, river_probe.y)
	var side := Vector3(flow_dir.z, 0, -flow_dir.x)
	var cam := p - flow_dir * 15.0 + side * (w_bf * 0.5 + 3.0)
	cam.y = _ground(cam.x, cam.z, bed) + 3.0
	var look := p + flow_dir * 20.0
	look.y = _ground(look.x, look.z, bed) + 0.5
	for state in [["river_dry", low], ["river_wet", high]]:
		_add(state[0], cam, look, 60)
		views.back().append(state[1])
	print("river_data_shots: basin=%d pos=%s Qmean=%.4f Qbf=%.4f SSC=%.0f D50=%.3f slope=%.5f source_bank=%.2f bed+dbf=%.2f" % [
		basin, river_probe, q_mean, q_bf, point[16], point[13], point[12], point[10], bed + d_bf])


func _enter() -> void:
	var sample_day: int = views[index][5]
	var clock := get_node("/root/GameClock")
	clock.set_calendar_start(sample_day)
	clock.restart(12.0)
	clock.set_paused(true)
	world.set_runoff_override(-1.0)
	var here: Dictionary = world.get_river_at(river_probe.x, river_probe.y)
	var q := q_mean * series[sample_day]
	var depth := d_bf * pow(q / q_bf, 0.4)
	var width := w_bf * pow(q / q_bf, 0.26)
	var speed := q / (width * depth)
	if absf(float(here["discharge_m3_s"]) - q) > 0.001 or absf(float(here["depth"]) - depth) > 0.001 or absf(float(here["width"]) - width) > 0.001 or absf(float(here["speed_m_s"]) - speed) > 0.001:
		push_error("river_data_shots: source daily contract failed %s" % here)
		get_tree().quit(1)
		return
	print("river_data_shots: PASS day=%d index=%.3f Q=%.4f width=%.3f depth=%.3f speed=%.3f" % [sample_day, series[sample_day], q, width, depth, speed])
	super._enter()
