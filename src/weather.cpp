#include "weather.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "weather_noise.h"

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr int KIND_COUNT = int(WeatherKind::COUNT);
constexpr double HEAVY_MM_H = 1.2; // 每小时过这么多算大雨 / 大雪

//                                                    云量  雨区   至少mm 风至少 风暴 云海 雪
const WeatherKindDef KINDS[KIND_COUNT] = {
    { WeatherKind::CLEAR, "clear", "晴", 0.12, 0.0, 0.0, 0.0, 0.0, 0.0, false },
    { WeatherKind::CLOUDY, "cloudy", "多云", 0.62, 0.0, 0.0, 0.0, 0.0, 0.0, false },
    { WeatherKind::RAIN, "rain", "小雨", 0.85, 0.45, 3.0, 0.0, 0.0, 0.0, false },
    { WeatherKind::HEAVY_RAIN, "heavy_rain", "大雨", 0.95, 0.72, 15.0, 3.0, 0.0, 0.0, false },
    { WeatherKind::SEA_FOG, "sea_fog", "云海漫顶", 0.55, 0.2, 1.0, 0.0, 0.0, 1.0, false },
    { WeatherKind::STORM, "storm", "风暴", 1.0, 0.9, 30.0, 14.0, 1.0, 0.0, false },
    { WeatherKind::SNOW, "snow", "小雪", 0.85, 0.5, 3.0, 0.0, 0.0, 0.0, true },
    { WeatherKind::HEAVY_SNOW, "heavy_snow", "大雪", 0.95, 0.75, 12.0, 3.0, 0.0, 0.0, true },
    { WeatherKind::BLIZZARD, "blizzard", "暴风雪", 1.0, 0.9, 25.0, 14.0, 1.0, 0.0, true },
};

// 表里每箱的字段
enum Field {
    F_SNOW,
    F_ICE,
    F_GREEN,
    F_LEAF,
    F_AUTUMN,
    F_SPRING,
    F_CROP,
    F_TEMP,
};

