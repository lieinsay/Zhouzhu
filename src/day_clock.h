#pragma once

#include <string>

// 钟点：一天多长、几点出工几点收工、午间歇晌、什么时候吃饭；时辰名。不含 Godot 头。
// 钟本身（从第 0 天子夜起走过的秒数）只有一个，在场景之上的 WorldClock（autoload）里；这里只有「秒数 → 钟点」的规矩。
// 村子（近处逐帧、远处结算）、农田生长都按它换算，所以换一天的长度，人和庄稼一起变快变慢。

struct DayRules {
    double day_seconds = 2400.0; // 一天（24 个钟头）在游戏里走多少秒
    double work_start = 6.0; // 几点出工
    double lunch_start = 11.5; // 歇晌（也是午饭）
    double lunch_end = 12.5;
    double work_end = 18.5; // 收工（也是晚饭）
};

struct ClockReading {
    int day = 0; // 第几天（从 0 起）
    double hour = 0.0; // [0, 24)
};

// p_seconds：从第 0 天子夜起走过的秒数
ClockReading read_clock(double p_seconds, const DayRules &p_rules);
// 这个钟点该在家里吗（夜里、歇晌）
bool is_home_hour(double p_hour, const DayRules &p_rules);
// 时辰名（子 23–1、丑 1–3 …），UTF-8
const char *shichen_name(double p_hour);
// 「巳时 · 第 1 日 · 09:40」这种给人看的钟点
std::string clock_text(double p_seconds, const DayRules &p_rules);

// 秒 ↔ 游戏小时
inline double game_hours(double p_seconds, const DayRules &p_rules) { return p_seconds * 24.0 / p_rules.day_seconds; }
inline double game_seconds(double p_hours, const DayRules &p_rules) { return p_hours * p_rules.day_seconds / 24.0; }
// 第 p_day 天 p_hour 点是第几秒
inline double clock_at(int p_day, double p_hour, const DayRules &p_rules) { return (p_day + p_hour / 24.0) * p_rules.day_seconds; }

// 作息的分界点：出工、歇晌（午饭）、歇晌完、收工（晚饭）
enum class DayMark {
    WORK_START,
    LUNCH_START,
    LUNCH_END,
    WORK_END,
};

// p_seconds 之后（严格之后）的下一个分界点：在第几秒、是哪一个
double next_day_mark(double p_seconds, const DayRules &p_rules, DayMark *r_mark = nullptr);
// 这个分界点开饭吗（午饭、晚饭）
inline bool is_meal(DayMark p_mark) { return p_mark == DayMark::LUNCH_START || p_mark == DayMark::WORK_END; }
