#pragma once

#include <string>
#include <vector>

// 背包：物品 id → 数量的堆栈表。不含 Godot 头。
// 只认 id 字符串，不知道物品是什么（名字、是不是种子，见 crops.h）；谁在背、显示成什么样，都不归它管。
// 堆栈按第一次放入的顺序排列，数量归零即从表里移除——面板按这个顺序列，光标才不会乱跳。

struct ItemStack {
    std::string item;
    int count = 0;
};

class Inventory {
    std::vector<ItemStack> stacks;

    int find(const std::string &p_item) const;

public:
    const std::vector<ItemStack> &get_stacks() const { return stacks; }
    int size() const { return int(stacks.size()); }
    bool empty() const { return stacks.empty(); }

    int count(const std::string &p_item) const;
    bool has(const std::string &p_item, int p_count = 1) const { return count(p_item) >= p_count; }

    // 放进去 p_count 份（≤ 0 不做事）
    void add(const std::string &p_item, int p_count);
    // 拿走 p_count 份；不够则什么都不动、返回 false
    bool remove(const std::string &p_item, int p_count);
};
