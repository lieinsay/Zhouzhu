#include "build_panel.h"

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
const Color ROW_OK(0.75f, 0.92f, 0.55f);
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

void BuildPanel::_bind_methods() {}

void BuildPanel::ensure_built() {
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
    set_custom_minimum_size(Vector2(400, 0));

    column = memnew(VBoxContainer);
    column->add_theme_constant_override("separation", 4);
    add_child(column);

    title = make_label(22, TITLE);
    column->add_child(title);

    cursor_label = make_label(18, ROW_CURSOR);
    column->add_child(cursor_label);
    status_label = make_label(16, ROW_PLAIN);
    column->add_child(status_label);

    palette_title = make_label(15, ROW_DIM);
    palette_title->set_text(String(U"要放的建筑（1 / 2 / 3 / 4 选）"));
    column->add_child(palette_title);
    for (int k = 0; k < buildings::COUNT && k < 8; k++) {
        palette[k] = make_label(18, ROW_PLAIN);
        column->add_child(palette[k]);
    }

    file_label = make_label(15, ROW_DIM);
    column->add_child(file_label);

    hint = make_label(14, ROW_DIM);
    hint->set_text(String(U"WASD 挪光标（Shift 快）· Q/R 转向 · 鼠标转视角 · 滚轮远近\nE 放一座 · X 拆掉光标下的 · F5 存盘 · Tab 合上"));
    column->add_child(hint);
}

void BuildPanel::refresh(const String &p_island, const Vector3 &p_local_cursor, double p_yaw_deg, const String &p_status, bool p_ok,
        int p_kind, int p_count, const String &p_layout_path, bool p_dirty) {
    ensure_built();
    title->set_text(String(U"布置建筑（开发者）· ") + p_island);
    cursor_label->set_text(vformat(String(U"光标：x %+.1f  z %+.1f  地面 %.1f m · 门朝 %d°"), p_local_cursor.x, p_local_cursor.z, p_local_cursor.y, int(std::lround(p_yaw_deg))));
    status_label->set_text(p_status);
    status_label->add_theme_color_override("font_color", p_ok ? ROW_OK : ROW_WARN);

    for (int k = 0; k < buildings::COUNT && k < 8; k++) {
        const BuildingDef &d = buildings::at(k);
        const bool chosen = k == p_kind;
        palette[k]->set_text((chosen ? String(U"▸ ") : String(U"   ")) + vformat(String(U"%d  %s    %.0f × %.0f m · 高 %.1f m"), k + 1, String::utf8(d.name), d.width_m, d.depth_m, d.plinth_m + d.wall_m + d.roof_m));
        palette[k]->add_theme_color_override("font_color", chosen ? ROW_CURSOR : ROW_PLAIN);
    }

    String file = vformat(String(U"清单 %d 座 · "), p_count);
    if (p_layout_path.is_empty()) {
        file += String(U"没设 layout_path，存不了");
        file_label->add_theme_color_override("font_color", ROW_WARN);
    } else {
        file += p_layout_path + (p_dirty ? String(U" · 有改动未存（F5）") : String(U" · 已存"));
        file_label->add_theme_color_override("font_color", p_dirty ? ROW_CURSOR : ROW_DIM);
    }
    file_label->set_text(file);
}
