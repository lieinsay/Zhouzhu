#pragma once

#include <string>

// 历法与日月：日历（年、季、月、日）、太阳与月亮在天上的位置、日出日落，以及由日出日落定的出工收工。不含 Godot 头。
//
// 时间用「历日」：从第 0 年第 0 天子夜起的天数，可带小数（0.5 = 第 0 天正午）。一年 year_days 天，分 seasons 季，
// 每季 months_per_season 个月，每月 days_per_month 天（skyisle-gen 的行星：336 天、4 季、每季 3 个月、每月 28 天）。
// 钟点按当地的真太阳时：12 点太阳正中（游戏里只有一个钟，就当整个岛群同一个时区）。
// 太阳直射点在南北回归线之间按余弦走：北半球夏至（solstice_n）那天赤纬 = +交角。月亮一个朔望月 28 天，与月份对齐：每月初一是朔、十五上下是望。
// 坐标与游戏一致：x 向东、y 向上、z 向南。

struct CalendarRules {
    int year_days = 336;
    int seasons = 4;
    int months_per_season = 3;
    int days_per_month = 28;
    double solstice_n = 126.0; // 北半球夏至（太阳直射最北）是一年里的第几天
    double tilt_deg = 34.0; // 黄赤交角
    double latitude_deg = 30.0; // 这一处的纬度（南负）
    double synodic_days = 28.0; // 朔望月
    double new_moon_day = 0.0; // 哪天是朔（一年里的第几天；与月初对齐）

    int months() const { return seasons * months_per_season; }
    int days_per_season() const { return months_per_season * days_per_month; }
};

struct CalendarDate {
    int year = 0; // 第几年（0 起）
    int day_of_year = 0; // 0 起
    int season = 0; // 0 起
    int month = 0; // 一年里的第几个月（0 起）
    int day_of_month = 0; // 月里的第几天（0 起，初一 = 0）
    double hour = 0.0; // [0, 24)
};

CalendarDate calendar_date(double p_day, const CalendarRules &p_rules);
// 「三月初四」（月份一到十二，日子初一……初十、十一……二十、廿一……）
std::string month_day_text(const CalendarDate &p_date);
// 一年里的第几天（按年循环，[0, year_days)）
double day_of_year(double p_day, const CalendarRules &p_rules);

struct SunState {
    double declination_deg = 0.0;
    double altitude_deg = 0.0; // 地平线以上（负 = 在地平线下）
    double azimuth_deg = 0.0; // 从正北顺时针
    double dir_x = 0.0, dir_y = 1.0, dir_z = 0.0; // 指向太阳的单位向量（x 东、y 上、z 南）
    double sunrise_hour = 6.0, sunset_hour = 18.0; // 当天的日出日落（极昼 0 / 24；极夜都是 12）
    double day_length_h = 12.0;
};

// 太阳赤纬（按一年里的第几天，可带小数）
double sun_declination_deg(double p_day, const CalendarRules &p_rules);
// 某一刻（历日，带小数）太阳在哪、这天几点日出日落
SunState sun_at(double p_day, const CalendarRules &p_rules);

struct MoonState {
    double phase = 0.0; // 0 朔 → 0.5 望 → 1 朔
    double illuminated = 0.0; // 亮着的份额 0..1
    double altitude_deg = 0.0;
    double dir_x = 0.0, dir_y = 1.0, dir_z = 0.0; // 指向月亮的单位向量
};

// 月亮：朔望按 synodic_days 走，位置按「比太阳落后 相位 × 360°」近似（不计白道倾角）
MoonState moon_at(double p_day, const CalendarRules &p_rules);

// 由日出日落定的出工收工：日出后 lead_h 出工、日落前 lead_h 收工，夹在 [start_min, start_max] / [end_min, end_max] 里
struct DaylightWork {
    double lead_h = 0.4;
    double start_min = 5.0, start_max = 8.0;
    double end_min = 16.0, end_max = 19.5;
};
void daylight_work_hours(double p_day, const CalendarRules &p_rules, const DaylightWork &p_work, double &r_start, double &r_end);