double smoothstep(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

long long pmod(long long p_a, long long p_n) {
    const long long m = p_a % p_n;
    return m < 0 ? m + p_n : m;
}

uint64_t mix64(uint64_t p_x) {
    p_x += 0x9E3779B97F4A7C15ull;
    p_x = (p_x ^ (p_x >> 30)) * 0xBF58476D1CE4E5B9ull;
    p_x = (p_x ^ (p_x >> 27)) * 0x94D049BB133111EBull;
    return p_x ^ (p_x >> 31);
}

double hash01(uint64_t p_seed, long long p_k) {
    return double(mix64(p_seed ^ mix64(uint64_t(p_k))) >> 11) * (1.0 / 9007199254740992.0);
}

// 风从 p_from_deg 来，往哪吹（x 东、z 南），单位向量
void wind_to(double p_from_deg, double &r_x, double &r_z) {
    const double to = (p_from_deg + 180.0) * PI / 180.0;
    r_x = std::sin(to);
    r_z = -std::cos(to);
}

const ClimateDay &default_day() {
    static const ClimateDay d;
    return d;
}

std::vector<std::string> split(const std::string &p_text, char p_sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p_text) {
        if (c == p_sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

} // namespace

// ---------------------------------------------------------------- 天气类型

namespace weather_kinds {

const WeatherKindDef &at(WeatherKind p_kind) {
    return KINDS[std::clamp(int(p_kind), 0, KIND_COUNT - 1)];
}

const WeatherKindDef *by_id(const std::string &p_id) {
    for (const WeatherKindDef &k : KINDS) {
        if (p_id == k.id) {
            return &k;
        }
    }
    return nullptr;
}

const WeatherKindDef *by_name(const std::string &p_name) {
    for (const WeatherKindDef &k : KINDS) {
        if (p_name == k.name) {
            return &k;
        }
    }
    return nullptr;
}

} // namespace weather_kinds

double snow_fraction(double p_temp_c, double p_snow_temp, double p_band_c) {
    return smoothstep(p_snow_temp + p_band_c, p_snow_temp - p_band_c, p_temp_c);
}

std::string wind_name(double p_to_x, double p_to_z) {
    if (std::hypot(p_to_x, p_to_z) < 0.5) {
        return "无风";
    }
    // 风从哪来：往哪吹的反方向；方位从正北顺时针
    const double from_east = -p_to_x, from_north = p_to_z;
    double az = std::atan2(from_east, from_north) * 180.0 / PI;
    az = az < 0.0 ? az + 360.0 : az;
    static const char *const NAMES[8] = { "北风", "东北风", "东风", "东南风", "南风", "西南风", "西风", "西北风" };
    return NAMES[int(std::floor((az + 22.5) / 45.0)) % 8];
}

// ---------------------------------------------------------------- 解析

bool parse_climate(const std::string &p_text, Climate &r_climate, std::string &r_error) {
    Climate c;
    std::istringstream in(p_text);
    std::string line;
    int line_no = 0;
    double year_days = 0.0;
    while (std::getline(in, line)) {
        line_no++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            std::istringstream words(line.substr(1));
            std::vector<std::string> tokens;
            std::string w;
            while (words >> w) {
                tokens.push_back(w);
            }
            for (size_t k = 0; k + 1 < tokens.size(); k++) {
                const std::string &key = tokens[k];
                const std::string &val = tokens[k + 1];
                const double v = std::atof(val.c_str());
                if (key == "year_days") {
                    year_days = v;
                } else if (key == "precip_mm") {
                    c.precip_mm = v;
                } else if (key == "seasons") {
                    c.calendar.seasons = std::max(1, int(v));
                } else if (key == "months_per_season") {
                    c.calendar.months_per_season = std::max(1, int(v));
                } else if (key == "days_per_month") {
                    c.calendar.days_per_month = std::max(1, int(v));
                    c.calendar.synodic_days = double(c.calendar.days_per_month);
                } else if (key == "solstice_n") {
                    c.calendar.solstice_n = v;
                } else if (key == "lat") {
                    c.calendar.latitude_deg = v;
                } else if (key == "tilt") {
                    c.calendar.tilt_deg = v;
                } else if (key == "ref_m") {
                    c.ref_m = v;
                } else if (key == "lapse") {
                    c.lapse = v;
                } else if (key == "rim_m") {
                    c.rim_m = v;
                } else if (key == "fog_rise_m") {
                    c.fog_rise_m = v;
                } else if (key == "snow_temp") {
                    c.snow_temp = v;
                } else if (key == "season_names") {
                    c.season_names = split(val, ',');
                }
            }
            continue;
        }
        std::vector<std::string> cols = split(line, '\t');
        if (cols.size() < 3) {
            std::istringstream row(line);
            cols.clear();
            std::string w;
            while (row >> w) {
                cols.push_back(w);
            }
        }
        if (cols.size() < 3 || std::atoi(cols[0].c_str()) != int(c.days.size())) {
            r_error = "weather.tsv 第 " + std::to_string(line_no) + " 行读不了（应为 day<TAB>precip_mm<TAB>temp_c<TAB>type…，日序从 0 连续）";
            return false;
        }
        ClimateDay d;
        d.precip_mm = std::max(0.0, std::atof(cols[1].c_str()));
        d.temp_c = std::atof(cols[2].c_str());
        if (cols.size() > 3) {
            const WeatherKindDef *k = weather_kinds::by_name(cols[3]);
            if (!k) {
                k = weather_kinds::by_id(cols[3]);
            }
            if (!k) {
                r_error = "weather.tsv 第 " + std::to_string(line_no) + " 行的天气「" + cols[3] + "」不认识";
                return false;
            }
            d.kind = k->kind;
        } else {
            d.kind = d.precip_mm > 0.0 ? WeatherKind::RAIN : WeatherKind::CLEAR;
        }
        if (cols.size() > 5) {
            d.wind_from_deg = std::atof(cols[4].c_str());
            d.wind_ms = std::max(0.0, std::atof(cols[5].c_str()));
        }
        if (cols.size() > 6) {
            d.sailable = std::atoi(cols[6].c_str()) != 0;
        }
        if (cols.size() > 7) {
            d.storm = std::atoi(cols[7].c_str());
        }
        c.days.push_back(d);
    }
    if (c.days.empty()) {
        r_error = "weather.tsv 里没有一天";
        return false;
    }
    if (year_days > 0.0 && int(year_days) != int(c.days.size())) {
        r_error = "weather.tsv 的 year_days 与行数对不上";
        return false;
    }
    c.calendar.year_days = int(c.days.size());
    if (c.precip_mm <= 0.0) {
        for (const ClimateDay &d : c.days) {
            c.precip_mm += d.precip_mm;
        }
    }
    r_climate = c;
    return true;
}

// ---------------------------------------------------------------- 模型

struct WeatherModel::DayState {
    double cloud = 0.0, coverage = 0.0, mm = 0.0, temp = 18.0, wind_x = 0.0, wind_z = 0.0, storm = 0.0, fog = 0.0, sailable = 1.0;
    double w[KIND_COUNT] = {};

    void mix_toward(const DayState &p_other, double p_t) {
        if (p_t <= 0.0) {
            return;
        }
        auto lerp = [p_t](double &a, double b) { a += (b - a) * p_t; };
        lerp(cloud, p_other.cloud);
        lerp(coverage, p_other.coverage);
        lerp(mm, p_other.mm);
        lerp(temp, p_other.temp);
        lerp(wind_x, p_other.wind_x);
        lerp(wind_z, p_other.wind_z);
        lerp(storm, p_other.storm);
        lerp(fog, p_other.fog);
        lerp(sailable, p_other.sailable);
        for (int k = 0; k < KIND_COUNT; k++) {
            lerp(w[k], p_other.w[k]);
        }
    }
    WeatherKind dominant() const { return WeatherKind(int(std::max_element(w, w + KIND_COUNT) - w)); }
};

WeatherModel::WeatherModel() {
    climate.calendar.year_days = 336;
    build_tables();
}

WeatherModel::WeatherModel(const Climate &p_climate, double p_h_min, double p_h_max, const WeatherRules &p_rules) :
        climate(p_climate), rules(p_rules) {
    data = !climate.days.empty();
    // 分箱多出上下各一截：岛底、峰顶之上一点也有值
    h_lo = std::min(p_h_min, p_h_max) - 50.0;
    h_hi = std::max(p_h_min, p_h_max) + 50.0;
    if (h_hi - h_lo < 100.0) {
        h_hi = h_lo + 100.0;
    }
    build_tables();
}

const ClimateDay &WeatherModel::day(long long p_day) const {
    if (climate.days.empty()) {
        return default_day();
    }
    return climate.days[size_t(pmod(p_day, (long long)climate.days.size()))];
}

std::string WeatherModel::season_name(int p_season) const {
    if (p_season >= 0 && p_season < int(climate.season_names.size())) {
        return climate.season_names[size_t(p_season)];
    }
    static const char *const FALLBACK[] = { "一季", "二季", "三季", "四季" };
    return FALLBACK[std::clamp(p_season, 0, 3)];
}

void WeatherModel::build_tables() {
    year = std::max(1, int(climate.days.size()));
    bins = std::max(2, rules.bins);
    if (!data) {
        h_lo = -50.0;
        h_hi = 2500.0;
    }
    table.assign(size_t(year) * size_t(bins) * FIELDS, 0.0f);
    soil_wet.assign(size_t(year), 0.0);

    // 前期降水指数 → 地湿（1 = 平常）
    std::vector<double> api(size_t(year), 0.0);
    double a = 0.0;
    for (int pass = 0; pass < 2; pass++) {
        for (int d = 0; d < year; d++) {
            a = 0.96 * a + day(d).precip_mm;
            api[size_t(d)] = a;
        }
    }
    double mean = 0.0;
    for (double v : api) {
        mean += v / year;
    }
    std::vector<double> wet_norm(size_t(year), 1.0);
    for (int d = 0; d < year; d++) {
        wet_norm[size_t(d)] = mean > 1e-6 ? api[size_t(d)] / mean : 1.0;
        soil_wet[size_t(d)] = std::clamp((wet_norm[size_t(d)] - 0.8) / 2.5, 0.0, 1.0);
    }

    const double k_ph = std::exp(-1.0 / std::max(1.0, rules.pheno_tau_d));
    const double k_lf = std::exp(-1.0 / std::max(1.0, rules.leaf_tau_d));
    const size_t nb = size_t(bins);
    std::vector<double> pack(nb, 0.0), ice(nb, 0.0), tph(nb, 0.0), tlf(nb, 0.0), gdd(nb, 0.0);
    std::vector<double> tlf_hist(size_t(year) * size_t(bins), 0.0);
    auto bin_h = [this](int b) { return h_lo + (h_hi - h_lo) * double(b) / double(bins - 1); };
    for (int b = 0; b < bins; b++) {
        tph[size_t(b)] = tlf[size_t(b)] = day(0).temp_c + climate.lapse * (climate.ref_m - bin_h(b)) / 1000.0;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int d = 0; d < year; d++) {
            const ClimateDay &cd = day(d);
            for (int b = 0; b < bins; b++) {
                const size_t bi = size_t(b);
                const double h = bin_h(b);
                const double t = cd.temp_c + climate.lapse * (climate.ref_m - h) / 1000.0;
                const double oro = 1.0 + rules.orographic_per_km * std::clamp((h - climate.ref_m) / 1000.0, -0.3, 1.5);
                const double p = cd.precip_mm * oro;
                pack[bi] += p * snow_fraction(t, climate.snow_temp, rules.snow_band_c);
                pack[bi] = std::max(0.0, pack[bi] - std::min(pack[bi], rules.melt_mm_per_c * std::max(0.0, t)));
                ice[bi] = std::clamp(ice[bi] + (t < 0.0 ? -t : -2.0 * t), 0.0, 60.0);
                tph[bi] = tph[bi] * k_ph + t * (1.0 - k_ph);
                tlf[bi] = tlf[bi] * k_lf + t * (1.0 - k_lf);
                const double wn = std::min(1.0, wet_norm[size_t(d)]);
                if (t < rules.crop_base_c) {
                    gdd[bi] = std::max(0.0, gdd[bi] - 30.0); // 冷了：地里只剩茬，歇着
                } else {
                    gdd[bi] += (t - rules.crop_base_c) * (0.4 + 0.6 * wn);
                    if (gdd[bi] >= rules.crop_gdd) {
                        gdd[bi] = 0.0; // 收了，再种
                    }
                }
                tlf_hist[size_t(d) * size_t(bins) + bi] = tlf[bi];
                if (pass == 0) {
                    continue;
                }
                const double trend = tlf[bi] - tlf_hist[size_t(pmod(d - 10, year)) * size_t(bins) + bi];
                const double cooling = smoothstep(0.0, -0.8, trend), warming = smoothstep(0.0, 0.8, trend);
                float *row = &table[(size_t(d) * size_t(bins) + bi) * FIELDS];
                row[F_SNOW] = float(pack[bi]);
                row[F_ICE] = float(ice[bi]);
                row[F_GREEN] = float(smoothstep(2.0, 12.0, tph[bi]) * (0.35 + 0.65 * wn));
                row[F_LEAF] = float(smoothstep(4.0, 9.0, tlf[bi]));
                row[F_AUTUMN] = float(cooling * smoothstep(14.0, 8.0, tlf[bi]));
                row[F_SPRING] = float(warming * smoothstep(5.0, 9.0, tlf[bi]) * smoothstep(15.0, 11.0, tlf[bi]));
                row[F_CROP] = float(data ? gdd[bi] / rules.crop_gdd : 0.5);
                row[F_TEMP] = float(t);
            }
        }
    }

    // 云团漂移：每天子夜时累计漂了多远
    drift_x.assign(size_t(year) + 1, 0.0);
    drift_z.assign(size_t(year) + 1, 0.0);
    const double per_day = 24.0 * 3600.0 * rules.drift_scale;
    for (int d = 0; d < year; d++) {
        double tx = 0.0, tz = 0.0;
        wind_to(day(d).wind_from_deg, tx, tz);
        drift_x[size_t(d) + 1] = drift_x[size_t(d)] + tx * day(d).wind_ms * per_day;
        drift_z[size_t(d) + 1] = drift_z[size_t(d)] + tz * day(d).wind_ms * per_day;
    }
}

