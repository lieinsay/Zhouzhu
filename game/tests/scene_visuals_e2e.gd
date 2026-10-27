extends Node
## 同源几何/生产材质的透明预览与受控人物、植被样件。真实世界另加载原开局，不改源数据。
var output:=""
var failed:=false
var viewport:SubViewport
var stage:Node3D
var camera:Camera3D
var results:={"models":{},"characters":{},"scatter":{}}
var visible_render:=false
var skip_interfaces:=false

func check(ok:bool,message:String)->void:
	if not ok:failed=true;push_error("scene_visuals: "+message)
func frames(n:int)->void:
	for i in n:await get_tree().process_frame
func _vec3(values:Array,offset:int)->Vector3:
	return Vector3(values[offset],values[offset+1],values[offset+2])
func clear_stage()->void:
	for child in stage.get_children():stage.remove_child(child);child.queue_free()
func render_image(name_:String)->void:
	await frames(4)
	if visible_render:
		await RenderingServer.frame_post_draw
		var image:=viewport.get_texture().get_image()
		check(image!=null and image.save_png(output.path_join(name_+".png"))==OK,"保存 "+name_)
func fit_model(bounds:AABB)->void:
	var center:=bounds.get_center()
	camera.position=center+Vector3(1.1,.85,1.35).normalized()*maxf(10,bounds.size.length()*2)
	camera.look_at(center)
	var screen_bounds:=Rect2()
	for i in 8:
		var corner:=bounds.position+Vector3(bounds.size.x if (i&1) else 0,bounds.size.y if (i&2) else 0,bounds.size.z if (i&4) else 0)
		var local:=camera.to_local(corner)
		if i==0:screen_bounds=Rect2(Vector2(local.x,local.y),Vector2.ZERO)
		else:screen_bounds=screen_bounds.expand(Vector2(local.x,local.y))
	camera.size=maxf(screen_bounds.size.x,screen_bounds.size.y)/.80
func model_node(data:Dictionary)->Node3D:
	var node:=Node3D.new()
	for part in data.parts:
		var arrays:=[];arrays.resize(Mesh.ARRAY_MAX)
		var vertices:=PackedVector3Array();var normals:=PackedVector3Array();var indices:=PackedInt32Array()
		for at in range(0,part.positions.size(),3):vertices.append(_vec3(part.positions,at));normals.append(_vec3(part.normals,at))
		for at in range(0,part.indices.size(),3):indices.append_array([part.indices[at],part.indices[at+2],part.indices[at+1]])
		arrays[Mesh.ARRAY_VERTEX]=vertices;arrays[Mesh.ARRAY_NORMAL]=normals;arrays[Mesh.ARRAY_INDEX]=indices
		for spec in [["uvs",Mesh.ARRAY_TEX_UV],["uv2s",Mesh.ARRAY_TEX_UV2]]:
			if part.get(spec[0],[]).size()!=vertices.size()*2:continue
			var uv:=PackedVector2Array()
			for at in range(0,part[spec[0]].size(),2):uv.append(Vector2(part[spec[0]][at],part[spec[0]][at+1]))
			arrays[spec[1]]=uv
		var mesh:=ArrayMesh.new();mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES,arrays)
		var color:=Color(part.color[0],part.color[1],part.color[2])
		var material:Material=ArtSurface.part_material(int(part.materialIndex)) if part.has("materialIndex") else ArtSurface.create(part.get("artAsset",""),color,part.get("roughness",1.0),part.get("metallic",0.0))
		if material is ShaderMaterial:
			if part.get("doubleSided",false):material.shader=load("res://shaders/curved_standard_two_sided.gdshader")
			if part.has("emission"):material.set_shader_parameter("emission",Color(part.emission[0],part.emission[1],part.emission[2]));material.set_shader_parameter("emission_energy",part.get("emissionEnergy",0.0))
		mesh.surface_set_material(0,material)
		var instance:=MeshInstance3D.new();instance.mesh=mesh;node.add_child(instance)
	return node
