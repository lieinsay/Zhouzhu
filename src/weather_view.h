#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "weather.h"

// 天气给脚本与界面看的样子（Godot 这边）：一次取样 → 字典；此地此刻 → 一行字（「小雨 18 °C · 东南风 3 m/s」）。
namespace weather_view {

// WeatherSample → { kind, kind_name, day_kind, day_kind_name, cloud, cloud_cover, coverage, precip, precip_mm_h, snow_frac, temp_c,
//   wind_x, wind_z, wind_ms, wind_name, storm, fog, fog_top, wet, front, sailable, snow_mm, ice, green, leaf, autumn, spring, crop }
godot::Dictionary to_dictionary(const WeatherSample &p_sample);
// 「小雨 18°C · 东南风 3 m/s」；p_with_wind 为假时不说风
godot::String short_text(const WeatherSample &p_sample, bool p_with_wind = true);
godot::String kind_name(WeatherKind p_kind);

} // namespace weather_view
