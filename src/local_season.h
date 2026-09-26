#pragma once

#include <cstdint>

#include "day_clock.h"
#include "farm_model.h"
#include "weather.h"

// 一处（村子、农田）的时令：天气模型 + 这一处的位置与海拔 + 日历偏移 → 作息、歇工、庄稼。不含 Godot 头。
// 钟的秒 → 历日 = 秒 / 一天的秒数 + 日历偏移（整日，WorldClock 给）。
//   作息：出工 = 日出后、收工 = 日落前，夹在 5–8 点 / 16–19.5 点（almanac 的 daylight_work_hours）；
//   歇工：这一处下大雨、大雪、刮风暴时（降水强度 ≥ shelter_precip，或风暴 ≥ shelter_storm）在外面的回家，雨停再出门。
//        按 shelter_step_h 一格格判（格里取格头那一刻），所以远处半小时一结和近处逐帧推进在同一个格边上歇工、出门；
//   庄稼：该海拔的日均温 grow_cold_c 以下不长、grow_warm_c 以上照常，中间平滑；积雪盖着不长。长得比 sow_min_factor 慢就不播不开荒。
// 近处（Villagers）与远处（账房）按同一个村心、同一个海拔各造一份，远近照旧同一套规矩。

struct SeasonRules {
    DaylightWork work;
    double shelter_step_h = 0.25;
    double shelter_precip = 0.45; // 约 2.4 mm/h：大雨、大雪
    double shelter_storm = 0.5;
    double grow_cold_c = 5.0, grow_warm_c = 16.0;
    double snow_stops_mm = 5.0;
    double sow_min_factor = 0.35;
};

// 庄稼的时令倍数：日均温与积雪（水当量 mm）→ 0..1
double crop_season_factor(double p_mean_temp_c, double p_snow_mm, const SeasonRules &p_rules);

class LocalSeason : public SeasonView {
    const WeatherModel *model = nullptr;
    DayRules day;
    double day_offset = 0.0;
    double x = 0.0, z = 0.0, h = 0.0;
    SeasonRules rules;
    // 同一格反复问（推进时每个事件都问一次）：记下最近问过的格
    mutable int64_t cached_tick = INT64_MIN;
    mutable bool cached_shelter = false;
    mutable double cached_growth = 1.0;
    mutable int cached_day = INT32_MIN;
    mutable double cached_start = 6.0, cached_end = 18.5;

    double step_seconds() const { return game_seconds(rules.shelter_step_h, day); }
    void evaluate(int64_t p_tick) const;

public:
    LocalSeason() = default;
    // p_x / p_z / p_h：这一处的世界坐标与海拔
    LocalSeason(const WeatherModel *p_model, const DayRules &p_day, double p_day_offset, double p_x, double p_z, double p_h,
            const SeasonRules &p_rules = SeasonRules());

    double calendar_day(double p_seconds) const { return p_seconds / day.day_seconds + day_offset; }
    const WeatherModel *get_model() const { return model; }

    void work_hours(int p_day, double &r_start, double &r_end) const override;
    bool sheltering(double p_seconds) const override;
    double next_shelter_change(double p_from, double p_until) const override;
    double growth_factor(double p_seconds) const override;
    bool sowing(double p_seconds) const override;
};

// 近处农田（一张地图的一片田）：某一刻，按格子的海拔问
class FieldClimate : public GrowthClimate {
    const WeatherModel *model = nullptr;
    double day = 0.0;
    SeasonRules rules;

public:
    FieldClimate(const WeatherModel *p_model, double p_calendar_day, const SeasonRules &p_rules = SeasonRules()) :
            model(p_model), day(p_calendar_day), rules(p_rules) {}
    double growth_factor(double p_height) const override;
};
