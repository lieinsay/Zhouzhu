#include "scene_character.h"
#include "art_mesh.h"
#include "art_mesh_kit.h"
#include "curved_material.h"
#include "item_art.h"
#include "model_art.h"
#include "art_surface.h"
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <cmath>
using namespace godot;
namespace {
MeshInstance3D *mesh(Node3D *parent,const String &name,const Ref<Mesh> &shape,const Vector3 &at){auto *node=memnew(MeshInstance3D);node->set_name(name);node->set_mesh(shape);node->set_position(at);node->set_visibility_range_end(80);parent->add_child(node);return node;}
Node3D *pivot(Node3D *parent,const String &name,const Vector3 &at){auto *node=memnew(Node3D);node->set_name(name);node->set_position(at);parent->add_child(node);return node;}
Ref<Mesh> capsule(SharedLooks &looks,double radius,double height,const Ref<Material> &material){
    const std::string key="actor_capsule_"+std::to_string(radius)+"_"+std::to_string(height)+"_"+std::to_string(uint64_t(material->get_instance_id()));
    return looks.mesh(key,[&](){Ref<CapsuleMesh> part;part.instantiate();part->set_radius(radius);part->set_height(height);part->set_radial_segments(16);part->set_rings(6);part->set_material(material);return Ref<Mesh>(part);});}
Ref<Mesh> cylinder(SharedLooks &looks,double top,double bottom,double height,const Ref<Material> &material){
    const std::string key="actor_cylinder_"+std::to_string(top)+"_"+std::to_string(bottom)+"_"+std::to_string(height)+"_"+std::to_string(uint64_t(material->get_instance_id()));
    return looks.mesh(key,[&](){Ref<CylinderMesh> part;part.instantiate();part->set_top_radius(top);part->set_bottom_radius(bottom);part->set_height(height);part->set_radial_segments(24);part->set_material(material);return Ref<Mesh>(part);});}
Ref<Mesh> sphere(SharedLooks &looks,double radius,double height,const Ref<Material> &material){
    const std::string key="actor_sphere_"+std::to_string(radius)+"_"+std::to_string(height)+"_"+std::to_string(uint64_t(material->get_instance_id()));
    return looks.mesh(key,[&](){Ref<SphereMesh> part;part.instantiate();part->set_radius(radius);part->set_height(height);part->set_radial_segments(24);part->set_rings(12);part->set_material(material);return Ref<Mesh>(part);});}
Ref<Mesh> box(SharedLooks &looks,const Vector3 &size,const Ref<Material> &material){
    const std::string key="actor_box_"+std::to_string(size.x)+"_"+std::to_string(size.y)+"_"+std::to_string(size.z)+"_"+std::to_string(uint64_t(material->get_instance_id()));
    return looks.mesh(key,[&](){BoxPart p;p.sx=size.x;p.sy=size.y;p.sz=size.z;p.shape=VisualShape::BEVEL_BOX;return Ref<Mesh>(art_mesh::shape(p,material));});}
double jaw(double y){return y<0?.72+.28*std::clamp((y+.1275)/.1275,0.0,1.0):1.0;}
Ref<Mesh> head(SharedLooks &looks,const Ref<Material> &skin){
    return looks.mesh("actor_head_"+std::to_string(uint64_t(skin->get_instance_id())),[&](){MeshPart part;art_mesh_kit::ellipsoid(part,mesh_kit::Xform(0,0,{}),{.19,.255,.19});
        for(int i=0;i<part.vertex_count();++i){part.positions[size_t(i)*3]*=float(jaw(part.positions[size_t(i)*3+1]));}
        return Ref<Mesh>(art_mesh::from_part(part,skin));});}
Ref<Mesh> hand(SharedLooks &looks,const Ref<Material> &skin){
    return looks.mesh("actor_hand_"+std::to_string(uint64_t(skin->get_instance_id())),[&](){MeshPart part;
        art_mesh_kit::ellipsoid(part,mesh_kit::Xform(0,0,{}),{.055,.07,.026});
        for(int i=0;i<4;++i)art_mesh_kit::ellipsoid(part,mesh_kit::Xform(0,0,{-.021+i*.014,-.040-std::sin((i+1)*.6)*.003,.001}),{.012,.045,.014});
        art_mesh_kit::ellipsoid(part,mesh_kit::Xform(0,-.45,{.031,-.005,.004}),{.018,.045,.018});
        return Ref<Mesh>(art_mesh::from_part(part,skin));});}
Ref<Mesh> tunic(SharedLooks &looks,const Ref<Material> &cloth){
    return looks.mesh("actor_tunic_"+std::to_string(uint64_t(cloth->get_instance_id())),[&](){MeshPart part;
        art_mesh_kit::lathe(part,mesh_kit::Xform(0,0,{}),{.4,.48,.4},{{0,0},{.475,0},{.438,.167},{.50,.646},{.45,.917},{.1625,1},{0,1}});
        for(int i=0;i<part.vertex_count();++i){part.positions[size_t(i)*3+2]*=.62f;part.normals[size_t(i)*3+2]/=.62f;const size_t at=size_t(i)*3;const auto n=Vector3(part.normals[at],part.normals[at+1],part.normals[at+2]).normalized();part.normals[at]=n.x;part.normals[at+1]=n.y;part.normals[at+2]=n.z;}
        return Ref<Mesh>(art_mesh::from_part(part,cloth));});}
Ref<Mesh> fringe(SharedLooks &looks,const Ref<Material> &hair){
    return looks.mesh("actor_fringe_"+std::to_string(uint64_t(hair->get_instance_id())),[&](){MeshPart part;
        art_mesh_kit::ellipsoid(part,mesh_kit::Xform(0,0,{}),{.046,.105,.018});
        for(int i=0;i<part.vertex_count();++i){const size_t at=size_t(i)*3;const float y=part.positions[at+1];if(y<0)part.positions[at]*=std::max(.05f,1+y/.0525f);}
        return Ref<Mesh>(art_mesh::from_part(part,hair));});}
}
void SceneCharacter::_bind_methods(){
    ClassDB::bind_method(D_METHOD("configure","variant","job","hat"),&SceneCharacter::configure);
    ClassDB::bind_method(D_METHOD("animate","delta","speed","tool_angle","working"),&SceneCharacter::animate,DEFVAL(0.0),DEFVAL(false));
    ClassDB::bind_method(D_METHOD("get_visual_state"),&SceneCharacter::get_visual_state);
    ClassDB::bind_method(D_METHOD("set_tool_frame","frame"),&SceneCharacter::set_tool_frame);
}
void SceneCharacter::configure(int p_variant,int p_job,bool p_hat){variant=((p_variant%4)+4)%4;job=std::clamp(p_job,0,4);hat=p_hat;if(is_inside_tree()&&looks)build();}
void SceneCharacter::_ready(){if(!looks){own_looks=std::make_unique<SharedLooks>();looks=own_looks.get();}build();}
void SceneCharacter::build(){
    const auto children=get_children();for(int i=0;i<children.size();++i){auto *child=Object::cast_to<Node>(children[i]);remove_child(child);child->queue_free();}
    skeleton=nullptr;animation=nullptr;imported_model=nullptr;skinned_meshes=0;skinned_clothing=0;textured_surfaces=0;face_loaded=false;active_animation=String();
    if(build_imported())return;
    const bool female=variant>=2,mature=(variant&1)!=0;
    const Color jobs[]={Color(.52,.40,.26),Color(.33,.40,.52),Color(.55,.20,.17),Color(.36,.50,.44),Color(.7,.24,.19)};
    const auto cloth=looks->surface_material("material.hemp",jobs[job],.96f);
    const auto lining=looks->surface_material("material.hemp",jobs[job].lightened(.13f),1.0f);
    const auto pants=looks->surface_material("material.hemp",Color(.24,.25,.26),.98f);
    const auto skin=looks->plain_material(mature?Color(.82,.65,.51):Color(.88,.72,.59),.8f);
    const auto leather=looks->surface_material("material.leather",Color(.25,.17,.12),.86f);
    const auto hair=looks->surface_material("material.hair",mature?Color(.26,.24,.23):Color(.11,.075,.055),.9f);
    auto *torso=mesh(this,"Tunic",tunic(*looks,cloth),Vector3(0,1.17,0));torso->set_scale(Vector3(female?1.0f:1.1f,1,1));
    mesh(this,"Belt",cylinder(*looks,.178,.178,.065,leather),Vector3(0,.92,0))->set_scale(Vector3(1,1,.72));
    mesh(this,"Waist",box(*looks,Vector3(.32,.13,.20),pants),Vector3(0,.875,0));
    for(int side=0;side<2;++side){const double sign=side?1:-1;
        legs[side]=pivot(this,side?"LeftLeg":"RightLeg",Vector3(real_t(sign*.105),.88f,0));
        mesh(legs[side],"Trousers",cylinder(*looks,.080,.052,.80,pants),Vector3(0,-.40,0));
        mesh(legs[side],"Shoe",box(*looks,Vector3(.13,.09,.25),leather),Vector3(0,-.835,.045));
        arms[side]=pivot(this,side?"LeftArm":"RightArm",Vector3(real_t(sign*(female?.205:.22)),1.36f,0));
        mesh(arms[side],"Sleeve",cylinder(*looks,.078,.065,.30,cloth),Vector3(0,-.15,0));
        mesh(arms[side],"Shoulder",sphere(*looks,.078,.10,cloth),Vector3(0,-.015,0));
        forearms[side]=pivot(arms[side],"Forearm",Vector3(0,-.29,0));
        mesh(forearms[side],"Skin",capsule(*looks,.041,.25,skin),Vector3(0,-.125,0));
        auto *palm=mesh(forearms[side],"Hand",hand(*looks,skin),Vector3(0,-.27,.004));if(side==1)palm->set_scale(Vector3(-1,1,1));
    }
    mesh(this,"Neck",cylinder(*looks,.053,.057,.11,skin),Vector3(0,1.443,0));
    mesh(this,"Head",head(*looks,skin),Vector3(0,1.585,0));
    for(double side:{-1.0,1.0})mesh(this,side<0?"EarR":"EarL",sphere(*looks,.018,.050,skin),Vector3(real_t(side*.096),1.587,-.003));
    auto *nose=mesh(this,"Nose",sphere(*looks,.012,.028,skin),Vector3(0,1.568,.095));nose->set_scale(Vector3(.65,1,.7));
    auto *cap=mesh(this,"HairCap",sphere(*looks,.103,.18,hair),Vector3(0,1.64,-.017));cap->set_scale(Vector3(1,1,.95));
    mesh(this,"HairTie",sphere(*looks,.047,mature?.065:.085,hair),Vector3(0,1.71,-.102));
    for(int i=0;i<5;++i){auto *lock=mesh(this,String("Fringe")+String::num_int64(i),fringe(*looks,hair),Vector3(-.067f+i*.0335f,1.68f,.060f));lock->set_rotation(Vector3(-.4f,0,(i-2.f)*.18f));}
    if(female)for(double side:{-1.0,1.0}){auto *lock=mesh(this,side<0?"SideLockR":"SideLockL",fringe(*looks,hair),Vector3(real_t(side*.09),1.61f,-.01f));lock->set_scale(Vector3(.8,1.6,1.5));}
    for(double side:{-1.0,1.0}){auto *collar=mesh(this,side<0?"CollarR":"CollarL",box(*looks,Vector3(.035,.26,.024),lining),Vector3(real_t(side*.052),1.30f,.118f));collar->set_rotation(Vector3(0,0,real_t(side*.40)));}
    if(hat){const auto straw=looks->surface_material(job==2?"material.hemp":"material.straw",job==2?jobs[2]:Color(.78,.68,.42));mesh(this,"Hat",cylinder(*looks,job==2?.15:.025,job==2?.19:.34,job==2?.14:.16,straw),Vector3(0,1.75,0));}
    const char *faces[]={"face_young_male","face_mature_male","face_young_female","face_mature_female"};
    const String asset=String("character.")+String(faces[variant]);ItemArt reader;const Ref<Texture2D> texture=reader.texture_asset(std::string(asset.utf8().get_data()));face_loaded=texture.is_valid();face_path=face_loaded?texture->get_path():String();
    if(face_loaded){const std::string key="actor_face_"+std::string(faces[variant]);
        const Ref<Mesh> face=looks->mesh(key,[&](){MeshPart part;
            auto point=[](int x,int y){const double u=x/10.0,v=y/10.0;const double px=(u-.5)*.17,py=(.5-v)*.215;const double rx=.095*jaw(py);const double z=.095*std::sqrt(std::max(0.0,1-px*px/(rx*rx)-py*py/(.1275*.1275)))+.002;return mesh_kit::V3{px,py,z};};
            for(int y=0;y<10;++y)for(int x=0;x<10;++x){const int first=part.vertex_count();mesh_kit::polygon(part,{point(x,y),point(x+1,y),point(x+1,y+1),point(x,y+1)},{0,0,1},nullptr);
                for(int i=first;i<part.vertex_count();++i){const size_t at=size_t(i)*3;const double x=part.positions[at],y=part.positions[at+1],z=part.positions[at+2]-.002,rx=.095*jaw(y);const auto n=Vector3(real_t(x/(rx*rx)),real_t(y/(.1275*.1275)),real_t(z/(.095*.095))).normalized();part.normals[at]=float(n.x);part.normals[at+1]=float(n.y);part.normals[at+2]=float(n.z);part.uvs[size_t(i)*2]=float(x/.17+.5);part.uvs[size_t(i)*2+1]=float(.5-y/.215);}}
            Ref<ShaderMaterial> material;material.instantiate();material->set_shader(ResourceLoader::get_singleton()->load("res://shaders/character_face.gdshader"));material->set_shader_parameter("face_tex",texture);material->set_meta("art_asset",asset);
            return Ref<Mesh>(art_mesh::from_part(part,material));});
        auto *decal=mesh(this,"FaceFeatures",face,Vector3(0,1.585,0));decal->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    }
    animate(0,0);
}
void SceneCharacter::animate(double delta,double speed,double tool_angle,bool working){
    if(imported_model&&skeleton&&animation){
        const String clip=working?String("work"):(speed>2.5?String("run"):(speed>.08?String("walk"):String("idle")));
        if(active_animation!=clip&&animation->has_animation(clip)){animation->play(clip,active_animation.is_empty()?0.0:.16);active_animation=clip;}
        animation->set_speed_scale(speed>.08?real_t(std::clamp(speed/(clip==String("run")?3.6:1.3),.5,2.4)):1.0f);
        animation->advance(std::max(.000001,delta));skeleton->force_update_all_bone_transforms();phase+=std::max(0.0,delta);
        right_grip_error=left_grip_error=0;
        const float height_shift=variant>=2?-.10f:0.0f;
        if(job==0){const Transform3D frame=has_tool_frame?tool_frame:Transform3D(Basis(Vector3(1,0,0),real_t(tool_angle)),Vector3(0,1.12f+height_shift,.05f));
            arm_ik(false,frame.xform(Vector3(0,-.04f,.20f)));arm_ik(true,frame.xform(Vector3(0,.16f,.20f)));}
        else if(job==1){const Transform3D frame=has_tool_frame?tool_frame:carrier_support_frame();arm_ik(false,frame.xform(Vector3(-.38f,0,0)));arm_ik(true,frame.xform(Vector3(.38f,0,0)));}
        else if(job==2){const Transform3D frame=has_tool_frame?tool_frame:Transform3D(Basis(),Vector3(-.30f,1.2f,.05f));arm_ik(false,frame.xform(Vector3(0,-.08f,.12f)));}
        else if(carrying){arm_ik(false,Vector3(.40f,1.05f,-.40f));arm_ik(true,Vector3(-.40f,1.05f,-.40f));}
        if(job<3||carrying){const Array keys=grip_pose.keys();for(int i=0;i<keys.size();++i){const String name=keys[i];const int bone=skeleton->find_bone(name);const Array values=grip_pose[name];if(bone<0||values.size()!=4)continue;
            Quaternion q(values[0],values[1],values[2],values[3]);
            if(name.begins_with("J_Bip_L_")){const String right=name.replace("_L_","_R_");const int counterpart=skeleton->find_bone(right);const Array r=grip_pose.get(right,Array());if(counterpart>=0&&r.size()==4){const Quaternion rest_r=skeleton->get_bone_global_rest(counterpart).basis.orthonormalized().get_quaternion(),rest_l=skeleton->get_bone_global_rest(bone).basis.orthonormalized().get_quaternion();const Quaternion world=rest_r*Quaternion(r[0],r[1],r[2],r[3]).normalized()*rest_r.inverse();q=rest_l.inverse()*Quaternion(world.x,-world.y,-world.z,world.w)*rest_l;}}
            skeleton->set_bone_pose_rotation(bone,q.normalized());}}
        skeleton->force_update_all_bone_transforms();
        return;
    }
    phase+=std::max(0.0,delta)*(4.0+std::min(4.0,speed));const double stride=std::clamp(speed/3.0,0.0,1.0);
    for(int i=0;i<2;++i){if(!legs[i]||!arms[i])continue;const double sign=i?1:-1;legs[i]->set_rotation(Vector3(real_t(sign*std::sin(phase)*.42*stride),0,0));
        if(job==1){arms[i]->set_rotation(Vector3(0,0,real_t(sign)));forearms[i]->set_rotation(Vector3(0,0,real_t(sign*1.2)));}
        else{const double base=(job==0||job==2)?-.27:-.05;arms[i]->set_rotation(Vector3(real_t(base-sign*std::sin(phase)*.32*stride+(working&&i==0?tool_angle:0)),0,real_t(sign*.05)));forearms[i]->set_rotation(Vector3(-.07f,0,0));}
    }
}
Dictionary SceneCharacter::get_visual_state()const{Dictionary out;out["variant"]=variant;out["job"]=job;out["adult"]=true;out["face_loaded"]=face_loaded;out["face_path"]=face_path;out["height_without_hat"]=variant>=2?1.68:1.78;out["imported_model"]=imported_model!=nullptr;out["model_id"]=model_id;out["model_path"]=model_path;out["bones"]=skeleton?skeleton->get_bone_count():0;out["skinned_meshes"]=skinned_meshes;out["skinned_clothing"]=skinned_clothing;out["animation"]=active_animation;out["right_grip_error"]=right_grip_error;out["left_grip_error"]=left_grip_error;
    out["textured_surfaces"]=textured_surfaces;if(skeleton){const int head=skeleton->find_bone("J_Bip_C_Head");if(head>=0)out["head_height"]=to_local(skeleton->to_global(skeleton->get_bone_global_pose(head).origin)).y;}
    return out;}

