#pragma once

#include <string>

#include <godot_cpp/variant/dictionary.hpp>

#include "backpack_panel.h"
#include "inventory.h"
#include "play_notice.h"

// 背包与手持：背包里有什么、手上拿着哪样种子、面板开没开、光标在哪。与场景无关的玩法部件（不是节点），编排持有、每帧推一下。
// 开合面板时人站不站住、手上的活作不作废，是编排的事；这里只管背包、手持与面板。
class PackHand {
    Inventory pack;
    std::string held; // 物品 id；空 = 没拿
    bool open = false;
    int cursor = 0;
    BackpackPanel *panel = nullptr;

public:
    // 开局背包（物品 id → 份数，按表里的顺序放，面板就按这个顺序列）
    void setup(const godot::Dictionary &p_starting, BackpackPanel *p_panel);

    bool is_open() const { return open; }
    // 开的时候光标落在手上那样东西上（没拿就从头）
    void set_open(bool p_open);
    // 翻背包：W/S 挪光标，E 把光标那样种子拿在手上（再按一次放下）
    void browse(bool p_interact, const Say &p_say);
    void refresh_panel();

    int get_cursor() const { return cursor; }
    const Inventory &items() const { return pack; }
    Inventory &items() { return pack; }
    const std::string &held_seed() const { return held; }
    bool has_seed_in_hand() const { return !held.empty() && pack.has(held); }
    // HUD 一行：「背包 粟 5 菽 3    手持 粟种」
    godot::String summary() const;

    static godot::String name_of(const std::string &p_item);
};
