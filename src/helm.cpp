#include "helm.h"

#include <cmath>

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "input_actions.h"

using namespace godot;

namespace {

double yaw_of(const Transform3D &p_transform) {
    return p_transform.basis.get_euler().y;
}

} // namespace

void Helm::attach(Ship *p_ship, Player *p_player) {
    ship = p_ship;
    player = p_player;
    state = ON_FOOT;
}

bool Helm::near_helm() const {
    return player->get_global_position().distance_to(ship->get_helm_position()) <= helm_range;
}

void Helm::board() {
    moor_berth = Berth(); // 上一回靠在哪不算数了（半空离舵时不会误当成野泊）
    player->set_controlled(false);
    ship->set_controlled(true);
    ship->get_camera()->make_current();
    const Transform3D seat = ship->get_seat_transform();
    player->set_global_position(seat.origin);
    player->set_facing_yaw(yaw_of(seat));
    state = PILOTING;
}

void Helm::disembark() {
    const Transform3D exit = ship->get_exit_transform();
    ship->set_controlled(false);
    player->set_global_position(exit.origin + Vector3(0, 0.05f, 0));
    player->set_look_yaw(yaw_of(exit));
    player->set_controlled(true);
    player->get_camera()->make_current();
    state = ON_FOOT;
}

Helm::Event Helm::step(bool p_interact, const std::vector<Berth> &p_berths) {
    if (state == ON_FOOT) {
        return NONE;
    }
    const Transform3D seat = ship->get_seat_transform();
    player->set_global_position(seat.origin);
    player->set_facing_yaw(yaw_of(seat));

    if (state == MOORING) {
        if (!ship->is_mooring()) {
            disembark();
            return DISEMBARKED;
        }
        return NONE;
    }

    Input *input = Input::get_singleton();
    if (input->is_action_just_pressed(input_actions::GEAR_UP)) {
        ship->set_gear(ship->get_gear() + 1);
    } else if (input->is_action_just_pressed(input_actions::GEAR_DOWN)) {
        ship->set_gear(ship->get_gear() - 1);
    }
    if (!p_interact) {
        return NONE;
    }
    Berth target;
    if (moor_target(p_berths, target) && !target.refusal.is_empty() && !ship->is_at(target.contact, 0.5)) {
        refusal = target.refusal;
        return REFUSED;
    }
    if (moor_target(p_berths, target)) {
        ship->moor_at(target.contact);
        ship->set_controlled(false);
        moor_berth = target;
        state = MOORING;
        return MOOR_STARTED;
    }
    if (ship->get_speed() <= leave_helm_speed) {
        disembark();
        return DISEMBARKED;
    }
    return STILL_MOVING;
}

bool Helm::moor_target(const std::vector<Berth> &p_berths, Berth &r_berth) const {
    bool found = false;
    double best_distance = dock_range;
    for (const Berth &berth : p_berths) {
        if (berth.wild) {
            continue;
        }
        const double distance = ship->get_global_position().distance_to(berth.contact.origin);
        if (distance <= best_distance) {
            found = true;
            best_distance = distance;
            r_berth = berth;
        }
    }
    if (found) {
        return true;
    }
    // 近处没有码头、船坞、船台：就地野泊（编排只在落得下时才递进来）
    for (const Berth &berth : p_berths) {
        if (berth.wild) {
            r_berth = berth;
            return true;
        }
    }
    return false;
}

String Helm::course_text(const Transform3D &p_target, const String &p_target_name) const {
    const Vector3 ship_position = ship->get_global_position();
    const Vector3 to_target = p_target.origin - ship_position;
    const double distance = Vector2(to_target.x, to_target.z).length();
    // 目标相对船头的方位：正 = 在右舷
    const Vector3 local = ship->get_global_transform().basis.xform_inv(to_target);
    const int relative_deg = int(std::lround(Math::rad_to_deg(std::atan2(double(local.x), double(-local.z)))));
    String bearing = String(U"正前方");
    if (relative_deg > 2) {
        bearing = vformat(String(U"右舷 %d°"), relative_deg);
    } else if (relative_deg < -2) {
        bearing = vformat(String(U"左舷 %d°"), -relative_deg);
    }
    const String range = distance >= 2000.0 ? vformat(String(U"%.1f km"), distance / 1000.0) : vformat(String(U"%d m"), int(std::lround(distance)));
    return vformat(String(U"航速 %d m/s（%s档）    高度 %d m\n%s %s    %s    高差 %+d m"),
            int(std::lround(ship->get_speed())), ship->get_gear_name(), int(std::lround(ship_position.y)),
            p_target_name, range, bearing, int(std::lround(to_target.y)));
}

String Helm::piloting_prompt(const std::vector<Berth> &p_berths) const {
    Berth target;
    if (moor_target(p_berths, target)) {
        if (ship->is_at(target.contact, 0.5)) {
            return String(U"按 E 离舵");
        }
        if (!target.refusal.is_empty()) {
            return target.refusal;
        }
        if (target.wild) {
            return vformat(String(U"按 E 野泊（舷梯放%s）"), target.gangway_side > 0 ? String(U"右舷") : String(U"左舷"));
        }
        return target.yard ? String(U"按 E 入坞") : (target.land ? String(U"按 E 落上船台") : String(U"按 E 靠泊"));
    }
    String keys = String(U"W/S 进退 · A/D 转舵 · 空格 升 · Shift 降 · R/F 换档");
    if (ship->get_speed() <= leave_helm_speed) {
        keys += String(U" · E 离舵");
    }
    return wild_note.is_empty() ? keys : keys + String(U"\n野泊不了：") + wild_note;
}
