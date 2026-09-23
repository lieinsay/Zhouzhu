#include "site_stream.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

constexpr double GUESS_M_PER_SQRT_HOUSE = 11.0; // 没铺之前估村子半径：≈ 格距 × √户数 × 0.65

} // namespace

void SiteStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_focus", "focus"), &SiteStream::set_focus);
    ClassDB::bind_method(D_METHOD("ensure_near", "point"), &SiteStream::ensure_near);
    ClassDB::bind_method(D_METHOD("get_built_docks"), &SiteStream::get_built_docks);
    ClassDB::bind_method(D_METHOD("get_built_villages"), &SiteStream::get_built_villages);
    ClassDB::bind_method(D_METHOD("get_built_hamlet_count"), &SiteStream::get_built_hamlet_count);
    ClassDB::bind_method(D_METHOD("get_village", "village_id"), &SiteStream::get_village);
    ClassDB::bind_method(D_METHOD("get_hamlet", "hamlet_id"), &SiteStream::get_hamlet);
    ClassDB::bind_method(D_METHOD("get_dock", "dock_id"), &SiteStream::get_dock);

    ClassDB::bind_method(D_METHOD("set_world_path", "value"), &SiteStream::set_world_path);
    ClassDB::bind_method(D_METHOD("get_world_path"), &SiteStream::get_world_path);
    ClassDB::bind_method(D_METHOD("set_dock_radius", "value"), &SiteStream::set_dock_radius);
    ClassDB::bind_method(D_METHOD("get_dock_radius"), &SiteStream::get_dock_radius);
    ClassDB::bind_method(D_METHOD("set_village_radius", "value"), &SiteStream::set_village_radius);
    ClassDB::bind_method(D_METHOD("get_village_radius"), &SiteStream::get_village_radius);
    ClassDB::bind_method(D_METHOD("set_folk_radius", "value"), &SiteStream::set_folk_radius);
    ClassDB::bind_method(D_METHOD("get_folk_radius"), &SiteStream::get_folk_radius);
    ClassDB::bind_method(D_METHOD("set_per_house", "value"), &SiteStream::set_per_house);
    ClassDB::bind_method(D_METHOD("get_per_house"), &SiteStream::get_per_house);
    ClassDB::bind_method(D_METHOD("set_max_folk", "value"), &SiteStream::set_max_folk);
    ClassDB::bind_method(D_METHOD("get_max_folk"), &SiteStream::get_max_folk);
    ClassDB::bind_method(D_METHOD("set_seed", "value"), &SiteStream::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &SiteStream::get_seed);

    ClassDB::bind_method(D_METHOD("set_layout_dir", "value"), &SiteStream::set_layout_dir);
    ClassDB::bind_method(D_METHOD("get_layout_dir"), &SiteStream::get_layout_dir);
    ClassDB::bind_method(D_METHOD("village_at", "point", "margin"), &SiteStream::village_at, DEFVAL(40.0));
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_path"), "set_world_path", "get_world_path");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "layout_dir", PROPERTY_HINT_DIR), "set_layout_dir", "get_layout_dir");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_radius", PROPERTY_HINT_RANGE, "200,20000,10,suffix:m"), "set_dock_radius", "get_dock_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "village_radius", PROPERTY_HINT_RANGE, "200,20000,10,suffix:m"), "set_village_radius", "get_village_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "folk_radius", PROPERTY_HINT_RANGE, "0,5000,10,suffix:m"), "set_folk_radius", "get_folk_radius");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "per_house", PROPERTY_HINT_RANGE, "0,6,1"), "set_per_house", "get_per_house");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "max_folk", PROPERTY_HINT_RANGE, "0,500,1"), "set_max_folk", "get_max_folk");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
}

void SiteStream::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    world = Object::cast_to<World>(get_node_or_null(world_path));
    ERR_FAIL_NULL_MSG(world, "SiteStream 找不到 World（world_path）");
}

void SiteStream::set_focus(const Vector3 &p_focus) {
    focus = p_focus;
    has_focus = true;
}

void SiteStream::_process(double p_delta) {
    since_check += p_delta;
    if (!world || !world->is_loaded() || !has_focus || since_check < check_interval) {
        return;
    }
    since_check = 0.0;
    refresh(1);
}

void SiteStream::ensure_near(const Vector3 &p_point) {
    focus = p_point;
    has_focus = true;
    if (world && world->is_loaded()) {
        refresh(1 << 20);
    }
}

