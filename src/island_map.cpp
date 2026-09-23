#include "island_map.h"

#include <cmath>
#include <limits>

#include <godot_cpp/core/class_db.hpp>

#include "farm.h"
#include "shipyard.h"

using namespace godot;

void IslandMap::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_islands", "value"), &IslandMap::set_islands);
    ClassDB::bind_method(D_METHOD("get_islands"), &IslandMap::get_islands);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "islands", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:Island", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES)), "set_islands", "get_islands");
}

void IslandMap::_ready() {
    resolved.clear();
    for (int64_t k = 0; k < islands.size(); k++) {
        Island *found = Object::cast_to<Island>(get_node_or_null(islands[k]));
        ERR_CONTINUE_MSG(!found, vformat("IslandMap: islands[%d] 不是 Island", k));
        resolved.push_back(found);
    }
}

bool IslandMap::map_ready() const {
    for (const Island *isl : resolved) {
        if (!isl->host_ready()) {
            return false;
        }
    }
    return !resolved.empty();
}

double IslandMap::map_surface(const Vector3 &p_global) const {
    for (const Island *isl : resolved) {
        const double y = isl->get_surface_height(p_global);
        if (!std::isnan(y)) {
            return y;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

int IslandMap::map_island_at(const Vector3 &p_global) const {
    for (int k = 0; k < int(resolved.size()); k++) {
        if (!std::isnan(resolved[size_t(k)]->get_surface_height(p_global))) {
            return k;
        }
    }
    return -1;
}

Vector3 IslandMap::map_dock_position(int p_index) const {
    const IslandDock &dock = island(p_index)->get_field().dock;
    return island(p_index)->get_global_transform().xform(Vector3(real_t(dock.x), real_t(dock.y), real_t(dock.z)));
}

String IslandMap::map_goal_text(int, bool p_arrived, const Vector3 &) const {
    return p_arrived ? String(U"已踏上对岸的岛") : String(U"目标：登上空艇，飞到对岸的岛上去");
}

String IslandMap::map_course_name(int, bool p_arrived) const {
    return p_arrived ? String(U"回程泊位") : String(U"对岸泊位");
}

int IslandMap::nearest_island(const Vector3 &p_global) const {
    int best = -1;
    double best_d = std::numeric_limits<double>::infinity();
    for (int k = 0; k < int(resolved.size()); k++) {
        const double d = p_global.distance_to(resolved[size_t(k)]->get_global_position());
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    return best;
}

std::vector<Shipyard *> IslandMap::map_shipyards() const {
    std::vector<Shipyard *> out;
    for (Island *isl : resolved) {
        const TypedArray<Node> children = isl->get_children();
        for (int64_t k = 0; k < children.size(); k++) {
            if (Shipyard *yard = Object::cast_to<Shipyard>(children[k])) {
                out.push_back(yard);
            }
        }
    }
    return out;
}

Farm *IslandMap::map_farm_at(const Vector3 &p_global) const {
    const int k = nearest_island(p_global);
    return k >= 0 ? island(k)->host_farm() : nullptr;
}

GroundHost *IslandMap::map_settlement_at(const Vector3 &p_global) const {
    const int k = nearest_island(p_global);
    return k >= 0 ? island(k) : nullptr;
}
