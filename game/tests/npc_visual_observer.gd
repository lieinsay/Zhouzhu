extends Node
## 只读真实NPC观察：不拨钟、不快进、不改天气/作息/任务/种植速度。
var output:=""
var scene_name:="seed42"
var duration:=180.0
var elapsed:=0.0
var sampling:=0.0
var started:=0
var start_clock:=0.0
var last_clock:=0.0
var failed:=false
var main:Expedition
var viewport:SubViewport
var camera:Camera3D
var caption:Label
var snapshots:=[]
var timeline:=[]
var seen:={}
var counts:={}
var max_grip:=0.0
var min_strike_gap:=INF
var next_log:=0.0
var events:FileAccess
var target_x:=NAN
var target_z:=NAN
var contact_ranges:={}
var bucket_ranges:={}
var max_shoulder_support_error:=0.0
var debug_surfaces:=false
var debug_actors:={}
var follow_nearest:=false
var next_follow:=0.0
var observer_moves:=[]
var follow_person:=-1
func check(ok:bool,message:String)->void:
	if not ok:failed=true;push_error("npc_visual_observer: "+message)
func vec(v:Vector3)->Array:return [v.x,v.y,v.z]
func clean(value):
	if value is float:return value if is_finite(value) else null
	if value is Dictionary:
		var d:={}
		for k in value:d[k]=clean(value[k])
		return d
	if value is Array:
		var a:=[]
		for v in value:a.append(clean(v))
		return a
	return value
func frames(n:int)->void:
	for i in n:await get_tree().physics_frame
func _ready()->void:
	set_physics_process(false);process_physics_priority=500
	output=ProjectSettings.globalize_path("res://../output/npc-visual-20261028/discovery")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
		if arg.begins_with("--duration="):duration=float(arg.trim_prefix("--duration="))
		if arg.begins_with("--scene="):scene_name=arg.trim_prefix("--scene=")
		if arg.begins_with("--observer-x="):target_x=float(arg.trim_prefix("--observer-x="))
		if arg.begins_with("--observer-z="):target_z=float(arg.trim_prefix("--observer-z="))
		if arg=="--debug-surfaces":debug_surfaces=true
		if arg=="--follow-nearest":follow_nearest=true
		if arg.begins_with("--follow-person="):follow_person=int(arg.trim_prefix("--follow-person="));follow_nearest=true
	check(scene_name in ["flat","main","seed42"],"合法既有场景")
	check(GameClock.get_rate()==1.0 and not GameClock.is_fast_forward(),"正常时钟1倍")
	DirAccess.make_dir_recursive_absolute(output);events=FileAccess.open(output.path_join("events.jsonl"),FileAccess.WRITE)
	BuildingTemplates.write_dir=output.path_join("isolated-templates");BuildingTemplates.reload()
	var path:String={"flat":"res://flat_map.tscn","main":"res://main.tscn","seed42":"res://world_seed42.tscn"}[scene_name]
	main=load(path).instantiate()
	if scene_name=="seed42":main.get_node("GroupProvider").world_dir=output.path_join("isolated-world")
	else:
		for node in main.find_children("*","Settlement",true,false):
			var existing:String=node.layout_path;var isolated:=output.path_join(str(node.get_parent().name)+"-isolated-layout.txt")
			if FileAccess.file_exists(existing):FileAccess.open(isolated,FileAccess.WRITE).store_buffer(FileAccess.get_file_as_bytes(existing))
			node.layout_path=isolated
	get_tree().root.size=Vector2i(1440,1000);add_child(main);await frames(120)
	if not is_nan(target_x) and not is_nan(target_z):
		var world:GroupWorld=main.find_children("*","GroupWorld",true,false)[0]
		var point:=world.to_global(Vector3(target_x,world.get_surface_height(target_x,target_z)+1,target_z))
		check(main.teleport_to(point),"只移动观察者至既有村，NPC/天气/时间不变")
		await frames(120)
	viewport=SubViewport.new();viewport.size=Vector2i(1200,900);viewport.world_3d=get_viewport().find_world_3d();viewport.render_target_update_mode=SubViewport.UPDATE_ALWAYS;add_child(viewport)
	camera=Camera3D.new();camera.projection=Camera3D.PROJECTION_ORTHOGONAL;camera.size=3.1;camera.far=100000;viewport.add_child(camera);camera.make_current()
	caption=Label.new();caption.position=Vector2(15,15);caption.add_theme_font_size_override("font_size",20);caption.add_theme_color_override("font_outline_color",Color.BLACK);caption.add_theme_constant_override("outline_size",6);viewport.add_child(caption)
	var display:=TextureRect.new();display.texture=viewport.get_texture();display.position=Vector2(120,80);display.size=Vector2(1200,900);add_child(display)
	started=Time.get_ticks_msec();start_clock=GameClock.get_seconds();last_clock=start_clock;set_physics_process(true)