const float *WeatherModel::row(long long p_day) const {
    return &table[size_t(pmod(p_day, year)) * size_t(bins) * FIELDS];
}

WeatherModel::DayState WeatherModel::day_state(long long p_day) const {
    const ClimateDay &cd = day(p_day);
    const WeatherKindDef &def = weather_kinds::at(cd.kind);
    DayState s;
    s.cloud = def.cloud;
    s.coverage = def.coverage;
    s.mm = def.coverage > 0.0 ? std::max(cd.precip_mm, def.min_mm) : 0.0;
    s.temp = cd.temp_c;
    double tx = 0.0, tz = 0.0;
    wind_to(cd.wind_from_deg, tx, tz);
    const double speed = std::max(cd.wind_ms, def.wind_min_ms);
    s.wind_x = tx * speed;
    s.wind_z = tz * speed;
    s.storm = def.storm;
    s.fog = def.fog;
    s.sailable = cd.sailable ? 1.0 : 0.0;
    s.w[int(cd.kind)] = 1.0;
    return s;
}

WeatherModel::DayState WeatherModel::override_state(long long p_day) const {
    DayState s = day_state(p_day);
    const WeatherKindDef &def = weather_kinds::at(over.kind);
    // 开发者要看的就是这种天气：下雨下雪的覆盖让雨区铺满（强弱照旧随云团起伏），多云多盖一点，所以人站在哪儿都看得见；
    // 云海漫顶看的是雾漫上来，不下毛毛雨
    const bool wet = def.coverage > 0.0 && def.fog <= 0.0;
    s.cloud = wet ? 1.0 : (over.kind == WeatherKind::CLOUDY ? 0.85 : def.cloud);
    s.coverage = wet ? 1.0 : 0.0;
    s.mm = wet ? 2.0 * def.min_mm : 0.0;
    if (def.snowy) {
        s.temp = std::min(s.temp, rules.snow_override_c);
    }
    const double speed = std::max(std::hypot(s.wind_x, s.wind_z), def.wind_min_ms);
    double tx = 0.0, tz = 0.0;
    wind_to(day(p_day).wind_from_deg, tx, tz);
    s.wind_x = tx * speed;
    s.wind_z = tz * speed;
    s.storm = def.storm;
    s.fog = def.fog;
    s.sailable = def.storm < 0.5 && speed < 10.0 ? 1.0 : 0.0;
    std::fill(s.w, s.w + KIND_COUNT, 0.0);
    s.w[int(over.kind)] = 1.0;
    return s;
}

