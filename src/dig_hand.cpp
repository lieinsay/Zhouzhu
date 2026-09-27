#include "dig_hand.h"

#include <cmath>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/input.hpp>

#include "crops.h"
#include "input_actions.h"
#include "placed_props.h"
#include "scatter_stream.h"
#include "settlement.h"
#include "ship.h"
#include "works_view.h"

using namespace godot;

namespace {

constexpr int PROGRESS_CELLS = 8;
constexpr real_t MARKER_LIFT = 0.04f; // 框浮在地面上这么高（不与地面打架）

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

// 准星打到的是不是地面（地面块、改过的块的补丁）：房子、搭建件、摆件、船、树干都不是
bool is_ground(Object *p_collider) {
    for (Node *node = Object::cast_to<Node>(p_collider); node; node = node->get_parent()) {
        if (Object::cast_to<WorksView>(node) || Object::cast_to<Settlement>(node) || Object::cast_to<Ship>(node) || Object::cast_to<ScatterStream>(node) ||
                Object::cast_to<PlacedProps>(node)) {
            return false;
        }
    }
    return p_collider != nullptr;
}

} // namespace

void DigHand::setup(Node3D *p_marker_parent) {
    marker_parent = p_marker_parent;
    for (Ref<StandardMaterial3D> *m : { &marker_ok, &marker_bad }) {
        m->instantiate();
        (*m)->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        (*m)->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        (*m)->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    }
    marker_ok->set_albedo(Color(0.45f, 1.0f, 0.5f, 0.35f));
    marker_bad->set_albedo(Color(1.0f, 0.35f, 0.3f, 0.35f));
}

bool DigHand::holds(const PackHand &p_pack) {
    return earth_digs_with(p_pack.held_item()) || earth_raises_with(p_pack.held_item());
}

void DigHand::show_marker(MapHost *p_map, bool p_ok) {
    if (!marker_parent) {
        return;
    }
    Vector3 center;
    if (!p_map->map_earth_cell_center(ti, tj, center)) {
        hide_marker();
        return;
    }
    if (!marker) {
        Ref<BoxMesh> box;
        box.instantiate();
        box->set_size(Vector3(1.02f, 0.08f, 1.02f));
        marker = memnew(MeshInstance3D);
        marker->set_name("DigMarker");
        marker->set_mesh(box);
        marker->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        marker->set_as_top_level(true);
        marker_parent->add_child(marker);
    }
    marker->set_material_override(p_ok ? marker_ok : marker_bad);
    // 垒：框画在这一格上面一级的位置（垒上去的那一块）
    const real_t lift = raising ? real_t(EditGrid::STEP_M) * 0.5f : 0.0f;
    marker->set_global_position(center + Vector3(0, MARKER_LIFT + lift, 0));
    marker->set_visible(true);
}

void DigHand::hide_marker() {
    if (marker) {
        marker->set_visible(false);
    }
}

void DigHand::stand_down() {
    working = false;
    work_time = 0.0;
    dug = 0;
    target_on = false;
    why.clear();
    hide_marker();
}

String DigHand::target_why() const {
    if (!why.empty()) {
        return utf8(why);
    }
    return check.ok ? String() : utf8(check.why);
}

