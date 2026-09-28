#include "refit_bench.h"

#include <cmath>

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/viewport.hpp>

#include "input_actions.h"

using namespace godot;

namespace {

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

String side_name(int p_x) {
    return p_x < 0 ? String(U"左舷") : (p_x > 0 ? String(U"右舷") : String(U"中线"));
}

String row_name(int p_z) {
    if (p_z == 0) {
        return String(U"舷门排");
    }
    return p_z < 0 ? vformat(String(U"舷门前 %d 排"), -p_z) : vformat(String(U"舷门后 %d 排"), p_z);
}

// 节点占在哪儿：「中线 · 第 1 层 · 舷门后 1–2 排」
String where_text(const Cell &p_anchor, ModuleKind p_kind) {
    const ModuleDef &d = modules::def(p_kind);
    const int x0 = p_anchor.x, x1 = p_anchor.x + d.size_x - 1;
    const int y0 = p_anchor.y, y1 = p_anchor.y + d.size_y - 1;
    const int z0 = p_anchor.z, z1 = p_anchor.z + d.size_z - 1;
    const String side = x0 == x1 ? side_name(x0) : side_name(x0) + String(U"到") + side_name(x1);
    const String level = y0 == y1 ? vformat(String(U"第 %d 层"), y0 + 1) : vformat(String(U"第 %d–%d 层"), y0 + 1, y1 + 1);
    String row;
    if (z0 == z1) {
        row = row_name(z0);
    } else if (z0 > 0) {
        row = vformat(String(U"舷门后 %d–%d 排"), z0, z1);
    } else if (z1 < 0) {
        row = vformat(String(U"舷门前 %d–%d 排"), -z1, -z0);
    } else {
        row = row_name(z0) + String(U"到") + row_name(z1);
    }
    return side + String(U" · ") + level + String(U" · ") + row;
}

String title_of(ModuleKind p_kind) {
    const ModuleDef &d = modules::def(p_kind);
    return utf8(d.name) + String(U"（") + utf8(modules::shape_name(d)) + String(U"）");
}

// 盒子六个面上的取样点（稍往里收一点）：先面中心，再每面 5 × 5
std::vector<Vector3> box_samples(const CellBox &p_box) {
    const Vector3 lo(real_t(p_box.x0), real_t(p_box.y0), real_t(p_box.z0)), hi(real_t(p_box.x1), real_t(p_box.y1), real_t(p_box.z1));
    const Vector3 mid = 0.5f * (lo + hi), half = 0.5f * (hi - lo) - Vector3(0.05f, 0.05f, 0.05f);
    std::vector<Vector3> out;
    const float grid[5] = { 0.0f, -0.6f, 0.6f, -0.85f, 0.85f };
    for (int pass = 0; pass < 2; pass++) {
        for (int axis = 0; axis < 3; axis++) {
            for (int sign = -1; sign <= 1; sign += 2) {
                const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                for (int i = 0; i < (pass == 0 ? 1 : 5); i++) {
                    for (int j = 0; j < (pass == 0 ? 1 : 5); j++) {
                        Vector3 p = mid;
                        p[axis] += sign * half[axis];
                        p[u] += grid[i] * half[u];
                        p[v] += grid[j] * half[v];
                        out.push_back(p);
                    }
                }
            }
        }
    }
    return out;
}

} // namespace

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
    put_down();
    has_selected = false;
    orbiting = false;
    mouse_known = false;
    hover = RefitHover();
    panel_hit = RefitPanel::Hit();
    spots_valid = false;
    panel->set_visible(true);
    overlay->set_visible(true);
    overlay->show_for(p_ship->get_layout_model(), p_ship->get_global_transform());
    overlay->get_camera()->set_cull_mask(~Shipyard::GANTRY_RENDER_LAYER); // 门架横梁挡着看船：装配视角不画它
    overlay->get_camera()->make_current();
    overlay->set_screen_shift(0.5 * panel->palette_right()); // 船落在面板右边那块的中间
    refresh(p_ship, 0.0);
    return true;
}

void RefitBench::close(Player *p_player) {
    if (!open) {
        return;
    }
    open = false;
    yard = nullptr;
    put_down();
    has_selected = false;
    orbiting = false;
    panel->set_visible(false);
    overlay->set_visible(false);
    p_player->get_camera()->make_current();
}

void RefitBench::put_down() {
    drag_kind = -1;
    drag_sticky = false;
    press_card = -1;
    left_card = false;
}

bool RefitBench::cancel_drag() {
    if (drag_kind < 0) {
        return false;
    }
    put_down();
    return true;
}

bool RefitBench::get_selected(Cell &r_anchor) const {
    r_anchor = selected;
    return has_selected;
}

