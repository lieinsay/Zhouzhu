extends Node
## 新实体的真实GLB/材质与占地检查；试接入文件通过显式CLI选择，不覆盖正式绑定。
var output:=""
var failed:=false
var viewport:SubViewport
var stage:Node3D
var camera:Camera3D
func check(ok:bool,message:String)->void:
	if not ok:failed=true;push_error("furniture_models: "+message)
func frames(n:int)->void:
	for i in n:await get_tree().process_frame
func collect_bounds(node:Node3D,frame:Transform3D)->Array:
	var rows:=[]
	if node is MeshInstance3D and node.mesh!=null:rows.append(frame*node.global_transform*node.mesh.get_aabb())
	for child in node.get_children():
		if child is Node3D:rows.append_array(collect_bounds(child,frame))
	return rows
func _ready()->void:
	output=ProjectSettings.globalize_path("res://../output/model-upgrade-20261027/furniture-review")
	var source:="res://art/model-bindings.json"
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
		if arg.begins_with("--model-bindings="):source=arg.trim_prefix("--model-bindings=")
	DirAccess.make_dir_recursive_absolute(output)
	var active:Dictionary=JSON.parse_string(FileAccess.get_file_as_string(source))
	get_tree().root.size=Vector2i(800,800)
	viewport=SubViewport.new();viewport.size=Vector2i(512,512);viewport.own_world_3d=true;viewport.transparent_bg=true;viewport.render_target_update_mode=SubViewport.UPDATE_ALWAYS;add_child(viewport)
	var world:=Node3D.new();viewport.add_child(world);stage=Node3D.new();world.add_child(stage)
	var environment:=WorldEnvironment.new();environment.environment=Environment.new();environment.environment.background_mode=Environment.BG_COLOR;environment.environment.background_color=Color(0,0,0,0)
	environment.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;environment.environment.ambient_light_color=Color(.86,.90,1);environment.environment.ambient_light_energy=.65;world.add_child(environment)
	var light:=DirectionalLight3D.new();light.rotation_degrees=Vector3(-45,-30,0);light.light_energy=1.1;light.shadow_enabled=true;world.add_child(light)
	camera=Camera3D.new();camera.projection=Camera3D.PROJECTION_ORTHOGONAL;camera.near=.01;camera.far=1000;world.add_child(camera);camera.make_current()
	var display:=TextureRect.new();display.texture=viewport.get_texture();display.position=Vector2(140,140);display.size=Vector2(512,512);add_child(display)
	var records:={}
	for key in active.bindings:
		if not key.begins_with("furniture.") and key!="prop.cargo_crate":continue
		var model:Node3D=ArtSurface.model_instance(key);check(model!=null,"实际GLB实体 "+key)
		if model==null:continue
		stage.add_child(model)
		var boxes:=collect_bounds(model,model.global_transform.affine_inverse());check(not boxes.is_empty(),"有真实网格 "+key)
		var bounds:AABB=boxes[0]
		for box in boxes:bounds=bounds.merge(box)
		var expected:Array=active.bindings[key].dimensions
		# 定义宽深是占地上限；鼓等原有定制器物本来就留有操作空间，不能为凑尺寸拉伸。
		check(bounds.size.x<=float(expected[0])+.12 and bounds.size.z<=float(expected[2])+.12,"不越出既有家具占地 "+key)
		var center:=bounds.get_center();camera.position=center+Vector3(1.1,.85,1.35).normalized()*maxf(10,bounds.size.length()*2);camera.look_at(center)
		var projection:=Rect2()
		for i in 8:
			var corner:=bounds.position+Vector3(bounds.size.x if (i&1) else 0,bounds.size.y if (i&2) else 0,bounds.size.z if (i&4) else 0)
			var point:=camera.to_local(corner)
			if i==0:projection=Rect2(Vector2(point.x,point.y),Vector2.ZERO)
			else:projection=projection.expand(Vector2(point.x,point.y))
		camera.size=maxf(projection.size.x,projection.size.y)/.8
		await frames(5)
		if DisplayServer.get_name()!="headless":
			await RenderingServer.frame_post_draw;viewport.get_texture().get_image().save_png(output.path_join(key+".png"))
		records[key]={"path":model.get_meta("art_model_path"),"sha256":FileAccess.get_sha256(str(model.get_meta("art_model_path"))),"bounds":{"min":[bounds.position.x,bounds.position.y,bounds.position.z],"max":[bounds.end.x,bounds.end.y,bounds.end.z]},"pixels":DisplayServer.get_name()!="headless"}
		stage.remove_child(model);model.queue_free();await frames(2)
	check(records.size()==14,"13家具与统一货箱覆盖")
	var report:={"passed":not failed,"sourceBindings":source,"renderer":DisplayServer.get_name(),"models":records,"scope":"真实作者/精修实体、生产材质、定义宽深和透明预览；碰撞与真实界面另验"}
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "));print("furniture_models: ",records.size()," passed=",not failed)
	get_tree().quit(1 if failed else 0)
