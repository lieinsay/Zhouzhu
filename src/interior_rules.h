#pragma once

#include <string>
#include <vector>

#include "house_template.h"

// 室内的规矩：模板编辑器里放什么、放哪儿行不行，以及整套模板走不走得通。不含 Godot 头，无状态。
//
// 坐标与层同 house_template.h（建筑局部坐标，米）。编辑函数返回空串表示做了，否则是中文原因、模板不动；
// 想先问「行不行」就在副本上放一次（模板很小，每帧拷一份也不费事）。
//
// 放的规矩：
//   家具：整个落在屋内净空里；不压别的家具、隔墙、梯板；不挡门（外门往里 1.2 m、门洞两侧各 0.9 m 要空着）；
//         不挡梯子（梯脚人站的地方、楼上梯口与下梯的落脚处）。
//   门窗：光标得在外墙边（离墙内皮 0.8 m 内）——落在哪面墙就开在哪面；门只开在底层（楼上开门一脚踩空）；
//         在墙上放得下（离墙角至少 0.25 m）、不和同一面的门窗挨着；门里头不能有东西挡着。
//         光标落在隔墙上时，门是开门洞（宽 1 m），窗不行。
//   隔墙：横平竖直（两点取偏得多的那根轴）、至少 1 m；端点离外墙不到 0.3 m 就接到墙上；不穿家具、梯子、门前的空地，不与别的隔墙重叠。
//   梯子：顶层没有再往上的地方；梯板与梯脚、楼上梯口与落脚处都在屋里、都空着。
//
// 走得通（interior_report）：每层铺 0.25 m 的格，格心离外墙、隔墙、挡人的家具、梯板都至少一个人的半径（0.35 m）、
// 不在楼板的梯口上，才算站得住；从底层的外门进来连片找（四邻），踩到梯脚就能上楼、踩到梯顶落脚处就能下楼。
// 站得住却走不到的地方超过 0.75 m² 就报出来。另外：没有门、正面没开门（村民认正面的门）、梯子上下不去也报。

enum class InteriorToolKind {
    WALL,
    DOOR,
    WINDOW,
    LADDER,
    FURNITURE,
};

// 编辑器的工具板：隔墙、门、窗、梯子，然后是家具表里的每一种
struct InteriorTool {
    InteriorToolKind kind = InteriorToolKind::WALL;
    int furniture = -1; // FURNITURE 时是家具表的下标
};

int interior_tool_count();
InteriorTool interior_tool(int p_index);
std::string interior_tool_name(int p_index);

// 点中了模板里的哪一样
struct InteriorHit {
    enum Kind {
        NONE,
        ITEM,
        LADDER,
        DOORWAY,
        OPENING,
        PARTITION,
    };
    Kind kind = NONE;
    int index = -1;
};

// 轴对齐的矩形（建筑局部坐标）
struct InteriorRect {
    double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0;
    bool contains(double p_x, double p_z, double p_margin = 0.0) const;
    bool overlaps(const InteriorRect &p_other, double p_gap = 0.0) const;
    double area() const { return (x1 - x0) * (z1 - z0); }
};

InteriorRect furnishing_rect(const Furnishing &p_item); // 家具转过之后的占地
InteriorRect ladder_board_rect(const LadderSpot &p_ladder); // 梯板（挡人）
InteriorRect ladder_foot_rect(const LadderSpot &p_ladder); // 梯脚：人站着爬的地方（本层）
InteriorRect ladder_hatch_rect(const LadderSpot &p_ladder); // 上一层楼板上的梯口
InteriorRect ladder_landing_rect(const LadderSpot &p_ladder); // 上一层下梯的落脚处（梯板另一侧）
// 一段隔墙扣掉门洞后剩下的几截（挡人的实墙）
std::vector<InteriorRect> partition_pieces(const HouseTemplate &p_template, int p_partition);
// 离点最近的外墙；点离墙内皮不超过 p_reach 才算，否则返回 false
bool nearest_wall(const BuildingDef &p_def, double p_x, double p_z, double p_reach, WallSide &r_side, double &r_at);
// 开口的高低（离本层地面）：门 0 → 门高，窗 窗台 → 窗顶；层高不够时往下压
void opening_span(const BuildingDef &p_def, const Opening &p_opening, double &r_y0, double &r_y1);

InteriorHit interior_find(const HouseTemplate &p_template, const BuildingDef &p_def, double p_x, double p_z, int p_level);
std::string interior_hit_name(const HouseTemplate &p_template, const InteriorHit &p_hit); // 「床」「正面的门」……

std::string interior_place_item(HouseTemplate &p_template, const BuildingDef &p_def, const std::string &p_kind, double p_x, double p_z, int p_quarter, int p_level);
std::string interior_place_opening(HouseTemplate &p_template, const BuildingDef &p_def, bool p_window, double p_x, double p_z, int p_level);
std::string interior_place_wall(HouseTemplate &p_template, const BuildingDef &p_def, double p_x0, double p_z0, double p_x1, double p_z1, int p_level);
std::string interior_place_ladder(HouseTemplate &p_template, const BuildingDef &p_def, double p_x, double p_z, int p_quarter, int p_level);
// 用工具板上第 p_tool 样在该处放（隔墙要两个点，用 interior_place_wall）
std::string interior_place_tool(HouseTemplate &p_template, const BuildingDef &p_def, int p_tool, double p_x, double p_z, int p_quarter, int p_level);
bool interior_remove(HouseTemplate &p_template, const InteriorHit &p_hit);
// 隔墙两点怎么取直、接墙（给编辑器画预览用；与 interior_place_wall 同一套）
void interior_wall_snap(const BuildingDef &p_def, double p_x0, double p_z0, double p_x1, double p_z1, double &r_x0, double &r_z0, double &r_x1, double &r_z1);

struct InteriorReport {
    std::vector<std::string> problems; // 空 = 这套模板没毛病
    std::vector<double> unreachable_m2; // 每层站得住却走不到的面积
    std::vector<double> walkable_m2; // 每层站得住的面积
    bool ok() const { return problems.empty(); }
};

InteriorReport interior_report(const HouseTemplate &p_template, const BuildingDef &p_def);
const char *level_label(int p_level); // 「底层」「二层」「三层」……
