#include "refit_panel.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

constexpr real_t MARGIN = 16;
constexpr real_t PAD = 12;
constexpr real_t PALETTE_W = 330;
constexpr real_t PALETTE_HEAD = 58; // 标题与说明
constexpr real_t CARD_GAP = 5;
constexpr real_t CARD_H_MIN = 46, CARD_H_MAX = 64;
constexpr real_t BOTTOM_RESERVE = 156; // 屏幕底下留给提示与通知那两行
constexpr real_t INFO_W = 370;
constexpr real_t BUTTON_W = 150, BUTTON_H = 32;
// 右上那块的行距（基线到基线）：标题与船况、一道线之后第一行的基线离顶多远；指着的一行；选中的名字、位置；拆不了的原因
constexpr real_t INFO_HEAD = 140, LINE_HOVER = 26, LINE_TITLE = 22, LINE_WHERE = 20, LINE_REASON = 20;

const Color BACK(0.08f, 0.07f, 0.06f, 0.9f);
const Color BORDER(0.55f, 0.48f, 0.32f, 0.95f);
const Color CARD_BACK(0.15f, 0.13f, 0.11f, 0.95f);
const Color CARD_EDGE(0.32f, 0.29f, 0.24f);
const Color HOVER(0.85f, 0.85f, 0.8f);
const Color DRAGGED(1.0f, 0.88f, 0.4f);
const Color TITLE(0.95f, 0.9f, 0.75f);
const Color BRIGHT(0.95f, 0.93f, 0.85f);
const Color PLAIN(0.82f, 0.8f, 0.74f);
const Color DIM(0.55f, 0.53f, 0.5f);
const Color GOOD(0.72f, 0.92f, 0.5f);
const Color WARN(0.95f, 0.55f, 0.45f);
const Color TAG(0.95f, 0.7f, 0.38f);
const Color OUTLINE(0, 0, 0, 0.85f);

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

Color look_color(ModuleLook p_look) {
    switch (p_look) {
        case ModuleLook::HELM:
            return Color(0.62f, 0.45f, 0.3f);
        case ModuleLook::CARGO:
            return Color(0.72f, 0.56f, 0.33f);
        case ModuleLook::SAIL:
            return Color(0.92f, 0.88f, 0.76f);
        case ModuleLook::FLOATSTONE:
            return Color(0.3f, 0.78f, 0.74f);
        case ModuleLook::BARE:
            return Color(0.78f, 0.66f, 0.46f);
    }
    return Color(0.7f, 0.7f, 0.7f);
}

Color tone_color(int p_tone) {
    return p_tone == RefitPanel::TONE_OK ? GOOD : (p_tone == RefitPanel::TONE_WARN ? WARN : DIM);
}

} // namespace

void RefitPanel::_bind_methods() {}

RefitPanel::RefitPanel() {
    set_mouse_filter(MOUSE_FILTER_IGNORE); // 点击由编排转给改装部件，这里只答落在哪
}

void RefitPanel::refresh(const View &p_view) {
    view = p_view;
    layout();
    queue_redraw();
}

void RefitPanel::layout() {
    const int kinds = int(ModuleKind::COUNT);
    const real_t available = get_size().y - MARGIN - BOTTOM_RESERVE - PALETTE_HEAD - PAD;
    card_h = std::clamp(std::floor(available / kinds - CARD_GAP), CARD_H_MIN, CARD_H_MAX);
    palette_rect = Rect2(MARGIN, MARGIN, PALETTE_W, PALETTE_HEAD + kinds * (card_h + CARD_GAP) + PAD - CARD_GAP);
    card_rects.assign(size_t(kinds), Rect2());
    for (int k = 0; k < kinds; k++) {
        card_rects[size_t(k)] = Rect2(MARGIN + PAD, MARGIN + PALETTE_HEAD + k * (card_h + CARD_GAP), PALETTE_W - 2 * PAD, card_h);
    }

    // 右上：船况四行 + 一道线，下面是指着的地方一行、选中的节点与拆除钮（行距与 draw_info 一致）
    real_t y = MARGIN + INFO_HEAD + LINE_HOVER; // 指着的那一行空着也留位置：不然鼠标挪到拆除钮上它一没，钮就往上跳
    remove_shown = view.has_selected;
    if (view.has_selected) {
        y += LINE_TITLE + LINE_WHERE + 12;
        remove_rect = Rect2(get_size().x - INFO_W - MARGIN + PAD, y, BUTTON_W, BUTTON_H);
        y += BUTTON_H + (!view.can_remove && !view.remove_reason.is_empty() ? LINE_REASON : 0);
    }
    info_rect = Rect2(get_size().x - INFO_W - MARGIN, MARGIN, INFO_W, y + 12 - MARGIN);
}

