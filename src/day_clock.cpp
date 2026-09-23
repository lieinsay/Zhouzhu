#include "day_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

ClockReading read_clock(double p_seconds, const DayRules &p_rules) {
    ClockReading c;
    const double days = std::max(0.0, p_seconds) / p_rules.day_seconds;
    c.day = int(std::floor(days));
    c.hour = (days - c.day) * 24.0;
    if (c.hour >= 24.0) {
        c.hour = 0.0;
        c.day++;
    }
    return c;
}

bool is_home_hour(double p_hour, const DayRules &p_rules) {
    return p_hour < p_rules.work_start || p_hour >= p_rules.work_end || (p_hour >= p_rules.lunch_start && p_hour < p_rules.lunch_end);
}

const char *shichen_name(double p_hour) {
    static const char *NAMES[12] = { "子时", "丑时", "寅时", "卯时", "辰时", "巳时", "午时", "未时", "申时", "酉时", "戌时", "亥时" };
    const double shifted = std::fmod(p_hour + 1.0 + 24.0, 24.0);
    return NAMES[std::min(11, int(shifted / 2.0))];
}

std::string clock_text(double p_seconds, const DayRules &p_rules) {
    const ClockReading c = read_clock(p_seconds, p_rules);
    const int minutes = int(std::floor(c.hour * 60.0 + 1e-6));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s · 第 %d 日 · %02d:%02d", shichen_name(c.hour), c.day + 1, minutes / 60, minutes % 60);
    return buf;
}

double next_day_mark(double p_seconds, const DayRules &p_rules, DayMark *r_mark) {
    const struct {
        double hour;
        DayMark mark;
    } MARKS[] = {
        { p_rules.work_start, DayMark::WORK_START },
        { p_rules.lunch_start, DayMark::LUNCH_START },
        { p_rules.lunch_end, DayMark::LUNCH_END },
        { p_rules.work_end, DayMark::WORK_END },
    };
    const int day = int(std::floor(std::max(0.0, p_seconds) / p_rules.day_seconds));
    for (int d = day; d <= day + 1; d++) {
        for (const auto &m : MARKS) {
            const double at = clock_at(d, m.hour, p_rules);
            if (at > p_seconds) {
                if (r_mark) {
                    *r_mark = m.mark;
                }
                return at;
            }
        }
    }
    if (r_mark) {
        *r_mark = DayMark::WORK_START;
    }
    return clock_at(day + 2, p_rules.work_start, p_rules);
}