double WeatherModel::front_arrival(long long p_day, double p_x, double p_z) const {
    const ClimateDay &cd = day(p_day);
    double tx = 0.0, tz = 0.0;
    wind_to(cd.wind_from_deg, tx, tz);
    const double along_km = (p_x * tx + p_z * tz) / 1000.0; // 顺风量：上风先到
    const double jitter_h = (2.0 * hash01(rules.seed, p_day) - 1.0) * rules.front_jitter_h;
    return double(p_day) + (jitter_h + along_km / std::max(1.0, rules.front_kmh)) / 24.0;
}

WeatherModel::DayState WeatherModel::blended(double p_day, double p_x, double p_z, double &r_front, int &r_day_index) const {
    const long long k = (long long)std::floor(p_day);
    const double half = 0.5 * rules.front_width_h / 24.0;
    const double arrive_k = front_arrival(k, p_x, p_z), arrive_next = front_arrival(k + 1, p_x, p_z);
    const double wk = smoothstep(arrive_k - half, arrive_k + half, p_day);
    const double wn = smoothstep(arrive_next - half, arrive_next + half, p_day);
    DayState s = day_state(k - 1);
    s.mix_toward(day_state(k), wk);
    s.mix_toward(day_state(k + 1), wn);
    r_front = std::max(1.0 - std::fabs(2.0 * wk - 1.0), 1.0 - std::fabs(2.0 * wn - 1.0));
    r_day_index = int(wn > 0.5 ? k + 1 : (wk > 0.5 ? k : k - 1));
    if (over.blend > 0.0) {
        DayState o = over.kind == WeatherKind::COUNT ? s : override_state(r_day_index); // COUNT：天气照自动，只偏气温
        o.temp += over.temp_offset_c;
        s.mix_toward(o, std::clamp(over.blend, 0.0, 1.0));
    }
    return s;
}

