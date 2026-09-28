#pragma once

#include <cstdint>
#include <string>

#include "placeables.h"
#include "ship_layout.h"
#include "wild_landing.h"

// 船舱与船上摆东西。不含 Godot 头；坐标是船的局部坐标（原点在舷门格第 0 层甲板面中心，x 右舷、z 船尾）。
//
// 每一节是什么地方（cell_space）：
//   · DECK：一列最上面那一节，开放甲板；
//   · ROOM：被上面盖住、节点能进（节点表 enterable：储存区、双层货舱、空节、宽空节）——一间舱：
//     地板（第 0 层是甲板板，往上是下面那一节的顶）、舱顶板（顶面就是上面那一节的地板）、四面按邻格定；
//   · SOLID：被盖住、节点不能进（浮石舱）——整块封死的舱体。
// 舱的一面（cell_side）按同一层那一边的邻格：
//   · 同一个节点的舱：OPEN（不砌墙，复合节是一整间）；
//   · 开放甲板，或别的节点的舱：DOOR（墙正中开门洞；两间舱之间两边各开各的，对得上）；
//   · 封死的舱体：WALL；
//   · 船外（没有邻格）：WINDOW（外墙开两扇窗，窗台高过人腰，掉不出去）。
// 开放甲板的一边：同层邻格也是开放甲板 → OPEN；是能进的舱 → DOOR（那间舱的墙，开着门）；是封死的舱体 → WALL；
// 邻列更矮或没有 → RAIL（舷栏；舷门、梯顶留缺口）。
// 所以人从甲板走进舱、从一间舱走进另一间，都不用爬；舱与舱上下不通（上层靠甲板上的梯子）。
//
// 船上摆东西（工作台、窑、木箱、火把、篱笆）：只在能摆的节点（节点表 furnish：空节、宽空节）上，开放甲板和舱里都行；
// 占地离舱墙、舷栏留一点，不压在门口、梯子前后、舷门口；一件跨两节时两节都得能摆、中间不隔墙。
// 和已摆的挤不挤由 Placements 查（账是船的局部坐标，跟着船走）。

constexpr double CABIN_WALL_M = 0.2; // 舱墙厚（砌在自己那一节的边界以内）
constexpr double CABIN_CEILING_M = 0.3; // 舱顶板厚
constexpr double CABIN_DOOR_W_M = 1.4, CABIN_DOOR_H_M = 2.5; // 门洞
constexpr double CABIN_WINDOW_W_M = 1.2, CABIN_WINDOW_H_M = 0.9, CABIN_WINDOW_SILL_M = 1.3; // 窗：宽、高、窗台离地
constexpr double CABIN_WINDOW_ALONG_M = 1.8; // 两扇窗离墙中点
constexpr double SHIP_RAIL_T_M = 0.15; // 舷栏厚（贴着节的边）
constexpr double SHIP_GATE_HALF_M = 1.0; // 舷门缺口半宽
constexpr double SHIP_LADDER_ALONG_M = -2.8; // 梯子沿墙偏在哪一头（左舷 / 船头那一头，避开舵柱、座位与货位）

enum class CellSpace : uint8_t {
    NONE, // 没有这一节
    DECK, // 开放甲板
    ROOM, // 能进的舱
    SOLID, // 封死的舱体
};

enum class SideKind : uint8_t {
    OPEN, // 通着（不砌墙、不设栏）
    DOOR, // 墙上正中一个门洞
    WALL, // 实墙
    WINDOW, // 外墙，开两扇窗
    RAIL, // 舷栏
};

CellSpace cell_space(const ShipLayout &p_layout, const Cell &p_cell);
// 一节（DECK 或 ROOM）朝 (p_dx, p_dz) 那一面是什么；别的节返回 WALL
SideKind cell_side(const ShipLayout &p_layout, const Cell &p_cell, int p_dx, int p_dz);
// 人（脚底）在局部高 p_y：站在第几层的地板上
int ship_level_at(double p_y);
// 这一点在不在一间能进的舱里
bool in_cabin(const ShipLayout &p_layout, double p_x, double p_y, double p_z);

struct ShipPropRules {
    double edge_gap_m = 0.05; // 离墙、栏再留这么宽
    double door_clear_m = 1.2; // 门洞两边各留这么深
    double ladder_clear_m = 1.4; // 梯脚、梯顶留这么深
    double gate_clear_m = 1.6; // 舷门口留这么深
};

// 船上的地面（给 plan_placement）：p_level 那一层的开放甲板与能进的舱的地板；别处没有地。能不能摆另由 ship_prop_problem 说
class ShipFloor : public LandingGround {
    const ShipLayout &layout;
    int level;

public:
    ShipFloor(const ShipLayout &p_layout, int p_level) :
            layout(p_layout), level(p_level) {}
    bool height(double p_x, double p_z, double &r_y) const override;
};

// 船上第 p_level 层、中心 (p_x, p_z)、朝 p_quarter 摆 p_def：能摆返回空串，否则说为什么。只查船本身（节、墙、栏、门口、梯子、舷门）
std::string ship_prop_problem(const ShipLayout &p_layout, const PropDef &p_def, double p_x, double p_z, int p_quarter, int p_level,
        const ShipPropRules &p_rules = ShipPropRules());
// 改船：船上摆着的东西在新布局里还放不放得下；都放得下返回空串，否则「工作台放不下了（挡着舱门了）」「木箱放不下了（脚下的节没了）」
std::string ship_props_problem(const ShipLayout &p_layout, const Placements &p_props, const ShipPropRules &p_rules = ShipPropRules());
