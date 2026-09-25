#pragma once

#include <string>
#include <vector>

// 装东西的地方：玩家的格子背包、船舱、木箱（item_slots.h），村仓的账（下面的 Inventory）。
// 跑货打包、拆箱、付运费这类「从哪儿拿几份、往哪儿放几份」的事只认这几问，不管背后是格子还是账。
class ItemBag {
public:
    virtual ~ItemBag() = default;
    virtual int count(const std::string &p_item) const = 0;
    bool has(const std::string &p_item, int p_count = 1) const { return count(p_item) >= p_count; }
    // 还放得下几份
    virtual int room_for(const std::string &p_item) const = 0;
    // 放进去 p_count 份，返回放不下的份数（≤ 0 不做事、返回 0）
    virtual int put(const std::string &p_item, int p_count) = 0;
    // 拿走 p_count 份；不够则什么都不动、返回 false
    virtual bool take(const std::string &p_item, int p_count) = 0;
};

// 背包的老样子、村仓的账：物品 id → 数量的堆栈表，不限格数、不限叠数。不含 Godot 头。
// 只认 id 字符串，不知道物品是什么（名字、是不是种子，见 crops.h）；谁在用、显示成什么样，都不归它管。
// 堆栈按第一次放入的顺序排列，数量归零即从表里移除。

struct ItemStack {
    std::string item;
    int count = 0;
    bool empty() const { return count <= 0; }
};

class Inventory : public ItemBag {
    std::vector<ItemStack> stacks;

    int find(const std::string &p_item) const;

public:
    const std::vector<ItemStack> &get_stacks() const { return stacks; }
    int size() const { return int(stacks.size()); }
    bool empty() const { return stacks.empty(); }

    int count(const std::string &p_item) const override;

    // 放进去 p_count 份（≤ 0 不做事）
    void add(const std::string &p_item, int p_count);
    // 拿走 p_count 份；不够则什么都不动、返回 false
    bool remove(const std::string &p_item, int p_count);

    int room_for(const std::string &p_item) const override;
    int put(const std::string &p_item, int p_count) override {
        add(p_item, p_count);
        return 0;
    }
    bool take(const std::string &p_item, int p_count) override { return remove(p_item, p_count); }
};
