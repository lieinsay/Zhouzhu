#include "local_season.h"

#include <algorithm>
#include <cmath>
#include <limits>

double crop_season_factor(double p_mean_temp_c, double p_snow_mm, const SeasonRules &p_rules) {
    if (p_snow_mm >= p_rules.snow_stops_mm) {
        return 0.0;
    }
    const double t = std::clamp((p_mean_temp_c - p_rules.grow_cold_c) / std::max(1e-6, p_rules.grow_warm_c - p_rules.grow_cold_c), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t) * (1.0 - std::max(0.0, p_snow_mm) / p_rules.snow_stops_mm);
}

LocalSeason::LocalSeason(const WeatherModel *p_model, const DayRules &p_day, double p_day_offset, double p_x, double p_z, double p_h,
        const SeasonRules &p_rules) :
        model(p_model), day(p_day), day_offset(p_day_offset), x(p_x), z(p_z), h(p_h), rules(p_rules) {}

void LocalSeason::evaluate(int64_t p_tick) const {
    if (p_tick == cached_tick) {
        return;
    }
    cached_tick = p_tick;
    if (!model) {
        cached_shelter = false;
        cached_growth = 1.0;
        return;
    }
    const double day_at = calendar_day(double(p_tick) * step_seconds());
    const WeatherSample s = model->sample(day_at, x, z, h);
    cached_shelter = s.precip >= rules.shelter_precip || s.storm >= rules.shelter_storm;
    cached_growth = crop_season_factor(model->mean_temperature(day_at, h), s.snow_mm, rules);
}

void LocalSeason::work_hours(int p_day, double &r_start, double &r_end) const {
    if (p_day != cached_day) {
        cached_day = p_day;
        if (model) {
            daylight_work_hours(double(p_day) + day_offset, model->calendar(), rules.work, cached_start, cached_end);
        } else {
            cached_start = day.work_start;
            cached_end = day.work_end;
        }
    }
    r_start = cached_start;
    r_end = cached_end;
}

bool LocalSeason::sheltering(double p_seconds) const {
    evaluate(int64_t(std::floor(p_seconds / step_seconds())));
    return cached_shelter;
}

double LocalSeason::next_shelter_change(double p_from, double p_until) const {
    const double step = step_seconds();
    const int64_t first = int64_t(std::floor(p_from / step));
    const bool now = sheltering(p_from);
    for (int64_t tick = first + 1;; tick++) {
        const double at = double(tick) * step;
        if (at > p_until) {
            return std::numeric_limits<double>::infinity();
        }
        if (at <= p_from) {
            continue;
        }
        evaluate(tick);
        if (cached_shelter != now) {
            return at;
        }
    }
}

double LocalSeason::growth_factor(double p_seconds) const {
    evaluate(int64_t(std::floor(p_seconds / step_seconds())));
    return cached_growth;
}

bool LocalSeason::sowing(double p_seconds) const {
    return growth_factor(p_seconds) >= rules.sow_min_factor;
}

double FieldClimate::growth_factor(double p_height) const {
    if (!model) {
        return 1.0;
    }
    return crop_season_factor(model->mean_temperature(day, p_height), model->elevation(day, p_height).snow_mm, rules);
}
