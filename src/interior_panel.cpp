#include "interior_panel.h"

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
const char32_t *FACING[4] = { U"朝南（+z）", U"朝东（+x）", U"朝北（−z）", U"朝西（−x）" };

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

void InteriorPanel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_asset_art", "asset"), &InteriorPanel::get_asset_art);
}

void InteriorPanel::ensure_built() {
    if (column) {
        return;
    }
    Ref<StyleBoxFlat> box;
    box.instantiate();
    box->set_bg_color(Color(0.06f, 0.08f, 0.10f, 0.84f));
    box->set_border_color(Color(0.7f, 0.55f, 0.35f, 0.9f));
    box->set_border_width_all(2);
    box->set_corner_radius_all(6);
    box->set_content_margin_all(14);
    add_theme_stylebox_override("panel", box);
    set_custom_minimum_size(Vector2(430, 0));

    column = memnew(VBoxContainer);
    column->add_theme_constant_override("separation", 4);
    add_child(column);
    title = make_label(22, TITLE);
    column->add_child(title);
    cursor_label = make_label(18, ROW_CURSOR);
    column->add_child(cursor_label);
    status_label = make_label(16, ROW_PLAIN);
    column->add_child(status_label);
    tools_label = make_label(16, ROW_PLAIN);
    preview = memnew(TextureRect);
    preview->set_custom_minimum_size(Vector2(96, 96));
    preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
    preview->set_tooltip_text(String(U"当前家具真实模型；尚缺精绘材质与细致造型"));
    column->add_child(preview);
    column->add_child(tools_label);
    problems_label = make_label(15, ROW_WARN);
    column->add_child(problems_label);
    file_label = make_label(15, ROW_DIM);
    column->add_child(file_label);
    hint = make_label(14, ROW_DIM);
    hint->set_text(String(U"WASD 挪光标（Shift 快）· 鼠标转视角 · 滚轮远近 · 空格 / Ctrl 上下层\nZ / V 换工具 · Q/R 转向 · E 放（隔墙：起点、终点各按一次）· X 拆\nN 另存为新模板（这座改用它）· F5 存盘 · Tab 合上"));
    column->add_child(hint);
}

void InteriorPanel::refresh(const View &p_view) {
    ensure_built();
    preview->set_visible(!p_view.preview_asset.is_empty());
    if (!p_view.preview_asset.is_empty()) preview->set_texture(art.texture_asset(std::string(p_view.preview_asset.utf8().get_data())));
    title->set_text(vformat(String(U"房屋模板（开发者）· %s · 第 %d / %d 层"), p_view.name, p_view.level + 1, p_view.levels));
    cursor_label->set_text(vformat(String(U"光标：x %+.2f  z %+.2f · %s"), p_view.x, p_view.z, String(FACING[p_view.quarter & 3])));
    status_label->set_text(p_view.status);
    status_label->add_theme_color_override("font_color", p_view.ok ? ROW_OK : ROW_WARN);

    // 工具板：选中的一样描出来，前后各带两样，Z / V 往两边翻
    const int n = int(p_view.tools.size());
    String row;
    for (int d = -2; d <= 2 && n > 0; d++) {
        const int k = ((p_view.tool + d) % n + n) % n;
        row += d == 0 ? String(U"【") + p_view.tools[k] + String(U"】") : String(U" ") + p_view.tools[k] + String(U" ");
    }
    tools_label->set_text(vformat(String(U"工具 %d / %d："), p_view.tool + 1, n) + row + (p_view.drawing_wall ? String(U" · 隔墙起点已定，挪到终点按 E") : String()));

    if (p_view.problems.is_empty()) {
        problems_label->set_text(String(U"这套模板没毛病：处处走得到"));
        problems_label->add_theme_color_override("font_color", ROW_OK);
    } else {
        String text;
        for (int64_t k = 0; k < p_view.problems.size() && k < 3; k++) {
            text += (k > 0 ? String("\n") : String()) + String(U"· ") + p_view.problems[k];
        }
        if (p_view.problems.size() > 3) {
            text += vformat(String(U"\n……还有 %d 条"), int(p_view.problems.size() - 3));
        }
        problems_label->set_text(text);
        problems_label->add_theme_color_override("font_color", ROW_WARN);
    }
    file_label->set_text(p_view.id + String(U" → ") + p_view.path + (p_view.dirty ? String(U" · 有改动未存（F5）") : String(U" · 已存")));
    file_label->add_theme_color_override("font_color", p_view.dirty ? ROW_CURSOR : ROW_DIM);
}
