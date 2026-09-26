#include "day_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

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

double SeasonView::next_shelter_change(double p_from, double p_until) const {
    return std::numeric_limits<double>::infinity();
}

DayMarks day_marks(int p_day, const DayRules &p_rules, const SeasonView *p_season) {
    DayMarks m{ p_rules.work_start, p_rules.lunch_start, p_rules.lunch_end, p_rules.work_end };
    if (p_season) {
        p_season->work_hours(p_day, m.work_start, m.work_end);
        // 出工在歇晌之前、收工在歇晌之后（极端的时令也不让分界点倒过来）
        m.work_start = std::min(m.work_start, p_rules.lunch_start - 0.25);
        m.work_end = std::max(m.work_end, p_rules.lunch_end + 0.25);
    }
    return m;
}

double next_day_mark(double p_seconds, const DayRules &p_rules, DayMark *r_mark, const SeasonView *p_season) {
    const int day = int(std::floor(std::max(0.0, p_seconds) / p_rules.day_seconds));
    for (int d = day; d <= day + 2; d++) {
        const DayMarks dm = day_marks(d, p_rules, p_season);
        const struct {
            double hour;
            DayMark mark;
        } MARKS[] = {
            { dm.work_start, DayMark::WORK_START },
            { dm.lunch_start, DayMark::LUNCH_START },
            { dm.lunch_end, DayMark::LUNCH_END },
            { dm.work_end, DayMark::WORK_END },
        };
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
    return clock_at(day + 3, day_marks(day + 3, p_rules, p_season).work_start, p_rules);
}

bool is_home_time(double p_seconds, const DayRules &p_rules, const SeasonView *p_season) {
    if (!p_season) {
        return is_home_hour(read_clock(p_seconds, p_rules).hour, p_rules);
    }
    const ClockReading c = read_clock(p_seconds, p_rules);
    const DayMarks m = day_marks(c.day, p_rules, p_season);
    const bool rest = c.hour < m.work_start || c.hour >= m.work_end || (c.hour >= m.lunch_start && c.hour < m.lunch_end);
    return rest || p_season->sheltering(p_seconds);
}