func nearest_segment(point:Vector3,a:Vector3,b:Vector3)->float:
	return point.distance_to(a+(b-a)*clampf((point-a).dot(b-a)/maxf(.00001,(b-a).length_squared()),0,1))
func actual_grip(actor:SceneCharacter,tool:Node3D,job:int)->Dictionary:
	var out:={};var rigs:=actor.find_children("*","Skeleton3D",true,false)
	if rigs.is_empty():return out
	var rig:Skeleton3D=rigs[0]
	for side in ["R","L"]:
		if job==2 and side=="L":continue
		var hand:=rig.find_bone("J_Bip_"+side+"_Hand");var finger:=rig.find_bone("J_Bip_"+side+"_Middle1")
		if hand<0 or finger<0:continue
		var palm:=rig.to_global(rig.get_bone_global_pose(hand).origin.lerp(rig.get_bone_global_pose(finger).origin,.65))
		var local:=tool.to_local(palm);var distance:=0.0
		if job==0:distance=nearest_segment(local,Vector3(0,-1,.2),Vector3(0,.3,.2))
		elif job==1:distance=nearest_segment(local,Vector3(-.85,0,0),Vector3(.85,0,0))
		elif job==2:distance=nearest_segment(local,Vector3(0,-1.15,.12),Vector3(0,1.15,.12))
		out[side]=distance;max_grip=maxf(max_grip,distance)
		check(distance<.06,"实际掌心到工具轴线距离小于6cm")
	return out
func blade_gap(root:Node3D,tool:Node3D)->Dictionary:
	var blade:MeshInstance3D=tool.get_node_or_null("Blade")
	if blade==null:return {}
	var bounds:=blade.mesh.get_aabb();var lowest:=Vector3.ZERO;lowest.y=INF
	for i in 8:
		var point:=blade.to_global(bounds.position+Vector3(bounds.size.x if i&1 else 0,bounds.size.y if i&2 else 0,bounds.size.z if i&4 else 0))
		if point.y<lowest.y:lowest=point
	var query:=PhysicsRayQueryParameters3D.create(lowest+Vector3.UP*2,lowest+Vector3.DOWN*2,1)
	var hit:=root.get_world_3d().direct_space_state.intersect_ray(query)
	return {"hit":not hit.is_empty(),"gap":lowest.y-hit.position.y if not hit.is_empty() else null,"blade":vec(lowest),"angle":tool.rotation.x}
func frame_actor(root:Node3D,reverse:bool)->bool:
	var target:=root.to_global(Vector3(0,.95,0))
	for i in 8:
		var angle:float=(.55 if not reverse else -.55)+i*PI/4
		var at:=root.to_global(Vector3(sin(angle)*4.2,1.65,cos(angle)*4.2))
		var ray:=PhysicsRayQueryParameters3D.create(target,at,1);ray.hit_from_inside=true
		if not root.get_world_3d().direct_space_state.intersect_ray(ray).is_empty():continue
		camera.global_position=at;camera.look_at(target);return true
	return false