RefitPanel::Hit RefitPanel::hit(const Vector2 &p_local) const {
    for (int k = 0; k < int(card_rects.size()); k++) {
        if (card_rects[size_t(k)].has_point(p_local)) {
            return Hit{ HIT_CARD, k };
        }
    }
    if (remove_shown && remove_rect.has_point(p_local)) {
        return Hit{ HIT_REMOVE, 0 };
    }
    if (palette_rect.has_point(p_local) || info_rect.has_point(p_local)) {
        return Hit{ HIT_PANEL, -1 };
    }
    return Hit{};
}

Vector2 RefitPanel::card_center(ModuleKind p_kind) const {
    const size_t k = size_t(p_kind);
    return k < card_rects.size() ? card_rects[k].get_center() : Vector2(-1, -1);
}

Vector2 RefitPanel::remove_center() const {
    return remove_shown ? remove_rect.get_center() : Vector2(-1, -1);
}

void RefitPanel::text(const Vector2 &p_at, const String &p_text, int p_size, const Color &p_color, real_t p_width, HorizontalAlignment p_align) {
    const Ref<Font> font = get_theme_default_font();
    draw_string_outline(font, p_at, p_text, p_align, p_width, p_size, 4, OUTLINE);
    draw_string(font, p_at, p_text, p_align, p_width, p_size, p_color);
}

