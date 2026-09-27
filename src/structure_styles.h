#pragma once

#include <map>
#include <string>
#include <vector>

#include "build_pieces.h"

// 整座样式（切片十一）：一座搭建件的全部件（槽位相对样式自己的格），一次摆下一整座。不含 Godot 头。
//
// 文本（game/structures/<id>.txt，玩家另存的在 user://structures/）：
//   # 注释
//   name <显示名>
//   piece <物品 id> <x> <z> <level> <tier> <dir>      槽位见 build_pieces.h（格 / 边 / 角由物品定）
// 摆：朝向按视线取整到 15°（再加 Q/R），整座的占地中心落在准星上；地坪 = 所有地基格里最高的地面 + 露出的一截，
// 每格的台基各自往下垫到它自己的最低处；有一格太陡、没地、在水里就不行；挨着别的一座也不行。按件扣背包里的件（缺什么说什么）。

struct StructureStyle {
    std::string id; // 文件名（不带 .txt）
    std::string name;
    std::vector<Piece> pieces; // id、bottom 不用
};

bool parse_structure_style(const std::string &p_text, StructureStyle &r_style, std::string &r_error);
std::string save_structure_style(const StructureStyle &p_style);
// 把一座存成样式：格号挪到从 (0, 0) 起，门都关上
StructureStyle style_from_structure(const Structure &p_structure, const std::string &p_id, const std::string &p_name);
// 要几件什么（物品 id → 件数，按表的次序排）
std::vector<std::pair<std::string, int>> style_bill(const StructureStyle &p_style);

struct StylePlan {
    bool ok = false;
    std::string why;
    Structure structure; // 摆下去的那一座（id 不用；件的 bottom 已按地面算好）
};

StylePlan plan_structure_style(const Structures &p_existing, const StructureStyle &p_style, const PieceAim &p_aim, const LandingGround &p_ground,
        const PieceRules &p_rules = PieceRules());
