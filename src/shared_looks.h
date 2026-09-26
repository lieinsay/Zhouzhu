#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/color.hpp>

#include "building_parts.h"

// 共用的材质与网格：同一份给一张地图上所有的村子——聚落的各材质（PartMaterial）、简模的顶点色材质、按颜色的素色材质、
// 按名字的网格（村民小人的身子、头、斗笠、家伙，村仓的货箱……调用方保证同名同形）。都是第一次要时建好。
// 材质都是弯曲标准材质（curved_material：远处按真球面弯，没有行星数据的地图与原来的 StandardMaterial3D 一样）。
// 一村几百座房子、几十个人，以前每座 / 每人 new 一份材质和网格；共用之后资源少了、渲染也能按材质合批。
// 由地图宿主持有（World 的成员：放进静态变量的话要等渲染服务器关了才析构，退出时报一串泄漏），
// 挂在下面的聚落与村民问宿主要（GroundHost::host_looks）；宿主不给的（甲乙两岛、平板岛）各自建一份。只在主线程上用。
class SharedLooks {
    std::array<godot::Ref<godot::Material>, size_t(PartMaterial::COUNT)> parts;
    godot::Ref<godot::Material> proxy;
    std::map<uint64_t, godot::Ref<godot::Material>> plain;
    std::map<std::string, godot::Ref<godot::Mesh>> meshes;

public:
    godot::Ref<godot::Material> part_material(PartMaterial p_material);
    godot::Ref<godot::Material> proxy_material(); // 顶点色当反照率（sRGB）
    godot::Ref<godot::Material> plain_material(const godot::Color &p_albedo, float p_roughness = 1.0f);
    // 按名字取网格；没有就调 p_make 建一份存下
    godot::Ref<godot::Mesh> mesh(const std::string &p_key, const std::function<godot::Ref<godot::Mesh>()> &p_make);
};
