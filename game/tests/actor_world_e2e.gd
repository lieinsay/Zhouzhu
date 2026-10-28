extends Node
## 使用原平板开发场景和真实聚落时间推进；不设置有利天气、不改人物状态或存档。
var output:=""
var failed:=false
func check(ok:bool,message:String)->void:
	if not ok:failed=true;push_error("actor_world: "+message)
func frames(n:int)->void:
	for i in n:await get_tree().physics_frame
func controlled_tools(actors:Array)->Array:
	# 复制实际Villagers创建的工具网格，单独样件只用于握点像素；不改变NPC账本或室内隐藏状态。
	var samples:=[];var seen:={}
	var vp:=SubViewport.new();vp.size=Vector2i(900,1000);vp.own_world_3d=true;vp.transparent_bg=true;vp.render_target_update_mode=SubViewport.UPDATE_ALWAYS;add_child(vp)
	var world:=Node3D.new();vp.add_child(world)
	var env:=WorldEnvironment.new();env.environment=Environment.new();env.environment.background_mode=Environment.BG_COLOR;env.environment.background_color=Color(0,0,0,0);env.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;env.environment.ambient_light_color=Color(.86,.90,1);env.environment.ambient_light_energy=.65;world.add_child(env)
	var light:=DirectionalLight3D.new();light.rotation_degrees=Vector3(-45,-30,0);light.light_energy=1.1;world.add_child(light)
	var cam:=Camera3D.new();cam.projection=Camera3D.PROJECTION_ORTHOGONAL;cam.size=2.65;world.add_child(cam);cam.position=Vector3(2.3,1.6,4);cam.look_at(Vector3(0,1,0));cam.make_current()
	for source in actors:
		var state:Dictionary=source.get_visual_state();var key:=str(state.job)+"_"+str(state.variant>=2)
		if state.job>=3 or seen.has(key):continue
		var original_tool:Node3D=source.get_parent().get_node_or_null("Tool")
		if original_tool==null:continue
		seen[key]=true;var root:=Node3D.new();world.add_child(root)
		var actor:=SceneCharacter.new();actor.configure(state.variant,state.job,true);root.add_child(actor)
		root.add_child(original_tool.duplicate(0))
		for i in 24:actor.animate(1.0/60,0)
		await frames(5)
		check(actor.get_visual_state().right_grip_error<.055,"实际工具样件掌心误差")
		if DisplayServer.get_name()!="headless":
			await RenderingServer.frame_post_draw;vp.get_texture().get_image().save_png(output.path_join("controlled_tool_"+key+".png"))
		samples.append({"job":state.job,"variant":state.variant,"toolSource":str(original_tool.get_path()),"state":actor.get_visual_state(),"scope":"受控工具与蒙皮装配；不是自然NPC工作画面"})
		world.remove_child(root);root.queue_free();await frames(2)
	vp.queue_free();await frames(2);return samples
func _ready()->void:
	output=ProjectSettings.globalize_path("res://../output/model-upgrade-20261027/world")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
	DirAccess.make_dir_recursive_absolute(output)
	get_tree().root.size=Vector2i(1440,1000)
	BuildingTemplates.write_dir=output.path_join("test-templates");BuildingTemplates.reload()
	var main:Expedition=load("res://flat_map.tscn").instantiate();main.get_node("Map/Settlement").layout_path=output.path_join("test-layout.txt");add_child(main)
	await frames(90)
	var actors:=main.find_children("*","SceneCharacter",true,false);check(actors.size()>1,"真实聚落人物已生成")
	var records:=[];var seen:={}
	var viewport:=SubViewport.new();viewport.size=Vector2i(1440,1000);viewport.world_3d=get_viewport().find_world_3d();viewport.render_target_update_mode=SubViewport.UPDATE_ALWAYS;add_child(viewport)
	var camera:=Camera3D.new();camera.far=10000;viewport.add_child(camera)
	for actor in actors:
		var state:Dictionary=actor.get_visual_state();check(state.imported_model and state.skinned_clothing>=2,"实际游戏人物使用作者模型与服装蒙皮");records.append(state)
		if state.job==4 or seen.has(str(state.job)) or not actor.is_visible_in_tree():continue
		seen[str(state.job)]=true
		camera.global_position=actor.to_global(Vector3(1.6,1.45,3.3));camera.look_at(actor.to_global(Vector3(0,.90,0)));camera.make_current();await frames(3)
		if DisplayServer.get_name()!="headless":
			await RenderingServer.frame_post_draw;viewport.get_texture().get_image().save_png(output.path_join("job_"+str(state.job)+".png"))
	var player:Player=main.get_node("Player");player.set_carrying(true,Color(.5,.3,.18));await frames(10)
	var cargo:Node3D=player.get_node_or_null("Body/Crate/AuthoredCargo");check(cargo!=null and cargo.get_meta("art_model_id","")=="prop.cargo_crate","实际玩家货箱使用正式作者实体")
	var furnitures:=[]
	for node in main.find_children("*","Node3D",true,false):
		if str(node.get_meta("art_model_id","")).begins_with("furniture."):furnitures.append({"id":node.get_meta("art_model_id"),"path":str(node.get_path()),"group":str(node.get_parent().name),"fallback":node.get_meta("art_model_fallback",false)})
	check(not furnitures.is_empty(),"真实Settlement已挂载家具实体")
	for row in furnitures:check(not row.fallback,"实际房屋家具没有退回旧几何")
	var ship:Ship=main.get_node("Ship");var ship_cargo:=false
	# 原开发船只有驾驶区和帆；测试实例显式增加既有storage模块，才能验真实货位，源布局不写回。
	var test_layout:PackedStringArray=ship.get_layout();test_layout.append("0,0,1:storage");ship.set_layout(test_layout)
	for slot in ship.get_hold_slot_count():
		if ship.is_hold_slot_open(slot):
			ship.set_cargo(slot,"timber")
			for node in ship.find_children("*","Node3D",true,false):
				if node.get_meta("art_model_id","")=="prop.cargo_crate":ship_cargo=true
			break
	check(ship_cargo,"实际船上货位使用同一作者货箱")
	var avatar:SceneCharacter=player.get_node("Body/Avatar");check(avatar.get_visual_state().imported_model,"实际玩家模型替换")
	camera.global_position=avatar.to_global(Vector3(-1.8,1.4,-3.6));camera.look_at(avatar.to_global(Vector3(0,.90,0)));camera.make_current();await frames(4)
	if DisplayServer.get_name()!="headless":
		await RenderingServer.frame_post_draw;viewport.get_texture().get_image().save_png(output.path_join("player_carry.png"))
	var fixture_samples:Array=await controlled_tools(actors)
	var activities:=[]
	for folk in main.find_children("*","Villagers",true,false):
		for i in folk.get_villager_count():activities.append({"job":folk.get_villager_job(i),"activity":folk.get_villager_activity(i),"phase":folk.get_villager_phase(i),"visible":folk.is_villager_visible(i)})
	var report:={"passed":not failed,"renderer":DisplayServer.get_name(),"actors":records,"count":records.size(),"visibleJobsCaptured":seen.keys(),"clockDay":GameClock.get_day(),"furnitureInstances":furnitures,"shipCargoAuthored":ship_cargo,"controlledTools":fixture_samples,"naturalNpcActivities":activities,"scope":"真实开发地图同一World3D离屏镜头，聚落/玩家实际状态与自动时间推进；示例地图不冒充seed42暴雨开局；受控工具装配与自然NPC状态分别记录"}
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "));print("actor_world: count=",records.size()," passed=",not failed)
	main.queue_free();await frames(3);get_tree().quit(1 if failed else 0)