func sample()->void:
	var phase_counts:={};var weather:Dictionary=main.get_node("Atmosphere").get_weather()
	check(GameClock.get_rate()==1.0 and GameClock.get_seconds()>=last_clock,"时钟连续且无快进")
	check(not weather.get("override",{}).get("active",false),"无天气覆盖")
	last_clock=GameClock.get_seconds()
	for folk in main.find_children("*","Villagers",true,false):
		for i in folk.get_villager_count():
			var task:String=folk.get_villager_task(i);var phase:int=folk.get_villager_phase(i);var key:=str(folk.get_path())+":"+str(i)
			var code:=str(phase)+":"+task;phase_counts[code]=phase_counts.get(code,0)+1
			if not folk.is_villager_visible(i):continue
			var root:Node3D=folk.get_node_or_null("Villager"+str(i));if root==null:continue
			var actor:SceneCharacter=root.get_node("Character");var state:Dictionary=actor.get_visual_state();check(state.imported_model,"真实NPC模型无回退")
			if debug_surfaces and not debug_actors.has(actor.get_instance_id()):
				debug_actors[actor.get_instance_id()]=true
				for instance in actor.find_children("*","MeshInstance3D",true,false):
					if instance.mesh==null or instance.skin==null:continue
					for s in instance.mesh.get_surface_count():
						var original:Material=instance.get_active_material(s)
						if original is not ShaderMaterial:continue
						var material:ShaderMaterial=original.duplicate();var role:int=material.get_shader_parameter("model_role")
						material.set_shader_parameter("model_debug_color",true);material.set_shader_parameter("model_tint",Color(1,0,1) if role==1 else Color(0,1,0) if role==3 else Color(0,.4,1) if role==2 else Color(1,1,0));instance.set_surface_override_material(s,material)
			var tool:Node3D=root.get_node("Tool");var grip:=actual_grip(actor,tool,state.job) if state.job<3 else {}
			var carry:String=folk.get_villager_carrying(i);var category:="walk" if phase==Villagers.WALKING else "work"
			if not carry.is_empty():category="carry_"+carry
			if phase==Villagers.WORKING:category="work_"+task
			counts[category]=counts.get(category,0)+1
			var gap:=blade_gap(root,tool) if state.job==0 and phase==Villagers.WORKING and task in ["till","tend"] else {}
			if gap.get("hit",false) and abs(tool.rotation.x)<.08:
				min_strike_gap=minf(min_strike_gap,gap.gap)
				var vkey:=str(state.variant);var range_:Dictionary=contact_ranges.get(vkey,{"samples":0,"min":INF,"max":-INF});range_.samples+=1;range_.min=minf(range_.min,gap.gap);range_.max=maxf(range_.max,gap.gap);contact_ranges[vkey]=range_
			var animation:AnimationPlayer=actor.find_children("*","AnimationPlayer",true,false)[0]
			state["animationPlaying"]=animation.is_playing();state["animationPosition"]=animation.current_animation_position if animation.is_playing() else null
			var record:={"elapsed":elapsed,"clock":GameClock.get_seconds(),"person":key,"phase":phase,"task":task,"carrying":carry,"position":vec(root.global_position),"state":state,"actualGrip":grip,"contact":gap}
			if state.job==1:
				var rig:Skeleton3D=actor.find_children("*","Skeleton3D",true,false)[0]
				var l:=rig.find_bone("J_Bip_L_UpperArm");var r:=rig.find_bone("J_Bip_R_UpperArm")
				var shoulder:=rig.to_global((rig.get_bone_global_pose(l).origin+rig.get_bone_global_pose(r).origin)*.5)
				var support:=tool.global_position.y-shoulder.y;max_shoulder_support_error=maxf(max_shoulder_support_error,abs(support-.055))
				record["poleAboveShoulderJoint"]=support
				if tool.get_meta("water_contact_available",false):
					var bucket:Node3D=tool.get_node("BucketR");var surface:Vector3=tool.get_meta("water_surface_target");var bottom:=bucket.global_position+Vector3.DOWN*.17
					record["waterContact"]={"dip":tool.get_meta("water_dip"),"bottom":vec(bottom),"sourceSurface":vec(surface),"verticalGap":bottom.y-surface.y,"horizontalGap":Vector2(bottom.x-surface.x,bottom.z-surface.z).length()}
					if tool.get_meta("water_dip",0.0)>.99:
						var bkey:=str(state.variant);var ranges:Dictionary=bucket_ranges.get(bkey,{"samples":0,"min":INF,"max":-INF});ranges.samples+=1;ranges.min=minf(ranges.min,bottom.y-surface.y);ranges.max=maxf(ranges.max,bottom.y-surface.y);bucket_ranges[bkey]=ranges
			events.store_line(JSON.stringify(clean(record)))
			var coverage:=str(state.variant)+"/"+str(state.job)+"/"+category
			var event_key:=key+"/"+category;seen[event_key+"start"]=seen.get(event_key+"start",elapsed)
			if gap.get("hit",false) and abs(tool.rotation.x)<.08:coverage+="/strike"
			elif record.get("waterContact",{}).get("dip",0.0)>.99:coverage+="/dip"
			elif phase==Villagers.WORKING and state.job==0 and elapsed-seen[event_key+"start"]>3:
				coverage+="/late";check(animation.is_playing(),"连续劳作动画没有播完停住")
			var captured:int=seen.get(coverage,0)
			var stamp:float=seen.get(coverage+"time",-100.0)
			if captured<4 and elapsed-stamp>=.22:
				# 保持实际玩家的近景LOD条件；远岛只记行为，不把不可见的网格或空镜头记作像素覆盖。
				if root.global_position.distance_to(main.get_node("Player").global_position)>65:continue
				if not frame_actor(root,captured%2==1):continue
				seen[coverage]=captured+1;seen[coverage+"time"]=elapsed
				caption.text=("诊断着色：发紫/裤绿/衣蓝；不作美术通过证据\n" if debug_surfaces else "")+"真实NPC · %s · %s · %s\n%s · %.2f秒 · 外观%d · %s"%[folk.get_villager_job(i),folk.get_villager_activity(i),task,GameClock.get_clock_text(),elapsed,state.variant,str(weather.get("kind_name",weather.get("kind","")))]
				if DisplayServer.get_name()!="headless":
					await RenderingServer.frame_post_draw
					var file:="%04d_%s_v%d_j%d%s.png"%[snapshots.size(),category,state.variant,state.job,"_strike" if coverage.ends_with("/strike") else "_late" if coverage.ends_with("/late") else "_dip" if coverage.ends_with("/dip") else ""]
					viewport.get_texture().get_image().save_png(output.path_join(file));record["image"]=file;snapshots.append(record)
	if elapsed>=next_log:
		next_log+=10;timeline.append({"elapsed":elapsed,"clock":GameClock.get_seconds(),"phases":phase_counts,"weather":weather});print("NPC_OBSERVE ",JSON.stringify(clean(timeline[-1])));events.flush();write_report(false)
