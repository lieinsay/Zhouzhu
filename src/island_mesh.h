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

// 河：沿中线铺一条水面带，出了岸缘接一道跌进云海的瀑布（越往下越透明，顶点色的 alpha）。没有河则返回空表。
// 只是看的，不参与碰撞：水浅，人直接蹚过去。
struct RiverMeshData {
    godot::PackedVector3Array vertices;
    godot::PackedVector3Array normals;
    godot::PackedColorArray colors;
    godot::PackedInt32Array indices;
};

RiverMeshData build_river_mesh(const IslandField &p_field);