// 一张节点卡片：左边一块按节点样子着色的小图（几节就几块），名字与形状、能放几处，功能，规矩
void RefitPanel::draw_card(ModuleKind p_kind, const Rect2 &p_rect, bool p_hover, bool p_dragged, int p_spots, float p_alpha) {
    const ModuleDef &d = modules::def(p_kind);
    const float a = p_spots > 0 || p_dragged ? p_alpha : 0.55f * p_alpha;
    draw_rect(p_rect, Color(CARD_BACK.r, CARD_BACK.g, CARD_BACK.b, CARD_BACK.a * p_alpha));
    draw_rect(p_rect, Color(CARD_EDGE.r, CARD_EDGE.g, CARD_EDGE.b, p_alpha), false, 1);

    // 小图：前后 / 左右几节横着排，上下几节竖着摞
    const Color c = look_color(d.look);
    const int across = std::max(d.size_x, d.size_z), up = d.size_y;
    const real_t unit = std::min(real_t(16), (p_rect.size.y - 14) / up);
    const Vector2 glyph0 = p_rect.position + Vector2(10, 0.5f * (p_rect.size.y - unit * up));
    for (int i = 0; i < across; i++) {
        for (int j = 0; j < up; j++) {
            const Rect2 cell(glyph0 + Vector2(i * (unit + 2), (up - 1 - j) * (unit + 2) - (up - 1) * 1.0f), Vector2(unit, unit));
            draw_rect(cell, Color(c.r, c.g, c.b, 0.9f * a));
            draw_rect(cell, Color(c.r * 0.55f, c.g * 0.55f, c.b * 0.55f, a), false, 1);
        }
    }

    const real_t x = p_rect.position.x + 50, w = p_rect.size.x - 58;
    const real_t line1 = p_rect.position.y + std::min(real_t(22), 0.42f * p_rect.size.y);
    const real_t line2 = p_rect.position.y + p_rect.size.y - 8;
    text(Vector2(x, line1), utf8(d.name), 18, Color(BRIGHT.r, BRIGHT.g, BRIGHT.b, a));
    const Ref<Font> font = get_theme_default_font();
    const real_t name_w = font->get_string_size(utf8(d.name), HORIZONTAL_ALIGNMENT_LEFT, -1, 18).x;
    text(Vector2(x + name_w + 8, line1), utf8(modules::shape_name(d)), 13, Color(PLAIN.r, PLAIN.g, PLAIN.b, a));
    text(Vector2(x, line1), p_spots > 0 ? vformat(String(U"能放 %d 处"), p_spots) : String(U"眼下没处放"), 13, Color(p_spots > 0 ? GOOD.r : DIM.r, p_spots > 0 ? GOOD.g : DIM.g, p_spots > 0 ? GOOD.b : DIM.b, a), w,
            HORIZONTAL_ALIGNMENT_RIGHT);

    // 第二行：功能，后面跟规矩
    String rules;
    for (const std::string &tag : modules::rule_tags(d)) {
        rules += (rules.is_empty() ? String() : String(U" · ")) + utf8(tag);
    }
    const String function = utf8(modules::function_text(d));
    const real_t function_w = font->get_string_size(function, HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x;
    text(Vector2(x, line2), function, 13, Color(PLAIN.r, PLAIN.g, PLAIN.b, a), w);
    if (!rules.is_empty() && function_w + 12 < w) {
        text(Vector2(x + function_w + 10, line2), rules, 12, Color(TAG.r, TAG.g, TAG.b, a), w - function_w - 10);
    }

    if (p_dragged) {
        draw_rect(p_rect.grow(2), DRAGGED, false, 2);
    } else if (p_hover) {
        draw_rect(p_rect.grow(-1), HOVER, false, 2);
    }
}

// 右上：船况、鼠标指着的地方、选中的节点与拆除钮（位置 layout 已算好）
void RefitPanel::draw_info() {
    const real_t x = info_rect.position.x + PAD, w = info_rect.size.x - 2 * PAD, top = info_rect.position.y;
    text(Vector2(x, top + 30), String(U"改装空艇"), 22, TITLE);
    const ShipStats &s = view.stats;
    text(Vector2(x, top + 56), vformat(String(U"%d/%d 节 · 质量 %.1f · 推力 %.1f"), s.sections, MAX_CELLS, s.mass, s.thrust), 16, PLAIN, w);
    text(Vector2(x, top + 78), vformat(String(U"货位 %d · 船舱 %d 格"), s.hold, s.bay), 16, PLAIN, w);
    if (s.can_fly()) {
        text(Vector2(x, top + 100),
                vformat(String(U"航速 %d m/s · 升速 %d m/s · 转向 %d°/s"), int(std::lround(s.max_speed)), int(std::lround(s.max_climb)),
                        int(std::lround(s.max_yaw_rate * 180.0 / 3.14159265358979323846))),
                16, PLAIN, w);
    } else {
        text(Vector2(x, top + 100), String(U"没有帆也没有浮石舱，飞不起来"), 16, WARN, w);
    }
    draw_line(Vector2(x, top + 116), Vector2(x + w, top + 116), Color(BORDER.r, BORDER.g, BORDER.b, 0.5f), 1);
    real_t y = top + INFO_HEAD;
    text(Vector2(x, y), view.hover_text, 16, tone_color(view.hover_tone), w);
    y += LINE_HOVER;
    if (!view.has_selected) {
        text(Vector2(x, y), String(U"点船上的节点选中它，就能拆"), 14, DIM, w);
        return;
    }
    text(Vector2(x, y), String(U"选中：") + view.selected_title, 18, BRIGHT, w);
    text(Vector2(x, y + LINE_TITLE), view.selected_where, 15, PLAIN, w);
    text(Vector2(x, y + LINE_TITLE + LINE_WHERE), view.selected_detail, 15, PLAIN, w);
    const Color edge = view.can_remove ? WARN : DIM;
    draw_rect(remove_rect, view.hover_remove && view.can_remove ? Color(0.45f, 0.16f, 0.12f, 0.95f) : Color(0.22f, 0.12f, 0.1f, 0.95f));
    draw_rect(remove_rect, edge, false, view.hover_remove ? 2 : 1);
    text(Vector2(remove_rect.position.x, remove_rect.position.y + 22), String(U"拆除（X）"), 16, view.can_remove ? BRIGHT : DIM, BUTTON_W, HORIZONTAL_ALIGNMENT_CENTER);
    if (!view.can_remove && !view.remove_reason.is_empty()) {
        text(Vector2(x, remove_rect.get_end().y + LINE_REASON), view.remove_reason, 14, WARN, w);
    }
}

void RefitPanel::_draw() {
    layout(); // 窗口大小可能变了

    // 左边：节点卡片
    draw_rect(palette_rect, BACK);
    draw_rect(palette_rect, BORDER, false, 2);
    text(Vector2(palette_rect.position.x + PAD, palette_rect.position.y + 30), String(U"船节点"), 22, TITLE);
    text(Vector2(palette_rect.position.x + PAD, palette_rect.position.y + 50), String(U"按住拖到船上亮着的地方；点一下拿着再点船上"), 13, DIM,
            palette_rect.size.x - 2 * PAD);
    for (int k = 0; k < int(card_rects.size()); k++) {
        const int spots = k < int(view.spots.size()) ? view.spots[size_t(k)] : 0;
        draw_card(ModuleKind(k), card_rects[size_t(k)], view.hover_card == k, view.drag_kind == k, spots, 1.0f);
    }

    // 右上：船况、指着的地方、选中的节点
    draw_rect(info_rect, BACK);
    draw_rect(info_rect, BORDER, false, 2);
    draw_info();

    // 拖着的节点：一枚小标签跟着鼠标（色块 + 名字），别挡住船上的虚影
    if (view.drag_kind >= 0 && view.drag_kind < int(ModuleKind::COUNT)) {
        const ModuleDef &d = modules::def(ModuleKind(view.drag_kind));
        const String name = utf8(d.name) + String(U" · ") + utf8(modules::shape_name(d));
        const real_t w = get_theme_default_font()->get_string_size(name, HORIZONTAL_ALIGNMENT_LEFT, -1, 15).x + 34;
        const Rect2 r(view.mouse + Vector2(18, 14), Vector2(w, 26));
        draw_rect(r, Color(BACK.r, BACK.g, BACK.b, 0.8f));
        draw_rect(r, DRAGGED, false, 1);
        const Color c = look_color(d.look);
        draw_rect(Rect2(r.position + Vector2(7, 7), Vector2(12, 12)), c);
        text(r.position + Vector2(26, 19), name, 15, BRIGHT);
    }
}
