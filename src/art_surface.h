#pragma once
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>

// GDScript验收样件与C++共用的正式材质入口；不生成或修改世界数据。
class ArtSurface : public godot::RefCounted {
    GDCLASS(ArtSurface, godot::RefCounted)
protected:
    static void _bind_methods();
public:
    static godot::Ref<godot::Material> create(const godot::String &p_asset, const godot::Color &p_color, double p_roughness, double p_metallic);
};
