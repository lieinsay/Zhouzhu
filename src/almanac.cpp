#include "almanac.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;
constexpr double HORIZON_DEG = -0.833; // 日出日落：日面上缘碰到地平线（含大气折射）

double positive_mod(double p_value, double p_period) {
    const double m = std::fmod(p_value, p_period);
    return m < 0.0 ? m + p_period : m;
}

// 按赤纬、时角算方向（x 东、y 上、z 南）与高度角、方位角
void sky_direction(double p_latitude_deg, double p_declination_deg, double p_hour_angle_deg, double &r_x, double &r_y, double &r_z, double &r_altitude,
        double &r_azimuth) {
    const double phi = p_latitude_deg * DEG, delta = p_declination_deg * DEG, h = p_hour_angle_deg * DEG;
    const double up = std::sin(phi) * std::sin(delta) + std::cos(phi) * std::cos(delta) * std::cos(h);
    const double east = -std::cos(delta) * std::sin(h);
    const double north = std::sin(delta) * std::cos(phi) - std::cos(delta) * std::sin(phi) * std::cos(h);
    const double len = std::max(1e-12, std::sqrt(up * up + east * east + north * north));
    r_x = east / len;
    r_y = up / len;
    r_z = -north / len;
    r_altitude = std::asin(std::clamp(up / len, -1.0, 1.0)) / DEG;
    r_azimuth = positive_mod(std::atan2(east, north) / DEG, 360.0);
}

const char *const DIGITS[] = { "", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十" };

std::string chinese_number(int p_n) {
    if (p_n <= 10) {
        return DIGITS[std::clamp(p_n, 0, 10)];
    }
    if (p_n < 20) {
        return std::string("十") + DIGITS[p_n - 10];
    }
    return std::string(DIGITS[std::min(9, p_n / 10)]) + "十" + DIGITS[p_n % 10];
}

} // namespace

double day_of_year(double p_day, const CalendarRules &p_rules) {
    return positive_mod(p_day, double(std::max(1, p_rules.year_days)));
}

CalendarDate calendar_date(double p_day, const CalendarRules &p_rules) {
    CalendarDate d;
    const double whole = std::floor(p_day);
    d.hour = std::clamp((p_day - whole) * 24.0, 0.0, 24.0 - 1e-9);
    const long long day = (long long)whole;
    const long long year_days = std::max(1, p_rules.year_days);
    long long year = day / year_days;
    long long doy = day % year_days;
    if (doy < 0) {
        doy += year_days;
        year -= 1;
    }
    d.year = int(year);
    d.day_of_year = int(doy);
    d.season = std::min(p_rules.seasons - 1, int(doy / std::max(1, p_rules.days_per_season())));
    d.month = std::min(p_rules.months() - 1, int(doy / std::max(1, p_rules.days_per_month)));
    d.day_of_month = int(doy - (long long)d.month * p_rules.days_per_month);
    return d;
}

std::string month_day_text(const CalendarDate &p_date) {
    const int m = p_date.month + 1, n = p_date.day_of_month + 1;
    std::string day;
    if (n <= 10) {
        day = std::string("初") + DIGITS[n];
    } else if (n < 20) {
        day = std::string("十") + DIGITS[n - 10];
    } else if (n == 20) {
        day = "二十";
    } else if (n < 30) {
        day = std::string("廿") + DIGITS[n - 20];
    } else {
        day = chinese_number(n);
    }
    return chinese_number(m) + "月" + day;
}

double sun_declination_deg(double p_day, const CalendarRules &p_rules) {
    const double year = double(std::max(1, p_rules.year_days));
    return p_rules.tilt_deg * std::cos(2.0 * PI * (day_of_year(p_day, p_rules) - p_rules.solstice_n) / year);
}

SunState sun_at(double p_day, const CalendarRules &p_rules) {
    SunState s;
    const double hour = (p_day - std::floor(p_day)) * 24.0;
    s.declination_deg = sun_declination_deg(p_day, p_rules);
    sky_direction(p_rules.latitude_deg, s.declination_deg, (hour - 12.0) * 15.0, s.dir_x, s.dir_y, s.dir_z, s.altitude_deg, s.azimuth_deg);
    // 日出日落按这天正午的赤纬
    const double delta = sun_declination_deg(std::floor(p_day) + 0.5, p_rules) * DEG, phi = p_rules.latitude_deg * DEG;
    const double denom = std::cos(phi) * std::cos(delta);
    const double cos_h0 = std::fabs(denom) < 1e-12 ? 2.0 : (std::sin(HORIZON_DEG * DEG) - std::sin(phi) * std::sin(delta)) / denom;
    if (cos_h0 >= 1.0) {
        s.sunrise_hour = s.sunset_hour = 12.0; // 极夜
        s.day_length_h = 0.0;
    } else if (cos_h0 <= -1.0) {
        s.sunrise_hour = 0.0; // 极昼
        s.sunset_hour = 24.0;
        s.day_length_h = 24.0;
    } else {
        const double h0 = std::acos(cos_h0) / DEG / 15.0;
        s.sunrise_hour = 12.0 - h0;
        s.sunset_hour = 12.0 + h0;
        s.day_length_h = 2.0 * h0;
    }
    return s;
}

MoonState moon_at(double p_day, const CalendarRules &p_rules) {
    MoonState m;
    m.phase = positive_mod((p_day - p_rules.new_moon_day) / std::max(1e-6, p_rules.synodic_days), 1.0);
    m.illuminated = 0.5 * (1.0 - std::cos(2.0 * PI * m.phase));
    const double year = double(std::max(1, p_rules.year_days));
    const double hour = (p_day - std::floor(p_day)) * 24.0;
    // 黄经比太阳超前 相位 × 360°：望月的赤纬与太阳相反，午夜过中天
    const double declination = p_rules.tilt_deg * std::cos(2.0 * PI * ((day_of_year(p_day, p_rules) - p_rules.solstice_n) / year + m.phase));
    double azimuth = 0.0;
    sky_direction(p_rules.latitude_deg, declination, (hour - 12.0) * 15.0 - m.phase * 360.0, m.dir_x, m.dir_y, m.dir_z, m.altitude_deg, azimuth);
    return m;
}

void daylight_work_hours(double p_day, const CalendarRules &p_rules, const DaylightWork &p_work, double &r_start, double &r_end) {
    const SunState s = sun_at(std::floor(p_day) + 0.5, p_rules);
    r_start = std::clamp(s.sunrise_hour + p_work.lead_h, p_work.start_min, p_work.start_max);
    r_end = std::clamp(s.sunset_hour - p_work.lead_h, p_work.end_min, p_work.end_max);
}
