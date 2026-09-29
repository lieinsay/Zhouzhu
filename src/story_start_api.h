#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string.hpp>

// 开局配置（story_start.h）给 GDScript：测试、工具按「开局那群」写，不写死群号——StoryStart.get_spawn_node() 这样调（静态方法，不用实例）。
class StoryStart : public godot::Object {
    GDCLASS(StoryStart, godot::Object)

protected:
    static void _bind_methods();

public:
    static int get_seed();
    static int get_spawn_node();
    static int get_clock_node();
    static int get_calendar_start();
    static int get_neighbor_node();
    static godot::String get_planet_dir();
    static godot::String get_groups_dir();
    static godot::String get_group_dir(int p_node); // 仓库里那一群的细版目录（有没有包不管）
};
