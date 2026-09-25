#include "pack_hand.h"

#include <algorithm>

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/core/math.hpp>

#include "crops.h"
#include "input_actions.h"
#include "recipes.h"

using namespace godot;

namespace {

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

constexpr int MAX_CRAFT_ALL = 64; // Shift 合到底：一回最多合这么多次

} // namespace

String PackHand::name_of(const std::string &p_item) {
    return utf8(catalog::item_name(p_item));
}

void PackHand::setup(const Dictionary &p_starting, InventoryPanel *p_panel, HotbarBar *p_bar) {
    panel = p_panel;
    bar = p_bar;
    pack = ItemSlots(SLOTS, HOTBAR);
    selected = 0;
    cursor = ItemStack();
    const Array keys = p_starting.keys();
    for (int k = 0; k < keys.size(); k++) {
        pack.put(play::to_std(keys[k]), int(p_starting[keys[k]]));
    }
    refresh_bar();
}

std::string PackHand::held_seed() const {
    const std::string &item = held_item();
    return catalog::is_seed(item) ? item : std::string();
}

void PackHand::select(int p_slot) {
    selected = Math::clamp(p_slot, 0, HOTBAR - 1);
    refresh_bar();
}

void PackHand::scroll(int p_step) {
    select(((selected + p_step) % HOTBAR + HOTBAR) % HOTBAR);
}

void PackHand::open_with(ItemSlots *p_other, const String &p_title) {
    open = true;
    other = p_other;
    other_title = p_title;
    focus_section = InventoryPanel::PACK;
    focus_index = HOTBAR;
    hover_section = InventoryPanel::NONE;
    hover_index = -1;
    mouse_active = false;
    panel->set_visible(true);
    bar->set_visible(false); // 面板里有快捷栏那一行
    refresh_panel();
}

void PackHand::close() {
    if (!open) {
        return;
    }
    slots::return_cursor(cursor, pack, other);
    open = false;
    other = nullptr;
    other_title = String();
    panel->set_visible(false);
    bar->set_visible(true);
    refresh_bar();
}

void PackHand::set_other(ItemSlots *p_other, const String &p_title) {
    if (!p_other && focus_section == InventoryPanel::OTHER) {
        focus_section = InventoryPanel::PACK;
        focus_index = HOTBAR;
    }
    other = p_other;
    other_title = p_title;
}

ItemSlots *PackHand::bag_of(int p_section) {
    return p_section == InventoryPanel::PACK ? &pack : (p_section == InventoryPanel::OTHER ? other : nullptr);
}

// 整叠搬过去：背包 ↔ 另一边；没有另一边时快捷栏 ↔ 主区
void PackHand::quick_move(int p_section, int p_index) {
    if (p_section == InventoryPanel::PACK) {
        if (other) {
            slots::move_stack(pack, p_index, *other);
        } else if (p_index < HOTBAR) {
            slots::move_stack(pack, p_index, pack, HOTBAR, SLOTS);
        } else {
            slots::move_stack(pack, p_index, pack, 0, HOTBAR);
        }
    } else if (p_section == InventoryPanel::OTHER && other && p_index >= 0 && p_index < other->size()) {
        // 进背包照背包的放法：先并、再主区空格、最后快捷栏
        const ItemStack stack = other->at(p_index);
        int left = pack.merge_into(stack.item, stack.count, 0, SLOTS);
        left = pack.fill_empty(stack.item, left, HOTBAR, SLOTS);
        left = pack.fill_empty(stack.item, left, 0, HOTBAR);
        other->set(p_index, stack.item, left);
    }
}

void PackHand::click(int p_section, int p_index, bool p_right, bool p_shift, const Say &p_say) {
    if (p_section == InventoryPanel::RECIPES) {
        if (!p_right) {
            craft(p_index, p_shift, p_say);
        }
        return;
    }
    ItemSlots *bag = bag_of(p_section);
    if (!bag || p_index < 0 || p_index >= bag->size()) {
        return;
    }
    if (p_shift && !p_right) {
        quick_move(p_section, p_index);
    } else if (p_right) {
        slots::right_click(*bag, p_index, cursor);
    } else {
        slots::left_click(*bag, p_index, cursor);
    }
}

// 数字键：光标（用鼠标时是悬停）那一格与快捷栏第 p_hotbar 格对调
void PackHand::swap_to_hotbar(int p_hotbar) {
    int section = focus_section, index = focus_index;
    if (mouse_active) {
        section = hover_section;
        index = hover_index;
    }
    ItemSlots *bag = bag_of(section);
    if (!bag || index < 0 || index >= bag->size()) {
        return;
    }
    slots::swap(*bag, index, pack, p_hotbar);
}

void PackHand::craft(int p_recipe, bool p_all, const Say &p_say) {
    if (p_recipe < 0 || p_recipe >= recipes::RECIPE_COUNT) {
        return;
    }
    const RecipeDef &recipe = recipes::RECIPES[p_recipe];
    int made = 0;
    std::string why;
    do {
        why = recipes::craft(recipe, stations, pack, cursor, other);
        made += why.empty() ? 1 : 0;
    } while (p_all && why.empty() && made < MAX_CRAFT_ALL);
    if (made == 0) {
        p_say(utf8(why));
        return;
    }
    const String product = name_of(recipe.product);
    p_say(made == 1 ? vformat(String(U"合了%s ×%d（背包 %d）"), product, recipe.yield, pack.count(recipe.product))
                    : vformat(String(U"合了 %d 回：%s ×%d（背包 %d）"), made, product, made * recipe.yield, pack.count(recipe.product)));
}

