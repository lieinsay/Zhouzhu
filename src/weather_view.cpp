#include "weather_view.h"

#include <cmath>

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace weather_view {

String kind_name(WeatherKind p_kind) {
    return String::utf8(weather_kinds::at(p_kind).name);
}

Dictionary to_dictionary(const WeatherSample &p_sample) {
    Dictionary d;
    d["kind"] = String(weather_kinds::at(p_sample.kind).id);
    d["kind_name"] = kind_name(p_sample.kind);
    d["day_kind"] = String(weather_kinds::at(p_sample.day_kind).id);
    d["day_kind_name"] = kind_name(p_sample.day_kind);
    d["cloud"] = p_sample.cloud;
    d["cloud_cover"] = p_sample.cloud_cover;
    d["coverage"] = p_sample.coverage;
    d["precip"] = p_sample.precip;
    d["precip_mm_h"] = p_sample.precip_mm_h;
    d["snow_frac"] = p_sample.snow_frac;
    d["temp_c"] = p_sample.temp_c;
    d["wind_x"] = p_sample.wind_x;
    d["wind_z"] = p_sample.wind_z;
    d["wind_ms"] = p_sample.wind_ms;
    d["wind_name"] = String::utf8(wind_name(p_sample.wind_x, p_sample.wind_z).c_str());
    d["storm"] = p_sample.storm;
    d["fog"] = p_sample.fog;
    d["fog_top"] = p_sample.fog_top;
    d["sea_fog"] = p_sample.sea_fog;
    d["wet"] = p_sample.wet;
    d["front"] = p_sample.front;
    d["sailable"] = p_sample.sailable;
    d["snow_mm"] = p_sample.snow_mm;
    d["ice"] = p_sample.ice;
    d["green"] = p_sample.green;
    d["leaf"] = p_sample.leaf;
    d["autumn"] = p_sample.autumn;
    d["spring"] = p_sample.spring;
    d["crop"] = p_sample.crop;
    return d;
}

String short_text(const WeatherSample &p_sample, bool p_with_wind) {
    String text = vformat(String(U"%s %d°C"), kind_name(p_sample.kind), int(std::lround(p_sample.temp_c)));
    if (p_with_wind) {
        if (p_sample.wind_ms < 0.5) {
            text += String(U" · 无风");
        } else {
            text += vformat(String(U" · %s %d m/s"), String::utf8(wind_name(p_sample.wind_x, p_sample.wind_z).c_str()), int(std::lround(p_sample.wind_ms)));
        }
    }
    return text;
}

} // namespace weather_view
