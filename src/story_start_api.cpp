#include "story_start_api.h"

#include <godot_cpp/core/class_db.hpp>

#include "story_start.h"

using namespace godot;

void StoryStart::_bind_methods() {
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_seed"), &StoryStart::get_seed);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_spawn_node"), &StoryStart::get_spawn_node);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_clock_node"), &StoryStart::get_clock_node);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_calendar_start"), &StoryStart::get_calendar_start);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_neighbor_node"), &StoryStart::get_neighbor_node);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_planet_dir"), &StoryStart::get_planet_dir);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_groups_dir"), &StoryStart::get_groups_dir);
    ClassDB::bind_static_method(get_class_static(), D_METHOD("get_group_dir", "node"), &StoryStart::get_group_dir);
}

int StoryStart::get_seed() {
    return story_start::SEED;
}

int StoryStart::get_spawn_node() {
    return story_start::SPAWN_NODE;
}

int StoryStart::get_clock_node() {
    return story_start::CLOCK_NODE;
}

int StoryStart::get_calendar_start() {
    return story_start::CALENDAR_START;
}

int StoryStart::get_neighbor_node() {
    return story_start::NEIGHBOR_NODE;
}

String StoryStart::get_planet_dir() {
    return String(story_start::PLANET_DIR);
}

String StoryStart::get_groups_dir() {
    return String(story_start::GROUPS_DIR);
}

String StoryStart::get_group_dir(int p_node) {
    return String(story_start::GROUPS_DIR).path_join(itos(p_node));
}