func bounds_of(node:Node3D)->AABB:
	var bounds:=AABB();var first:=true
	for child in node.get_children():
		if child is MeshInstance3D:
			var box:AABB=child.transform*child.mesh.get_aabb()
			bounds=box if first else bounds.merge(box);first=false
	return bounds
func model_previews()->void:
	var directory:=ProjectSettings.globalize_path("res://../assets/art/models/scene-20261027-v2")
	var manifest:Dictionary=JSON.parse_string(FileAccess.get_file_as_string(directory.path_join("manifest.json")))
	var records:Array=manifest.models.filter(func(row):return row.id.begins_with("module:") or row.id.begins_with("building:"))
	var furniture_directory:=ProjectSettings.globalize_path("res://../assets/art/models/furniture-scene-20261027-v2")
	for file in DirAccess.get_files_at(furniture_directory):
		if file.ends_with(".json"):records.append({"id":"furniture:"+file.get_basename(),"file":furniture_directory.path_join(file)})
	for row in records:
		clear_stage()
		var path:String=row.get("file",directory.path_join(str(row.get("url","")).get_file()))
		var data:Dictionary=JSON.parse_string(FileAccess.get_file_as_string(path))
		check(data.get("schemaVersion",0)==1 and not data.get("parts",[]).is_empty(),"源模型 "+row.id)
		var model:=model_node(data);stage.add_child(model)
		var bounds:=bounds_of(model);fit_model(bounds)
		var key:String=row.id.replace(":",".")
		await render_image(key)
		results.models[key]={"file":path,"bounds":{"min":[bounds.position.x,bounds.position.y,bounds.position.z],"max":[bounds.end.x,bounds.end.y,bounds.end.z]},"orthographicSize":camera.size,"pixels":visible_render}
	check(results.models.size()==26,"26项真实模型预览")
func character_samples()->void:
	for variant in 4:
		clear_stage()
		var actor:=SceneCharacter.new();actor.configure(variant,variant,false);stage.add_child(actor)
		var state:=actor.get_visual_state();check(state.face_loaded and state.adult,"成年曲面人物 "+str(variant))
		results.characters[str(variant)]=state
		camera.position=Vector3(.23,1.62,1.5);camera.look_at(Vector3(0,1.585,0));camera.size=.35
		await render_image("face_"+str(variant))
		camera.position=Vector3(2.4,1.9,4.4);camera.look_at(Vector3(0,.9,0));camera.size=2.15
		await render_image("body_"+str(variant))
		actor.animate(.25,1.3)
		await render_image("walk_"+str(variant))
func scatter_samples()->void:
	var image:=Image.create(2,2,false,Image.FORMAT_RGBAF)
	for x in 2:image.set_pixel(x,0,Color(0,0,1,1));image.set_pixel(x,1,Color(0,0,1,20))
	RenderingServer.global_shader_parameter_set("wx_elev",ImageTexture.create_from_image(image))
	for look in 11:
		clear_stage()
		var instance:=MeshInstance3D.new();instance.mesh=ArtSurface.scatter_mesh(look,0);stage.add_child(instance)
		check(instance.mesh!=null,"实际散布模型 "+str(look))
		var material:=instance.get_active_material(0) as ShaderMaterial
		var slots:={}
		for slot in ["bark","foliage","needle","hemp","soil","gravel","stone"]:
			check(material.get_shader_parameter("detail_"+slot+"_enabled")==true,"散布材质 "+slot)
			slots[slot]=material.get_shader_parameter("detail_"+slot+"_tex").resource_path
		results.scatter[str(look)]={"vertices":instance.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX].size(),"materials":slots}
		fit_model(instance.mesh.get_aabb());await render_image("scatter_"+str(look))
		if look<2:
			instance.mesh=ArtSurface.scatter_mesh(look,1);check(instance.mesh!=null,"树木远景LOD");await render_image("scatter_"+str(look)+"_low")
			instance.mesh=ArtSurface.scatter_mesh(look,2);check(instance.mesh!=null,"采伐树桩");fit_model(instance.mesh.get_aabb());await render_image("scatter_"+str(look)+"_stump")
	# 雨湿/挂雪只是独立样件输入检查，不写世界天气或源数据。
	clear_stage();var tree:=MeshInstance3D.new();tree.mesh=ArtSurface.scatter_mesh(0,0);stage.add_child(tree);fit_model(tree.mesh.get_aabb())
	RenderingServer.global_shader_parameter_set("wx_wet",1.0);await render_image("tree_wet")
	RenderingServer.global_shader_parameter_set("wx_wet",0.0)
	for x in 2:image.set_pixel(x,0,Color(25,0,1,1))
	RenderingServer.global_shader_parameter_set("wx_elev",ImageTexture.create_from_image(image));await render_image("tree_snow")
	RenderingServer.global_shader_parameter_set("wx_elev",null)
