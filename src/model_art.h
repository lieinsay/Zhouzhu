#pragma once
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include "shared_looks.h"

// 独立三维美术绑定；仅经Godot资源API读取，可用于PCK。不读取内容工坊的玩法草稿。
namespace model_art {
godot::Dictionary binding(const godot::String &p_id);
godot::Node3D *instantiate(const godot::String &p_id);
godot::Node3D *instantiate_styled(const godot::String &p_id,SharedLooks &p_looks);
godot::Node3D *furniture(const std::string &p_kind,SharedLooks &p_looks);
}