void RefitBench::update_spots(const ShipLayout &p_layout) {
    if (spots_valid && spots_for == p_layout) {
        return;
    }
    for (int k = 0; k < int(ModuleKind::COUNT); k++) {
        spots[k] = layout_placements(p_layout, ModuleKind(k));
    }
    spots_for = p_layout;
    spots_valid = true;
}

RefitHover RefitBench::pick_at(const ShipLayout &p_layout, const Vector2 &p_screen) const {
    const PickRay ray = overlay->ray_at(p_screen);
    if (drag_kind >= 0) {
        return pick_spot(p_layout, ModuleKind(drag_kind), spots[drag_kind], ray);
    }
    return pick_node(p_layout, ray);
}

void RefitBench::update_hover(const Ship *p_ship) {
    if (!mouse_known) {
        hover = RefitHover();
        panel_hit = RefitPanel::Hit();
        return;
    }
    update_spots(p_ship->get_layout_model());
    panel_hit = panel->hit(mouse);
    hover = panel_hit.type == RefitPanel::HIT_NONE ? pick_at(p_ship->get_layout_model(), mouse) : RefitHover();
}

bool RefitBench::apply(Ship *p_ship, const ShipLayout &p_next, const Say &p_say, int p_bay_in_use) {
    // 船舱变小了装不下里面的东西：不改（先把东西搬出来）
    const int bay = layout_stats(p_next).bay;
    if (bay < p_bay_in_use) {
        p_say(vformat(String(U"船舱里放着 %d 格东西，改完只剩 %d 格：先搬出来"), p_bay_in_use, bay));
        return false;
    }
    if (!p_ship->apply_layout(p_next)) {
        return false;
    }
    p_ship->place_at(yard->get_cradle_contact(), true); // 船身变了，舷门还得贴着跳板
    spots_valid = false;
    return true;
}

void RefitBench::drop(Ship *p_ship, const Say &p_say, int p_bay_in_use) {
    const ModuleKind kind = ModuleKind(drag_kind);
    update_hover(p_ship);
    const RefitHover at = hover;
    put_down();
    if (at.type == RefitHover::SPOT && at.legal) {
        ShipLayout next = p_ship->get_layout_model();
        const std::string reason = layout_place(next, at.anchor, kind);
        if (!reason.empty()) {
            p_say(String(U"这儿放不了：") + utf8(reason));
        } else if (apply(p_ship, next, p_say, p_bay_in_use)) {
            p_say(String(U"放了") + title_of(kind) + String(U"：") + where_text(at.anchor, kind));
            has_selected = true; // 刚放的就选中：放错了按 X 就拆
            selected = at.anchor;
        }
    } else if (at.type == RefitHover::SPOT) {
        p_say(String(U"这儿放不了：") + utf8(at.reason));
    } else {
        p_say(String(U"没放：要放在船上亮着的地方"));
    }
    update_hover(p_ship);
}

void RefitBench::remove_selected(Ship *p_ship, const Say &p_say, int p_bay_in_use) {
    const ShipLayout &layout = p_ship->get_layout_model();
    const Module *m = has_selected ? layout.at(selected) : nullptr;
    if (!m) {
        p_say(String(U"先点船上的节点选中它，再拆"));
        return;
    }
    const ModuleKind kind = m->kind;
    ShipLayout next = layout;
    const std::string reason = layout_remove(next, selected);
    if (!reason.empty()) {
        p_say(utf8(reason));
        return;
    }
    if (apply(p_ship, next, p_say, p_bay_in_use)) {
        p_say(String(U"拆掉了") + title_of(kind));
        has_selected = false;
    }
}