void WeatherModel::cloud_drift(double p_day, double &r_x, double &r_z) const {
    const double whole = std::floor(p_day);
    const double years = std::floor(whole / year);
    const long long doy = (long long)(whole - years * year);
    const double frac = p_day - whole;
    double tx = 0.0, tz = 0.0;
    wind_to(day(doy).wind_from_deg, tx, tz);
    const double per_day = 24.0 * 3600.0 * rules.drift_scale * day(doy).wind_ms;
    r_x = years * drift_x[size_t(year)] + drift_x[size_t(doy)] + tx * per_day * frac;
    r_z = years * drift_z[size_t(year)] + drift_z[size_t(doy)] + tz * per_day * frac;
}

ElevationState WeatherModel::elevation(double p_day_in, double p_h_in) const {
    // 不是有限数（上层的位置坏了）：当台面、第 0 天，别拿它当下标
    const double p_day = std::isfinite(p_day_in) ? p_day_in : 0.0;
    const double p_h = std::isfinite(p_h_in) ? p_h_in : climate.ref_m;
    const double blend = std::clamp(over.blend, 0.0, 1.0);
    // 气温偏移等效成海拔挪动：冷 1 °C ≈ 高 1000 / 直减率 米
    const double h = p_h - over.temp_offset_c * blend * 1000.0 / std::max(0.1, climate.lapse);
    const long long d = (long long)std::floor(p_day);
    const double f = p_day - double(d);
    const double u = std::clamp((h - h_lo) / (h_hi - h_lo), 0.0, 1.0) * double(bins - 1);
    const int i = std::min(bins - 2, int(u));
    const double t = u - i;
    const float *a = row(d - 1), *b = row(d);
    auto get = [&](int field) {
        const double va = a[size_t(i) * FIELDS + field] + (a[size_t(i + 1) * FIELDS + field] - a[size_t(i) * FIELDS + field]) * t;
        const double vb = b[size_t(i) * FIELDS + field] + (b[size_t(i + 1) * FIELDS + field] - b[size_t(i) * FIELDS + field]) * t;
        return va + (vb - va) * f;
    };
    ElevationState e;
    e.snow_mm = get(F_SNOW);
    e.ice = get(F_ICE);
    e.green = get(F_GREEN);
    e.leaf = get(F_LEAF);
    e.autumn = get(F_AUTUMN);
    e.spring = get(F_SPRING);
    e.crop = get(F_CROP);
    // 表是按挪过的海拔取的，偏移已经算进去；超出分箱的两头按直减率外推
    e.temp_mean = get(F_TEMP) + climate.lapse * (std::clamp(h, h_lo, h_hi) - h) / 1000.0;
    if (over.fresh_snow_mm > 0.0) {
        // 覆盖成下雪时新落的雪：此刻（含覆盖的气温）冷得住的地方才留得住
        const double cold = smoothstep(climate.snow_temp + 1.5, climate.snow_temp - 0.5, mean_temperature(p_day, p_h));
        e.snow_mm = std::max(e.snow_mm, over.fresh_snow_mm * cold);
    }
    return e;
}

