#pragma once

#include <array>
#include <vector>

#include "build_pieces.h"
#include "building_mesh.h"

// 一座搭建件的样子（纯数据，切片十一）：WorksView 照它建节点。不含 Godot 头，无状态。
//
// 坐标是这一座的局部坐标（build_pieces.h：格 (0, 0) 的西北角为原点、y = 地坪）；上层把节点摆在这一座的原点、转 yaw。
// 按材质分的网格（与村里房子同一套材质与颜色，part_material_color）+ 整座一份碰撞三角面（上层设成两面都挡）+ 梯子的爬梯区域。
//   地基：一块石台从地坪往下到台基底；楼板：一块木板，面在 level·H；
//   墙：沿边立一块，两头各多出半个墙厚（转角处两面墙叠着，不留缝）；门墙：中间开 1.1 × 2.2 m 的洞，洞上过梁、洞边门框，门扇关着在洞里、
//     开着转 90° 贴到一边（碰撞跟着）；窗墙：中间开 1.0 × 0.9 m 的窗（窗台 1 m），窗棂只画不挡；
//   山墙：从墙顶补到挨着的坡顶底下（梯形 / 三角，形状按坡顶算）；
//   坡顶：一块斜板，底面从檐口那边（tier·R）升到另一边（(tier+1)·R）；最下一阶往外出檐，两头没挨着别的坡顶就出际；
//     高的那条边对面也有一块反着坡的（同阶）就补一道屋脊；
//   木柱：角上一根；梯子：靠着一边的梯板 + 横档，区域的 +z 朝人站的那侧（与房子、船上的梯子同一套）。
struct StructureMesh {
    std::array<MeshPart, size_t(PartMaterial::COUNT)> parts;
    std::vector<float> collision; // 每三个点（xyz）一个三角面
    std::vector<LadderZone> ladders;

    int triangle_count() const;
};

StructureMesh build_structure_mesh(const Structure &p_structure);
// 只拼一件（虚影用；山墙、屋脊照这一座别的件算）
StructureMesh build_piece_mesh(const Structure &p_structure, const Piece &p_piece);
