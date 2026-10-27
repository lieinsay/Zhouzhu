extends Node
## 使用现有平板开发场景安排一座房与入坞船，真实输入打开三种界面；仅写指定测试目录。
var main: Expedition
var output := ""
var failed := false
var checked := {}
var mapped_surfaces := {}
signal finished(passed:bool)
var quit_when_done:=true
var output_override:=""

func frames(count: int) -> void:
	for i in count: await get_tree().physics_frame

func tap(action: String) -> void:
	Input.action_press(action);await frames(3)
	Input.action_release(action);await frames(3)

func check(ok: bool, message: String) -> void:
	if not ok:
		failed=true;push_error("art_previews: "+message)

func shot(name_: String) -> void:
	await frames(5)
	if DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(output.path_join(name_+".png"))

func _ready() -> void:
	output=ProjectSettings.globalize_path("res://../output/art-production-20261026/preview-game")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
	DirAccess.make_dir_recursive_absolute(output)
	if not output_override.is_empty():output=output_override;DirAccess.make_dir_recursive_absolute(output)
	get_tree().root.size=Vector2i(1440,1000)
	BuildingTemplates.write_dir=output.path_join("test-templates")
	BuildingTemplates.reload()
	main=load("res://flat_map.tscn").instantiate()
	var settlement: Settlement=main.get_node("Map/Settlement")
	settlement.layout_path=output.path_join("test-layout.txt")
	add_child(main);await frames(30)
	var flat: FlatMap=main.get_node("Map")
	var near:=flat.to_global(Vector3(flat.village_x,0,flat.village_z+60))
	var spot:=settlement.find_site("house",near,0,80)
	check(not is_nan(spot.y),"可放房屋的测试地")
	check(settlement.place("house",spot,0,"sanjian")=="","安排真实房屋")
	await frames(5)
	for found in settlement.find_children("*","MeshInstance3D",true,false):
		var node:=found as MeshInstance3D
		if node.mesh==null:continue
		for surface in node.mesh.get_surface_count():
			var material: Material=node.get_surface_override_material(surface)
			if material==null:material=node.mesh.surface_get_material(surface)
			if material is ShaderMaterial and material.get_shader_parameter("surface_enabled")==true:
				var texture: Texture2D=material.get_shader_parameter("surface_tex")
				if texture!=null:mapped_surfaces[texture.resource_path]=true
	check(main.teleport_to(spot),"移动到房屋")
	await frames(5);await tap("build_mode")
	check(main.is_build_open(),"真实B建筑入口")
	var build: Control=main.get_node("Hud/Build")
	for id in ["house","hall","granary","tower","berth"]:
		var info: Dictionary=build.get_asset_art("building."+id)
		check(info.loaded,"建筑图片 "+id);checked["building."+id]=info
	await shot("01_buildings")
	await tap("edit_interior")
	check(main.is_interior_open(),"真实T室内入口")
	for i in 4:await tap("next_style")
	check(main.get_interior_tool()=="床","家具床工具")
	await shot("02_furniture_bed")
	var interior: Control=main.get_node("Hud/Interior")
	for id in ["bed","stove","table","bench","chest","vat","bin","altar","loom","shelf","sacks","drum","mat"]:
		var info: Dictionary=interior.get_asset_art("furniture."+id)
		check(info.loaded,"家具图片 "+id);checked["furniture."+id]=info
	await tap("inventory")
	var yard: Shipyard=main.get_node("Map/Shipyard")
	var ship: Ship=main.get_node("Ship")
	check(yard.is_present(),"真实船坞存在")
	ship.place_at(yard.get_cradle_contact())
	check(main.teleport_to(yard.get_bench_position()),"移动到画样台")
	await frames(8);await tap("interact")
	check(main.is_refit_open(),"真实E改装入口")
	var refit: Control=main.get_node("Hud/Refit")
	for id in ["bridge","storage","sail","floatstone","deck","long_sail","tall_hold","wide_deck"]:
		var info: Dictionary=refit.get_asset_art("module."+id)
		check(info.loaded,"船模块图片 "+id);checked["module."+id]=info
	check(refit.get_asset_art("module.floatstone").path!=refit.get_asset_art("item.floatstone").path,"浮石物品与舱模块命名空间独立")
	check(interior.get_asset_art("furniture.chest").path!=interior.get_asset_art("item.chest").path,"柜与木箱命名空间独立")
	await shot("03_modules")
	var report:={"passed":not failed,"checked":checked.size(),"renderer":DisplayServer.get_name(),"resources":checked,"mappedSurfaces":mapped_surfaces.keys(),"scope":"真实界面入口；模型精绘缺口另记"}
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "))
	print("art_previews: ",JSON.stringify(report))
	var actors:=main.find_children("*","SceneCharacter",true,false)
	check(actors.size()>1,"实际平板聚落已有成年人物")
	for actor in actors:check(actor.get_visual_state().face_loaded,"聚落角色面部已加载")
	report.npcCount=actors.size()-1
	report.passed=not failed
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "))
	main.queue_free();await frames(3);finished.emit(not failed)
	if quit_when_done:get_tree().quit(1 if failed else 0)
