"""在便携Blender内运行：适配授权人物网格、保留蒙皮，重定向作者动作；不拼接人体基本几何。"""
import bpy
import bmesh
import json
import math
import pathlib
import sys
import zipfile,struct
from mathutils import Quaternion,Vector,Matrix

ROOT=pathlib.Path(__file__).resolve().parents[2]
SOURCE=ROOT/'build/model-sources'
MOTION=ROOT/'assets/art/external/quaternius/ual-standard-20250610/AnimationLibrary_Godot_Standard.gltf'
OUT=ROOT/'build/prepared-actors'
OUT.mkdir(parents=True,exist_ok=True)

def ensure_sources():
    SOURCE.mkdir(parents=True,exist_ok=True)
    for gender,stem in [('male','Sakurada Fumiriya'),('female','Sendagaya Shino')]:
        archive=ROOT/'assets/art/external/vroid-cc0/20261027'/(gender+'-original.zip')
        with zipfile.ZipFile(archive) as source:
            names=[name for name in source.namelist() if name.lower().endswith('.vrm')]
            if len(names)!=1:raise ValueError('原始VRM包应仅有一件模型')
            data=source.read(names[0])
        at=12;chunks={}
        while at<len(data):
            count,kind=struct.unpack_from('<II',data,at);chunks[kind]=data[at+8:at+8+count];at+=8+count
        model=json.loads(chunks[0x4e4f534a]);model.pop('extensions',None);model['extensionsUsed']=[];model.pop('extensionsRequired',None)
        for material in model.get('materials',[]):material.pop('extensions',None)
        encoded=json.dumps(model,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4);binary=chunks[0x004e4942];binary+=b'\0'*(-len(binary)%4)
        (SOURCE/(stem+'.glb')).write_bytes(struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary)

def import_asset(path):
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    objects=[obj for obj in bpy.data.objects if obj not in before]
    rig=next(obj for obj in objects if obj.type=='ARMATURE')
    meshes=[obj for obj in objects if obj.type=='MESH' and any(m.type=='ARMATURE' for m in obj.modifiers)]
    return rig,meshes,objects

def subset(obj,keep,name):
    clone=obj.copy();clone.data=obj.data.copy();bpy.context.scene.collection.objects.link(clone);clone.name=name
    mesh=bmesh.new();mesh.from_mesh(clone.data)
    bmesh.ops.delete(mesh,geom=[p for p in mesh.faces if not keep(clone.data.materials[p.material_index].name)],context='FACES')
    bmesh.ops.delete(mesh,geom=[v for v in mesh.verts if not v.link_faces],context='VERTS')
    mesh.to_mesh(clone.data);mesh.free();clone.data.update()
    return clone

def bone_world(rig,bone):return rig.matrix_world@bone.matrix_local

def fit_pants(pants,source_rig,target_rig):
    # 相同人体语义骨的绑定姿态变换，按原顶点权重传递至女体；不是按最近点猜蒙皮。
    transforms={}
    for group in pants.vertex_groups:
        if group.name in source_rig.data.bones and group.name in target_rig.data.bones:
            src=source_rig.data.bones[group.name];dst=target_rig.data.bones[group.name]
            ratio=dst.length/src.length if any(token in group.name for token in ['_UpperLeg','_LowerLeg','_Foot']) and src.length>1e-6 else 1.0
            transforms[group.index]=bone_world(target_rig,dst)@Matrix.Diagonal((1,ratio,1,1))@bone_world(source_rig,src).inverted()
    source_world=pants.matrix_world.copy();target_inverse=target_rig.matrix_world.inverted()
    for vertex in pants.data.vertices:
        world=source_world@vertex.co;point=Vector((0,0,0));total=0
        for link in vertex.groups:
            if link.group in transforms:point+=(transforms[link.group]@world)*link.weight;total+=link.weight
        if total>0:vertex.co=target_inverse@(point/total)
    # 衣服比身体留出穿着松量；不改变关节位置或用刚性分段替换原蒙皮。
    for vertex in pants.data.vertices:
        vertex.co.x*=1.055;vertex.co.y*=1.07
    pants.parent=target_rig;pants.matrix_world=target_rig.matrix_world.copy()
    for modifier in pants.modifiers:
        if modifier.type=='ARMATURE':modifier.object=target_rig
    if pants.data.has_custom_normals:pants.data.normals_split_custom_set([(0.0,0.0,0.0)]*len(pants.data.loops))