func actual_world()->void:
	viewport.queue_free();await frames(2)
	var main:Expedition=load("res://world_seed42.tscn").instantiate()
	main.get_node("GroupProvider").world_dir=output.path_join("test-save")
	add_child(main)
	for i in 180:await get_tree().physics_frame
	var actor:SceneCharacter=main.get_node("Player/Body/Avatar")
	check(actor.get_visual_state().face_loaded,"真实玩家使用曲面面部")
	results.world={"player":actor.get_visual_state(),"npcCount":main.find_children("*","SceneCharacter",true,false).size()-1,"clock":GameClock.get_day(),"weather":"原开局自动天气与真实时间序列"}
	if visible_render:
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(output.path_join("world_start.png"))
	main.queue_free();await frames(3)
func _ready()->void:
	output=ProjectSettings.globalize_path("res://../output/scene-art-20261027/pixels")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
		if arg=="--skip-interfaces":skip_interfaces=true
	DirAccess.make_dir_recursive_absolute(output)
	visible_render=DisplayServer.get_name()!="headless"
	get_tree().root.size=Vector2i(1024,800)
	viewport=SubViewport.new();viewport.size=Vector2i(512,512);viewport.own_world_3d=true;viewport.transparent_bg=true;viewport.render_target_update_mode=SubViewport.UPDATE_ALWAYS;add_child(viewport)
	var world:=Node3D.new();viewport.add_child(world)
	stage=Node3D.new();world.add_child(stage)
	var environment:=WorldEnvironment.new();environment.environment=Environment.new();environment.environment.background_mode=Environment.BG_COLOR;environment.environment.background_color=Color(0,0,0,0)
	environment.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;environment.environment.ambient_light_color=Color(.86,.90,1);environment.environment.ambient_light_energy=.7;world.add_child(environment)
	var light:=DirectionalLight3D.new();light.rotation_degrees=Vector3(-45,-30,0);light.light_energy=1.1;world.add_child(light)
	camera=Camera3D.new();camera.projection=Camera3D.PROJECTION_ORTHOGONAL;camera.near=.01;camera.far=10000;world.add_child(camera);camera.make_current()
	var display:=TextureRect.new();display.texture=viewport.get_texture();display.position=Vector2(256,100);display.size=Vector2(512,512);add_child(display)
	await model_previews();await character_samples();await scatter_samples()
	display.queue_free();await actual_world()
	for spec in [["art_previews_e2e","interfaces"],["item_art_e2e","inventory"]]:
		if skip_interfaces:break
		var test=load("res://tests/"+spec[0]+".tscn").instantiate()
		test.quit_when_done=false;test.output_override=output.path_join(spec[1]);add_child(test)
		var passed:bool=await test.finished;check(passed,"真实界面 "+spec[0]);test.queue_free();await frames(3)
	results.passed=not failed;results.pixelCapture=visible_render;results.userConfirmed=false
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(results,"  "))
	print("scene_visuals: models=",results.models.size()," characters=",results.characters.size()," scatter=",results.scatter.size()," passed=",not failed," pixels=",visible_render)
	get_tree().quit(1 if failed else 0)