bool SceneCharacter::build_imported(){
    const char *ids[]={"actor.young_male","actor.mature_male","actor.young_female","actor.mature_female"};model_id=ids[variant];
    imported_model=model_art::instantiate(model_id);if(!imported_model)return false;
    const Dictionary entry=model_art::binding(model_id);model_path=entry.get("path",String());
    grip_pose=entry.get("gripPose",Dictionary());
    const double scale=double(entry.get("scale",1.0));if(!std::isfinite(scale)||scale<=0||scale>4){memdelete(imported_model);imported_model=nullptr;return false;}
    imported_model->set_name("AuthoredModel");imported_model->set_scale(Vector3(real_t(scale),real_t(scale),real_t(scale)));
    // 原作者模型朝-Z；村民行为朝+Z，玩家Body的原转向和货箱约定朝-Z。
    if(job!=4)imported_model->set_rotation(Vector3(0,real_t(3.141592653589793),0));add_child(imported_model);
    prepare_imported(imported_model);
    if(!skeleton||!animation||skinned_meshes==0){remove_child(imported_model);imported_model->queue_free();imported_model=nullptr;return false;}
    animation->set_callback_mode_process(AnimationMixer::ANIMATION_CALLBACK_MODE_PROCESS_MANUAL);
    const PackedStringArray clips=animation->get_animation_list();for(int i=0;i<clips.size();++i){const Ref<Animation> clip=animation->get_animation(clips[i]);if(clip.is_valid()&&(clips[i]==String("idle")||clips[i]==String("walk")||clips[i]==String("run")||clips[i]==String("work")||clips[i]==String("carry")))clip->set_loop_mode(Animation::LOOP_LINEAR);}
    face_loaded=true;face_path=model_path;animate(0,0);
    if(hat){const int head=skeleton->find_bone("J_Bip_C_Head");if(head>=0){auto *attachment=memnew(BoneAttachment3D);attachment->set_name("HeadAccessory");attachment->set_bone_idx(head);skeleton->add_child(attachment);
        const auto material=looks->surface_material(job==2?"material.hemp":"material.straw",job==2?Color(.55,.20,.17):Color(.78,.68,.42));auto *cap=mesh(attachment,"Hat",cylinder(*looks,job==2?.15:.025,job==2?.19:.34,job==2?.14:.16,material),Vector3());
        const Transform3D head_world=skeleton->get_global_transform()*skeleton->get_bone_global_pose(head);const Transform3D desired=get_global_transform()*Transform3D(Basis(),Vector3(0,variant>=2?1.66f:1.76f,0));cap->set_transform(head_world.affine_inverse()*desired);}}
    return true;
}
Transform3D SceneCharacter::carrier_support_frame() const{
    if(!skeleton)return Transform3D(Basis(),Vector3(0,variant>=2?1.22f:1.32f,0));
    const int l=skeleton->find_bone("J_Bip_L_UpperArm"),r=skeleton->find_bone("J_Bip_R_UpperArm");
    if(l<0||r<0)return Transform3D(Basis(),Vector3(0,variant>=2?1.22f:1.32f,0));
    const Vector3 a=to_local(skeleton->to_global(skeleton->get_bone_global_pose(l).origin)),b=to_local(skeleton->to_global(skeleton->get_bone_global_pose(r).origin));
    const Vector3 x=(a-b).normalized(),z=x.cross(Vector3(0,1,0)).normalized(),y=z.cross(x).normalized();
    // 骨关节在衣物内部：肩面承托留5.5cm，扁担后移到颈后，避免原1.32m横穿上胸。
    return Transform3D(Basis(x,y,z),(a+b)*.5f+y*.055f-z*.06f);
}
void SceneCharacter::prepare_imported(Node *node){
    if(auto *rig=Object::cast_to<Skeleton3D>(node))skeleton=rig;
    if(auto *player=Object::cast_to<AnimationPlayer>(node))animation=player;
    if(auto *instance=Object::cast_to<MeshInstance3D>(node)){
        if(instance->get_skin().is_valid())++skinned_meshes;
        instance->set_visibility_range_end(80);
        const Ref<Mesh> mesh=instance->get_mesh();if(mesh.is_valid())for(int surface=0;surface<mesh->get_surface_count();++surface){
            const Ref<StandardMaterial3D> original=instance->get_active_material(surface);if(original.is_null())continue;
            if(original->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_valid())++textured_surfaces;
            const String name=original->get_name();const int role=name.begins_with("ZZ_top_")?2:name.begins_with("ZZ_pants_")?3:name.begins_with("ZZ_shoes_")?4:name.begins_with("ZZ_hair_")?1:0;
            if((role==2||role==3)&&instance->get_skin().is_valid())++skinned_clothing;
            const std::string key="actor_"+std::to_string(uint64_t(original->get_instance_id()))+"_"+std::to_string(job)+"_"+std::to_string(variant);
            const Ref<Material> material=looks->imported_material(key,[&](){
                Ref<ShaderMaterial> created;created.instantiate();created->set_shader(ResourceLoader::get_singleton()->load("res://shaders/model_surface.gdshader"));
                const Ref<Texture2D> texture=original->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);created->set_shader_parameter("model_has_texture",texture.is_valid());if(texture.is_valid())created->set_shader_parameter("model_tex",texture);
                const Color jobs[]={Color(.52,.40,.26),Color(.33,.40,.52),Color(.55,.20,.17),Color(.36,.50,.44),Color(.7,.24,.19)};
                Color tint=original->get_albedo();if(role==2)tint=jobs[job];else if(role==3)tint=Color(.20,.22,.23);else if(role==4)tint=Color(.24,.16,.11);else if(role==1)tint=Color(.25,.19,.15);else if(name.begins_with("ZZ_skin_"))tint=(variant&1)?Color(.78,.67,.55):Color(.86,.75,.65);
                created->set_shader_parameter("model_tint",tint);created->set_shader_parameter("model_role",role);created->set_shader_parameter("model_mature",(variant&1)!=0);
                created->set_shader_parameter("model_iris",name.contains("EyeIris"));
                if(role==2||role==3)ArtSurface::configure(created,"model_detail","material.hemp");if(role==4)ArtSurface::configure(created,"model_detail","material.leather");
                created->set_meta("art_model_id",model_id);return Ref<Material>(created);});
            instance->set_surface_override_material(surface,material);
        }
    }
    const auto children=node->get_children();for(int i=0;i<children.size();++i)prepare_imported(Object::cast_to<Node>(children[i]));
}
void SceneCharacter::point_bone(int bone,const Vector3 &target,int tip){
    if(bone<0||tip<0)return;const Transform3D current=skeleton->get_bone_global_pose(bone);const Vector3 from=skeleton->get_bone_global_pose(tip).origin-current.origin,to=target-current.origin;if(from.length_squared()<1e-8f||to.length_squared()<1e-8f)return;
    const Quaternion delta(from.normalized(),to.normalized());const int parent=skeleton->get_bone_parent(bone);const Basis parent_basis=parent>=0?skeleton->get_bone_global_pose(parent).basis:Basis();
    const Basis pose=skeleton->get_bone_rest(bone).basis.inverse()*parent_basis.inverse()*Basis(delta)*current.basis;
    skeleton->set_bone_pose_rotation(bone,pose.orthonormalized().get_quaternion());skeleton->force_update_all_bone_transforms();
}
void SceneCharacter::arm_ik(bool left,const Vector3 &target){
    const String prefix=left?String("J_Bip_L_"):String("J_Bip_R_");const int upper=skeleton->find_bone(prefix+String("UpperArm")),lower=skeleton->find_bone(prefix+String("LowerArm")),hand=skeleton->find_bone(prefix+String("Hand"));if(upper<0||lower<0||hand<0)return;
    const Vector3 wanted=skeleton->to_local(to_global(target));const Vector3 a=skeleton->get_bone_global_pose(upper).origin,b=skeleton->get_bone_global_pose(lower).origin,c=skeleton->get_bone_global_pose(hand).origin;
    const double l1=a.distance_to(b),l2=b.distance_to(c);const Vector3 direction=(wanted-a).normalized();const double distance=std::clamp(double(a.distance_to(wanted)),.02,l1+l2-.002);
    const Vector3 outward=Vector3(real_t((left?1:-1)*(job==4?-1:1)),-.85f,-.15f);Vector3 hint=skeleton->get_global_transform().basis.inverse().xform(get_global_transform().basis.xform(outward));hint-=direction*direction.dot(hint);if(hint.length_squared()<1e-8f)hint=Vector3(0,0,1);hint.normalize();
    const double along=(l1*l1+distance*distance-l2*l2)/(2*distance);const Vector3 elbow=a+direction*real_t(along)+hint*real_t(std::sqrt(std::max(0.0,l1*l1-along*along)));
    point_bone(upper,elbow,lower);point_bone(lower,wanted,hand);
    const int middle=skeleton->find_bone(prefix+String("Middle1"));
    for(int i=0;i<3&&middle>=0;++i){const Vector3 wrist=skeleton->get_bone_global_pose(hand).origin,palm=wrist.lerp(skeleton->get_bone_global_pose(middle).origin,.65f);const Vector3 wrist_goal=wrist+wanted-palm;
        // 以掌心而非手腕贴住工具；修正不会移动游戏工具或改变人物状态。
        point_bone(upper,skeleton->get_bone_global_pose(lower).origin+(wanted-palm)*.45f,lower);point_bone(lower,wrist_goal,hand);}
    const Vector3 wrist=skeleton->get_bone_global_pose(hand).origin,palm=middle>=0?wrist.lerp(skeleton->get_bone_global_pose(middle).origin,.65f):wrist;
    const double error=skeleton->to_global(palm).distance_to(to_global(target));if(left)left_grip_error=error;else right_grip_error=error;
}