double WeatherModel::mean_temperature(double p_day_in, double p_h_in) const {
    const double p_day = std::isfinite(p_day_in) ? p_day_in : 0.0;
    const double p_h = std::isfinite(p_h_in) ? p_h_in : climate.ref_m;
    double front = 0.0;
    int di = 0;
    const DayState s = blended(p_day, 0.0, 0.0, front, di);
    return s.temp + climate.lapse * (climate.ref_m - p_h) / 1000.0;
}

double WeatherModel::snow_line(double p_day) const {
    double prev_h = 0.0, prev_s = 0.0;
    for (int b = 0; b < bins; b++) {
        const double h = h_lo + (h_hi - h_lo) * double(b) / double(bins - 1);
        const double s = elevation(p_day, h).snow_mm;
        if (s > 1.0) {
            if (b == 0) {
                return h;
            }
            return prev_h + (h - prev_h) * std::clamp((1.0 - prev_s) / std::max(1e-6, s - prev_s), 0.0, 1.0);
        }
        prev_h = h;
        prev_s = s;
    }
    return 1e9;
}

double WeatherModel::ice_line(double p_day, double p_threshold) const {
    for (int b = 0; b < bins; b++) {
        const double h = h_lo + (h_hi - h_lo) * double(b) / double(bins - 1);
        if (elevation(p_day, h).ice >= p_threshold) {
            return h;
        }
    }
    return 1e9;
}

