#include "map_host.h"

#include <cmath>

#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

bool MapHost::map_footing(const godot::Vector3 &p_global) const {
    const double ground = map_surface(p_global);
    return !std::isnan(ground) && std::abs(p_global.y - ground) <= 1.0;
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
