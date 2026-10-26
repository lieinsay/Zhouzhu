#include "inventory_panel.h"

#include <algorithm>

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "crops.h"

using namespace godot;

namespace {

constexpr real_t SLOT = 52; // 一格的边长
constexpr real_t PITCH = 56; // 格距
constexpr int COLS = 9;
constexpr real_t PAD = 18;
constexpr real_t RECIPE_W = 380;
constexpr real_t RECIPE_ROW = 30;
constexpr int RECIPE_ONE_COLUMN_MAX = 14; // 配方多过这么多条就分两栏（默认窗口 648 高，一栏放不下）
constexpr real_t RECIPE_W_TWO = 270; // 两栏时每栏多宽
constexpr real_t RECIPE_COL_GAP = 8;
constexpr real_t GAP = 24; // 左右两块之间
constexpr real_t BOTTOM_RESERVE = 124; // 屏幕底下留给通知那一行

const Color BACK(0.08f, 0.07f, 0.06f, 0.95f);
const Color BORDER(0.55f, 0.48f, 0.32f, 0.95f);
const Color SLOT_BACK(0.16f, 0.14f, 0.12f, 0.95f);
const Color SLOT_EDGE(0.32f, 0.29f, 0.24f);
const Color FOCUS(1.0f, 0.9f, 0.45f);
const Color HOVER(0.85f, 0.85f, 0.8f);
const Color SELECTED(0.55f, 0.9f, 0.45f);
const Color TITLE(0.95f, 0.9f, 0.75f);
const Color LABEL(0.72f, 0.7f, 0.64f);
const Color BRIGHT(0.95f, 0.93f, 0.85f);
const Color DIM(0.55f, 0.53f, 0.5f);
const Color NEED(0.9f, 0.6f, 0.45f);
const Color OUTLINE(0, 0, 0, 0.85f);

String name_of(const std::string &p_item) {
    return String::utf8(catalog::item_name(p_item).c_str());
}

int rows_of(int p_count) {
    return (p_count + COLS - 1) / COLS;
}

// 配方分几栏、每栏几条、每栏多宽
void recipe_columns(int p_count, int &r_cols, int &r_per_col, real_t &r_col_w) {
    r_cols = p_count > RECIPE_ONE_COLUMN_MAX ? 2 : 1;
    r_per_col = std::max(1, (p_count + r_cols - 1) / r_cols);
    r_col_w = r_cols == 1 ? RECIPE_W : RECIPE_W_TWO;
}

} // namespace

void InventoryPanel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_item_art", "item"), &InventoryPanel::get_item_art);
}

InventoryPanel::InventoryPanel() {
    set_mouse_filter(MOUSE_FILTER_IGNORE); // 点击由编排转给背包部件，这里只答落在哪
}

void InventoryPanel::refresh(const View &p_view) {
    view = p_view;
    layout();
    queue_redraw();
}

// 算出每一格、每一条配方在哪（面板在本控件里居中）
void InventoryPanel::layout() {
    const real_t grid_w = COLS * PITCH;
    const int other_rows = view.has_other ? rows_of(int(view.other.size())) : 0;
    real_t left_h = 40; // 标题
    if (view.has_other) {
        left_h += 26 + other_rows * PITCH + 12;
    }
    left_h += 26 + 3 * PITCH + 10 + 26 + PITCH + 34; // 主区、快捷栏、说明
    int recipe_cols = 1, per_col = 1;
    real_t col_w = RECIPE_W;
    recipe_columns(int(view.recipes.size()), recipe_cols, per_col, col_w);
    const real_t right_w = recipe_cols * col_w + (recipe_cols - 1) * RECIPE_COL_GAP;
    const real_t right_h = 40 + real_t(per_col) * RECIPE_ROW + 10;
    panel_size = Vector2(PAD + grid_w + GAP + right_w + PAD, std::max(left_h, right_h) + 36 + PAD);
    // 居中，但让开底下的通知
    origin = Vector2((get_size().x - panel_size.x) * 0.5f, (get_size().y - BOTTOM_RESERVE - panel_size.y) * 0.5f).floor();
    origin = Vector2(std::max(real_t(0), origin.x), std::max(real_t(8), origin.y));

    pack_rects.assign(view.pack.size(), Rect2());
    other_rects.assign(view.other.size(), Rect2());
    recipe_rects.assign(view.recipes.size(), Rect2());
    const real_t x0 = origin.x + PAD;
    real_t y = origin.y + 40;
    if (view.has_other) {
        y += 26;
        for (int k = 0; k < int(view.other.size()); k++) {
            other_rects[size_t(k)] = Rect2(x0 + (k % COLS) * PITCH, y + (k / COLS) * PITCH, SLOT, SLOT);
        }
        y += other_rows * PITCH + 12;
    }
    y += 26;
    for (int k = COLS; k < int(view.pack.size()); k++) {
        const int m = k - COLS;
        pack_rects[size_t(k)] = Rect2(x0 + (m % COLS) * PITCH, y + (m / COLS) * PITCH, SLOT, SLOT);
    }
    y += 3 * PITCH + 10 + 26;
    for (int k = 0; k < std::min(COLS, int(view.pack.size())); k++) {
        pack_rects[size_t(k)] = Rect2(x0 + k * PITCH, y, SLOT, SLOT);
    }
    const real_t rx = x0 + grid_w + GAP;
    for (int k = 0; k < int(view.recipes.size()); k++) {
        recipe_rects[size_t(k)] = Rect2(rx + (k / per_col) * (col_w + RECIPE_COL_GAP), origin.y + 40 + (k % per_col) * RECIPE_ROW, col_w, RECIPE_ROW - 2);
    }
}

