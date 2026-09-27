#include "planet_jobs.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void PlanetJobs::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_lanes_in_use"), &PlanetJobs::get_lanes_in_use);
    ClassDB::bind_method(D_METHOD("get_queued"), &PlanetJobs::get_queued);
    ClassDB::bind_method(D_METHOD("get_running"), &PlanetJobs::get_running);
    ClassDB::bind_method(D_METHOD("set_threads", "value"), &PlanetJobs::set_threads);
    ClassDB::bind_method(D_METHOD("get_threads"), &PlanetJobs::get_threads);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "threads", PROPERTY_HINT_RANGE, "1,16,1"), "set_threads", "get_threads");
}

void PlanetJobs::_enter_tree() {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    add_to_group(GROUP);
    share_pool();
}

PlanetJobs *PlanetJobs::find(const Node *p_from) {
    if (!p_from || !p_from->is_inside_tree()) {
        return nullptr;
    }
    return Object::cast_to<PlanetJobs>(p_from->get_tree()->get_first_node_in_group(GROUP));
}

std::shared_ptr<JobPool> PlanetJobs::share_pool() {
    if (!pool) {
        pool = std::make_shared<JobPool>();
        pool->start(threads);
    }
    return pool;
}

int PlanetJobs::acquire_lane() {
    for (int k = 1; k < JobPool::LANES; k++) {
        if (!(lanes_used & (1u << k))) {
            lanes_used |= 1u << k;
            return k;
        }
    }
    return -1;
}

void PlanetJobs::release_lane(int p_lane) {
    if (p_lane > 0 && p_lane < JobPool::LANES) {
        lanes_used &= ~(1u << p_lane);
    }
}

int PlanetJobs::get_lanes_in_use() const {
    int n = 0;
    for (int k = 1; k < JobPool::LANES; k++) {
        n += (lanes_used >> k) & 1u;
    }
    return n;
}

int PlanetJobs::get_queued() const {
    if (!pool) {
        return 0;
    }
    int n = 0;
    for (int lane = 0; lane < JobPool::LANES; lane++) {
        n += int(pool->queued_in_lane(lane));
    }
    return n;
}

int PlanetJobs::get_running() const {
    return pool ? int(pool->running_count()) : 0;
}
