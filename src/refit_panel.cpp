#include "refit_panel.h"

#include <cmath>

#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

const Color ROW_PLAIN(0.85f, 0.85f, 0.82f);
const Color ROW_CURSOR(1.0f, 0.96f, 0.72f);
const Color ROW_DIM(0.6f, 0.6f, 0.58f);
const Color ROW_WARN(0.95f, 0.55f, 0.45f);

Label *make_label(int p_font_size, const Color &p_color) {
    Label *label = memnew(Label);
    label->add_theme_font_size_override("font_size", p_font_size);
    label->add_theme_color_override("font_color", p_color);
    label->add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85f));
    label->add_theme_constant_override("outline_size", 4);
    return label;
}

} // namespace

void RefitPanel::_bind_methods() {}

void RefitPanel::ensure_built() {
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
    set_custom_minimum_size(Vector2(340, 0));

    column = memnew(VBoxContainer);
    column->add_theme_constant_override("separation", 4);
    add_child(column);

    title = make_label(22, Color(0.95f, 0.9f, 0.75f));
    title->set_text(String(U"改装空艇 · 自船头到船尾"));
    column->add_child(title);

    stats_label = make_label(16, ROW_PLAIN);
    column->add_child(stats_label);

    hint = make_label(15, ROW_DIM);
    hint->set_text(String(U"A/D 选节 · W/S 换类型 · E 加一节 · X 拆掉 · Tab 合上"));
    column->add_child(hint);
}

void RefitPanel::refresh(const ShipLayout &p_layout, int p_cursor, const ShipStats &p_stats) {
    ensure_built();
    const int wanted = int(p_layout.size());
    while (int(rows.size()) < wanted) {
        Label *row = make_label(19, ROW_PLAIN);
        column->add_child(row);
        column->move_child(row, int(rows.size()) + 1); // 排在标题后、船况前
        rows.push_back(row);
    }
    for (int k = wanted; k < int(rows.size()); k++) {
        rows[size_t(k)]->set_visible(false);
    }
    for (int k = 0; k < wanted; k++) {
        const ModuleDef &d = modules::def(p_layout[size_t(k)]);
        String text = (k == p_cursor ? String(U"▸ ") : String(U"   ")) + vformat(String(U"第 %d 节  %s"), k + 1, String::utf8(d.name));
        if (k == 0) {
            text += String(U"    ← 船头");
        } else if (k == wanted - 1) {
            text += String(U"    ← 船尾");
        }
        Label *row = rows[size_t(k)];
        row->set_visible(true);
        row->set_text(text);
        row->add_theme_color_override("font_color", k == p_cursor ? ROW_CURSOR : ROW_PLAIN);
    }
    String stats = vformat(String(U"%d/%d 节 · 货舱 %d 格"), p_stats.sections, MAX_SECTIONS, p_stats.hold);
    if (p_stats.can_fly()) {
        stats += vformat(String(U"\n航速 %d m/s · 升速 %d m/s · 转向 %d°/s"), int(std::lround(p_stats.max_speed)), int(std::lround(p_stats.max_climb)),
                int(std::lround(p_stats.max_yaw_rate * 180.0 / 3.14159265358979323846)));
        stats_label->add_theme_color_override("font_color", ROW_PLAIN);
    } else {
        stats += String(U"\n没有动力区，飞不起来");
        stats_label->add_theme_color_override("font_color", ROW_WARN);
    }
    stats_label->set_text(stats);
}
