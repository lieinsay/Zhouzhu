#include "backpack_panel.h"

#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "crops.h"

using namespace godot;

namespace {

const Color ROW_PLAIN(0.85f, 0.85f, 0.82f);
const Color ROW_CURSOR(1.0f, 0.96f, 0.72f);
const Color ROW_HELD(0.75f, 0.92f, 0.55f);
const Color ROW_DIM(0.6f, 0.6f, 0.58f);

Label *make_label(int p_font_size, const Color &p_color) {
    Label *label = memnew(Label);
    label->add_theme_font_size_override("font_size", p_font_size);
    label->add_theme_color_override("font_color", p_color);
    label->add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85f));
    label->add_theme_constant_override("outline_size", 4);
    return label;
}

} // namespace

void BackpackPanel::_bind_methods() {}

void BackpackPanel::ensure_built() {
    if (column) {
        return;
    }
    Ref<StyleBoxFlat> box;
    box.instantiate();
    box->set_bg_color(Color(0.08f, 0.07f, 0.06f, 0.82f));
    box->set_border_color(Color(0.55f, 0.48f, 0.32f, 0.9f));
    box->set_border_width_all(2);
    box->set_corner_radius_all(6);
    box->set_content_margin_all(14);
    add_theme_stylebox_override("panel", box);
    set_custom_minimum_size(Vector2(260, 0));

    column = memnew(VBoxContainer);
    column->add_theme_constant_override("separation", 4);
    add_child(column);

    title = make_label(22, Color(0.95f, 0.9f, 0.75f));
    title->set_text(String(U"背包"));
    column->add_child(title);

    hint = make_label(15, ROW_DIM);
    hint->set_text(String(U"W/S 选 · E 拿在手上 · Tab 合上"));
    column->add_child(hint);
}

void BackpackPanel::refresh(const Inventory &p_pack, int p_cursor, const std::string &p_held) {
    ensure_built();
    const std::vector<ItemStack> &stacks = p_pack.get_stacks();
    const int wanted = stacks.empty() ? 1 : int(stacks.size());
    while (int(rows.size()) < wanted) {
        Label *row = make_label(19, ROW_PLAIN);
        column->add_child(row);
        column->move_child(row, int(rows.size()) + 1); // 排在标题后、提示前
        rows.push_back(row);
    }
    for (int k = wanted; k < int(rows.size()); k++) {
        rows[size_t(k)]->set_visible(false);
    }
    if (stacks.empty()) {
        rows[0]->set_visible(true);
        rows[0]->set_text(String(U"（空空如也）"));
        rows[0]->add_theme_color_override("font_color", ROW_DIM);
        return;
    }
    for (int k = 0; k < int(stacks.size()); k++) {
        const ItemStack &stack = stacks[size_t(k)];
        const bool seed = catalog::is_seed(stack.item);
        const bool held = seed && stack.item == p_held;
        String text = (k == p_cursor ? String(U"▸ ") : String(U"   ")) + String::utf8(catalog::item_name(stack.item).c_str()) + vformat(String(U" ×%d"), stack.count);
        if (held) {
            text += String(U"    种子 · 手持");
        } else if (seed) {
            text += String(U"    种子");
        }
        Label *row = rows[size_t(k)];
        row->set_visible(true);
        row->set_text(text);
        row->add_theme_color_override("font_color", k == p_cursor ? ROW_CURSOR : (held ? ROW_HELD : (seed ? ROW_PLAIN : ROW_DIM)));
    }
}
