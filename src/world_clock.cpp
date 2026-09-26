#include "world_clock.h"

#include <algorithm>
#include <cmath>

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
    ClassDB::bind_method(D_METHOD("set_paused", "value"), &WorldClock::set_paused);
    ClassDB::bind_method(D_METHOD("is_paused"), &WorldClock::is_paused);
    ClassDB::bind_method(D_METHOD("set_calendar_start", "value"), &WorldClock::set_calendar_start);
    ClassDB::bind_method(D_METHOD("get_calendar_start"), &WorldClock::get_calendar_start);
    ClassDB::bind_method(D_METHOD("set_calendar_shift", "value"), &WorldClock::set_calendar_shift);
    ClassDB::bind_method(D_METHOD("get_calendar_shift"), &WorldClock::get_calendar_shift);
    ClassDB::bind_method(D_METHOD("get_calendar_offset"), &WorldClock::get_calendar_offset);
    ClassDB::bind_method(D_METHOD("get_calendar_day"), &WorldClock::get_calendar_day);
    ClassDB::bind_method(D_METHOD("get_date"), &WorldClock::get_date);
    ClassDB::bind_method(D_METHOD("shift_days", "days"), &WorldClock::shift_days);
    ClassDB::bind_method(D_METHOD("step_hours", "hours"), &WorldClock::step_hours);
    ClassDB::bind_method(D_METHOD("set_date", "day_of_year", "hour"), &WorldClock::set_date);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "day_seconds", PROPERTY_HINT_RANGE, "10,14400,1,suffix:s"), "set_day_seconds", "get_day_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start_hour", PROPERTY_HINT_RANGE, "0,23.99,0.25"), "set_start_hour", "get_start_hour");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "calendar_start", PROPERTY_HINT_RANGE, "0,335,1"), "set_calendar_start", "get_calendar_start");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "calendar_shift", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_calendar_shift", "get_calendar_shift");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fast_forward"), "set_fast_forward", "is_fast_forward");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fast_scale", PROPERTY_HINT_RANGE, "1,960,1"), "set_fast_scale", "get_fast_scale");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "set_paused", "is_paused");
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
    // 「三月初四 · 巳时 · 第 1 日 · 09:40」：历日在前，后面是原来的钟点
    const CalendarDate date = calendar_date(get_calendar_day(), calendar);
    return String::utf8((month_day_text(date) + " · " + clock_text(seconds, rules)).c_str());
}

Dictionary WorldClock::get_date() const {
    const CalendarDate date = calendar_date(get_calendar_day(), calendar);
    Dictionary d;
    d["year"] = date.year;
    d["day_of_year"] = date.day_of_year;
    d["season"] = date.season;
    d["month"] = date.month;
    d["day_of_month"] = date.day_of_month;
    d["hour"] = date.hour;
    d["text"] = String::utf8(month_day_text(date).c_str());
    d["calendar_day"] = get_calendar_day();
    d["day"] = get_day();
    return d;
}

void WorldClock::shift_days(int p_days) {
    calendar_shift += p_days;
}

void WorldClock::step_hours(double p_hours) {
    if (p_hours >= 0.0) {
        seconds += game_seconds(p_hours, rules);
        return;
    }
    const int back_days = int(std::ceil(-p_hours / 24.0 - 1e-9));
    calendar_shift -= back_days;
    seconds += game_seconds(24.0 * back_days + p_hours, rules);
}

void WorldClock::set_date(int p_day_of_year, double p_hour) {
    const double hour = Math::clamp(p_hour, 0.0, 23.999);
    const double now_hour = get_hour();
    const int year = std::max(1, calendar.year_days);
    const int today = int(std::floor(get_calendar_day()));
    int delta = (p_day_of_year - today) % year;
    delta = delta < 0 ? delta + year : delta;
    // 钟点比此刻早：钟会拨到明天，日历先退一天抵掉
    calendar_shift += delta - (hour < now_hour ? 1 : 0);
    set_hour(hour);
}

void WorldClock::set_hour(double p_hour) {
    const ClockReading now = read_clock(seconds, rules);
    const double hour = Math::clamp(p_hour, 0.0, 23.999);
    seconds = clock_at(now.day + (hour < now.hour ? 1 : 0), hour, rules);
}

void WorldClock::restart(double p_hour) {
    seconds = clock_at(0, Math::clamp(p_hour, 0.0, 23.999), rules);
    calendar_shift = 0;
}

void WorldClock::set_day_seconds(double p_value) {
    const double hours = game_hours(seconds, rules); // 换一天的长度时钟点不跳
    rules.day_seconds = p_value > 1.0 ? p_value : 1.0;
    seconds = game_seconds(hours, rules);
}
