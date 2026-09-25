#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>

#include "item_slots.h"

// 背包面板（Tab）：只负责画，并回答屏幕上的一点落在哪一格 / 哪条配方上。自己不读输入、不改背包。
// 画的是（照 MC 的样子，居中）：
//   左边——另一边的容器（在船上是船舱，开着木箱是木箱，没有就不画）、背包主区 3 × 9、快捷栏 9 格（选中那格描绿）；
//   右边——配方清单：能合的亮、料不够的暗、设施不在身边的写「要工作台」；
//   鼠标上拖着的那一叠跟着鼠标（没用鼠标时跟着键盘光标）；最下面一行说悬停的那一格 / 那条配方是什么。
// 格子编号：背包 0–8 是快捷栏、9–35 是主区（与 ItemSlots 一致）；另一边从 0 起。
class InventoryPanel : public godot::Control {
    GDCLASS(InventoryPanel, godot::Control)

public:
    enum Section {
        NONE = -1,
        PACK = 0,
        OTHER = 1,
        RECIPES = 2,
    };

    struct RecipeRow {
        std::string product; // 物品 id
        int yield = 1;
        godot::String inputs; // 「木料 1 · 木板 2」
        bool station_ok = true;
        godot::String station; // 「工作台」
        bool can = false; // 眼下合得了
        godot::String why; // 合不了的原因
    };

    struct View {
        std::vector<ItemStack> pack;
        int selected = 0; // 快捷栏选中
        bool has_other = false;
        std::vector<ItemStack> other;
        godot::String other_title;
        std::vector<RecipeRow> recipes;
        godot::String stations; // 「身边：随身 · 工作台」
        ItemStack cursor;
        int focus_section = PACK, focus_index = 9;
        int hover_section = NONE, hover_index = -1;
        godot::Vector2 mouse;
        bool mouse_active = false; // 最近在用鼠标（拖着的那叠跟鼠标走）
    };

    struct Hit {
        int section = NONE;
        int index = -1;
    };

private:
    View view;
    godot::Vector2 origin; // 面板左上角（本控件坐标）
    godot::Vector2 panel_size;
    std::vector<godot::Rect2> pack_rects, other_rects, recipe_rects;

    void layout();
    void draw_slot(const godot::Rect2 &p_rect, const ItemStack &p_stack, bool p_focus, bool p_hover, bool p_selected);
    void draw_stack(const godot::Rect2 &p_rect, const ItemStack &p_stack, float p_alpha);
    void text(const godot::Vector2 &p_at, const godot::String &p_text, int p_size, const godot::Color &p_color, real_t p_width = -1,
            godot::HorizontalAlignment p_align = godot::HORIZONTAL_ALIGNMENT_LEFT);
    godot::String hover_text() const;

protected:
    static void _bind_methods();

public:
    InventoryPanel();
    void _draw() override;

    void refresh(const View &p_view);
    // 本控件坐标里的一点落在哪
    Hit hit(const godot::Vector2 &p_local) const;
    // 某一格 / 某条配方的中心（本控件坐标；没有返回 (−1, −1)）——测试拿它点鼠标
    godot::Vector2 center_of(int p_section, int p_index) const;
    // 键盘在网格里挪：这一格往 (dx, dy) 挪一步落到哪（跨过另一边 / 主区 / 快捷栏；往右出了网格进配方）
    Hit step(int p_section, int p_index, int p_dx, int p_dy) const;
};