void PackHand::browse(bool p_interact, const Say &p_say) {
    Input *input = Input::get_singleton();
    int dx = 0, dy = 0;
    dx += input->is_action_just_pressed(input_actions::MOVE_RIGHT) ? 1 : 0;
    dx -= input->is_action_just_pressed(input_actions::MOVE_LEFT) ? 1 : 0;
    dy += input->is_action_just_pressed(input_actions::MOVE_BACK) ? 1 : 0;
    dy -= input->is_action_just_pressed(input_actions::MOVE_FORWARD) ? 1 : 0;
    if (dx != 0 || dy != 0) {
        const InventoryPanel::Hit next = panel->step(focus_section, focus_index, dx, dy);
        focus_section = next.section;
        focus_index = next.index;
        mouse_active = false;
    }
    for (int k = 0; k < HOTBAR; k++) {
        if (input->is_action_just_pressed(input_actions::HOTBAR[k])) {
            swap_to_hotbar(k);
        }
    }
    const bool split = input->is_action_just_pressed(input_actions::SPLIT);
    const bool quick = input->is_action_just_pressed(input_actions::QUICK_MOVE);
    if (focus_section == InventoryPanel::RECIPES) {
        if (p_interact || quick) {
            craft(focus_index, quick, p_say);
        }
    } else if (p_interact || split || quick) {
        click(focus_section, focus_index, split, quick, p_say);
    }
}

bool PackHand::on_mouse(const Ref<InputEvent> &p_event, const Say &p_say) {
    if (!open) {
        return false;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        mouse = motion->get_position();
        mouse_active = true;
        const InventoryPanel::Hit hit = panel->hit(mouse);
        hover_section = hit.section;
        hover_index = hit.index;
        refresh_panel();
        return true;
    }
    const Ref<InputEventMouseButton> button = p_event;
    if (!button.is_valid() || !button->is_pressed()) {
        return button.is_valid();
    }
    const MouseButton which = button->get_button_index();
    if (which != MOUSE_BUTTON_LEFT && which != MOUSE_BUTTON_RIGHT) {
        return false;
    }
    mouse = button->get_position();
    mouse_active = true;
    const InventoryPanel::Hit hit = panel->hit(mouse);
    hover_section = hit.section;
    hover_index = hit.index;
    if (hit.section != InventoryPanel::NONE) {
        click(hit.section, hit.index, which == MOUSE_BUTTON_RIGHT, button->is_shift_pressed(), p_say);
    }
    refresh_panel();
    return true;
}

Vector2 PackHand::slot_center(int p_section, int p_index) const {
    return panel ? panel->center_of(p_section, p_index) : Vector2(-1, -1);
}

void PackHand::refresh_bar() {
    if (!bar) {
        return;
    }
    std::vector<ItemStack> row(pack.get_slots().begin(), pack.get_slots().begin() + HOTBAR);
    bar->refresh(row, selected);
}

void PackHand::refresh_panel() {
    refresh_bar();
    if (!open || !panel) {
        return;
    }
    InventoryPanel::View v;
    v.pack = pack.get_slots();
    v.selected = selected;
    v.has_other = other != nullptr;
    if (other) {
        v.other = other->get_slots();
        v.other_title = other_title;
    }
    String near = String(U"身边：随身");
    for (int s = int(Station::HAND) + 1; s < int(Station::COUNT); s++) {
        if (stations & station_bit(Station(s))) {
            near += String(U" · ") + utf8(recipes::station_name(Station(s)));
        }
    }
    v.stations = near;
    for (int k = 0; k < recipes::RECIPE_COUNT; k++) {
        const RecipeDef &r = recipes::RECIPES[k];
        InventoryPanel::RecipeRow row;
        row.product = r.product;
        row.yield = r.yield;
        for (int i = 0; i < recipes::ingredient_count(r); i++) {
            row.inputs += (i > 0 ? String(U" · ") : String()) + name_of(r.in[i].item) + vformat(" %d", r.in[i].count);
        }
        row.station_ok = recipes::station_ok(r, stations);
        row.station = utf8(recipes::station_name(r.station));
        const std::string why = recipes::problem(r, stations, pack, cursor, other);
        row.can = why.empty();
        row.why = utf8(why);
        v.recipes.push_back(row);
    }
    v.cursor = cursor;
    v.focus_section = focus_section;
    v.focus_index = focus_index;
    v.hover_section = hover_section;
    v.hover_index = hover_index;
    v.mouse = mouse;
    v.mouse_active = mouse_active;
    panel->refresh(v);
}

String PackHand::summary() const {
    const ItemStack &held = pack.at(selected);
    String line = String(U"手持 ") + (held.empty() ? String(U"—") : name_of(held.item) + (held.count > 1 ? vformat(String(U" ×%d"), held.count) : String()));
    line += vformat(String(U" · 背包 %d / %d 格"), pack.used(), SLOTS);
    return line;
}
