#pragma once

#include <vector>

#include "island_field.h"

// 在一座已成形的岛上开一条河：腹地的泉眼 → 顺地势找路 → 从岸缘跌进云海。
// skyisle-gen 的岛内地形靠水流侵蚀长出谷地，河是算出来的；游戏尺度上要的是一条走得到、种得了地的河，
// 所以这里反过来：先定河道，再让地形迁就它——刻出河槽，两岸各压出一条平缓的河滩，滩外按固定坡度接回原地形。
// 本文件不含 Godot 头：读 r 的掩膜 / 向内程度 / 码头，改 h，把河写进 r.river、r.river_dist、r.river_depth。
//
// 找不到合适的河道（岛太小、码头挡住了所有去路）就什么都不改，r.river 留空。
void carve_island_river(const IslandParams &p_params, IslandField &r, std::vector<double> &h);

// 从停机坪修一条走得上去的路到河滩：岛缘往往是一圈陡坡，没有路，下了船够不着河。
// 选线罚陡坡（所以会斜着上、会拐之字），路面压平、纵坡有上限，两侧按边坡接回原地形。
// 要先有河；写进 r.trail、r.trail_dist。没有河或找不到路就什么都不改。
void carve_dock_trail(const IslandParams &p_params, IslandField &r, std::vector<double> &h);
