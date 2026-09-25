#include "item_slots.h"

#include <algorithm>
#include <utility>

#include "crops.h"

namespace {

void settle(ItemStack &r_stack) {
    if (r_stack.count <= 0) {
        r_stack.count = 0;
        r_stack.item.clear();
    }
}

} // namespace

ItemSlots::ItemSlots(int p_size, int p_front) :
        slots(size_t(std::max(0, p_size))), front(std::clamp(p_front, 0, std::max(0, p_size))) {}

int ItemSlots::stack_limit(const std::string &p_item) {
    return std::max(1, catalog::stack_of(p_item));
}

bool ItemSlots::empty() const {
    return used() == 0;
}

int ItemSlots::used() const {
    int n = 0;
    for (const ItemStack &s : slots) {
        n += s.empty() ? 0 : 1;
    }
    return n;
}

void ItemSlots::set(int p_slot, const std::string &p_item, int p_count) {
    if (p_slot < 0 || p_slot >= size()) {
        return;
    }
    ItemStack &s = slots[size_t(p_slot)];
    s.item = p_item;
    s.count = p_item.empty() ? 0 : std::min(p_count, stack_limit(p_item));
    settle(s);
}

int ItemSlots::count(const std::string &p_item) const {
    int n = 0;
    for (const ItemStack &s : slots) {
        n += s.item == p_item ? s.count : 0;
    }
    return n;
}

int ItemSlots::room_for(const std::string &p_item) const {
    if (p_item.empty()) {
        return 0;
    }
    const int limit = stack_limit(p_item);
    int n = 0;
    for (const ItemStack &s : slots) {
        n += s.empty() ? limit : (s.item == p_item ? std::max(0, limit - s.count) : 0);
    }
    return n;
}

int ItemSlots::merge_into(const std::string &p_item, int p_count, int p_begin, int p_end, int p_skip) {
    const int limit = stack_limit(p_item);
    for (int k = std::max(0, p_begin); k < std::min(p_end, size()) && p_count > 0; k++) {
        ItemStack &s = slots[size_t(k)];
        if (k == p_skip || s.empty() || s.item != p_item) {
            continue;
        }
        const int moved = std::min(p_count, limit - s.count);
        if (moved > 0) {
            s.count += moved;
            p_count -= moved;
        }
    }
    return p_count;
}

int ItemSlots::fill_empty(const std::string &p_item, int p_count, int p_begin, int p_end, int p_skip) {
    const int limit = stack_limit(p_item);
    for (int k = std::max(0, p_begin); k < std::min(p_end, size()) && p_count > 0; k++) {
        ItemStack &s = slots[size_t(k)];
        if (k == p_skip || !s.empty()) {
            continue;
        }
        const int moved = std::min(p_count, limit);
        s.item = p_item;
        s.count = moved;
        p_count -= moved;
    }
    return p_count;
}

int ItemSlots::put(const std::string &p_item, int p_count) {
    if (p_count <= 0 || p_item.empty()) {
        return 0;
    }
    int left = merge_into(p_item, p_count, 0, size());
    left = fill_empty(p_item, left, front, size());
    return fill_empty(p_item, left, 0, front);
}

bool ItemSlots::take(const std::string &p_item, int p_count) {
    if (p_count <= 0) {
        return true;
    }
    if (p_item.empty() || count(p_item) < p_count) {
        return false;
    }
    for (int k = size() - 1; k >= 0 && p_count > 0; k--) {
        ItemStack &s = slots[size_t(k)];
        if (s.item != p_item) {
            continue;
        }
        const int moved = std::min(p_count, s.count);
        s.count -= moved;
        p_count -= moved;
        settle(s);
    }
    return true;
}

int ItemSlots::take_at(int p_slot, int p_count) {
    if (p_slot < 0 || p_slot >= size() || p_count <= 0) {
        return 0;
    }
    ItemStack &s = slots[size_t(p_slot)];
    const int moved = std::min(p_count, s.count);
    s.count -= moved;
    settle(s);
    return moved;
}

void ItemSlots::compact() {
    std::vector<ItemStack> totals;
    for (const ItemStack &s : slots) {
        if (s.empty()) {
            continue;
        }
        auto it = std::find_if(totals.begin(), totals.end(), [&](const ItemStack &t) { return t.item == s.item; });
        if (it == totals.end()) {
            totals.push_back(s);
        } else {
            it->count += s.count;
        }
    }
    std::vector<ItemStack> packed;
    for (const ItemStack &t : totals) {
        const int limit = stack_limit(t.item);
        for (int left = t.count; left > 0; left -= limit) {
            packed.push_back({ t.item, std::min(left, limit) });
        }
    }
    // 放不下（原本就超了一叠的）不会发生：原来每格都不超一叠，并完只会更少
    packed.resize(std::max(packed.size(), slots.size()));
    slots.assign(packed.begin(), packed.begin() + ptrdiff_t(slots.size()));
}

