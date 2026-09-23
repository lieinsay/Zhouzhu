#include "pack_hand.h"

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/math.hpp>

#include "crops.h"
#include "input_actions.h"

using namespace godot;

String PackHand::name_of(const std::string &p_item) {
    return String::utf8(catalog::item_name(p_item).c_str());
}

void PackHand::setup(const Dictionary &p_starting, BackpackPanel *p_panel) {
    panel = p_panel;
    pack = Inventory();
    held.clear();
    const Array keys = p_starting.keys();
    for (int k = 0; k < keys.size(); k++) {
        pack.add(play::to_std(keys[k]), int(p_starting[keys[k]]));
    }
}

void PackHand::set_open(bool p_open) {
    if (open == p_open) {
        return;
    }
    open = p_open;
    panel->set_visible(open);
    if (!open) {
        return;
    }
    cursor = 0;
    const std::vector<ItemStack> &stacks = pack.get_stacks();
    for (int k = 0; k < int(stacks.size()); k++) {
        if (stacks[size_t(k)].item == held) {
            cursor = k;
        }
    }
}

void PackHand::browse(bool p_interact, const Say &p_say) {
    const int n = pack.size();
    if (n == 0) {
        cursor = 0;
        return;
    }
    Input *input = Input::get_singleton();
    if (input->is_action_just_pressed(input_actions::MOVE_FORWARD)) {
        cursor = (cursor + n - 1) % n;
    }
    if (input->is_action_just_pressed(input_actions::MOVE_BACK)) {
        cursor = (cursor + 1) % n;
    }
    cursor = Math::clamp(cursor, 0, n - 1);
    if (!p_interact) {
        return;
    }
    const std::string &item = pack.get_stacks()[size_t(cursor)].item;
    if (!catalog::is_seed(item)) {
        p_say(name_of(item) + String(U"不是种子，种不了"));
    } else if (held == item) {
        held.clear();
        p_say(String(U"放下了") + name_of(item) + String(U"种"));
    } else {
        held = item;
        p_say(String(U"手上拿着") + name_of(item) + String(U"种，去田里按住 E 播"));
    }
}

void PackHand::refresh_panel() {
    panel->refresh(pack, cursor, held);
}

String PackHand::summary() const {
    String line = String(U"背包");
    if (pack.empty()) {
        line += String(U" 空");
    }
    for (const ItemStack &stack : pack.get_stacks()) {
        line += vformat(String(U" %s %d"), name_of(stack.item), stack.count);
    }
    line += held.empty() ? String(U"    手持 —") : String(U"    手持 ") + name_of(held) + String(U"种");
    return line;
}
