#include "hotbar_bar.h"

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "crops.h"

using namespace godot;

namespace {

constexpr real_t SLOT = 46;
constexpr real_t PITCH = 50;
constexpr real_t BOTTOM = 12; // 离屏幕下沿

const Color OUTLINE(0, 0, 0, 0.85f);

} // namespace

void HotbarBar::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_item_art", "item"), &HotbarBar::get_item_art);
}

HotbarBar::HotbarBar() {
    set_mouse_filter(MOUSE_FILTER_IGNORE);
}

void HotbarBar::refresh(const std::vector<ItemStack> &p_slots, int p_selected) {
    bool same = p_selected == selected && p_slots.size() == slots.size();
    for (size_t k = 0; same && k < slots.size(); k++) {
        same = slots[k].item == p_slots[k].item && slots[k].count == p_slots[k].count;
    }
    if (same) {
        return;
    }
    slots = p_slots;
    selected = p_selected;
    queue_redraw();
}

void HotbarBar::_draw() {
    const int n = int(slots.size());
    if (n == 0) {
        return;
    }
    const Ref<Font> font = get_theme_default_font();
    auto text = [&](const Vector2 &p_at, const String &p_text, int p_size, const Color &p_color, real_t p_width, HorizontalAlignment p_align) {
        draw_string_outline(font, p_at, p_text, p_align, p_width, p_size, 4, OUTLINE);
        draw_string(font, p_at, p_text, p_align, p_width, p_size, p_color);
    };
    const Vector2 size = get_size();
    const real_t width = n * PITCH;
    const Vector2 corner((size.x - width) * 0.5f, size.y - BOTTOM - SLOT);
    draw_rect(Rect2(corner - Vector2(6, 6), Vector2(width + 8, SLOT + 12)), Color(0.08f, 0.07f, 0.06f, 0.6f));
    for (int k = 0; k < n; k++) {
        const Rect2 r(corner + Vector2(k * PITCH, 0), Vector2(SLOT, SLOT));
        draw_rect(r, Color(0.16f, 0.14f, 0.12f, 0.85f));
        draw_rect(r, Color(0.35f, 0.32f, 0.26f), false, 1);
        const ItemStack &s = slots[size_t(k)];
        if (!s.empty()) {
            float cr = 0, cg = 0, cb = 0;
            catalog::item_color(s.item, cr, cg, cb);
            if (!art.draw(this, r.grow(-1), s.item)) {
            draw_rect(r.grow(-6), Color(cr, cg, cb, 0.85f));
            const String name = String::utf8(catalog::item_name(s.item).c_str());
            const int fs = name.length() <= 2 ? 15 : (name.length() == 3 ? 13 : 11);
            text(Vector2(r.position.x, r.position.y + SLOT * 0.5f + fs * 0.35f), name, fs, Color(1, 1, 1), SLOT, HORIZONTAL_ALIGNMENT_CENTER);
            }
            if (s.count > 1) {
                text(Vector2(r.position.x, r.get_end().y - 2), vformat("%d", s.count), 13, Color(1, 0.97f, 0.85f), SLOT - 3, HORIZONTAL_ALIGNMENT_RIGHT);
            }
        }
        text(r.position + Vector2(3, 12), vformat("%d", k + 1), 11, Color(0.8f, 0.78f, 0.7f), -1, HORIZONTAL_ALIGNMENT_LEFT);
        if (k == selected) {
            draw_rect(r.grow(2), Color(0.55f, 0.9f, 0.45f), false, 3);
        }
    }
}
