#pragma once
#include <memory>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include "shared_looks.h"
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/animation_player.hpp>

// 成年人物的纯视觉组件，四套外观仅为稳定的美术变体，不改PersonState、工作或存档。
class SceneCharacter:public godot::Node3D {
    GDCLASS(SceneCharacter,godot::Node3D)
    int variant=0,job=0;bool hat=false,face_loaded=false;
    double phase=0;
    godot::String face_path;
    SharedLooks *looks=nullptr;
    std::unique_ptr<SharedLooks> own_looks;
    godot::Node3D *arms[2]={nullptr,nullptr},*forearms[2]={nullptr,nullptr},*legs[2]={nullptr,nullptr};
    void build();
    bool build_imported();
    void prepare_imported(godot::Node *p_node);
    godot::Skeleton3D *skeleton=nullptr;
    godot::AnimationPlayer *animation=nullptr;
    godot::Node3D *imported_model=nullptr;
    godot::String model_id,model_path,active_animation;
    int skinned_meshes=0,skinned_clothing=0,textured_surfaces=0;
    godot::Dictionary grip_pose;
    bool carrying=false;
    double right_grip_error=0,left_grip_error=0;
    void point_bone(int p_bone,const godot::Vector3 &p_target,int p_tip);
    void arm_ik(bool p_left,const godot::Vector3 &p_target);
protected:
    static void _bind_methods();
public:
    void set_shared_looks(SharedLooks *p_looks){looks=p_looks;}
    void configure(int p_variant,int p_job,bool p_hat);
    void animate(double p_delta,double p_speed,double p_tool_angle=0,bool p_working=false);
    void set_carrying(bool p_value){carrying=p_value;}
    bool uses_imported_model()const{return imported_model!=nullptr;}
    godot::Dictionary get_visual_state() const;
    void _ready() override;
};
