#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "farm_model.h"

// 村民对地做的事：看一格地是什么样、能做什么、垦 / 播 / 收。不含 Godot 头。
// 村子在近处时，地在地图的农田里（ModelFields 直接包 FarmModel；Villagers 节点另包一层，好让 Farm 重画）；
// 村子走远了，它认领的地连同长势从农田里拿出来、记在村子的账本里（HeldFields），远处结算就对着这些地干活、让它们长。
// 决策（decide_task）与落账（village_sim）只认这个接口，所以近处远处是同一套规矩。
//
// 格子是地图农田的地块格 (i, j)，与 FarmModel 一致。

class FieldBook {
public:
    virtual ~FieldBook() = default;
    virtual Plot plot_at(int p_i, int p_j) const = 0;
    // 对这一格能做什么；p_has_seed = 有没有种子
    virtual FarmAction available(int p_i, int p_j, bool p_has_seed) const = 0;
    virtual bool till(int p_i, int p_j) = 0;
    virtual bool sow(int p_i, int p_j, const CropDef *p_crop) = 0;
    virtual Harvest harvest(int p_i, int p_j) = 0;
    // 让地长 p_hours 个游戏小时。地图的农田自己按钟长（Farm 节点每帧推进），这里什么都不做
    virtual void grow(double p_hours) {}
};

// 全岛群（一张地图）已被村子认领的格子：远处村子开荒时别挑别的村认领了的（那块地此刻可能正记在别的村的账本里，不在农田里）
class ClaimRegistry {
    std::unordered_set<uint64_t> cells;
    static uint64_t key(int p_i, int p_j) { return (uint64_t(uint32_t(p_i)) << 32) | uint64_t(uint32_t(p_j)); }

public:
    bool taken(int p_i, int p_j) const { return cells.count(key(p_i, p_j)) != 0; }
    void add(int p_i, int p_j) { cells.insert(key(p_i, p_j)); }
    void remove(int p_i, int p_j) { cells.erase(key(p_i, p_j)); }
    size_t size() const { return cells.size(); }
};

// 一处村子周围哪些格能垦（地块格下标上的一张位图）：开账时在后台勘察一遍记下，挑荒地时先问它，
// 省得在岛群的连续地形上一格格现勘（勘察要问好几次地形）。只管「地能不能垦」，谁垦了、谁认领了照样现问。
struct ArableMask {
    int i0 = 0, j0 = 0, rows = 0, cols = 0; // 覆盖 [i0, i0 + rows) × [j0, j0 + cols)
    std::vector<uint8_t> bits; // 一格一位

    bool covers(int p_i, int p_j) const { return p_i >= i0 && p_j >= j0 && p_i < i0 + rows && p_j < j0 + cols; }
    bool arable(int p_i, int p_j) const {
        const size_t k = size_t(p_i - i0) * size_t(cols) + size_t(p_j - j0);
        return (bits[k >> 3] >> (k & 7)) & 1;
    }
    void set(int p_i, int p_j) {
        const size_t k = size_t(p_i - i0) * size_t(cols) + size_t(p_j - j0);
        bits[k >> 3] = uint8_t(bits[k >> 3] | (1u << (k & 7)));
    }
    bool any() const;
};

// 以 (x, z)（农田的坐标）为心、p_radius 以内勘察一遍
ArableMask survey_arable(const FarmModel &p_land, double p_x, double p_z, double p_radius);

// 近处：地图的农田本身
class ModelFields : public FieldBook {
    FarmModel &model;

public:
    explicit ModelFields(FarmModel &p_model) :
            model(p_model) {}
    Plot plot_at(int p_i, int p_j) const override { return model.plot_at(p_i, p_j); }
    FarmAction available(int p_i, int p_j, bool p_has_seed) const override { return model.available_action(p_i, p_j, p_has_seed); }
    bool till(int p_i, int p_j) override { return model.till(p_i, p_j); }
    bool sow(int p_i, int p_j, const CropDef *p_crop) override { return model.sow(p_i, p_j, p_crop); }
    Harvest harvest(int p_i, int p_j) override { return model.harvest(p_i, p_j); }
};

// 村子账本里拿着的一格地：长势和它的墒情（开垦时勘察一次记下，长的时候不再问地面）
struct HeldPlot {
    int i = 0, j = 0;
    Plot plot;
    float moisture = 0.0f;
};

// 远处：账本里的地块。荒地能不能垦问地图的农田（勘察与「是不是已经有人垦了」）；别的村认领的格子由挑地的人避开（pick_wild_plot 查 ClaimRegistry）。
// 状态转移与 FarmModel 同一套（荒 → 垦 → 长 → 熟 → 收了还是垦好的），生长用同一个 growth_rate。
class HeldFields : public FieldBook {
    std::vector<HeldPlot> &held;
    const FarmModel *land; // 勘察用（可垦、墒情、玩家垦过没有）；nullptr = 哪儿都垦不了
    const ArableMask *mask; // 开账时勘好的「能不能垦」；它盖得到的格就不再现勘（可以为 nullptr）
    FarmRules rules;

    int find(int p_i, int p_j) const;

public:
    HeldFields(std::vector<HeldPlot> &r_held, const FarmModel *p_land, const ArableMask *p_mask = nullptr) :
            held(r_held), land(p_land), mask(p_mask && p_mask->rows > 0 ? p_mask : nullptr), rules(p_land ? p_land->get_rules() : FarmRules()) {}
    Plot plot_at(int p_i, int p_j) const override;
    FarmAction available(int p_i, int p_j, bool p_has_seed) const override;
    bool till(int p_i, int p_j) override;
    bool sow(int p_i, int p_j, const CropDef *p_crop) override;
    Harvest harvest(int p_i, int p_j) override;
    void grow(double p_hours) override;
};
