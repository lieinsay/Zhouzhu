#include "dev_works_panel.h"

#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

const Color ROW_PLAIN(0.85f, 0.85f, 0.82f);
const Color ROW_CURSOR(1.0f, 0.95f, 0.6f);
const Color ROW_HELD(0.75f, 0.92f, 0.55f);
const Color ROW_DIM(0.6f, 0.6f, 0.58f);
const Color TITLE(1.0f, 0.8f, 0.45f);

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

void DevWorksPanel::_bind_methods() {}

void DevWorksPanel::ensure_built() {
    if (column) {
        return;
    }
    Ref<StyleBoxFlat> box;
    box.instantiate();
    box->set_bg_color(Color(0.10f, 0.07f, 0.04f, 0.86f));
    box->set_border_color(Color(0.8f, 0.6f, 0.3f, 0.9f));
    box->set_border_width_all(2);
    box->set_corner_radius_all(6);
    box->set_content_margin_all(14);
    add_theme_stylebox_override("panel", box);
    set_custom_minimum_size(Vector2(380, 0));
    column = memnew(VBoxContainer);
    column->add_theme_constant_override("separation", 2);
    add_child(column);
    title = make_label(22, TITLE);
    column->add_child(title);
    hint = make_label(14, ROW_DIM);
    hint->set_text(String(U"W/S 或 ↑↓ 选 · E 拿在手上（不看背包、不扣料）· A/D 或 ←→ 换笔刷\n拿着挖 / 垒时 [ ] 也换笔刷 · 1–9 拿回快捷栏那格 · F8 / Tab 合上"));
    column->add_child(hint);
}

void DevWorksPanel::refresh(const std::vector<String> &p_rows, int p_cursor, int p_held, int p_brush) {
    ensure_built();
    title->set_text(vformat(String(U"开发者营造（god）· 笔刷 %d × %d"), p_brush, p_brush));
    while (rows.size() < p_rows.size()) {
        Label *row = make_label(16, ROW_PLAIN);
        column->add_child(row);
        column->move_child(row, int(rows.size()) + 1);
        rows.push_back(row);
    }
    for (size_t k = 0; k < rows.size(); k++) {
        if (k >= p_rows.size()) {
            rows[k]->set_visible(false);
            continue;
        }
        rows[k]->set_visible(true);
        const bool cursor = int(k) == p_cursor;
        const bool held = int(k) == p_held;
        rows[k]->set_text((cursor ? String(U"▶ ") : String(U"   ")) + p_rows[k] + (held ? String(U"　（手上）") : String()));
        rows[k]->add_theme_color_override("font_color", cursor ? ROW_CURSOR : held ? ROW_HELD : ROW_PLAIN);
    }
}
