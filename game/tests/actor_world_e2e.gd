extends Node
## 使用原平板开发场景和真实聚落时间推进；不设置有利天气、不改人物状态或存档。
var output:=""
var failed:=false
func check(ok:bool,message:String)->void:
	if not ok:failed=true;push_error("actor_world: "+message)
func frames(n:int)->void:
	for i in n:await get_tree().physics_frame
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
	var avatar:SceneCharacter=player.get_node("Body/Avatar");check(avatar.get_visual_state().imported_model,"实际玩家模型替换")
	camera.global_position=avatar.to_global(Vector3(-1.8,1.4,-3.6));camera.look_at(avatar.to_global(Vector3(0,.90,0)));camera.make_current();await frames(4)
	if DisplayServer.get_name()!="headless":
		await RenderingServer.frame_post_draw;viewport.get_texture().get_image().save_png(output.path_join("player_carry.png"))
	var report:={"passed":not failed,"renderer":DisplayServer.get_name(),"actors":records,"count":records.size(),"visibleJobsCaptured":seen.keys(),"clockDay":GameClock.get_day(),"scope":"真实开发地图同一World3D离屏镜头，聚落/玩家实际状态与自动时间推进；示例地图不冒充seed42暴雨开局"}
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "));print("actor_world: count=",records.size()," passed=",not failed)
	main.queue_free();await frames(3);get_tree().quit(1 if failed else 0)