def clean_materials(objects):
    for obj in objects:
        for mat in obj.data.materials:
            if not mat:continue
            raw=mat.name
            role='top' if '_Tops_' in raw else 'pants' if '_Bottoms_' in raw else 'shoes' if '_Shoes_' in raw else 'hair' if '_HAIR' in raw else 'skin' if '_SKIN' in raw else 'face'
            mat['zhouzhu_role']=role
            if not mat.name.startswith('ZZ_'):mat.name='ZZ_'+role+'_'+raw
            mat.use_nodes=True;bsdf=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
            if bsdf:bsdf.inputs['Metallic'].default_value=0;bsdf.inputs['Roughness'].default_value=.86 if role=='hair' else .96
        obj['source_model']='VRoid Project / pixiv CC0; adapted civilian mesh'

SOURCE_BONES={
 'hips':'DEF-hips','spine':'DEF-spine.001','chest':'DEF-spine.002','upperChest':'DEF-spine.003','neck':'DEF-neck','head':'DEF-head',
 'leftShoulder':'DEF-shoulder.L','rightShoulder':'DEF-shoulder.R','leftUpperArm':'DEF-upper_arm.L','rightUpperArm':'DEF-upper_arm.R',
 'leftLowerArm':'DEF-forearm.L','rightLowerArm':'DEF-forearm.R','leftHand':'DEF-hand.L','rightHand':'DEF-hand.R',
 'leftUpperLeg':'DEF-thigh.L','rightUpperLeg':'DEF-thigh.R','leftLowerLeg':'DEF-shin.L','rightLowerLeg':'DEF-shin.R',
 'leftFoot':'DEF-foot.L','rightFoot':'DEF-foot.R','leftToes':'DEF-toe.L','rightToes':'DEF-toe.R'}
TARGET_BONES={'hips':'J_Bip_C_Hips','spine':'J_Bip_C_Spine','chest':'J_Bip_C_Chest','upperChest':'J_Bip_C_UpperChest','neck':'J_Bip_C_Neck','head':'J_Bip_C_Head'}
for side,letter in [('left','L'),('right','R')]:
    for key,suffix in [('Shoulder','Shoulder'),('UpperArm','UpperArm'),('LowerArm','LowerArm'),('Hand','Hand'),('UpperLeg','UpperLeg'),('LowerLeg','LowerLeg'),('Foot','Foot'),('Toes','ToeBase')]:TARGET_BONES[side+key]='J_Bip_'+letter+'_'+suffix
    for finger,source_finger in [('Thumb','thumb'),('Index','f_index'),('Middle','f_middle'),('Ring','f_ring'),('Little','f_pinky')]:
        for segment in range(1,4):
            semantic=side+finger+str(segment);SOURCE_BONES[semantic]='DEF-'+source_finger+'.0'+str(segment)+'.'+letter
            TARGET_BONES[semantic]='J_Bip_'+letter+'_'+('Little' if finger=='Little' else finger)+str(segment)

def forward(rig,foot,toe):
    value=(bone_world(rig,rig.data.bones[toe]).translation-bone_world(rig,rig.data.bones[foot]).translation);value.z=0
    return value.normalized()