bool RefitBench::on_mouse(const Ref<InputEvent> &p_event, Ship *p_ship, const Say &p_say, int p_bay_in_use) {
    if (!open || !p_ship) {
        return false;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        mouse = motion->get_position();
        mouse_known = true;
        if (orbiting) {
            const Vector2 rel = motion->get_relative();
            overlay->orbit(-rel.x * ORBIT_SENSITIVITY, rel.y * ORBIT_SENSITIVITY);
        }
        update_hover(p_ship);
        if (drag_kind >= 0 && !drag_sticky && !(panel_hit.type == RefitPanel::HIT_CARD && panel_hit.index == press_card)) {
            left_card = true; // 离开了按下的那张卡片：是在拖
        }
        return true;
    }
    const Ref<InputEventMouseButton> button = p_event;
    if (!button.is_valid()) {
        return false;
    }
    mouse = button->get_position();
    mouse_known = true;
    update_hover(p_ship);
    const MouseButton which = button->get_button_index();
    const bool pressed = button->is_pressed();

    if (which == MOUSE_BUTTON_LEFT && pressed) {
        if (panel_hit.type == RefitPanel::HIT_CARD) {
            if (drag_kind == panel_hit.index && drag_sticky) {
                put_down(); // 再点一下拿着的那张：放回去
            } else {
                drag_kind = panel_hit.index;
                drag_sticky = false;
                press_card = panel_hit.index;
                left_card = false;
                has_selected = false;
            }
            update_hover(p_ship);
        } else if (panel_hit.type == RefitPanel::HIT_REMOVE) {
            remove_selected(p_ship, p_say, p_bay_in_use);
        } else if (panel_hit.type == RefitPanel::HIT_NONE) {
            if (drag_kind >= 0) {
                drop(p_ship, p_say, p_bay_in_use); // 拿着的：点船上放下
            } else if (hover.type == RefitHover::NODE) {
                has_selected = true;
                selected = hover.anchor;
            } else {
                has_selected = false;
            }
        }
        return true;
    }
    if (which == MOUSE_BUTTON_LEFT && !pressed) {
        if (drag_kind >= 0 && !drag_sticky) {
            if (!left_card && panel_hit.type == RefitPanel::HIT_CARD && panel_hit.index == press_card) {
                drag_sticky = true; // 点了一下没拖：拿着
                p_say(String(U"拿着") + title_of(ModuleKind(drag_kind)) + String(U"：点船上亮着的地方放下，右键放回"));
            } else if (panel_hit.type != RefitPanel::HIT_NONE) {
                put_down(); // 拖回面板上：放回去
            } else {
                drop(p_ship, p_say, p_bay_in_use);
            }
        }
        press_card = -1;
        return true;
    }
    if (which == MOUSE_BUTTON_RIGHT || which == MOUSE_BUTTON_MIDDLE) {
        if (pressed && which == MOUSE_BUTTON_RIGHT && drag_kind >= 0) {
            put_down();
            update_hover(p_ship);
        } else {
            orbiting = pressed;
        }
        return true;
    }
    if ((which == MOUSE_BUTTON_WHEEL_UP || which == MOUSE_BUTTON_WHEEL_DOWN) && pressed) {
        overlay->zoom(which == MOUSE_BUTTON_WHEEL_UP);
        update_hover(p_ship);
        return true;
    }
    return true;
}

void RefitBench::browse(Ship *p_ship, const Say &p_say, int p_bay_in_use) {
    if (!open || !p_ship) {
        return;
    }
    if (Input::get_singleton()->is_action_just_pressed(input_actions::REMOVE)) {
        remove_selected(p_ship, p_say, p_bay_in_use);
    }
}

std::vector<Cell> RefitBench::glow_now() const {
    const int active = drag_kind >= 0 ? drag_kind : (panel_hit.type == RefitPanel::HIT_CARD ? panel_hit.index : -1);
    if (active < 0 || !spots_valid) {
        return {};
    }
    return glow_cells(spots[active], ModuleKind(active));
}