bool ItemSlots::resize(int p_size) {
    p_size = std::max(0, p_size);
    if (p_size >= size()) {
        slots.resize(size_t(p_size));
        return true;
    }
    bool tail_empty = true;
    for (int k = p_size; k < size(); k++) {
        tail_empty = tail_empty && slots[size_t(k)].empty();
    }
    if (!tail_empty) {
        ItemSlots packed = *this;
        packed.compact();
        if (packed.used() > p_size) {
            return false;
        }
        slots = packed.slots;
    }
    slots.resize(size_t(p_size));
    front = std::min(front, p_size);
    return true;
}

namespace slots {

void left_click(ItemSlots &r_bag, int p_slot, ItemStack &r_cursor) {
    if (p_slot < 0 || p_slot >= r_bag.size()) {
        return;
    }
    const ItemStack here = r_bag.at(p_slot);
    if (r_cursor.empty()) {
        r_cursor = here; // 空手：拿起整叠
        r_bag.clear(p_slot);
    } else if (here.empty()) {
        const int moved = std::min(r_cursor.count, ItemSlots::stack_limit(r_cursor.item));
        r_bag.set(p_slot, r_cursor.item, moved);
        r_cursor.count -= moved;
    } else if (here.item == r_cursor.item) {
        const int moved = std::min(r_cursor.count, ItemSlots::stack_limit(here.item) - here.count);
        if (moved > 0) {
            r_bag.set(p_slot, here.item, here.count + moved);
            r_cursor.count -= moved;
        }
    } else {
        r_bag.set(p_slot, r_cursor.item, r_cursor.count); // 不同物品：对调
        r_cursor = here;
    }
    settle(r_cursor);
}

void right_click(ItemSlots &r_bag, int p_slot, ItemStack &r_cursor) {
    if (p_slot < 0 || p_slot >= r_bag.size()) {
        return;
    }
    const ItemStack here = r_bag.at(p_slot);
    if (r_cursor.empty()) {
        if (here.empty()) {
            return;
        }
        const int half = (here.count + 1) / 2; // 拿多的那一半
        r_cursor = { here.item, half };
        r_bag.set(p_slot, here.item, here.count - half);
    } else if (here.empty() || here.item == r_cursor.item) {
        if (!here.empty() && here.count >= ItemSlots::stack_limit(here.item)) {
            return; // 满了
        }
        r_bag.set(p_slot, r_cursor.item, here.count + 1);
        r_cursor.count -= 1;
    } else {
        r_bag.set(p_slot, r_cursor.item, r_cursor.count);
        r_cursor = here;
    }
    settle(r_cursor);
}

int move_stack(ItemSlots &r_from, int p_slot, ItemSlots &r_to, int p_begin, int p_end) {
    if (p_slot < 0 || p_slot >= r_from.size() || r_from.at(p_slot).empty()) {
        return 0;
    }
    const ItemStack stack = r_from.at(p_slot);
    const int end = p_end < 0 ? r_to.size() : p_end;
    const int skip = &r_from == &r_to ? p_slot : -1; // 同一个容器里搬：别搬回自己那格
    int left = r_to.merge_into(stack.item, stack.count, p_begin, end, skip);
    left = r_to.fill_empty(stack.item, left, p_begin, end, skip);
    const int moved = stack.count - left;
    r_from.set(p_slot, stack.item, left);
    return moved;
}

void swap(ItemSlots &r_a, int p_a, ItemSlots &r_b, int p_b) {
    if (p_a < 0 || p_a >= r_a.size() || p_b < 0 || p_b >= r_b.size() || (&r_a == &r_b && p_a == p_b)) {
        return;
    }
    const ItemStack a = r_a.at(p_a), b = r_b.at(p_b);
    r_a.set(p_a, b.item, b.count);
    r_b.set(p_b, a.item, a.count);
}

int return_cursor(ItemStack &r_cursor, ItemSlots &r_first, ItemSlots *r_second) {
    if (r_cursor.empty()) {
        return 0;
    }
    int left = r_first.put(r_cursor.item, r_cursor.count);
    if (left > 0 && r_second) {
        left = r_second->put(r_cursor.item, left);
    }
    r_cursor.count = left;
    settle(r_cursor);
    return left;
}

} // namespace slots
