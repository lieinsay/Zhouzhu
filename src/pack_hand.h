#pragma once

#include <string>

#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "hotbar_bar.h"
#include "inventory_panel.h"
#include "item_slots.h"
#include "play_notice.h"

// 背包与快捷栏：36 格的背包（前 9 格是快捷栏）、选中快捷栏哪一格（拿在手上的就是那格里的东西：种子、工具、能摆的东西）、
// 背包面板开没开、开着时另一边是什么容器（船舱 / 木箱）、鼠标上拖着的那一叠、键盘光标、合成。
// 与场景无关的玩法部件（不是节点），编排持有：面板开着时每帧推一下键盘（browse）、把鼠标事件转过来（on_mouse）；
// 另一边的容器与身边的设施由编排每帧给（船舱是编排的账，木箱是摆放部件的账）。
// 面板上的点法照 MC（见 item_slots.h）；配方点一下合一份，Shift 合到料用完（recipes.h）。合上面板时拖着的那叠放回去。
// 开合面板时人站不站住、手上的活作不作废，是编排的事。
class PackHand {
public:
    static constexpr int SLOTS = 36;
    static constexpr int HOTBAR = 9;

private:
    ItemSlots pack = ItemSlots(SLOTS, HOTBAR);
    int selected = 0; // 快捷栏选中哪格
    bool open = false;
    ItemStack cursor; // 鼠标上拖着的
    ItemSlots *other = nullptr; // 开着时另一边的容器
    godot::String other_title;
    unsigned stations = 0; // 身边的设施（station_bit 的或）
    int focus_section = InventoryPanel::PACK, focus_index = HOTBAR; // 键盘光标
    int hover_section = InventoryPanel::NONE, hover_index = -1; // 鼠标悬停
    godot::Vector2 mouse;
    bool mouse_active = false;
    InventoryPanel *panel = nullptr;
    HotbarBar *bar = nullptr;

    ItemSlots *bag_of(int p_section);
    void click(int p_section, int p_index, bool p_right, bool p_shift, const Say &p_say);
    void quick_move(int p_section, int p_index);
    void swap_to_hotbar(int p_hotbar);
    void craft(int p_recipe, bool p_all, const Say &p_say);

public:
    // 开局背包（物品 id → 份数，按表里的顺序放进主区；快捷栏空着）
    void setup(const godot::Dictionary &p_starting, InventoryPanel *p_panel, HotbarBar *p_bar);

    bool is_open() const { return open; }
    // 开面板：p_other 是另一边的容器（没有给 nullptr）；光标落在主区第一格
    void open_with(ItemSlots *p_other, const godot::String &p_title);
    // 合上：拖着的那叠放回背包（放不下再放另一边）
    void close();
    // 开着时每帧：另一边容器的地址可能变了（木箱的账）
    void set_other(ItemSlots *p_other, const godot::String &p_title);
    ItemSlots *get_other() const { return other; }
    godot::String get_other_title() const { return other_title; }
    void set_stations(unsigned p_bits) { stations = p_bits; }
    unsigned get_stations() const { return stations; }
    // 开着面板时每个物理帧：WASD 挪光标、E 左键、Q 右键、F 搬过去、1–9 放上快捷栏；配方上 E 合一份、F 合到底
    void browse(bool p_interact, const Say &p_say);
    // 开着面板时的鼠标事件（编排转过来）：用掉了返回 true
    bool on_mouse(const godot::Ref<godot::InputEvent> &p_event, const Say &p_say);

    // 快捷栏（没开面板时）
    void select(int p_slot);
    void scroll(int p_step);
    int get_selected() const { return selected; }
    // 拿在手上的：快捷栏选中那格里的东西（空格是空串）
    const std::string &held_item() const { return pack.at(selected).item; }
    std::string held_seed() const; // 手上的是种子才给
    bool has_seed_in_hand() const { return !held_seed().empty(); }

    const ItemSlots &items() const { return pack; }
    ItemSlots &items() { return pack; }
    const ItemStack &get_cursor() const { return cursor; }
    int get_focus_section() const { return focus_section; }
    int get_focus_index() const { return focus_index; }
    // 面板上某一格 / 某条配方的中心（面板坐标 = 屏幕坐标；测试拿它点鼠标）
    godot::Vector2 slot_center(int p_section, int p_index) const;

    void refresh_panel();
    void refresh_bar();
    // HUD 一行：「手持 石斧 · 背包 12 / 36 格」
    godot::String summary() const;

    static godot::String name_of(const std::string &p_item);
};
