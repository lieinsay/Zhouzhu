extends Node
## 正式开局场景的真实背包/快捷栏/船舱/配方入口；独立存档目录，不改用户世界。
const FIRST := ["millet","copper_ore","stone_axe","chest","pottery","cloth","floatstone","roof_tile"]
var main: Expedition
var output := ""
var failed := false
var records := {}

func check(ok: bool, message: String) -> void:
	if not ok:
		failed = true
		push_error("item_art: " + message)

func frames(count: int) -> void:
	for i in count:
		await get_tree().process_frame

func tap(action: String) -> void:
	Input.action_press(action)
	await frames(4)
	Input.action_release(action)
	await frames(4)

func click_slot(section: int, index: int, shift := false) -> void:
	var at := main.get_pack_slot_center(section, index)
	for pressed in [true,false]:
		var event := InputEventMouseButton.new()
		event.position = at
		event.global_position = at
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = pressed
		event.shift_pressed = shift
		Input.parse_input_event(event)
		await frames(3)

func slot_of(item: String) -> int:
	for i in 36:
		if main.get_pack_slot(i).item == item:
			return i
	return -1

func shot(name_: String) -> void:
	await frames(6)
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		var image := get_viewport().get_texture().get_image()
		check(image.save_png(output.path_join(name_ + ".png")) == OK, "截图保存")

func _ready() -> void:
	output = ProjectSettings.globalize_path("res://../output/art-production-20261026/game-first-batch")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):
			output = arg.trim_prefix("--output=")
	DirAccess.make_dir_recursive_absolute(output)
	get_tree().root.size = Vector2i(1440,1000)
	main = load("res://world_seed42.tscn").instantiate()
	main.get_node("GroupProvider").world_dir = output.path_join("test-save")
	var starting := {"bean":3,"timber":6,"plank":10,"clay":8}
	for id in FIRST:
		starting[id] = 1 if id == "stone_axe" else 6
	main.starting_pack = starting
	add_child(main)
	await frames(45)
	var panel: Control = main.get_node("Hud/Backpack")
	var hotbar: Control = main.get_node("Hud/Hotbar")
	var binding_file := FileAccess.open("res://art/bindings.json", FileAccess.READ)
	check(binding_file != null, "正式资源绑定可经FileAccess读取")
	var active: Dictionary = JSON.parse_string(binding_file.get_as_text())
	for key in active.bindings:
		if not key.begins_with("item."): continue
		var id: String = key.trim_prefix("item.")
		var info: Dictionary = panel.get_item_art(id)
		check(info.loaded and info.path == active.bindings[key].path, "正式图标加载 " + id)
		records[id] = info
	check(not panel.get_item_art("missing_item_art_test").loaded, "未绑定物品回退")
	await tap("inventory")
	check(main.is_pack_open(), "真实背包入口打开")
	await shot("01_pack_and_recipes")
	await click_slot(0,slot_of("millet"))
	check(main.get_pack_cursor_stack().item == "millet", "真实拖拽物品")
	await shot("02_cursor")
	await click_slot(0,0)
	check(main.get_pack_slot(0).item == "millet" and main.get_pack_slot(0).count == 6, "快捷栏保留数量")
	await tap("inventory")
	await tap("hotbar_1")
	await shot("03_hotbar")
	var pack_info: Dictionary = panel.get_item_art("millet")
	var bar_info: Dictionary = hotbar.get_item_art("millet")
	check(pack_info.instanceId == bar_info.instanceId, "背包与快捷栏共享同一纹理身份")
	check(main.get_hotbar_selected() == 0, "选择态未改变快捷栏规则")
	var player: Player = main.get_node("Player")
	var ship: Ship = main.get_node("Ship")
	player.global_position = ship.get_deck_position(0,0) + Vector3(0,0.2,0)
	player.velocity = Vector3.ZERO
	await frames(15)
	check(main.is_aboard(), "真实船上容器入口")
	await tap("inventory")
	check(main.get_pack_other_title().contains("船舱"), "船舱与背包共用界面")
	await click_slot(0,slot_of("pottery"),true)
	check(main.get_hold_count("pottery") == 6, "Shift搬入容器数量正确")
	await shot("04_container")
	if DisplayServer.get_name() != "headless":
		check(panel.get_item_art("pottery").draws > 0 and hotbar.get_item_art("millet").draws > 0, "实际绘制路径已执行")
	var report := {"passed":not failed,"renderer":DisplayServer.get_name(),"bindingsChecked":records.size(),"resources":records,"sharedTexture":pack_info.instanceId == bar_info.instanceId,"containerPottery":main.get_hold_count("pottery"),"userConfirmed":false}
	var file := FileAccess.open(output.path_join("report.json"),FileAccess.WRITE)
	file.store_string(JSON.stringify(report,"  "))
	print("item_art: ", JSON.stringify(report))
	main.queue_free()
	await frames(3)
	get_tree().quit(1 if failed else 0)
