#include "gather_hand.h"

#include <cmath>

#include <godot_cpp/classes/input.hpp>

#include "crops.h"
#include "input_actions.h"
#include "world_clock.h"

using namespace godot;

namespace {

constexpr int PROGRESS_CELLS = 8;

String item_name(const std::string &p_item) {
    return PackHand::name_of(p_item);
}

String hours_text(double p_hours) {
    return p_hours < 1.0 ? vformat(String(U"%d 分钟"), int(std::ceil(p_hours * 60.0))) : vformat(String(U"%s 个钟头"), String::num(p_hours, 1));
}

double clock_hours() {
    const WorldClock *clock = WorldClock::get_singleton();
    return clock ? clock->get_total_hours() : 0.0;
}

} // namespace

double GatherHand::work_seconds(const PackHand &p_pack) const {
    return spot.gather_s / catalog::tool_speed(p_pack.held_item(), spot.item); // 手上拿着对口的工具快一倍
}

double GatherHand::stock_now() const {
    return spot.stock ? stock_amount(*spot.stock, spot.cap, spot.refill_hours, clock_hours()) : 0.0;
}

void GatherHand::stand_down() {
    working = false;
    work_time = 0.0;
    gathered = 0;
}

bool GatherHand::step(MapHost *p_map, Player *p_player, PackHand &p_pack, bool p_hands_full, bool p_interact, double p_delta, const Say &p_say) {
    const Vector3 at = p_player->get_global_position();
    spot = p_map->map_resource_near(at, face_range);
    if (spot.site >= 0 && Vector2(at.x - spot.face.x, at.z - spot.face.z).length() > face_range) {
        spot = ResourceSpot(); // 够得着的是堆场，不是采掘面
    }
    if (spot.site < 0 || !spot.stock) {
        stand_down();
        return false;
    }
    if (p_interact) {
        if (p_hands_full) {
            p_say(String(U"抱着货箱腾不出手：先放下再采"));
            return true;
        }
        if (stock_now() + 1e-9 < 1.0) {
            const double wait = stock_hours_until(*spot.stock, spot.cap, spot.refill_hours, clock_hours(), 1.0);
            p_say(vformat(String(U"采空了：约 %s后回来一份"), hours_text(wait)));
            return true;
        }
        if (p_pack.items().room_for(spot.item) < 1) {
            p_say(String(U"背包满了：先腾出一格（或搬到船舱里）"));
            return true;
        }
        working = true;
        work_time = 0.0;
        gathered = 0;
    }
    if (!working) {
        return false;
    }
    if (p_hands_full || !Input::get_singleton()->is_action_pressed(input_actions::INTERACT)) {
        stand_down();
        return false;
    }
    // 人面朝采掘面干活
    p_player->set_facing_yaw(std::atan2(double(at.x - spot.face.x), double(at.z - spot.face.z)));
    work_time += p_delta;
    if (work_time < work_seconds(p_pack)) {
        return true;
    }
    work_time = 0.0;
    if (p_pack.items().room_for(spot.item) < 1) {
        stand_down();
        p_say(String(U"背包满了"));
        return true;
    }
    if (!stock_take(*spot.stock, spot.cap, spot.refill_hours, clock_hours(), 1)) {
        stand_down();
        p_say(String(U"采空了"));
        return true;
    }
    p_pack.items().put(spot.item, 1);
    p_pack.refresh_panel();
    gathered++;
    const int have = p_pack.items().count(spot.item);
    if (stock_now() + 1e-9 < 1.0) {
        stand_down();
        p_say(vformat(String(U"采了一份%s（背包 %d）· 这里采空了"), item_name(spot.item), have));
    } else {
        p_say(vformat(String(U"采了一份%s（背包 %d）"), item_name(spot.item), have));
    }
    return true;
}

String GatherHand::prompt(const PackHand &p_pack) const {
    if (spot.site < 0 || !spot.stock) {
        return String();
    }
    const int stock = int(std::floor(stock_now() + 1e-9));
    const String head = spot.name + vformat(String(U" · 存 %d / %d · "), stock, spot.cap);
    const double seconds = work_seconds(p_pack);
    if (working) {
        const int filled = int(std::floor(PROGRESS_CELLS * std::min(1.0, work_time / seconds)));
        String bar;
        for (int k = 0; k < PROGRESS_CELLS; k++) {
            bar += k < filled ? String(U"■") : String(U"□");
        }
        return head + vformat(String(U"%s中 %s（背包 %s %d）"), spot.verb, bar, item_name(spot.item), p_pack.items().count(spot.item));
    }
    if (stock < 1) {
        const double wait = stock_hours_until(*spot.stock, spot.cap, spot.refill_hours, clock_hours(), 1.0);
        return head + vformat(String(U"采空了，约 %s后回来一份"), hours_text(wait));
    }
    const String tool = seconds < spot.gather_s ? String(U"，手上的") + item_name(p_pack.held_item()) + String(U"快一倍") : String();
    return head + vformat(String(U"按住 E %s（%s 秒一份%s%s）"), spot.verb, String::num(seconds, 1), item_name(spot.item), tool);
}
