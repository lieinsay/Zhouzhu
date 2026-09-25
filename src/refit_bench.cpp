#include "refit_bench.h"

#include <godot_cpp/classes/input.hpp>

#include "input_actions.h"

using namespace godot;

void RefitBench::setup(RefitPanel *p_panel, RefitOverlay *p_overlay) {
    panel = p_panel;
    overlay = p_overlay;
}

Shipyard *RefitBench::bench_near(const std::vector<Shipyard *> &p_yards, const Vector3 &p_at, double p_range) {
    for (Shipyard *yard : p_yards) {
        if (yard->is_present() && p_at.distance_to(yard->get_bench_position()) <= p_range) {
            return yard;
        }
    }
    return nullptr;
}

bool RefitBench::ship_in(const Ship *p_ship, const Shipyard *p_yard) {
    return p_ship && p_yard && p_yard->is_present() && !p_ship->is_mooring() && p_ship->is_at(p_yard->get_cradle_contact(), IN_YARD_TOLERANCE);
}

bool RefitBench::open_at(Shipyard *p_yard, Ship *p_ship, const Say &p_say) {
    if (!ship_in(p_ship, p_yard)) {
        p_say(String(U"船坞里没有船：掌舵开到船坞跟前按 E 入坞，再来改"));
        return false;
    }
    open = true;
    yard = p_yard;
    panel->set_visible(true);
    overlay->set_visible(true);
    // 光标落在驾驶区上，选中的模块默认储存区
    const Module *bridge = p_ship->get_layout_model().bridge();
    cursor = bridge ? bridge->at : Cell{ 0, 0, 0 };
    kind = ModuleKind::STORAGE;
    overlay->show_for(p_ship->get_layout_model(), p_ship->get_global_transform());
    overlay->get_camera()->make_current();
    refresh(p_ship);
    return true;
}

void RefitBench::close(Player *p_player) {
    if (!open) {
        return;
    }
    open = false;
    yard = nullptr;
    panel->set_visible(false);
    overlay->set_visible(false);
    p_player->get_camera()->make_current();
}

void RefitBench::orbit(double p_dx, double p_dy) {
    overlay->orbit(-p_dx * MOUSE_SENSITIVITY, p_dy * MOUSE_SENSITIVITY);
}

String RefitBench::cursor_status(const Ship *p_ship, RefitOverlay::CursorState &r_state) const {
    const ShipLayout &layout = p_ship->get_layout_model();
    const Module *here = layout.at(cursor);
    if (here) {
        r_state = RefitOverlay::CURSOR_SELECTED;
        String text = String(U"这里是") + String::utf8(modules::def(here->kind).name);
        if (here->kind == ModuleKind::BRIDGE) {
            return text + String(U"：换不得、拆不得");
        }
        ShipLayout trial = layout;
        const std::string removable = layout_remove(trial, cursor);
        if (here->kind != kind) {
            trial = layout;
            const std::string swap = layout_set_kind(trial, cursor, kind);
            text += swap.empty() ? String(U"：E 换成") + String::utf8(modules::def(kind).name) : String(U"：") + String::utf8(swap.c_str());
        }
        return text + (removable.empty() ? String(U" · X 拆掉") : String(U" · ") + String::utf8(removable.c_str()));
    }
    ShipLayout trial = layout;
    const std::string reason = layout_place(trial, cursor, kind);
    if (reason.empty()) {
        r_state = RefitOverlay::CURSOR_CAN_PLACE;
        return String(U"空格：E 放一节") + String::utf8(modules::def(kind).name);
    }
    r_state = RefitOverlay::CURSOR_CANNOT_PLACE;
    return String(U"空格：") + String::utf8(reason.c_str());
}

void RefitBench::refresh(const Ship *p_ship) {
    RefitOverlay::CursorState state = RefitOverlay::CURSOR_CANNOT_PLACE;
    const String status = cursor_status(p_ship, state);
    panel->refresh(cursor, status, state != RefitOverlay::CURSOR_CANNOT_PLACE, kind, p_ship->get_stats_model());
    overlay->refresh(p_ship->get_layout_model(), p_ship->get_global_transform(), cursor, state, layout_candidates(p_ship->get_layout_model()));
}

void RefitBench::browse(Ship *p_ship, bool p_interact, const Say &p_say, int p_bay_in_use) {
    Input *input = Input::get_singleton();
    auto pressed = [input](const char *p_action) { return input->is_action_just_pressed(p_action); };
    Cell next = cursor;
    next.z += pressed(input_actions::MOVE_BACK) - pressed(input_actions::MOVE_FORWARD);
    next.x += pressed(input_actions::MOVE_RIGHT) - pressed(input_actions::MOVE_LEFT);
    next.y += pressed(input_actions::ASCEND) - pressed(input_actions::DESCEND);
    if (next != cursor) {
        if (cell_in_range(next)) {
            cursor = next;
        } else {
            p_say(String(U"船坞能造的范围到头了"));
        }
    }
    if (pressed(input_actions::KIND_1)) {
        kind = ModuleKind::BRIDGE;
    } else if (pressed(input_actions::KIND_2)) {
        kind = ModuleKind::STORAGE;
    } else if (pressed(input_actions::KIND_3)) {
        kind = ModuleKind::POWER;
    }

    ShipLayout draft = p_ship->get_layout_model();
    std::string reason;
    String done;
    const String kind_name = String::utf8(modules::def(kind).name);
    if (pressed(input_actions::REMOVE)) {
        reason = layout_remove(draft, cursor);
        if (reason.empty()) {
            done = String(U"拆掉了这一节");
        }
    } else if (p_interact) {
        const Module *here = draft.at(cursor);
        if (here) {
            if (here->kind == kind) {
                reason = std::string("这一节已经是") + modules::def(kind).name + "了";
            } else {
                reason = layout_set_kind(draft, cursor, kind);
                if (reason.empty()) {
                    done = String(U"这一节换成了") + kind_name;
                }
            }
        } else {
            reason = layout_place(draft, cursor, kind);
            if (reason.empty()) {
                done = String(U"放了一节") + kind_name;
            }
        }
    }
    // 船舱变小了装不下里面的东西：不改（先把东西搬出来）
    if (reason.empty() && !done.is_empty() && layout_stats(draft).bay < p_bay_in_use) {
        reason = "船舱里放着 " + std::to_string(p_bay_in_use) + " 格东西，改完只剩 " + std::to_string(layout_stats(draft).bay) + " 格：先搬出来";
        done = String();
    }
    if (!reason.empty()) {
        p_say(String::utf8(reason.c_str()));
    } else if (!done.is_empty()) {
        p_ship->apply_layout(draft);
        p_ship->place_at(yard->get_cradle_contact(), true); // 船身变了，舷门还得贴着跳板
        p_say(done);
    }
}
