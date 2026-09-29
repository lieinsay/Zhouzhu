#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "almanac.h"
#include "day_clock.h"
#include "story_start.h"

// 全局唯一的世界时钟（autoload「GameClock」，见 game/world_clock.tscn）：从第 0 天子夜起走过的秒数只有这一份。
// 在场景之上，所以换地图（F2）钟点不断；所有村子（近处逐帧、远处结算）、农田生长都读它，将来的码头班次也读它。
// 物理帧里排在最前面（process_physics_priority 很小），别的节点这一帧读到的都是已经走过的钟。
// 快进（F4，开发者）时按 fast_scale 倍走：近处远处都按钟走过的时间推进，所以一切照常，只是更快。暂停时钟不走（村子、庄稼都停着）。
//
// 日历：历日 = 钟的日 + 日历偏移（整日）；偏移 = 开局是一年里的第几天（calendar_start）+ 开发者拨过的日子（calendar_shift）。
// 一游戏日就是一历日（一年 336 天：skyisle-gen 的行星）。拨日期只动偏移——村子、庄稼不用结几个月的账；
// 往回拨钟点 = 偏移退一天、钟再往前拨，所以钟的秒只往前走，村子的账也只往前结。偏移是整日，钟点与历日的钟点永远一样。
class WorldClock : public godot::Node {
    GDCLASS(WorldClock, godot::Node)

    static WorldClock *singleton;

    DayRules rules;
    CalendarRules calendar;
    double start_hour = 7.5;
    int calendar_start = story_start::CALENDAR_START; // 开局是一年里的第几天（开局配置：#6329 的第 85 天，开春第一个晴天；#2051 时是第 3 天）
    int calendar_shift = 0; // 开发者拨过的日子
    double seconds = 0.0;
    bool fast_forward = false;
    double fast_scale = 30.0;
    bool paused = false;

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    WorldClock();
    ~WorldClock();

    // 场景里的 C++ 节点从这里拿钟；没有 autoload（不该发生）返回 nullptr
    static WorldClock *get_singleton() { return singleton; }

    void _ready() override;
    void _physics_process(double p_delta) override;

    const DayRules &get_rules() const { return rules; }
    const CalendarRules &get_calendar() const { return calendar; }
    double get_seconds() const { return seconds; }
    double get_total_hours() const { return seconds / (rules.day_seconds / 24.0); } // 从第 0 天子夜起的游戏钟头（资源点的存量按它回满）
    double get_hour() const { return read_clock(seconds, rules).hour; }
    int get_day() const { return read_clock(seconds, rules).day; }
    godot::String get_clock_text() const;
    // 拨钟：往前拨到今天（或明天，若已过）的这个钟点。只往前，不倒流
    void set_hour(double p_hour);
    // 从头开始：第 0 天的这个钟点（测试在场景建起来之前用）；拨过的日子清零
    void restart(double p_hour);

    // 日历
    int get_calendar_offset() const { return calendar_start + calendar_shift; } // 历日 − 钟的日（整日）
    double get_calendar_day() const { return seconds / rules.day_seconds + double(get_calendar_offset()); } // 历日（带小数）
    double calendar_day_at(double p_seconds) const { return p_seconds / rules.day_seconds + double(get_calendar_offset()); }
    // { year, day_of_year, season, month, day_of_month, hour, text（「三月初四」）, calendar_day, day（钟的日）}
    godot::Dictionary get_date() const;
    // 开发者：拨日子（只动日历，钟不走）
    void shift_days(int p_days);
    // 开发者：拨钟点 p_hours（可正可负）：往后拨是钟真的往前走；往回拨是日历退一天、钟往前走 24 − |p| 个钟头
    void step_hours(double p_hours);
    // 开发者：拨到一年里的第 p_day_of_year 天的 p_hour 点（日历拨过去、钟往前拨到那个钟点，不跨过一天）
    void set_date(int p_day_of_year, double p_hour);

    void set_day_seconds(double p_value);
    double get_day_seconds() const { return rules.day_seconds; }
    void set_start_hour(double p_value) { start_hour = p_value; }
    double get_start_hour() const { return start_hour; }
    void set_fast_forward(bool p_value) { fast_forward = p_value; }
    bool is_fast_forward() const { return fast_forward; }
    void set_fast_scale(double p_value) { fast_scale = p_value > 1.0 ? p_value : 1.0; }
    double get_fast_scale() const { return fast_scale; }
    double get_rate() const { return paused ? 0.0 : (fast_forward ? fast_scale : 1.0); }
    void set_paused(bool p_value) { paused = p_value; }
    bool is_paused() const { return paused; }
    void set_calendar_start(int p_value) { calendar_start = p_value; }
    int get_calendar_start() const { return calendar_start; }
    void set_calendar_shift(int p_value) { calendar_shift = p_value; }
    int get_calendar_shift() const { return calendar_shift; }
};