bool DigHand::step(MapHost *p_map, const WorksSite &p_site, Player *p_player, PackHand &p_pack, const AimHit &p_aim, bool p_free, bool p_interact, double p_delta, const Say &p_say) {
    const std::string held = p_pack.held_item();
    if (!holds(p_pack)) {
        stand_down();
        return false;
    }
    raising = earth_raises_with(held);
    const int prev_i = ti, prev_j = tj;
    const bool had_target = target_on;
    target_on = false;
    why.clear();
    const Vector3 at = p_player->get_global_position();
    auto refuse = [&](const std::string &p_why) {
        why = p_why;
        hide_marker();
        working = false;
        if (p_interact) {
            p_say(utf8(p_why));
        }
        return p_interact;
    };
    if (!p_site.works || !p_site.terrain) {
        return refuse("这里改不了地形");
    }
    if (!p_aim.hit || !is_ground(p_aim.collider)) {
        return refuse(raising ? "准星对准地面垒土" : "准星对准地面挖");
    }
    // 打在顶面上：就是这一格；打在竖着的坎上：挖坎后面那格，垒坎前面那格
    const bool top_face = std::abs(p_aim.normal.y) > 0.7f;
    const Vector3 probe = raising && !top_face ? p_aim.point + p_aim.normal * 0.05f : p_aim.point - p_aim.normal * 0.05f;
    if (!p_map->map_earth_cell(probe, ti, tj)) {
        return refuse("这里改不了地形");
    }
    Vector3 center;
    if (!p_map->map_earth_cell_center(ti, tj, center)) {
        return refuse("这儿没有地");
    }
    if (Vector2(center.x - at.x, center.z - at.z).length() > rules.reach_m || std::abs(center.y - at.y) > rules.reach_up_m + EditGrid::STEP_M) {
        return refuse("够不着那一格");
    }
    EarthFacts facts;
    if (!p_map->map_earth_facts(ti, tj, at, facts)) {
        return refuse("这里改不了地形");
    }
    check = plan_earthwork(facts, p_map->map_earth_step(ti, tj), raising ? EarthAction::RAISE : EarthAction::DIG, held, p_free, rules.earth);
    target_on = true;
    show_marker(p_map, check.ok);
    if (working && had_target && (ti != prev_i || tj != prev_j)) {
        work_time = 0.0; // 准星挪到别的格：这一下从头挖
    }

    if (raising) {
        if (!p_interact) {
            return false;
        }
        if (!check.ok) {
            p_say(utf8(check.why));
            return true;
        }
        if (!p_free && p_pack.items().take_at(p_pack.get_selected(), 1) != 1) {
            p_say(String(U"手上没土了"));
            return true;
        }
        if (!p_map->map_set_earth_step(ti, tj, check.after)) {
            if (!p_free) {
                p_pack.items().put("earth", 1); // 没垒成：土还回去
            }
            p_say(String(U"垒不上去"));
            return true;
        }
        p_pack.refresh_bar();
        p_pack.refresh_panel();
        p_say(vformat(String(U"垒了一级（这一格高出原地面 %s m）%s"), String::num(check.after * EditGrid::STEP_M, 1),
                p_free ? String() : vformat(String(U" · 背包里的土 %d"), p_pack.items().count("earth"))));
        return true;
    }

    // 挖：按下 E 起工，按住才算工时，满一级就挖下去
    if (p_interact) {
        if (!check.ok) {
            p_say(utf8(check.why));
            return true;
        }
        if (!p_free && p_pack.items().room_for(check.item) < 1) {
            p_say(String(U"背包满了：挖出来的") + PackHand::name_of(check.item) + String(U"没地方放"));
            return true;
        }
        working = true;
        work_time = 0.0;
        dug = 0;
    }
    if (!working) {
        return false;
    }
    if (!Input::get_singleton()->is_action_pressed(input_actions::INTERACT) || !check.ok) {
        working = false;
        work_time = 0.0;
        return false;
    }
    p_player->set_facing_yaw(std::atan2(double(at.x - center.x), double(at.z - center.z))); // 人面朝那一格干活
    work_time += p_delta;
    if (work_time < check.seconds) {
        return true;
    }
    work_time = 0.0;
    if (!p_free && p_pack.items().room_for(check.item) < 1) {
        working = false;
        p_say(String(U"背包满了"));
        return true;
    }
    if (!p_map->map_set_earth_step(ti, tj, check.after)) {
        working = false;
        p_say(String(U"挖不动"));
        return true;
    }
    dug++;
    if (!p_free) {
        p_pack.items().put(check.item, 1);
        p_pack.refresh_bar();
        p_pack.refresh_panel();
    }
    p_say(vformat(String(U"挖下一级：%s（这一格低于原地面 %s m）%s"), PackHand::name_of(check.item), String::num(-check.after * EditGrid::STEP_M, 1),
            p_free ? String() : vformat(String(U" · 背包 %d"), p_pack.items().count(check.item))));
    return true;
}

String DigHand::prompt(const PackHand &p_pack) const {
    if (!holds(p_pack)) {
        return String();
    }
    const String what = raising ? String(U"垒土") : String(U"挖");
    if (!target_on) {
        return why.empty() ? String() : what + String(U"：") + utf8(why);
    }
    const String level = vformat(String(U"这一格%s原地面 %s m"), check.before >= 0 ? String(U"高出") : String(U"低于"), String::num(std::abs(check.before) * EditGrid::STEP_M, 1));
    if (!check.ok) {
        return what + String(U"不了：") + utf8(check.why) + String(U" · ") + level;
    }
    if (raising) {
        return String(U"按 E 垒一级（0.5 m，扣一份土）· ") + level;
    }
    if (working) {
        const int filled = int(std::floor(PROGRESS_CELLS * std::min(1.0, work_time / std::max(1e-3, check.seconds))));
        String bar;
        for (int k = 0; k < PROGRESS_CELLS; k++) {
            bar += k < filled ? String(U"■") : String(U"□");
        }
        return vformat(String(U"挖%s中 %s · %s"), check.stone ? String(U"石") : String(U"土"), bar, level);
    }
    return vformat(String(U"按住 E 挖一级（%s 秒，出%s）· %s"), String::num(check.seconds, 2), PackHand::name_of(check.item), level);
}
