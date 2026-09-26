#include "site_stream.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "hinterland.h"

using namespace godot;

void SiteStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_focus", "focus"), &SiteStream::set_focus);
    ClassDB::bind_method(D_METHOD("ensure_near", "point"), &SiteStream::ensure_near);
    ClassDB::bind_method(D_METHOD("get_built_docks"), &SiteStream::get_built_docks);
    ClassDB::bind_method(D_METHOD("get_built_villages"), &SiteStream::get_built_villages);
    ClassDB::bind_method(D_METHOD("get_built_hamlet_count"), &SiteStream::get_built_hamlet_count);
    ClassDB::bind_method(D_METHOD("get_village", "village_id"), &SiteStream::get_village);
    ClassDB::bind_method(D_METHOD("get_hamlet", "hamlet_id"), &SiteStream::get_hamlet);
    ClassDB::bind_method(D_METHOD("get_dock", "dock_id"), &SiteStream::get_dock);
    ClassDB::bind_method(D_METHOD("get_built_resources"), &SiteStream::get_built_resources);
    ClassDB::bind_method(D_METHOD("get_resource", "working_id"), &SiteStream::get_resource);
    ClassDB::bind_method(D_METHOD("get_built_landmarks"), &SiteStream::get_built_landmarks);
    ClassDB::bind_method(D_METHOD("get_village_version"), &SiteStream::get_village_version);
    ClassDB::bind_method(D_METHOD("set_resource_radius", "value"), &SiteStream::set_resource_radius);
    ClassDB::bind_method(D_METHOD("get_resource_radius"), &SiteStream::get_resource_radius);
    ClassDB::bind_method(D_METHOD("get_focus"), &SiteStream::get_focus);
    ClassDB::bind_method(D_METHOD("get_hinterland"), &SiteStream::get_hinterland);
    ClassDB::bind_method(D_METHOD("set_hinterland_path", "value"), &SiteStream::set_hinterland_path);
    ClassDB::bind_method(D_METHOD("get_hinterland_path"), &SiteStream::get_hinterland_path);

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
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "hinterland_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Hinterland"), "set_hinterland_path", "get_hinterland_path");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "layout_dir", PROPERTY_HINT_DIR), "set_layout_dir", "get_layout_dir");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_radius", PROPERTY_HINT_RANGE, "200,20000,10,suffix:m"), "set_dock_radius", "get_dock_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "village_radius", PROPERTY_HINT_RANGE, "200,20000,10,suffix:m"), "set_village_radius", "get_village_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "folk_radius", PROPERTY_HINT_RANGE, "0,5000,10,suffix:m"), "set_folk_radius", "get_folk_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "resource_radius", PROPERTY_HINT_RANGE, "100,10000,10,suffix:m"), "set_resource_radius", "get_resource_radius");
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
    hinterland = Object::cast_to<Hinterland>(get_node_or_null(hinterland_path));
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
    const double radius = built != villages.end() ? built->second->get_radius() : settlement_guess_radius(s);
    return std::max(0.0, std::hypot(s.x - focus.x, s.z - focus.z) - radius);
}

WorldVillage *SiteStream::build_village(int p_index) {
    WorldVillage *village = memnew(WorldVillage);
    const SiteSettlement &site = world->get_sites().settlements[size_t(p_index)];
    const String file = vformat("%s_%d.txt", site.hamlet ? "hamlet" : "village", site.id);
    village->setup(world, site, uint64_t(seed), layout_dir.is_empty() ? String() : layout_dir.path_join(file), hinterland, p_index);
    add_child(village);
    villages[p_index] = village;
    village_version++;
    return village;
}

