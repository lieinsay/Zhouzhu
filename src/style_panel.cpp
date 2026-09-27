#include "style_panel.h"

#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

const Color ROW_PLAIN(0.85f, 0.85f, 0.82f);
const Color ROW_PICKED(0.75f, 0.92f, 0.55f);
const Color ROW_DIM(0.6f, 0.6f, 0.58f);
const Color TITLE(0.7f, 0.9f, 1.0f);

Label *make_label(int p_font_size, const Color &p_color) {
    Label *label = memnew(Label);
    label->add_theme_font_size_override("font_size", p_font_size);
    label->add_theme_color_override("font_color", p_color);
    label->add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85f));
    label->add_theme_constant_override("outline_size", 4);
    label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    return label;
}

} // namespace

void StylePanel::_bind_methods() {}

void StylePanel::ensure_built() {
    if (column) {
        return;
    }
    Ref<StyleBoxFlat> box;
    box.instantiate();
    box->set_bg_color(Color(0.06f, 0.08f, 0.10f, 0.84f));
    box->set_border_color(Color(0.35f, 0.55f, 0.7f, 0.9f));
    box->set_border_width_all(2);
    box->set_corner_radius_all(6);
    box->set_content_margin_all(14);
    add_theme_stylebox_override("panel", box);
    set_custom_minimum_size(Vector2(420, 0));
    column = memnew(VBoxContainer);
    column->add_theme_constant_override("separation", 4);
    add_child(column);
    title = make_label(22, TITLE);
    title->set_text(String(U"整座样式（1–9 拿起一套）"));
    column->add_child(title);
    hint = make_label(14, ROW_DIM);
    hint->set_text(String(U"拿起来以后准星处出整座的虚影，E 一次摆下（背包里得有全部的件）\nN 把准星对着的那一座另存成样式 · G / Tab 合上"));
    column->add_child(hint);
}

void StylePanel::refresh(const std::vector<String> &p_names, const std::vector<String> &p_bills, int p_selected) {
    ensure_built();
    while (rows.size() < p_names.size()) {
        Label *row = make_label(17, ROW_PLAIN);
        column->add_child(row);
        column->move_child(row, int(rows.size()) + 1);
        rows.push_back(row);
    }
    for (size_t k = 0; k < rows.size(); k++) {
        if (k >= p_names.size()) {
            rows[k]->set_visible(false);
            continue;
        }
        rows[k]->set_visible(true);
        const String key = k < 9 ? vformat("%d  ", int(k) + 1) : String("    ");
        rows[k]->set_text(key + p_names[k] + String(U"　") + (k < p_bills.size() ? p_bills[k] : String()));
        rows[k]->add_theme_color_override("font_color", int(k) == p_selected ? ROW_PICKED : ROW_PLAIN);
    }
}
