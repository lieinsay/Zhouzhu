#include "map_host.h"

#include <cmath>

#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

bool MapHost::map_footing(const godot::Vector3 &p_global) const {
    const double ground = map_surface(p_global);
    return !std::isnan(ground) && std::abs(p_global.y - ground) <= 1.0;
}

const std::vector<FreightPort> &MapHost::map_freight_ports() const {
    static const std::vector<FreightPort> none;
    return none;
}

bool MapHost::map_landing_height(double p_x, double p_z, double &r_y) const {
    r_y = map_surface(godot::Vector3(godot::real_t(p_x), 0, godot::real_t(p_z)));
    return !std::isnan(r_y);
}

void MapHost::map_weather_mix(const godot::Vector3 &p_global, std::vector<WeatherMix> &r_out) {
    r_out.clear();
    if (WeatherModel *wx = map_weather()) {
        r_out.push_back(WeatherMix{ wx, p_global, 1.0 });
    }
}

void MapHost::map_weather_models(std::vector<WeatherModel *> &r_out) {
    r_out.clear();
    if (WeatherModel *wx = map_weather()) {
        r_out.push_back(wx);
    }
}

int MapHost::map_find_dock(int p_id) const {
    for (int k = 0; k < map_dock_count(); k++) {
        if (map_dock_id(k) == p_id) {
            return k;
        }
    }
    return -1;
}

String MapHost::map_goal_text(int p_index, bool p_arrived, const Vector3 &p_global) const {
    const Vector3 dock = map_dock_position(p_index);
    return (p_arrived ? String(U"已到对岸，回程去 ") : String(U"目标：乘船去 ")) + map_dock_label(p_index) +
            vformat(String(U"（%.1f km）"), Vector2(dock.x - p_global.x, dock.z - p_global.z).length() / 1000.0);
}

String MapHost::map_course_name(int p_index, bool p_arrived) const {
    return (p_arrived ? String(U"回程 ") : String(U"目的 ")) + map_dock_label(p_index);
}

ResourceSpot MapHost::map_gather_near(const godot::Vector3 &p_global, double p_face_range, double /* p_facing_yaw */) {
    ResourceSpot spot = map_resource_near(p_global, p_face_range);
    if (spot.site >= 0 && godot::Vector2(p_global.x - spot.face.x, p_global.z - spot.face.z).length() > p_face_range) {
        return ResourceSpot(); // 够得着的是堆场，不是采掘面
    }
    return spot;
}
