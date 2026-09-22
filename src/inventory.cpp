#include "inventory.h"

int Inventory::find(const std::string &p_item) const {
    for (size_t k = 0; k < stacks.size(); k++) {
        if (stacks[k].item == p_item) {
            return int(k);
        }
    }
    return -1;
}

int Inventory::count(const std::string &p_item) const {
    const int k = find(p_item);
    return k < 0 ? 0 : stacks[size_t(k)].count;
}

void Inventory::add(const std::string &p_item, int p_count) {
    if (p_count <= 0 || p_item.empty()) {
        return;
    }
    const int k = find(p_item);
    if (k < 0) {
        stacks.push_back({ p_item, p_count });
    } else {
        stacks[size_t(k)].count += p_count;
    }
}

bool Inventory::remove(const std::string &p_item, int p_count) {
    if (p_count <= 0) {
        return true;
    }
    const int k = find(p_item);
    if (k < 0 || stacks[size_t(k)].count < p_count) {
        return false;
    }
    stacks[size_t(k)].count -= p_count;
    if (stacks[size_t(k)].count == 0) {
        stacks.erase(stacks.begin() + k);
    }
    return true;
}
