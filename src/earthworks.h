#pragma once

#include <string>

// 挖与垒的规矩（切片十一，方格台阶）：一格（1 m 见方）一下挖掉 / 垒上 0.5 m。不含 Godot 头，无状态。
//
// 挖：手上拿着木锹或石镐。土层（原地形往下 soil_depth 米，按地表：裸岩没有土、崖缘半米、高山草甸一米、别处两米、湿地两米半）里出「土」，
//   木锹快、石镐慢些；土层以下是石，只有石镐挖得动，出「石料」。垒上来的土挖回去照样出土。最深挖到原地形往下 max_down 级（8 m）。
// 垒：手上拿着土，一下垒一级、扣一份土；最高垒到原地形往上 max_up 级（4 m）；垒的那格压着人（脚底的圆）就不行。
// 在哪都不行：这格所在的粗格（2 m）不全是地（崖边）、离岸不到 min_shore_m、压着水、码头的坪、船坞坪、田、房子、树干、搭建件、摆件
//   ——这些由上层量好填进 EarthFacts（blocked 写为什么）。开着 god 模式（free）不扣土、不进背包，锹镐也不要。

enum class EarthAction {
    DIG,
    RAISE,
};

struct EarthRules {
    int max_down = 16; // 最深挖几级（原地形往下）
    int max_up = 8; // 最高垒几级
    double min_shore_m = 3.0;
    double shovel_s = 0.45; // 木锹挖一级土要按住 E 几秒
    double pick_soil_s = 0.8; // 石镐挖土
    double pick_stone_s = 1.1; // 石镐凿石
    double free_s = 0.25; // god 模式
};

// 一格的事实（上层量好）
struct EarthFacts {
    bool solid = false; // 这格所在的粗格四角都是地
    double shore = 0.0; // 格心的岸距（米）
    std::string blocked; // 压着什么（「这是田」「房子挡着」……）；空 = 没有
    double soil_depth = 2.0; // 原地形往下多深是土
    bool under_feet = false; // 这格压着人（只管垒）
};

struct EarthCheck {
    bool ok = false;
    std::string why;
    int before = 0, after = 0; // 级数
    std::string item; // 挖：出什么（"earth" / "stone"）；垒：扣什么（"earth"）
    bool stone = false; // 挖的是土层以下的石
    double seconds = 0.0; // 挖：按住 E 几秒；垒：0（按一下）
};

// p_held：手上拿着的物品 id（快捷栏选中那格）；p_free：god 模式
EarthCheck plan_earthwork(const EarthFacts &p_facts, int p_step, EarthAction p_action, const std::string &p_held, bool p_free, const EarthRules &p_rules = EarthRules());

// 手上这样东西是挖的家伙 / 是垒的土
bool earth_digs_with(const std::string &p_item);
bool earth_raises_with(const std::string &p_item);
