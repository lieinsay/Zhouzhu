"""Blender：从已登记作者编辑源给女式上衣补裤腰余量；不改UV、骨骼或权重。"""
import bpy,pathlib,json
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=ROOT/'build/prepared-workwear-v2';OUT.mkdir(parents=True,exist_ok=True)
source=ROOT/'assets/art/models/actors/female/v1/source.blend'
bpy.ops.wm.open_mainfile(filepath=str(source))
rig=next(o for o in bpy.data.objects if o.type=='ARMATURE')
top=bpy.data.objects['female_WorkTunic'];pants=bpy.data.objects['female_WorkTrousers']
scale=json.loads((ROOT/'game/art/model-bindings.json').read_text(encoding='utf8'))['bindings']['actor.young_female']['scale']
changed=0
for v in top.data.vertices:
    height=v.co.z*scale
    # 男裤按女骨长适配后，后腰包络比原女上衣大约2cm；只放松下摆至腰部，肩袖/领口保持作者造型。
    ease=max(0,min(1,(1.24-height)/.14))
    if ease<=0:continue
    if v.co.y<0:v.co.y*=1+.42*ease
    v.co.x*=1+.08*ease
    changed+=1
if top.data.has_custom_normals:top.data.normals_split_custom_set([(0,0,0)]*len(top.data.loops))
top.data.update()
objects=[o for o in bpy.data.objects if o.type=='MESH' and o.name.startswith('female_')]
bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
for obj in objects:obj.select_set(True)
bpy.context.view_layer.objects.active=rig;rig.animation_data.use_nla=True
for track in rig.animation_data.nla_tracks:track.mute=False
bpy.ops.export_scene.gltf(filepath=str(OUT/'female.glb'),export_format='GLB',use_selection=True,export_animations=True,export_animation_mode='NLA_TRACKS',export_skins=True,export_all_influences=False,export_morph=True,export_yup=True,export_extras=True)
rig.animation_data.use_nla=False;bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'female.blend'))
report=dict(source=source.relative_to(ROOT).as_posix(),modifiedVertices=changed,operation='女式上衣后腰/衣摆余量适配；UV、原骨骼、逐顶点权重与动作不变',parameters=dict(fullEaseBelowMetres=1.10,blendEndsAtMetres=1.24,backEase=.42,sideEase=.08),garment='female_WorkTunic',underlayer='female_WorkTrousers')
(OUT/'female.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print('WORKWEAR_FIT',json.dumps(report,ensure_ascii=False),flush=True)
