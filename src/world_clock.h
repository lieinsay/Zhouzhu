#pragma once

#include <godot_cpp/classes/node.hpp>

#include "day_clock.h"

// 全局唯一的世界时钟（autoload「GameClock」，见 game/world_clock.tscn）：从第 0 天子夜起走过的秒数只有这一份。
// 在场景之上，所以换地图（F2）钟点不断；所有村子（近处逐帧、远处结算）、农田生长都读它，将来的码头班次也读它。
// 物理帧里排在最前面（process_physics_priority 很小），别的节点这一帧读到的都是已经走过的钟。
// 快进（F4，开发者）时按 fast_scale 倍走：近处远处都按钟走过的时间推进，所以一切照常，只是更快。
class WorldClock : public godot::Node {
    GDCLASS(WorldClock, godot::Node)

    static WorldClock *singleton;

    DayRules rules;
    double start_hour = 7.5;
    double seconds = 0.0;
    bool fast_forward = false;
    double fast_scale = 30.0;

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    WorldClock();
    ~WorldClock();

    // 场景里的 C++ 节点从这里拿钟；没有 autoload（不该发生）返回 nullptr
    static WorldClock *get_singleton() { return singleton; }

    void _ready() override;
    void _physics_process(double p_delta) override;

    const DayRules &get_rules() const { return rules; }
    double get_seconds() const { return seconds; }
    double get_hour() const { return read_clock(seconds, rules).hour; }
    int get_day() const { return read_clock(seconds, rules).day; }
    godot::String get_clock_text() const;
    // 拨钟：往前拨到今天（或明天，若已过）的这个钟点。只往前，不倒流
    void set_hour(double p_hour);
    // 从头开始：第 0 天的这个钟点（测试在场景建起来之前用）
    void restart(double p_hour);

    void set_day_seconds(double p_value);
    double get_day_seconds() const { return rules.day_seconds; }
    void set_start_hour(double p_value) { start_hour = p_value; }
    double get_start_hour() const { return start_hour; }
    void set_fast_forward(bool p_value) { fast_forward = p_value; }
    bool is_fast_forward() const { return fast_forward; }
    void set_fast_scale(double p_value) { fast_scale = p_value > 1.0 ? p_value : 1.0; }
    double get_fast_scale() const { return fast_scale; }
    double get_rate() const { return fast_forward ? fast_scale : 1.0; }
};
