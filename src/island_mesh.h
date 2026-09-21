#pragma once

#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "island_field.h"

// IslandField → 顶点数组。只做数据转换：顶面（平滑法线、按坡度与高度上色）、崖壁、倒锥岛底，
// 外加同一批三角面的碰撞用展开表。绕序按 Godot 的约定（正面顺时针）。
struct IslandMeshData {
    godot::PackedVector3Array vertices;
    godot::PackedVector3Array normals;
    godot::PackedColorArray colors;
    godot::PackedInt32Array indices;
    godot::PackedVector3Array collision_faces; // 每三个点一个三角面
};

IslandMeshData build_island_mesh(const IslandField &p_field);
