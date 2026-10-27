#include "scene_character.h"
#include "art_mesh.h"
#include "art_mesh_kit.h"
#include "curved_material.h"
#include "item_art.h"
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
}
void SceneCharacter::configure(int p_variant,int p_job,bool p_hat){variant=((p_variant%4)+4)%4;job=std::clamp(p_job,0,4);hat=p_hat;if(is_inside_tree()&&looks)build();}
void SceneCharacter::_ready(){if(!looks){own_looks=std::make_unique<SharedLooks>();looks=own_looks.get();}build();}
void SceneCharacter::build(){
    const auto children=get_children();for(int i=0;i<children.size();++i){auto *child=Object::cast_to<Node>(children[i]);remove_child(child);child->queue_free();}
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
    if(hat){const auto straw=looks->surface_material("material.straw",Color(.78,.68,.42));mesh(this,"Hat",cylinder(*looks,.025,.34,.16,straw),Vector3(0,1.75,0));}
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
    phase+=std::max(0.0,delta)*(4.0+std::min(4.0,speed));const double stride=std::clamp(speed/3.0,0.0,1.0);
    for(int i=0;i<2;++i){if(!legs[i]||!arms[i])continue;const double sign=i?1:-1;legs[i]->set_rotation(Vector3(real_t(sign*std::sin(phase)*.42*stride),0,0));
        if(job==1){arms[i]->set_rotation(Vector3(0,0,real_t(sign)));forearms[i]->set_rotation(Vector3(0,0,real_t(sign*1.2)));}
        else{const double base=(job==0||job==2)?-.27:-.05;arms[i]->set_rotation(Vector3(real_t(base-sign*std::sin(phase)*.32*stride+(working&&i==0?tool_angle:0)),0,real_t(sign*.05)));forearms[i]->set_rotation(Vector3(-.07f,0,0));}
    }
}
Dictionary SceneCharacter::get_visual_state()const{Dictionary out;out["variant"]=variant;out["job"]=job;out["adult"]=true;out["face_loaded"]=face_loaded;out["face_path"]=face_path;out["height_without_hat"]=1.78;return out;}