double SiteStream::edge_distance(int p_index) const {
    const SiteSettlement &s = world->get_sites().settlements[size_t(p_index)];
    const auto built = villages.find(p_index);
    const double radius = built != villages.end() ? built->second->get_radius() : GUESS_M_PER_SQRT_HOUSE * std::sqrt(double(std::max(1, s.households)));
    return std::max(0.0, std::hypot(s.x - focus.x, s.z - focus.z) - radius);
}

WorldVillage *SiteStream::build_village(int p_index) {
    WorldVillage *village = memnew(WorldVillage);
    const SiteSettlement &site = world->get_sites().settlements[size_t(p_index)];
    const String file = vformat("%s_%d.txt", site.hamlet ? "hamlet" : "village", site.id);
    village->setup(world, site, uint64_t(seed), layout_dir.is_empty() ? String() : layout_dir.path_join(file));
    add_child(village);
    villages[p_index] = village;
    return village;
}

void SiteStream::refresh(int p_build_budget) {
    // 码头
    const std::vector<DockPlan> &plans = world->get_docks();
    for (int k = 0; k < int(plans.size()); k++) {
        if (!plans[size_t(k)].ok) {
            continue;
        }
        const double d = std::hypot(plans[size_t(k)].x - focus.x, plans[size_t(k)].z - focus.z);
        const auto found = docks.find(k);
        if (found == docks.end() && d <= dock_radius) {
            WorldDock *dock = memnew(WorldDock);
            dock->setup(plans[size_t(k)], k, world->get_dock_id(k));
            add_child(dock);
            docks[k] = dock;
        } else if (found != docks.end() && d > dock_radius * hysteresis) {
            found->second->queue_free();
            docks.erase(found);
        }
    }

    // 村与散户：先拆远的，再由近及远建
    const int count = int(world->get_sites().settlements.size());
    std::vector<std::pair<double, int>> wanted;
    for (int k = 0; k < count; k++) {
        const double d = edge_distance(k);
        const auto found = villages.find(k);
        if (found != villages.end()) {
            if (d > village_radius * hysteresis && !found->second->is_edited()) {
                found->second->queue_free();
                villages.erase(found);
            } else {
                // 村民：近了招、远了散
                if (d <= folk_radius) {
                    found->second->set_folk(true, per_house, max_folk);
                } else if (d > folk_radius * hysteresis) {
                    found->second->set_folk(false, per_house, max_folk);
                }
            }
        } else if (d <= village_radius) {
            wanted.emplace_back(d, k);
        }
    }
    std::sort(wanted.begin(), wanted.end());
    for (size_t k = 0; k < wanted.size() && int(k) < p_build_budget; k++) {
        WorldVillage *village = build_village(wanted[k].second);
        if (edge_distance(wanted[k].second) <= folk_radius) {
            village->set_folk(true, per_house, max_folk);
        }
    }
}

PackedInt32Array SiteStream::get_built_docks() const {
    PackedInt32Array ids;
    for (const auto &entry : docks) {
        ids.push_back(entry.second->get_dock_id());
    }
    return ids;
}

PackedInt32Array SiteStream::get_built_villages() const {
    PackedInt32Array ids;
    for (const auto &entry : villages) {
        if (!entry.second->is_hamlet()) {
            ids.push_back(entry.second->get_site_id());
        }
    }
    return ids;
}

int SiteStream::get_built_hamlet_count() const {
    int n = 0;
    for (const auto &entry : villages) {
        n += entry.second->is_hamlet() ? 1 : 0;
    }
    return n;
}

WorldVillage *SiteStream::get_village(int p_village_id) const {
    for (const auto &entry : villages) {
        if (!entry.second->is_hamlet() && entry.second->get_site_id() == p_village_id) {
            return entry.second;
        }
    }
    return nullptr;
}

WorldVillage *SiteStream::get_hamlet(int p_hamlet_id) const {
    for (const auto &entry : villages) {
        if (entry.second->is_hamlet() && entry.second->get_site_id() == p_hamlet_id) {
            return entry.second;
        }
    }
    return nullptr;
}

WorldVillage *SiteStream::village_at(const Vector3 &p_point, double p_margin) const {
    WorldVillage *best = nullptr;
    double best_d = 1e18;
    for (const auto &entry : villages) {
        const SiteSettlement &s = entry.second->get_site();
        const double d = std::hypot(s.x - p_point.x, s.z - p_point.z);
        if (d <= entry.second->get_radius() + p_margin && d < best_d) {
            best_d = d;
            best = entry.second;
        }
    }
    return best;
}

WorldDock *SiteStream::get_dock(int p_dock_id) const {
    for (const auto &entry : docks) {
        if (entry.second->get_dock_id() == p_dock_id) {
            return entry.second;
        }
    }
    return nullptr;
}