void WeatherModel::elevation_table(double p_day, std::vector<float> &r_out) const {
    r_out.resize(size_t(bins) * FIELDS);
    for (int b = 0; b < bins; b++) {
        const double h = h_lo + (h_hi - h_lo) * double(b) / double(bins - 1);
        const ElevationState e = elevation(p_day, h);
        float *o = &r_out[size_t(b) * FIELDS];
        o[F_SNOW] = float(e.snow_mm);
        o[F_ICE] = float(e.ice);
        o[F_GREEN] = float(e.green);
        o[F_LEAF] = float(e.leaf);
        o[F_AUTUMN] = float(e.autumn);
        o[F_SPRING] = float(e.spring);
        o[F_CROP] = float(e.crop);
        o[F_TEMP] = float(e.temp_mean);
    }
}

WeatherSample WeatherModel::sample(double p_day_in, double p_x_in, double p_z_in, double p_h_in) const {
    // 不是有限数（上层的位置坏了）：按群心、台面、第 0 天取，别拿它当下标
    const double p_day = std::isfinite(p_day_in) ? p_day_in : 0.0;
    const double p_x = std::isfinite(p_x_in) ? p_x_in : 0.0, p_z = std::isfinite(p_z_in) ? p_z_in : 0.0;
    const double p_h = std::isfinite(p_h_in) ? p_h_in : climate.ref_m;
    WeatherSample out;
    double front = 0.0;
    int di = 0;
    const DayState s = blended(p_day, p_x, p_z, front, di);
    out.front = front;
    out.day_kind = s.dominant();

    double dx = 0.0, dz = 0.0;
    cloud_drift(p_day, dx, dz);
    const double field = wx_fbm((p_x - dx) / rules.cell_m, (p_z - dz) / rules.cell_m);
    out.cloud_cover = std::clamp(s.cloud, 0.0, 1.0);
    out.coverage = std::clamp(s.coverage, 0.0, 1.0);
    const double cthr = wx_threshold(out.cloud_cover);
    out.cloud = smoothstep(cthr - rules.cloud_edge, cthr + rules.cloud_edge, field);
    double rate = 0.0;
    if (out.coverage > 1e-3 && s.mm > 0.0) {
        const double rthr = wx_threshold(out.coverage);
        const double inside = smoothstep(rthr - rules.rain_edge, rthr + rules.rain_edge, field);
        const double core = smoothstep(rthr, rthr + 0.2, field);
        const double oro = 1.0 + rules.orographic_per_km * std::clamp((p_h - climate.ref_m) / 1000.0, -0.3, 1.5);
        rate = s.mm / 24.0 / std::max(0.05, out.coverage) * (0.5 + core) * inside * oro;
    }
    out.precip_mm_h = rate;
    out.precip = 1.0 - std::exp(-rate / 3.0);

    const double hour = (p_day - std::floor(p_day)) * 24.0;
    const double clouds = std::max(out.cloud, 0.5 * out.cloud_cover);
    const double diurnal = rules.diurnal_c * (1.0 - 0.6 * clouds) * std::cos(2.0 * PI * (hour - 14.5) / 24.0);
    out.temp_c = s.temp + climate.lapse * (climate.ref_m - p_h) / 1000.0 + diurnal - 1.5 * std::min(1.0, rate / 4.0);
    out.snow_frac = snow_fraction(out.temp_c, climate.snow_temp, rules.snow_band_c);

    const double gust = 1.0 + 0.15 * std::clamp((p_h - climate.ref_m) / 1000.0, 0.0, 2.0);
    out.wind_x = s.wind_x * gust;
    out.wind_z = s.wind_z * gust;
    out.wind_ms = std::hypot(out.wind_x, out.wind_z);
    out.storm = std::clamp(s.storm, 0.0, 1.0);

    if (s.fog > 1e-3) {
        out.fog_top = climate.rim_m + climate.fog_rise_m;
    }
    out.sea_fog = std::clamp(s.fog, 0.0, 1.0);
    const double sea_fog = s.fog * smoothstep(out.fog_top + 40.0, out.fog_top - 60.0, p_h);
    out.fog = std::clamp(std::max(sea_fog, 0.25 * out.precip * (1.0 + out.snow_frac)), 0.0, 1.0);

    const ElevationState e = elevation(p_day, p_h);
    out.snow_mm = e.snow_mm;
    out.ice = e.ice;
    out.green = e.green;
    out.leaf = e.leaf;
    out.autumn = e.autumn;
    out.spring = e.spring;
    out.crop = e.crop;
    const long long d = (long long)std::floor(p_day);
    const double f = p_day - double(d);
    const double soil = soil_wet[size_t(pmod(d - 1, year))] + (soil_wet[size_t(pmod(d, year))] - soil_wet[size_t(pmod(d - 1, year))]) * f;
    out.wet = std::clamp(std::max(0.7 * soil, 1.6 * out.precip) * (1.0 - out.snow_frac * 0.5), 0.0, 1.0);

    // 此地此刻看着像什么（风暴天雨区外也是狂风，照样叫风暴）
    const bool snowy = out.snow_frac > 0.5;
    if (out.storm > 0.5) {
        out.kind = snowy ? WeatherKind::BLIZZARD : WeatherKind::STORM;
    } else if (rate > 0.05) {
        if (rate > HEAVY_MM_H) {
            out.kind = snowy ? WeatherKind::HEAVY_SNOW : WeatherKind::HEAVY_RAIN;
        } else {
            out.kind = snowy ? WeatherKind::SNOW : WeatherKind::RAIN;
        }
    } else if (sea_fog > 0.35) {
        out.kind = WeatherKind::SEA_FOG;
    } else if (out.cloud > 0.55) {
        out.kind = WeatherKind::CLOUDY;
    } else {
        out.kind = WeatherKind::CLEAR;
    }
    out.sailable = s.sailable > 0.5 && out.storm < 0.5 && out.wind_ms < 10.0;
    return out;
}