def retarget(source,target,source_actions):
    mapping={TARGET_BONES[key]:value for key,value in SOURCE_BONES.items() if TARGET_BONES[key] in target.data.bones and value in source.data.bones}
    assert len(mapping)>=18,('missing humanoid bone mapping',list(source.data.bones.keys()))
    align=forward(source,'DEF-foot.L','DEF-toe.L').rotation_difference(forward(target,'J_Bip_L_Foot','J_Bip_L_ToeBase'))
    src_rest={name:bone_world(source,bone).to_quaternion() for name,bone in source.data.bones.items()}
    dst_rest={name:bone_world(target,bone).to_quaternion() for name,bone in target.data.bones.items()}
    src_hips=bone_world(source,source.data.bones['DEF-hips']).translation
    dst_hips=bone_world(target,target.data.bones['J_Bip_C_Hips']).translation
    height_ratio=dst_hips.z/src_hips.z
    source.animation_data.use_nla=False
    target.animation_data_create();target.animation_data.use_nla=False
    clips={}
    grip_pose={}
    for canonical,source_name in [('idle','Idle_Loop'),('walk','Walk_Loop'),('run','Jog_Fwd_Loop'),('carry','Push_Loop'),('work','Interact'),('grip','Sword_Idle')]:
        action=next((a for a in source_actions if a.name==source_name),None)
        if action is None:action=next(a for a in source_actions if a.name.rsplit('|',1)[-1]==source_name)
        source.animation_data.action=action
        if action.slots:source.animation_data.action_slot=action.slots[0]
        new=bpy.data.actions.new('zhouzhu_'+canonical);target.animation_data.action=new
        start,end=map(float,action.frame_range);frames=max(2,int(round(end-start)))
        for frame in range(frames+1):
            bpy.context.scene.frame_set(int(start+frame));world_rot={}
            for bone in target.data.bones:
                name=bone.name;parent_rotation=world_rot.get(bone.parent.name,target.matrix_world.to_quaternion()) if bone.parent else target.matrix_world.to_quaternion()
                local_rest=bone.matrix_local if not bone.parent else bone.parent.matrix_local.inverted()@bone.matrix_local
                if name in mapping:
                    src=mapping[name];source_pose=(source.matrix_world@source.pose.bones[src].matrix).to_quaternion()
                    desired=align@source_pose@src_rest[src].inverted()@align.inverted()@dst_rest[name]
                    rotation=local_rest.to_quaternion().inverted()@parent_rotation.inverted()@desired
                    target.pose.bones[name].rotation_mode='QUATERNION';target.pose.bones[name].rotation_quaternion=rotation
                    target.pose.bones[name].keyframe_insert('rotation_quaternion',frame=frame,group=name)
                    world_rot[name]=desired
                else:world_rot[name]=parent_rotation@local_rest.to_quaternion()
            # 只保留起伏；模型根节点由现有游戏运动负责，不引入作者动作的根位移。
            movement=align@((source.matrix_world@source.pose.bones['DEF-hips'].matrix).translation-src_hips)*height_ratio
            hip=target.data.bones['J_Bip_C_Hips'];hip_local=hip.parent.matrix_local.inverted()@hip.matrix_local
            target.pose.bones['J_Bip_C_Hips'].location=hip_local.to_quaternion().inverted()@world_rot[hip.parent.name].inverted()@Vector((0,0,movement.z))
            target.pose.bones['J_Bip_C_Hips'].keyframe_insert('location',frame=frame,group='J_Bip_C_Hips')
            if canonical=='grip' and frame==0:
                for name in mapping:
                    if any(token in name for token in ['_Thumb','_Index','_Middle','_Ring','_Little']):
                        q=target.pose.bones[name].rotation_quaternion;grip_pose[name]=[q.x,q.y,q.z,q.w]
        target.animation_data.action=None
        track=target.animation_data.nla_tracks.new();track.name=canonical;strip=track.strips.new(canonical,0,new);strip.action_frame_start=0;strip.action_frame_end=frames
        clips[canonical]=dict(source=action.name,frames=frames,seconds=frames/bpy.context.scene.render.fps)
        print('RETARGET',canonical,frames,len(mapping),flush=True)
    target.animation_data.action=None;return clips,grip_pose