InventoryPanel::Hit InventoryPanel::hit(const Vector2 &p_local) const {
    for (int k = 0; k < int(pack_rects.size()); k++) {
        if (pack_rects[size_t(k)].has_point(p_local)) {
            return Hit{ PACK, k };
        }
    }
    for (int k = 0; k < int(other_rects.size()); k++) {
        if (other_rects[size_t(k)].has_point(p_local)) {
            return Hit{ OTHER, k };
        }
    }
    for (int k = 0; k < int(recipe_rects.size()); k++) {
        if (recipe_rects[size_t(k)].has_point(p_local)) {
            return Hit{ RECIPES, k };
        }
    }
    return Hit();
}

Vector2 InventoryPanel::center_of(int p_section, int p_index) const {
    const std::vector<Rect2> *rects = p_section == PACK ? &pack_rects : p_section == OTHER ? &other_rects : p_section == RECIPES ? &recipe_rects : nullptr;
    if (!rects || p_index < 0 || p_index >= int(rects->size())) {
        return Vector2(-1, -1);
    }
    return (*rects)[size_t(p_index)].get_center();
}

InventoryPanel::Hit InventoryPanel::step(int p_section, int p_index, int p_dx, int p_dy) const {
    // 网格的行，从上到下：另一边的每一行、主区三行、快捷栏
    struct Row {
        int section, first, count;
    };
    std::vector<Row> rows;
    if (view.has_other) {
        for (int r = 0; r < rows_of(int(view.other.size())); r++) {
            rows.push_back({ OTHER, r * COLS, std::min(COLS, int(view.other.size()) - r * COLS) });
        }
    }
    for (int r = 0; r < 3; r++) {
        rows.push_back({ PACK, COLS + r * COLS, COLS });
    }
    rows.push_back({ PACK, 0, COLS });
    const int recipes = int(view.recipes.size());
    if (p_section == RECIPES) {
        if (p_dx < 0) {
            const int r = std::clamp(p_index, 0, int(rows.size()) - 1);
            return Hit{ rows[size_t(r)].section, rows[size_t(r)].first + rows[size_t(r)].count - 1 };
        }
        return Hit{ RECIPES, std::clamp(p_index + p_dy, 0, std::max(0, recipes - 1)) };
    }
    int row = 0, col = 0;
    for (int r = 0; r < int(rows.size()); r++) {
        if (rows[size_t(r)].section == p_section && p_index >= rows[size_t(r)].first && p_index < rows[size_t(r)].first + rows[size_t(r)].count) {
            row = r;
            col = p_index - rows[size_t(r)].first;
        }
    }
    if (p_dx > 0 && col + p_dx >= rows[size_t(row)].count && recipes > 0) {
        return Hit{ RECIPES, std::min(row, recipes - 1) };
    }
    row = std::clamp(row + p_dy, 0, int(rows.size()) - 1);
    col = std::clamp(col + p_dx, 0, rows[size_t(row)].count - 1);
    return Hit{ rows[size_t(row)].section, rows[size_t(row)].first + col };
}

