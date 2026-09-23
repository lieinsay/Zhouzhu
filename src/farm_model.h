#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "crops.h"
#include "map_ground.h"

// 一张地图上的农田。地块格由地面给（PlotLattice：岛上就是高度场的格子，岛群里是与岛群坐标对齐的 2.5 m 格），
// 所以垦出来的地恰好贴着地形，不用另立一套坐标。
// 本文件不含 Godot 头。可垦不可垦是「地面 → 值」的纯函数（survey_plot，两种地面同一套规矩）；
// FarmModel 是一张地块表加几条转移规则，勘察按需做、做过的记下来（岛群无边，不能一次勘完）。
// 谁来干活、背包里有没有种子，都不归它管：播种时上层把作物递进来，收获时它把「产物 × 几份」交出去。
// 生长按游戏小时推进（钟在上层）：换一天的长度，庄稼跟着钟点一起变快变慢。

enum class PlotState : uint8_t {
    WILD, // 荒地（表里没有这一格）
    TILLED, // 垦好了，空着
    GROWING,
    RIPE,
};

enum class FarmAction : uint8_t {
    NONE,
    TILL, // 开垦
    SOW, // 播种
    HARVEST, // 收获
};

// 一格地能不能垦；不能的话为什么
enum class Arability : uint8_t {
    ARABLE,
    NO_GROUND,
    WATER, // 在水里
    TOO_DRY, // 离水太远，也不是熟地
    TOO_STEEP,
    DOCK, // 停机坪
    TRAIL, // 路上
    YARD, // 船坞坪
};

struct FarmRules {
    double max_river_m = 16.0; // 格子四角到水中线的平均距离不超过这么远才引得到水
    double max_slope = 0.2; // 格内最大坡度（高差 / 对角线）
    double dock_margin_m = 4.0; // 停机坪、船坞坪外再让出这么多
    double trail_margin_m = 2.5; // 格子的哪个角离路中线这么近，就算在路上
    double grow_hours = 1.2; // 紧挨着水的地从播种到成熟要几个游戏小时（生长期倍率为 1 的作物）；一天 40 分钟时是 2 分钟
    double dry_growth = 0.6; // 勉强引得到水的地，生长速度是水边的这么多倍
    double fertile_moisture = 0.6; // 熟地（岛群的可耕地、梯田）不靠水也有这么好的墒情
};

// 一格地每个游戏小时长多少（0 → 1 是从播种到成熟）：墒情越好越快，作物的生长期倍率越大越慢。
// 近处农田（FarmModel::advance）与远处村子账本里拿着的地块同用这一个式子
double growth_rate(double p_moisture, const CropDef *p_crop, const FarmRules &p_rules);

struct PlotSurvey {
    Arability land = Arability::NO_GROUND;
    float moisture = 0.0f; // 墒情：水边 1 → 引水极限 0（熟地至少 fertile_moisture）
};

// 勘察一格：四角都得有地面；任一角在水里 → 水；格心在停机坪 / 船坞坪上；任一角在路上；
// 四角离水的平均距离在 max_river_m 以内、或格心是熟地，才引得到水；格内坡度不超过 max_slope。
PlotSurvey survey_plot(const MapGround &p_ground, const PlotLattice &p_lattice, int p_i, int p_j, const FarmRules &p_rules);

struct Plot {
    PlotState state = PlotState::WILD;
    double growth = 0.0; // 0 → 1
    const CropDef *crop = nullptr; // 种的是什么；只在 GROWING / RIPE 时有
};

// 收获所得
struct Harvest {
    const char *item = nullptr; // 产物物品 id；没收到东西为 nullptr
    int count = 0;
};

class FarmModel {
    const MapGround *ground = nullptr; // 不持有：上层（Farm 节点）持有，须比本对象活得久
    PlotLattice lattice;
    FarmRules rules;
    mutable std::unordered_map<uint64_t, PlotSurvey> surveyed; // 勘察过的格
    std::unordered_map<uint64_t, Plot> plots;

    static uint64_t key(int p_i, int p_j) { return (uint64_t(uint32_t(p_i)) << 32) | uint64_t(uint32_t(p_j)); }
    static int key_i(uint64_t p_key) { return int(int32_t(uint32_t(p_key >> 32))); }
    static int key_j(uint64_t p_key) { return int(int32_t(uint32_t(p_key & 0xFFFFFFFFu))); }

public:
    FarmModel() = default;
    FarmModel(const MapGround *p_ground, const PlotLattice &p_lattice, const FarmRules &p_rules) :
            ground(p_ground), lattice(p_lattice), rules(p_rules) {}

    const PlotLattice &get_lattice() const { return lattice; }
    const FarmRules &get_rules() const { return rules; }
    // 规矩变了，勘察作废（地块表不动）
    void set_rules(const FarmRules &p_rules);

    PlotSurvey survey(int p_i, int p_j) const;
    Arability land_at(int p_i, int p_j) const { return survey(p_i, p_j).land; }
    // 以 (x, z) 为心、p_radius 以内有没有可垦的荒地（每隔 p_step 格探一格，由近及远，找到就停）
    bool any_arable_near(double p_x, double p_z, double p_radius, int p_step = 2) const;

    Plot plot_at(int p_i, int p_j) const;
    // 对这一格能做什么；p_has_seed = 手上有没有种子（没有就播不了）
    FarmAction available_action(int p_i, int p_j, bool p_has_seed) const;
    // 三样活。做不了（与 available_action 不符）就什么都不动：till / sow 返回 false，harvest 返回空
    bool till(int p_i, int p_j);
    bool sow(int p_i, int p_j, const CropDef *p_crop);
    Harvest harvest(int p_i, int p_j);
    // 推进生长 p_hours 个游戏小时；返回这一步里外观阶段变了的格子（(i, j) 成对）
    std::vector<int> advance(double p_hours);
    // 把一格地整个拿走（村子走远了，地块连同长势记回村子的账本里）；表里没有返回荒地
    Plot take(int p_i, int p_j);
    // 放回一格地（村子又走近了）。那一格已经有人垦了（玩家、别的村）就放不下，返回 false
    bool put(int p_i, int p_j, const Plot &p_plot);

    int count(PlotState p_state) const;
    // 表里的格子（垦过的），(i, j) 成对
    std::vector<int> plot_cells() const;

    // 粟苗的外观阶段：0 刚出土 … STAGES − 1 成熟
    static constexpr int STAGES = 4;
    static int stage_of(const Plot &p_plot);
};