void SiteStream::refresh(int p_build_budget) {
    // 码头
    {
    StreamTimer dock_timer(world, StreamChannel::DOCKS);
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
    }

    // 村与散户：先拆远的，再由近及远建
    const int count = int(world->get_sites().settlements.size());
    std::vector<std::pair<double, int>> wanted;
    for (int k = 0; k < count; k++) {
        const double d = edge_distance(k);
        const auto found = villages.find(k);
        if (found != villages.end()) {
            if (d > village_radius * hysteresis && !found->second->is_edited()) {
                StreamTimer timer(world, StreamChannel::VILLAGES);
                found->second->queue_free();
                villages.erase(found);
                village_version++;
            } else {
                // 村民：近了招、远了散
                StreamTimer timer(world, StreamChannel::FOLK);
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
        WorldVillage *village = nullptr;
        {
            StreamTimer timer(world, StreamChannel::VILLAGES);
            village = build_village(wanted[k].second);
        }
        if (edge_distance(wanted[k].second) <= folk_radius) {
            StreamTimer timer(world, StreamChannel::FOLK);
            village->set_folk(true, per_house, max_folk);
        }
    }

    // 采场与地标：先拆远的，再由近及远摆（两样合用一份预算）
    StreamTimer site_timer(world, StreamChannel::SITES);
    const ResourcePlan &plan = world->get_resource_plan();
    std::vector<std::pair<double, int>> wanted_resources; // 下标 ≥ 0 是采场，< 0 是地标（−1 − 下标）
    for (int k = 0; k < int(plan.sites.size()); k++) {
        const double d = std::hypot(plan.sites[size_t(k)].face_x - focus.x, plan.sites[size_t(k)].face_z - focus.z);
        const auto found = resources.find(k);
        if (found != resources.end()) {
            if (d > resource_radius * hysteresis) {
                found->second->queue_free();
                resources.erase(found);
            }
        } else if (d <= resource_radius) {
            wanted_resources.emplace_back(d, k);
        }
    }
    for (int k = 0; k < int(plan.landmarks.size()); k++) {
        const double d = std::hypot(plan.landmarks[size_t(k)].x - focus.x, plan.landmarks[size_t(k)].z - focus.z);
        const auto found = landmarks.find(k);
        if (found != landmarks.end()) {
            if (d > resource_radius * hysteresis) {
                found->second->queue_free();
                landmarks.erase(found);
            }
        } else if (d <= resource_radius) {
            wanted_resources.emplace_back(d, -1 - k);
        }
    }
    std::sort(wanted_resources.begin(), wanted_resources.end());
    const int budget = p_build_budget > 1 ? p_build_budget : resource_budget;
    for (size_t k = 0; k < wanted_resources.size() && int(k) < budget; k++) {
        const int index = wanted_resources[k].second;
        WorldResource *node = memnew(WorldResource);
        if (index >= 0) {
            node->setup(plan.sites[size_t(index)], index);
            resources[index] = node;
        } else {
            node->setup_landmark(plan.landmarks[size_t(-1 - index)], -1 - index);
            landmarks[-1 - index] = node;
        }
        add_child(node);
    }
}

PackedInt32Array SiteStream::get_built_landmarks() const {
    PackedInt32Array ids;
    for (const auto &entry : landmarks) {
        ids.push_back(entry.second->get_deposit_id());
    }
    return ids;
}

std::vector<WorldVillage *> SiteStream::built_villages() const {
    std::vector<WorldVillage *> out;
    out.reserve(villages.size());
    for (const auto &entry : villages) {
        out.push_back(entry.second);
    }
    return out;
}

PackedInt32Array SiteStream::get_built_resources() const {
    PackedInt32Array ids;
    for (const auto &entry : resources) {
        ids.push_back(entry.second->get_deposit_id());
    }
    return ids;
}

WorldResource *SiteStream::get_resource(int p_working_id) const {
    for (const auto &entry : resources) {
        if (entry.second->get_deposit_id() == p_working_id) {
            return entry.second;
        }
    }
    return nullptr;
}

WorldResource *SiteStream::resource_near(const Vector3 &p_point, double p_range) const {
    WorldResource *best = nullptr;
    double best_d = p_range;
    for (const auto &entry : resources) {
        const ResourceSite &s = entry.second->get_site();
        const double d = std::min(std::hypot(s.face_x - p_point.x, s.face_z - p_point.z), std::hypot(s.pile_x - p_point.x, s.pile_z - p_point.z));
        if (d <= best_d) {
            best_d = d;
            best = entry.second;
        }
    }
    return best;
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