void RefitBench::refresh(const Ship *p_ship, double p_delta) {
    if (!p_ship) {
        return;
    }
    clock += p_delta;
    const ShipLayout &layout = p_ship->get_layout_model();
    update_spots(layout);
    if (has_selected) {
        const Module *m = layout.at(selected);
        has_selected = m != nullptr;
        selected = m ? m->at : selected;
    }
    update_hover(p_ship);

    RefitOverlay::View v;
    v.layout = &layout;
    v.ship = p_ship->get_global_transform();
    v.glow = glow_now();
    v.pulse = 0.5 + 0.5 * std::sin(clock * 4.0);
    if (drag_kind >= 0 && hover.type == RefitHover::SPOT) {
        v.ghost = true;
        v.ghost_box = footprint_box(hover.anchor, ModuleKind(drag_kind));
        v.ghost_legal = hover.legal;
        v.ghost_text = hover.legal ? utf8(modules::def(ModuleKind(drag_kind)).name) : utf8(hover.reason);
    }
    const Module *chosen = has_selected ? layout.at(selected) : nullptr;
    if (chosen) {
        v.selected = true;
        v.selected_box = footprint_box(chosen->at, chosen->kind, true);
    }
    if (drag_kind < 0 && hover.type == RefitHover::NODE && !(chosen && hover.anchor == chosen->at)) {
        if (const Module *m = layout.at(hover.anchor)) {
            v.hovered = true;
            v.hovered_box = footprint_box(m->at, m->kind, true);
        }
    }
    overlay->set_screen_shift(0.5 * panel->palette_right());
    overlay->refresh(v);

    RefitPanel::View pv;
    for (int k = 0; k < int(ModuleKind::COUNT); k++) {
        pv.spots.push_back(int(spots[k].size()));
    }
    pv.hover_card = panel_hit.type == RefitPanel::HIT_CARD ? panel_hit.index : -1;
    pv.drag_kind = drag_kind;
    pv.sticky = drag_sticky;
    pv.mouse = mouse;
    pv.hover_remove = panel_hit.type == RefitPanel::HIT_REMOVE;
    if (drag_kind >= 0) {
        const ModuleKind kind = ModuleKind(drag_kind);
        if (hover.type == RefitHover::SPOT && hover.legal) {
            pv.hover_text = String(U"松手放在这儿：") + where_text(hover.anchor, kind);
            pv.hover_tone = RefitPanel::TONE_OK;
        } else if (hover.type == RefitHover::SPOT) {
            pv.hover_text = String(U"这儿放不了：") + utf8(hover.reason);
            pv.hover_tone = RefitPanel::TONE_WARN;
        } else if (spots[drag_kind].empty()) {
            pv.hover_text = utf8(modules::def(kind).name) + String(U"眼下没处放");
            pv.hover_tone = RefitPanel::TONE_WARN;
        } else {
            pv.hover_text = drag_sticky ? String(U"点船上亮着的地方放下；右键放回") : String(U"拖到船上亮着的地方松手");
        }
    } else if (hover.type == RefitHover::NODE) {
        if (const Module *m = layout.at(hover.anchor)) {
            pv.hover_text = title_of(m->kind) + String(U" · 点一下选中");
        }
    } else if (pv.hover_remove && chosen) {
        ShipLayout trial = layout;
        const bool removable = layout_remove(trial, chosen->at).empty();
        pv.hover_text = removable ? String(U"拆掉选中的") + title_of(chosen->kind) : String(U"拆不了：原因在钮下面");
        pv.hover_tone = RefitPanel::TONE_WARN;
    } else if (pv.hover_card >= 0) {
        const int n = int(spots[pv.hover_card].size());
        pv.hover_text = n > 0 ? vformat(String(U"能放 %d 处（船上亮着的）：按住拖过去"), n) : String(U"眼下没处放");
        pv.hover_tone = n > 0 ? RefitPanel::TONE_DIM : RefitPanel::TONE_WARN;
    }
    if (chosen) {
        pv.has_selected = true;
        pv.selected_title = title_of(chosen->kind);
        pv.selected_where = where_text(chosen->at, chosen->kind);
        pv.selected_detail = utf8(modules::function_text(modules::def(chosen->kind)));
        ShipLayout trial = layout;
        const std::string reason = layout_remove(trial, chosen->at);
        pv.can_remove = reason.empty();
        pv.remove_reason = utf8(reason);
    }
    pv.stats = p_ship->get_stats_model();
    panel->refresh(pv);
}

Vector2 RefitBench::card_center(ModuleKind p_kind) const {
    return panel ? panel->card_center(p_kind) : Vector2(-1, -1);
}

Vector2 RefitBench::remove_center() const {
    return panel ? panel->remove_center() : Vector2(-1, -1);
}

Vector2 RefitBench::aim_spot(const Ship *p_ship, const Cell &p_anchor, ModuleKind p_kind) const {
    if (!open || !p_ship) {
        return Vector2(-1, -1);
    }
    const ShipLayout &layout = p_ship->get_layout_model();
    const std::vector<Cell> placements = layout_placements(layout, p_kind);
    const Rect2 screen = overlay->get_viewport()->get_visible_rect();
    for (const Vector3 &p : box_samples(footprint_box(p_anchor, p_kind))) {
        Vector2 at;
        if (!overlay->screen_point(p, at) || !screen.has_point(at) || panel->hit(at).type != RefitPanel::HIT_NONE) {
            continue;
        }
        const RefitHover h = pick_spot(layout, p_kind, placements, overlay->ray_at(at));
        if (h.type == RefitHover::SPOT && h.anchor == p_anchor) {
            return at;
        }
    }
    return Vector2(-1, -1);
}

Vector2 RefitBench::aim_node(const Ship *p_ship, const Cell &p_anchor) const {
    if (!open || !p_ship) {
        return Vector2(-1, -1);
    }
    const ShipLayout &layout = p_ship->get_layout_model();
    const Module *m = layout.at(p_anchor);
    if (!m) {
        return Vector2(-1, -1);
    }
    const Rect2 screen = overlay->get_viewport()->get_visible_rect();
    for (const Cell &c : module_cells(*m)) {
        for (const Vector3 &p : box_samples(cell_box(c, true))) {
            Vector2 at;
            if (!overlay->screen_point(p, at) || !screen.has_point(at) || panel->hit(at).type != RefitPanel::HIT_NONE) {
                continue;
            }
            const RefitHover h = pick_node(layout, overlay->ray_at(at));
            if (h.type == RefitHover::NODE && h.anchor == m->at) {
                return at;
            }
        }
    }
    return Vector2(-1, -1);
}
