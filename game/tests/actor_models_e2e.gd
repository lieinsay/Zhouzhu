extends Node3D
## 真实导入人物与蒙皮：原地动作检查和同一生产材质的集中像素样件。
var output:=""
var failed:=false
var camera:Camera3D
var states:={}
var transparent_previews:=false
func check(ok:bool,message:String)->void:
	if not ok:failed=true;push_error("actor_models: "+message)
func frames(n:int)->void:
	for i in n:await get_tree().process_frame
func shot(name_:String)->void:
	await frames(4)
	if DisplayServer.get_name()!="headless":
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(output.path_join(name_+".png"))
func _ready()->void:
	output=ProjectSettings.globalize_path("res://../output/model-upgrade-20261027/actors")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
		if arg=="--transparent-previews":transparent_previews=true
	DirAccess.make_dir_recursive_absolute(output)
	get_tree().root.size=Vector2i(1000,1000)
	get_tree().root.transparent_bg=transparent_previews
	var environment:=WorldEnvironment.new();environment.environment=Environment.new();environment.environment.background_mode=Environment.BG_COLOR;environment.environment.background_color=Color(0,0,0,0) if transparent_previews else Color(.25,.30,.35)
	environment.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;environment.environment.ambient_light_color=Color(.85,.9,1);environment.environment.ambient_light_energy=.65;add_child(environment)
	var light:=DirectionalLight3D.new();light.rotation_degrees=Vector3(-40,-25,0);light.light_energy=1.2;add_child(light)
	camera=Camera3D.new();camera.projection=Camera3D.PROJECTION_ORTHOGONAL;camera.size=2.15;camera.position=Vector3(1.8,1.5,4.2);add_child(camera);camera.look_at(Vector3(0,.90,0));camera.make_current()
	for variant in 4:
		var actor:=SceneCharacter.new();actor.configure(variant,3,false);add_child(actor);await frames(3)
		var state:=actor.get_visual_state();check(state.imported_model,"真正导入模型 "+str(variant));check(state.bones>=50 and state.skinned_meshes>=4 and state.skinned_clothing>=2,"身体和服装均有蒙皮")
		for i in 24:actor.animate(1.0/60,0.0)
		print("IDLE_STATE ",variant," ",JSON.stringify(actor.get_visual_state()))
		check(actor.get_visual_state().head_height>1.40,"待机为真实站立姿态")
		var rigs:=actor.find_children("*","Skeleton3D",true,false)
		check(rigs.size()==1,"一套人物骨骼")
		var rig:Skeleton3D=rigs[0] if not rigs.is_empty() else null
		for instance in actor.find_children("*","MeshInstance3D",true,false):
			if instance.skin==null:continue
			for surface in instance.mesh.get_surface_count():
				var arrays:Array=instance.mesh.surface_get_arrays(surface)
				var vertices:PackedVector3Array=arrays[Mesh.ARRAY_VERTEX]
				var weights:PackedFloat32Array=arrays[Mesh.ARRAY_WEIGHTS]
				var bones:PackedInt32Array=arrays[Mesh.ARRAY_BONES]
				var stride:int=weights.size()/vertices.size()
				check(stride in [4,8] and bones.size()==weights.size(),"真实逐顶点蒙皮数据")
				for vertex in range(0,vertices.size(),7):
					var total:=0.0
					for j in stride:total+=weights[vertex*stride+j];check(bones[vertex*stride+j]>=0 and bones[vertex*stride+j]<instance.skin.get_bind_count(),"骨骼索引有效")
					check(abs(total-1.0)<.002,"蒙皮权重归一")
		var leg:int=rig.find_bone("J_Bip_L_UpperLeg") if rig!=null else -1
		var start:Quaternion=rig.get_bone_pose_rotation(leg) if leg>=0 else Quaternion.IDENTITY
		await shot("actor_"+str(variant)+"_idle")
		for i in 15:actor.animate(1.0/60,1.3)
		if leg>=0:check(start.angle_to(rig.get_bone_pose_rotation(leg))>.01,"作者行走动作确实驱动关节")
		check(actor.get_visual_state().animation=="walk","行走动画状态")
		await shot("actor_"+str(variant)+"_walk")
		for i in 12:actor.animate(1.0/60,5.0)
		check(actor.get_visual_state().animation=="run","快速移动动画状态")
		await shot("actor_"+str(variant)+"_run")
		camera.position=Vector3(.08,1.61,2.5);camera.look_at(Vector3(0,1.56,0));camera.size=.45;await shot("actor_"+str(variant)+"_face")
		states[str(variant)]=actor.get_visual_state()
		actor.queue_free();await frames(2)
		camera.size=2.15;camera.position=Vector3(1.8,1.5,4.2);camera.look_at(Vector3(0,.90,0))
	for variant in [0,2]:
		for job in 3:
			var actor:=SceneCharacter.new();actor.configure(variant,job,true);add_child(actor)
			for i in 20:actor.animate(1.0/60,0)
			var state:=actor.get_visual_state();print("GRIP_STATE ",variant," ",job," ",JSON.stringify(state))
			check(state.right_grip_error<.055,"右手掌心与工具握点对齐")
			if job<2:check(state.left_grip_error<.055,"左手掌心与工具握点对齐")
			await shot("grip_"+str(variant)+"_"+str(job));actor.queue_free();await frames(2)
	var report:={"passed":not failed,"renderer":DisplayServer.get_name(),"actors":states,"scope":"真实作者网格、服装骨骼、重定向动作和生产着色器；真实世界及持物另验"}
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "));print("actor_models: ",JSON.stringify(report));get_tree().quit(1 if failed else 0)
