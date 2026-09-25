#pragma once

#include <map>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "deposit_kinds.h"

// 散布物的低模：程序拼出来的几何，顶点色上色、面法线（平涂的低多边形），大小按 1 倍（ScatterItem::scale 再缩放）。
// 原点在根上：树、灌丛、苇从地面长起；石块、土坡的底略在地下（摆放时再往下埋一点）。
// 实例色（MultiMesh 的 color）乘在顶点色上：树冠深浅、矿石的矿色、结壳是硫磺还是鸟粪都靠它。
// 一套网格由摆放的节点持有（第一次要时建好）：别放进静态变量——静态的要等到引擎的渲染服务器关了才析构，退出时报一串泄漏。
class ScatterMeshes {
    std::map<int, godot::Ref<godot::ArrayMesh>> up, felled;
    godot::Ref<godot::StandardMaterial3D> shared;

public:
    // 立着的样子
    godot::Ref<godot::ArrayMesh> mesh(ScatterLook p_look);
    // 采空了的样子：树 → 树桩，灌丛、苇丛 → 一小截茬；石块、土坡这些采空了就没了（返回空）
    godot::Ref<godot::ArrayMesh> felled_mesh(ScatterLook p_look);
    // 共用的材质：顶点色当反照率
    godot::Ref<godot::StandardMaterial3D> material();
};

namespace scatter_meshes {

// 这一株的实例色（按键哈希抖一点深浅；矿石按矿种、结壳按硫磺 / 鸟粪上色）
godot::Color instance_color(ScatterLook p_look, uint8_t p_sub, uint64_t p_key);

} // namespace scatter_meshes