func write_report(done:bool)->void:
	var report:={"done":done,"passedStructural":not failed,"scene":scene_name,"elapsed":elapsed,"wallSeconds":(Time.get_ticks_msec()-started)/1000.0,"clockStart":start_clock,"clockEnd":GameClock.get_seconds(),"daySeconds":GameClock.get_day_seconds(),"normalRate":GameClock.get_rate(),"counts":counts,"maxActualGripDistance":max_grip,"minStrikeGap":min_strike_gap if is_finite(min_strike_gap) else null,"strikeContactByVariant":contact_ranges,"bucketContactByVariant":bucket_ranges,"maxShoulderSupportError":max_shoulder_support_error,"timeline":timeline,"snapshots":snapshots,"pixels":DisplayServer.get_name()!="headless","scope":"同一真实场景正常时间/天气与原NPC任务；只移动观察相机/玩家，不改NPC状态；结构通过不能替代像素审查"}
	report["diagnosticMaterials"]=debug_surfaces;report["observerMoves"]=observer_moves
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(clean(report),"  "))
func move_observer_nearby()->void:
	var player:Player=main.get_node("Player");var nearest:SceneCharacter=null;var distance:=INF
	for actor in main.find_children("*","SceneCharacter",true,false):
		if not actor.is_visible_in_tree() or actor.get_visual_state().job==4:continue
		if follow_person>=0 and str(actor.get_parent().name)!="Villager"+str(follow_person):continue
		var d:float=actor.global_position.distance_to(player.global_position)
		if d<distance:distance=d;nearest=actor
	if nearest!=null and distance>35 and distance<1000:
		var at:Vector3=nearest.get_parent().global_position+Vector3(0,.5,0)
		if main.teleport_to(at):observer_moves.append({"elapsed":elapsed,"clock":GameClock.get_seconds(),"nearPerson":str(nearest.get_parent().get_path()),"position":vec(at),"reason":"随行观察已有NPC；只移动玩家，不改NPC任务、作息或天气"})
func _physics_process(delta:float)->void:
	elapsed=(Time.get_ticks_msec()-started)/1000.0;sampling+=delta
	if follow_nearest and elapsed>=next_follow:next_follow=elapsed+10;move_observer_nearby()
	if elapsed>=duration:
		set_physics_process(false);check(not counts.is_empty(),"实际观察到可见NPC，空场景不能通过");events.flush();write_report(true);print("NPC_OBSERVE_DONE ",JSON.stringify(counts));main.queue_free();await frames(3);get_tree().quit(1 if failed else 0);return
	if sampling>=.12:
		sampling=0;set_physics_process(false);await sample();set_physics_process(true)
