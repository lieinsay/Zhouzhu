#pragma once

#include <string>
#include <vector>

#include "inventory.h"

// 格子容器：玩家的背包（36 格，前 9 格是快捷栏）、船舱（每节储存区 12 格）、摆在地上的木箱。不含 Godot 头。
// 每格一叠（一样物品、若干份），一叠最多多少份看物品表（catalog::stack_of）。格子的位置是玩家摆的，放东西不挪别的格子。
//
// 放东西（put，采集、收获、合成、拆箱都走这儿）：先往已有的同样物品的叠上添（从第 0 格往后），
// 再占空格——先占 front 之后的空格，最后才占前 front 格。背包的 front = 9：新东西进背包的主区，快捷栏只放玩家自己挪上去的。
// 拿东西（take）：全够才拿，从后往前拿（快捷栏上的那叠最后动）。
//
// 面板上的点法（照 MC），cursor 是鼠标上拖着的那一叠：
//   左键：空手拿起整叠；拖着的放进空格（放得下的都放）；同样物品并过去（并到满为止）；不同物品对调。
//   右键：空手拿起一半（多的那一半）；拖着的往空格或同样物品的叠上放一份；不同物品对调。
//   Shift + 左键：整叠搬到另一个容器（move_stack，按那边的放法：先并、再空格）。
//   数字键 k：这一格与快捷栏第 k 格对调（swap，两格可以在不同容器里）。
class ItemSlots : public ItemBag {
    std::vector<ItemStack> slots;
    int front = 0; // 放东西时最后才占的前几格

public:
    // 往 [p_begin, p_end) 里同样物品的叠上并 / 占其中的空格（p_skip 那格不算）；返回放不下的份数
    int merge_into(const std::string &p_item, int p_count, int p_begin, int p_end, int p_skip = -1);
    int fill_empty(const std::string &p_item, int p_count, int p_begin, int p_end, int p_skip = -1);

    ItemSlots() = default;
    explicit ItemSlots(int p_size, int p_front = 0);

    int size() const { return int(slots.size()); }
    int get_front() const { return front; }
    const ItemStack &at(int p_slot) const { return slots[size_t(p_slot)]; }
    const std::vector<ItemStack> &get_slots() const { return slots; }
    bool empty() const; // 一格都没放
    int used() const; // 放着东西的格数
    // 直接摆一格（开局、测试）：超过一叠的截掉
    void set(int p_slot, const std::string &p_item, int p_count);
    void clear(int p_slot) { set(p_slot, std::string(), 0); }

    int count(const std::string &p_item) const override;
    int room_for(const std::string &p_item) const override;
    int put(const std::string &p_item, int p_count) override;
    bool take(const std::string &p_item, int p_count) override;
    // 从第 p_slot 格拿最多 p_count 份，返回拿到几份
    int take_at(int p_slot, int p_count);

    // 改格数（船改大改小）：变少时先把东西并叠、往前挪；挪完还放不下就不改、返回 false
    bool resize(int p_size);
    // 并叠、往前挪（格子顺序按物品第一次出现的顺序）；resize 变少前用
    void compact();

    static int stack_limit(const std::string &p_item);
};

namespace slots {

void left_click(ItemSlots &r_bag, int p_slot, ItemStack &r_cursor);
void right_click(ItemSlots &r_bag, int p_slot, ItemStack &r_cursor);
// 第 p_slot 格整叠搬进 r_to 的 [p_begin, p_end)（p_end < 0 = 到底）：先并同样物品，再占空格；返回搬了几份（放不下的留在原格）
int move_stack(ItemSlots &r_from, int p_slot, ItemSlots &r_to, int p_begin = 0, int p_end = -1);
// 两格对调（可以在不同容器里，也可以同一个容器）
void swap(ItemSlots &r_a, int p_a, ItemSlots &r_b, int p_b);
// 拖着的那叠放回去：先 r_first、再 r_second（可以为空）；返回还剩几份没地方放
int return_cursor(ItemStack &r_cursor, ItemSlots &r_first, ItemSlots *r_second);

} // namespace slots
