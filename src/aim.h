#pragma once

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector3.hpp>

// 准星（切片十一）：屏幕中心那条视线（相机往前的射线；测试可以另给一条）打到的第一样东西。编排每帧打一回，交给挖垒、搭建。
struct AimHit {
    bool hit = false;
    godot::Vector3 from; // 射线起点（相机）
    godot::Vector3 point; // 打到的那一点（全局）
    godot::Vector3 normal;
    godot::Object *collider = nullptr;
};
