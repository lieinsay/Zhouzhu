#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>

// GDScript验收样件与C++共用的正式材质入口；不生成或修改世界数据。
class ArtSurface : public godot::RefCounted {
    GDCLASS(ArtSurface, godot::RefCounted)
protected:
    static void _bind_methods();
public:
    static godot::Ref<godot::Material> create(const godot::String &p_asset, const godot::Color &p_color, double p_roughness, double p_metallic);
    static bool configure(const godot::Ref<godot::ShaderMaterial> &p_material, const godot::String &p_prefix, const godot::String &p_asset);
    static godot::Ref<godot::Material> part_material(int p_index);
    static godot::Ref<godot::Mesh> scatter_mesh(int p_look,int p_lod);
    static godot::Node3D *model_instance(const godot::String &p_id);
};
