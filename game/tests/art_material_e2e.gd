extends Node3D
## 受控材质样件：不加载世界/改天气数据，仅验证同一生产着色器的映射与输入响应。
var output := ""
var camera: Camera3D
var light: DirectionalLight3D
var failed := false
var records := {}
var dry: Image
var wet: Image

func check(ok: bool, message: String) -> void:
	if not ok:failed=true;push_error("art_material: "+message)

func frames(n: int) -> void:
	for i in n:await get_tree().process_frame

func shot(name_: String) -> Image:
	await frames(6)
	if DisplayServer.get_name()=="headless":return null
	await RenderingServer.frame_post_draw
	var image:=get_viewport().get_texture().get_image()
	image.save_png(output.path_join(name_+".png"))
	return image

func _ready() -> void:
	output=ProjectSettings.globalize_path("res://../output/art-production-20261026/material-game")
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--output="):output=arg.trim_prefix("--output=")
	DirAccess.make_dir_recursive_absolute(output)
	get_tree().root.size=Vector2i(1440,900)
	RenderingServer.global_shader_parameter_set("planet_curve_on",0.0)
	RenderingServer.global_shader_parameter_set("wx_wet",0.0)
	var environment:=WorldEnvironment.new()
	environment.environment=Environment.new()
	environment.environment.background_mode=Environment.BG_COLOR
	environment.environment.background_color=Color(.16,.19,.23)
	environment.environment.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color=Color(.8,.85,1)
	environment.environment.ambient_light_energy=.55
	add_child(environment)
	light=DirectionalLight3D.new();light.rotation_degrees=Vector3(-45,-30,0);light.light_energy=1.4;add_child(light)
	camera=Camera3D.new();camera.projection=Camera3D.PROJECTION_ORTHOGONAL;camera.size=5.4
	camera.position=Vector3(3.8,2.8,6.8);add_child(camera);camera.look_at(Vector3(0,.8,0));camera.make_current()
	var names:=["wood","rammed_earth","terracotta","iron"]
	var colors:=[Color(.6,.43,.27),Color(.67,.57,.4),Color(.7,.38,.23),Color(.42,.43,.45)]
	for i in 4:
		var material: ShaderMaterial=ArtSurface.create("material."+names[i],colors[i],.65 if i==3 else .95,.85 if i==3 else 0.0)
		if i==3:
			# 铁不进入正式绑定；仅在本样件临时读取仓库外于game的候选基础色。
			var image:=Image.new()
			var candidate:=ProjectSettings.globalize_path("res://../assets/art/materials/iron/v1/basecolor.png")
			check(image.load_png_from_buffer(FileAccess.get_file_as_bytes(candidate))==OK,"铁候选图片读取")
			image.generate_mipmaps()
			material.set_shader_parameter("surface_tex",ImageTexture.create_from_image(image))
			var registry: Dictionary=JSON.parse_string(FileAccess.get_file_as_string(ProjectSettings.globalize_path("res://../assets/art/registry.json")))
			for asset in registry.assets:
				if asset.id=="material.iron":
					var mean: Array=asset.versions[0].surface.meanLinear
					material.set_shader_parameter("surface_mean",Vector3(mean[0],mean[1],mean[2]))
			material.set_shader_parameter("surface_tile_metres",.5)
			material.set_shader_parameter("surface_enabled",true)
			material.set_shader_parameter("metallic",.85)
		check(material.get_shader_parameter("surface_enabled")==true,"材质样件基础色 "+names[i])
		var texture: Texture2D=material.get_shader_parameter("surface_tex")
		check(texture!=null,"纹理加载 "+names[i])
		var expected:=1.0 if i==1 else .5
		check(is_equal_approx(material.get_shader_parameter("surface_tile_metres"),expected),"米制尺度 "+names[i])
		records[names[i]]={"path":texture.resource_path if i!=3 else "assets/art/materials/iron/v1/basecolor.png","formalBinding":i!=3,"tileMetres":expected,"meanLinear":material.get_shader_parameter("surface_mean"),"channels":["baseColor"],"roughness":material.get_shader_parameter("roughness"),"metallic":material.get_shader_parameter("metallic")}
		var object:=MeshInstance3D.new()
		if i==2:
			var cylinder:=CylinderMesh.new();cylinder.top_radius=.56;cylinder.bottom_radius=.56;cylinder.height=1.35;object.mesh=cylinder
		elif i==3:
			var sphere:=SphereMesh.new();sphere.radius=.6;sphere.height=1.2;object.mesh=sphere
		else:
			var box:=BoxMesh.new();box.size=Vector3(1.35,1.35,.45);object.mesh=box
		object.material_override=material;object.position=Vector3((i-1.5)*1.7,.75,0);add_child(object)
		var label:=Label3D.new();label.text=names[i]+" · "+str(expected)+"m";label.position=Vector3((i-1.5)*1.7,1.7,0);label.font_size=32;label.pixel_size=.006;label.billboard=BaseMaterial3D.BILLBOARD_ENABLED;add_child(label)
	var missing: ShaderMaterial=ArtSurface.create("material.missing",Color.WHITE,1.0)
	check(missing.get_shader_parameter("surface_enabled")!=true,"缺失材质回退素色")
	dry=await shot("01_dry_near")
	light.rotation_degrees=Vector3(-35,145,0)
	await shot("02_changed_light")
	light.rotation_degrees=Vector3(-45,-30,0)
	RenderingServer.global_shader_parameter_set("wx_wet",1.0)
	wet=await shot("03_wet")
	camera.size=11
	await shot("04_far")
	var changed:=0
	if dry!=null and wet!=null:
		for y in range(100,800,5):
			for x in range(100,1340,5):
				var a:=dry.get_pixel(x,y)
				var b:=wet.get_pixel(x,y)
				if abs(a.r-b.r)+abs(a.g-b.g)+abs(a.b-b.b)>.03:changed+=1
		check(changed>300,"天气湿润输入确实改变表面像素")
	var report:={"passed":not failed,"renderer":DisplayServer.get_name(),"materials":records,"wetChangedPixelsSampled":changed,"scope":"受控材质样件；未改生成器或世界天气数据"}
	FileAccess.open(output.path_join("report.json"),FileAccess.WRITE).store_string(JSON.stringify(report,"  "))
	print("art_material: ",JSON.stringify(report))
	get_tree().quit(1 if failed else 0)
