#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include "dock_site.h"

// 码头的 Godot 一侧：栈桥 + 系缆桩 + 碰撞的搭法，泊位接触线与出生位姿。Island（游戏尺度的岛）与 WorldDock（岛群）共用。
// 几何在 DockPlan 所在的坐标系里（Island 是岛的局部坐标，WorldDock 是以岸缘点为原点的局部坐标），调用方自己乘上节点变换。
namespace dock_builder {

constexpr double PIER_WIDTH = 3.0;
constexpr double PIER_LIFT = 0.1; // 栈桥面比停机坪高这么多，免得与地面共面闪烁
constexpr double PIER_INLAND = 4.0; // 栈桥根埋进岸里多少
constexpr double PIER_THICKNESS = 0.5;
constexpr double BERTH_GAP = 0.15; // 船舷与栈桥头之间的缝
constexpr double SPAWN_INLAND = 12.0; // 出生点在岸缘内多远

godot::Vector3 point(const DockPlan &p_plan, double p_along_out, double p_up);
// 泊位接触线：X 指向栈桥（岛内），Z 顺着岸（见 Island::get_berth_contact）
godot::Transform3D berth_contact(const DockPlan &p_plan);
godot::Vector3 pier_end(const DockPlan &p_plan);
// 停机坪上、面朝栈桥的出生位姿；y 取坪高（调用方可再按地面修正）
godot::Transform3D spawn(const DockPlan &p_plan);

// 往 p_parent（须是 CollisionObject3D 才有碰撞）下挂栈桥、碰撞与两根系缆桩；p_adopt 给每个新节点起名、打标记、add_child
using Adopt = void (*)(godot::Node3D *p_parent, godot::Node *p_node, const char *p_name);
void build_pier(godot::Node3D *p_parent, const DockPlan &p_plan, Adopt p_adopt);

} // namespace dock_builder
