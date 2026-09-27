#pragma once

#include <memory>
#include <string>

#include "build_pieces.h"
#include "placeables.h"
#include "terrain_edits.h"

// 一处地面上玩家营造的一切（切片十一）：改过的地形、搭建件、摆在地上的东西（切片十的工作台、窑、木箱……挪到这里）。不含 Godot 头。
// 坐标一律是宿主的局部坐标（岛群 = 这一群的平面坐标：锚群就是世界坐标；没有岛群的地图由编排自带一份、用全局坐标）。
// 岛群的每一群一份，GroupStream 留在内存、存进世界存档目录（works/<群号>.txt），群载入时交给它的 GroupWorld；所以飞走再回来、退出再进都还在。
// 改地形的账是整份不可变的（terrain_edits.h）：改一格换一份，同一份指针交给 WorldTerrain 给后台线程读。
struct GroundWorks {
    std::shared_ptr<const TerrainEdits> edits;
    Structures structures;
    Placements props;

    bool empty() const { return (!edits || edits->empty()) && structures.size() == 0 && props.size() == 0; }
};

// 存成文本 / 读回。格式见 ground_works.cpp 开头
std::string save_ground_works(const GroundWorks &p_works);
bool load_ground_works(const std::string &p_text, GroundWorks &r_works, std::string &r_error);
