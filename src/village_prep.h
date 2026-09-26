#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "building_mesh.h"
#include "village_sim.h"
#include "world_sites.h"

// 岛群里一处村落 / 散户的备料：都是纯计算，放在后台线程上做，主线程拿到就能挂。不含 Godot 头。
//
// prepare_world_village：铺底清单（plan_world_village）、叠上布置模式的改动（文本行由主线程先读好）、每座的地坪、按格子分好的简模。
// 格子（village_cells）：村子局部坐标里 cell_m 见方，房子按中心落在哪格归哪格；一格一份简模网格（顶点相对格的西北角）与格里的清单下标，
//   外加格里房子占地（算上出檐）的外接框——离焦点多远按它量。近处按格子换细模：一格的细模挂齐了才藏那一格的简模（WorldVillage、SiteStream）。
// prepare_village_nav：村民的导航格与挑水的线（Villagers 招人时要的那一份），窗口按村子周围一圈，附近的水够不着就往水那边扩。

struct VillageCell {
    int ix = 0, iz = 0; // 格号（村子局部坐标 / cell_m 向下取整）
    double x0 = 0.0, z0 = 0.0; // 西北角（村子局部坐标）
    double min_x = 0.0, min_z = 0.0, max_x = 0.0, max_z = 0.0; // 格里房子占地的外接框（村子局部坐标）
    std::vector<int> buildings; // 清单下标
    MeshPart proxy; // 简模（顶点相对西北角）
};

struct VillagePrepRules {
    WorldVillageRules village;
    double cell_m = 96.0;
};

struct VillagePrep {
    VillagePlan plan; // 底清单（铺村算法铺的）
    SettlementLayout layout; // 底 + 改动
    int unmatched = 0; // 改动里要拆的、底清单里对不上的
    std::vector<BuildingFrame> frames; // 与 layout 一一对应
    std::vector<VillageCell> cells;
};

// 一份清单每座的地坪（p_ground 是宿主的地面，局部坐标）
std::vector<BuildingFrame> layout_frames(const MapGround &p_ground, const SettlementLayout &p_layout, const SiteRules &p_rules);
// 按格子分好、画出简模（布置模式改了清单也调它重画）
std::vector<VillageCell> village_cells(const SettlementLayout &p_layout, const std::vector<BuildingFrame> &p_frames, double p_cell_m);
// 点到外接框的水平距离（点在框里为 0）
double cell_distance(const VillageCell &p_cell, double p_x, double p_z);

VillagePrep prepare_world_village(const WorldTerrain &p_terrain, const SiteSettlement &p_site, uint64_t p_seed, const std::vector<std::string> &p_edits,
        const VillagePrepRules &p_rules = VillagePrepRules());

struct VillageNav {
    NavGrid nav;
    std::vector<WaterLine> water;
};

// 导航格的窗口：房子外接框外扩 p_margin（岛群里的村）
void village_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double p_margin, double &r_x0, double &r_z0, int &r_cols, int &r_rows);
// 挑水的线 + 导航格：窗口（x0, z0, cols, rows）够不着最近的那处水，就往那边扩到水边再多 10 m
VillageNav prepare_village_nav(const MapGround &p_ground, const SettlementLayout &p_layout, const SimRules &p_rules, double p_x0, double p_z0, int p_cols, int p_rows);
