#include "world_clock.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

WorldClock *WorldClock::singleton = nullptr;

void WorldClock::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_seconds"), &WorldClock::get_seconds);
    ClassDB::bind_method(D_METHOD("get_hour"), &WorldClock::get_hour);
    ClassDB::bind_method(D_METHOD("get_day"), &WorldClock::get_day);
    ClassDB::bind_method(D_METHOD("get_clock_text"), &WorldClock::get_clock_text);
    ClassDB::bind_method(D_METHOD("set_hour", "hour"), &WorldClock::set_hour);
    ClassDB::bind_method(D_METHOD("restart", "hour"), &WorldClock::restart);
    ClassDB::bind_method(D_METHOD("get_rate"), &WorldClock::get_rate);
    ClassDB::bind_method(D_METHOD("set_day_seconds", "value"), &WorldClock::set_day_seconds);
    ClassDB::bind_method(D_METHOD("get_day_seconds"), &WorldClock::get_day_seconds);
    ClassDB::bind_method(D_METHOD("set_start_hour", "value"), &WorldClock::set_start_hour);
    ClassDB::bind_method(D_METHOD("get_start_hour"), &WorldClock::get_start_hour);
    ClassDB::bind_method(D_METHOD("set_fast_forward", "value"), &WorldClock::set_fast_forward);
    ClassDB::bind_method(D_METHOD("is_fast_forward"), &WorldClock::is_fast_forward);
    ClassDB::bind_method(D_METHOD("set_fast_scale", "value"), &WorldClock::set_fast_scale);
    ClassDB::bind_method(D_METHOD("get_fast_scale"), &WorldClock::get_fast_scale);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "day_seconds", PROPERTY_HINT_RANGE, "10,14400,1,suffix:s"), "set_day_seconds", "get_day_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start_hour", PROPERTY_HINT_RANGE, "0,23.99,0.25"), "set_start_hour", "get_start_hour");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fast_forward"), "set_fast_forward", "is_fast_forward");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fast_scale", PROPERTY_HINT_RANGE, "1,240,1"), "set_fast_scale", "get_fast_scale");
}

WorldClock::WorldClock() {
    if (!singleton) {
        singleton = this;
    }
}

WorldClock::~WorldClock() {
    if (singleton == this) {
        singleton = nullptr;
    }
}

void WorldClock::_notification(int p_what) {
    // 进树前（包括 autoload 在别的节点之前）就定好开局钟点
    if (p_what == NOTIFICATION_ENTER_TREE && seconds == 0.0) {
        seconds = game_seconds(start_hour, rules);
    }
}

void WorldClock::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        return;
    }
    set_physics_process_priority(-1000); // 物理帧里最先走
}

void WorldClock::_physics_process(double p_delta) {
    seconds += p_delta * get_rate();
}

String WorldClock::get_clock_text() const {
    return String::utf8(clock_text(seconds, rules).c_str());
}

void WorldClock::set_hour(double p_hour) {
    const ClockReading now = read_clock(seconds, rules);
    const double hour = Math::clamp(p_hour, 0.0, 23.999);
    seconds = clock_at(now.day + (hour < now.hour ? 1 : 0), hour, rules);
}

void WorldClock::restart(double p_hour) {
    seconds = clock_at(0, Math::clamp(p_hour, 0.0, 23.999), rules);
}

void WorldClock::set_day_seconds(double p_value) {
    const double hours = game_hours(seconds, rules); // 换一天的长度时钟点不跳
    rules.day_seconds = p_value > 1.0 ? p_value : 1.0;
    seconds = game_seconds(hours, rules);
}
