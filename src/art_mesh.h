#pragma once
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include "building_mesh.h"

namespace art_mesh {
godot::Ref<godot::ArrayMesh> from_part(const MeshPart &p_part, const godot::Ref<godot::Material> &p_material);
godot::Ref<godot::ArrayMesh> box(const godot::Vector3 &p_size, const godot::Ref<godot::Material> &p_material);
godot::Ref<godot::ArrayMesh> shape(const BoxPart &p_part,const godot::Ref<godot::Material> &p_material);
}