def main():
    ensure_sources()
    for gender,source_name in [('male','Sakurada Fumiriya'),('female','Sendagaya Shino')]:
        bpy.ops.wm.read_factory_settings(use_empty=True);scene=bpy.context.scene;scene.render.fps=24
        rig,meshes,objects=import_asset(SOURCE/(source_name+'.glb'))
        body=next(obj for obj in meshes if 'Body' in obj.name)
        top=subset(body,lambda name:'_Tops_' in name,'WorkTunic')
        keep_body=subset(body,lambda name:all(token not in name for token in ['_Tops_','_Bottoms_','_Accessory_']),'SkinAndShoes')
        # 常规游戏角色优化：去掉长裤内完全不可见的腿/胯表面，避免布料姿态中穿出内层身体。
        covered=bmesh.new();covered.from_mesh(keep_body.data);hip_z=rig.data.bones['J_Bip_C_Hips'].head_local.z
        hidden=[face for face in covered.faces if '_Body_' in keep_body.data.materials[face.material_index].name and all(.13<v.co.z<hip_z+.035 for v in face.verts)]
        bmesh.ops.delete(covered,geom=hidden,context='FACES');bmesh.ops.delete(covered,geom=[v for v in covered.verts if not v.link_faces],context='VERTS');covered.to_mesh(keep_body.data);covered.free()
        if gender=='male':pants=subset(body,lambda name:'_Bottoms_' in name,'WorkTrousers')
        else:
            male_rig,male_meshes,male_objects=import_asset(SOURCE/'Sakurada Fumiriya.glb')
            pants=subset(next(obj for obj in male_meshes if 'Body' in obj.name),lambda name:'_Bottoms_' in name,'WorkTrousers')
            fit_pants(pants,male_rig,rig)
            for obj in male_objects:bpy.data.objects.remove(obj,do_unlink=True)
        chosen=[obj for obj in meshes if obj!=body]+[top,keep_body,pants]
        bpy.data.objects.remove(body,do_unlink=True)
        # 以原骨骼权重适配宽松工作上衣，接缝和UV仍来自作者网格。
        for vertex in top.data.vertices:
            p=vertex.co
            if p.z<1.32:p.x*=1.035;p.y*=1.06
        for obj in chosen:
            obj.name=gender+'_'+obj.name
            if 'Hair' in obj.name and not obj.data.shape_keys:
                bpy.context.view_layer.objects.active=obj;obj.select_set(True)
                modifier=obj.modifiers.new('Hair silhouette LOD0 optimization','DECIMATE');modifier.ratio=.72
                bpy.ops.object.modifier_apply(modifier=modifier.name);obj.select_set(False)
        clean_materials(chosen)
        motion_rig,motion_meshes,motion_objects=import_asset(MOTION)
        motion_actions=list(bpy.data.actions)
        clips,grip_pose=retarget(motion_rig,rig,motion_actions)
        for obj in motion_objects:bpy.data.objects.remove(obj,do_unlink=True)
        # 角色统一站在原地；模型自身高度保持成年体态，再由引擎规范到1.78/1.68米。
        rig.name='Humanoid';rig['source_author']='VRoid Project / pixiv';rig['source_license']='CC0-1.0';rig['adaptation']='Adult civilian workwear; original topology and skin weights retained'
        for image in bpy.data.images:
            if image.has_data and image.name!='Render Result':image.pack()
        bpy.ops.object.select_all(action='DESELECT');rig.select_set(True)
        for obj in chosen:obj.select_set(True)
        bpy.context.view_layer.objects.active=rig
        rig.animation_data.use_nla=True
        for track in rig.animation_data.nla_tracks:track.mute=False
        bpy.ops.export_scene.gltf(filepath=str(OUT/(gender+'.glb')),export_format='GLB',use_selection=True,export_animations=True,export_animation_mode='NLA_TRACKS',export_skins=True,export_all_influences=False,export_morph=True,export_yup=True,export_extras=True)
        rig.animation_data.use_nla=False;bpy.context.scene.frame_set(0)
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(gender+'.blend')))
        points=[obj.matrix_world@v.co for obj in chosen for v in obj.data.vertices]
        bounds=dict(min=[min(p[k] for p in points) for k in range(3)],max=[max(p[k] for p in points) for k in range(3)])
        report=dict(gender=gender,source=source_name,bones=len(rig.data.bones),sourceBoundsZUp=bounds,gripPose=grip_pose,meshes=[dict(name=o.name,vertices=len(o.data.vertices),triangles=sum(len(p.vertices)-2 for p in o.data.polygons),skinned=any(m.type=='ARMATURE' for m in o.modifiers)) for o in chosen],clips=clips)
        (OUT/(gender+'.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        print('PREPARED',gender,json.dumps(report),flush=True)

if __name__=='__main__':main()