void InventoryPanel::text(const Vector2 &p_at, const String &p_text, int p_size, const Color &p_color, real_t p_width, HorizontalAlignment p_align) {
    const Ref<Font> font = get_theme_default_font();
    draw_string_outline(font, p_at, p_text, p_align, p_width, p_size, 4, OUTLINE);
    draw_string(font, p_at, p_text, p_align, p_width, p_size, p_color);
}

// 正式图标与动态数量分层；缺图保留原色块和名称。
void InventoryPanel::draw_stack(const Rect2 &p_rect, const ItemStack &p_stack, float p_alpha) {
    if (p_stack.empty()) {
        return;
    }
    float r = 0, g = 0, b = 0;
    catalog::item_color(p_stack.item, r, g, b);
    const Rect2 swatch = p_rect.grow(-7);
    if (!art.draw(this, p_rect.grow(-2), p_stack.item, p_alpha)) {
    draw_rect(swatch, Color(r, g, b, 0.85f * p_alpha));
    draw_rect(swatch, Color(r * 0.6f, g * 0.6f, b * 0.6f, p_alpha), false, 2);
    const String name = name_of(p_stack.item);
    const int size = name.length() <= 2 ? 17 : (name.length() == 3 ? 14 : 12);
    text(Vector2(p_rect.position.x, p_rect.position.y + p_rect.size.y * 0.5f + size * 0.35f), name, size, Color(1, 1, 1, p_alpha), p_rect.size.x, HORIZONTAL_ALIGNMENT_CENTER);
    }
    if (p_stack.count > 1) {
        text(Vector2(p_rect.position.x, p_rect.get_end().y - 3), vformat("%d", p_stack.count), 14, Color(1, 0.97f, 0.85f, p_alpha), p_rect.size.x - 4, HORIZONTAL_ALIGNMENT_RIGHT);
    }
}

void InventoryPanel::draw_slot(const Rect2 &p_rect, const ItemStack &p_stack, bool p_focus, bool p_hover, bool p_selected) {
    draw_rect(p_rect, SLOT_BACK);
    draw_rect(p_rect, SLOT_EDGE, false, 1);
    draw_stack(p_rect, p_stack, 1.0f);
    if (p_selected) {
        draw_rect(p_rect.grow(1), SELECTED, false, 3);
    }
    if (p_hover) {
        draw_rect(p_rect.grow(-1), HOVER, false, 2);
    }
    if (p_focus) {
        draw_rect(p_rect.grow(2), FOCUS, false, 2);
    }
}

String InventoryPanel::hover_text() const {
    int section = view.hover_section, index = view.hover_index;
    if (section == NONE) {
        section = view.focus_section;
        index = view.focus_index;
    }
    const ItemStack *stack = nullptr;
    if (section == PACK && index >= 0 && index < int(view.pack.size())) {
        stack = &view.pack[size_t(index)];
    } else if (section == OTHER && index >= 0 && index < int(view.other.size())) {
        stack = &view.other[size_t(index)];
    } else if (section == RECIPES && index >= 0 && index < int(view.recipes.size())) {
        const RecipeRow &row = view.recipes[size_t(index)];
        String line = name_of(row.product) + vformat(String(U" ×%d ← "), row.yield) + row.inputs + String(U" · ") + row.station;
        return row.can ? line + String(U" · 点一下合一份，Shift 合到料用完") : line + String(U" · ") + row.why;
    }
    if (!stack || stack->empty()) {
        return String();
    }
    return name_of(stack->item) + vformat(String(U" ×%d（一格最多 %d）"), stack->count, catalog::stack_of(stack->item));
}

