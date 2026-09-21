#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "island_field.h"

// 一座岛上的农田。地块就是高度场的格子（节点 (i, j) 为左上角的那一格，边长 cell_m），
// 所以垦出来的地恰好贴着地形的两个三角面，不用另立一套坐标。
// 本文件不含 Godot 头。勘察是「高度场 → 值」的纯函数；FarmModel 只是一张地块表加几条转移规则，
// 谁来干活、手里有多少粟，都不归它管。

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
    WATER, // 在河里
    TOO_DRY, // 离河太远，引不到水
    TOO_STEEP,
    DOCK, // 停机坪
    TRAIL, // 路上
};

struct FarmRules {
    double max_river_m = 16.0; // 格心离河中线多远以内引得到水
    double max_slope = 0.2; // 格内最大坡度（高差 / 对角线）
    double dock_margin_m = 4.0; // 停机坪外再让出这么多
    double trail_margin_m = 2.5; // 格子的哪个角离路中线这么近，就算在路上
    double grow_seconds = 60.0; // 紧挨着河的地从播种到成熟
    double dry_growth = 0.6; // 勉强引得到水的地，生长速度是河边的这么多倍
    int seed_cost = 1;
    int harvest_yield = 4; // 收一格得多少粟（粟既是粮也是种）
};

struct FarmSurvey {
    int n = 0; // 与高度场同：格子 (i, j) 有效范围 [0, n − 1)
    std::vector<uint8_t> arability; // Arability
    std::vector<float> moisture; // 墒情：河边 1 → 引水极限 0

    size_t index(int p_i, int p_j) const { return size_t(p_i) * size_t(n) + size_t(p_j); }
    bool in_range(int p_i, int p_j) const { return p_i >= 0 && p_j >= 0 && p_i < n - 1 && p_j < n - 1; }
    Arability at(int p_i, int p_j) const { return in_range(p_i, p_j) ? Arability(arability[index(p_i, p_j)]) : Arability::NO_GROUND; }
    int arable_count() const;
};

// p_dock_apron_m：停机坪半径（IslandParams::dock_apron_m）
FarmSurvey survey_farmland(const IslandField &p_field, double p_dock_apron_m, const FarmRules &p_rules);

struct Plot {
    PlotState state = PlotState::WILD;
    double growth = 0.0; // 0 → 1
};

class FarmModel {
    FarmSurvey survey;
    FarmRules rules;
    std::unordered_map<uint32_t, Plot> plots;

    static uint32_t key(int p_i, int p_j) { return (uint32_t(p_i) << 16) | uint32_t(p_j); }

public:
    FarmModel() = default;
    FarmModel(FarmSurvey p_survey, const FarmRules &p_rules) :
            survey(std::move(p_survey)), rules(p_rules) {}

    const FarmSurvey &get_survey() const { return survey; }
    const FarmRules &get_rules() const { return rules; }
    void set_rules(const FarmRules &p_rules) { rules = p_rules; }

    Plot plot_at(int p_i, int p_j) const;
    // 手里有 p_grain 粟时，对这一格能做什么
    FarmAction available_action(int p_i, int p_j, int p_grain) const;
    // 做了；返回粟的增减（播种为负、收获为正）。p_action 与 available_action 不符则什么都不做、返回 0
    int perform(int p_i, int p_j, FarmAction p_action, int p_grain);
    // 推进生长；返回这一步里外观阶段变了的格子（(i, j) 成对）
    std::vector<int> advance(double p_delta);

    int count(PlotState p_state) const;

    // 粟苗的外观阶段：0 刚出土 … STAGES − 1 成熟
    static constexpr int STAGES = 4;
    static int stage_of(const Plot &p_plot);
};