void InventoryPanel::_draw() {
    layout(); // 窗口大小可能变了
    const Rect2 box(origin, panel_size);
    draw_rect(box, BACK);
    draw_rect(box, BORDER, false, 2);
    const real_t x0 = origin.x + PAD;
    text(Vector2(x0, origin.y + 30), String(U"背包"), 22, TITLE);

    auto is_focus = [&](int p_section, int p_index) { return !view.mouse_active && view.focus_section == p_section && view.focus_index == p_index; };
    auto is_hover = [&](int p_section, int p_index) { return view.mouse_active && view.hover_section == p_section && view.hover_index == p_index; };

    if (view.has_other && !other_rects.empty()) {
        text(Vector2(x0, other_rects[0].position.y - 8), view.other_title, 17, LABEL);
        for (int k = 0; k < int(other_rects.size()); k++) {
            draw_slot(other_rects[size_t(k)], view.other[size_t(k)], is_focus(OTHER, k), is_hover(OTHER, k), false);
        }
    }
    if (int(pack_rects.size()) > COLS) {
        text(Vector2(x0, pack_rects[COLS].position.y - 8), String(U"背包"), 17, LABEL);
        text(Vector2(x0, pack_rects[0].position.y - 8), String(U"快捷栏（1–9 选，拿在手上的就是选中那格）"), 17, LABEL);
    }
    for (int k = 0; k < int(pack_rects.size()); k++) {
        draw_slot(pack_rects[size_t(k)], view.pack[size_t(k)], is_focus(PACK, k), is_hover(PACK, k), k == view.selected);
    }
    const real_t hint_y = (pack_rects.empty() ? origin.y + 80 : pack_rects[0].get_end().y) + 26;
    text(Vector2(x0, hint_y), String(U"左键 拿/放 · 右键 拿一半/放一份 · Shift+左键 搬过去 · 1–9 放上快捷栏 · Tab 合上"), 14, DIM);
    text(Vector2(x0, hint_y + 20), String(U"键盘：WASD 挪 · E 左键 · Q 右键 · F 搬过去（配方上：E 合一份、F 合到底）"), 14, DIM);

    // 配方
    const real_t rx = x0 + COLS * PITCH + GAP;
    text(Vector2(rx, origin.y + 30), String(U"合成  ") + view.stations, 18, TITLE);
    for (int k = 0; k < int(view.recipes.size()); k++) {
        const RecipeRow &row = view.recipes[size_t(k)];
        const Rect2 &r = recipe_rects[size_t(k)];
        if (is_focus(RECIPES, k) || is_hover(RECIPES, k)) {
            draw_rect(r, Color(1, 1, 1, 0.08f));
            draw_rect(r, is_focus(RECIPES, k) ? FOCUS : HOVER, false, 1);
        }
        float cr = 0, cg = 0, cb = 0;
        catalog::item_color(row.product, cr, cg, cb);
        const float alpha = row.can ? 1.0f : 0.4f;
        if (!art.draw(this, Rect2(r.position + Vector2(1, 1), Vector2(26, 26)), row.product, alpha))
            draw_rect(Rect2(r.position + Vector2(4, 5), Vector2(18, 18)), Color(cr, cg, cb, alpha));
        const Color c = row.can ? BRIGHT : DIM;
        text(r.position + Vector2(30, 21), name_of(row.product) + (row.yield > 1 ? vformat(String(U" ×%d"), row.yield) : String()), 17, c);
        const real_t in_x = r.size.x >= RECIPE_W ? 128 : 104; // 两栏时窄些
        if (!row.station_ok) {
            text(r.position + Vector2(in_x, 21), String(U"要") + row.station, 15, NEED, r.size.x - in_x - 4);
        } else {
            for (size_t i = 0; i < row.ingredients.size(); ++i) {
                const ItemStack &ingredient = row.ingredients[i];
                const Vector2 at = r.position + Vector2(in_x + real_t(i) * 54, 3);
                if (!art.draw(this, Rect2(at, Vector2(24, 24)), ingredient.item, alpha)) {
                    text(at + Vector2(0, 18), name_of(ingredient.item), 10, c, 27);
                }
                text(at + Vector2(26, 18), vformat("%d", ingredient.count), 13, c, 26);
            }
        }
    }

    // 悬停的那一格 / 那条配方
    text(Vector2(x0, box.get_end().y - 14), hover_text(), 16, BRIGHT);

    // 拖着的那一叠：跟着鼠标（没用鼠标时跟着键盘光标）
    if (!view.cursor.empty()) {
        Vector2 at = view.mouse;
        if (!view.mouse_active) {
            const Vector2 c = center_of(view.focus_section, view.focus_index);
            at = c.x < 0 ? origin : c + Vector2(14, 14);
        }
        draw_stack(Rect2(at - Vector2(SLOT * 0.5f, SLOT * 0.5f), Vector2(SLOT, SLOT)), view.cursor, 0.95f);
    }
}
