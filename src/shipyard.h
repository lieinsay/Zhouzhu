#pragma once

#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>

#include "island.h"

// 一座岛上的船坞（地面的那种）：Island 的子节点，变换留在原点。岛 rebuilt 后按高度场里的船坞坪（IslandYard）搭：
// 两道坞墩梁托船体、两端各一座门架（够三层船加帆）、从岛内一侧接到舷门的跳板（带碰撞，人从这儿上下船）、跳板脚边一张画样台。
// 它只管搭和回答位置——坞位接触线（跳板顶所在的甲板边缘线：船舷该贴上的位姿，X 指向跳板）、跳板顶 / 脚、画样台在哪；
// 船在不在坞里、怎么改，都是 Voyage 的事。
// 岛上没凿出坪（has_yard 没开，或岸线退得太深）就什么都不搭，is_present() 为假。
class Shipyard : public godot::StaticBody3D {
    GDCLASS(Shipyard, godot::StaticBody3D)

    Island *island = nullptr;
    godot::Ref<godot::StandardMaterial3D> mat_timber, mat_beam, mat_paper;

    void on_island_rebuilt();
    void clear_generated();
    void build();
    godot::Vector3 yard_point(double p_u, double p_v, double p_up) const; // 岛的局部坐标

protected:
    static void _bind_methods();

public:
    static constexpr double CRADLE_LIFT = 3.6; // 坞里的船：甲板面在坪面之上多高（船体坐在坞墩上，浮石嵌进坪面的槽）
    static constexpr double CONTACT_V = 2.0; // 接触线（船舷 / 跳板顶）在坪的短轴上的位置：船往海那边伸，跳板与画样台在岛这边
    static constexpr double GANGWAY_RUN = 7.2; // 跳板的水平投影长（坡约 27°）
    static constexpr double BENCH_U = -4.0, BENCH_V = 4.0; // 画样台在跳板靠码头的一侧：从停机坪走过来不用跨跳板

    void _ready() override;

    bool is_present() const;
    // 坞位接触线：船舷该贴上的位姿（全局），X 指向跳板（岛内），Z 顺着坪的长轴
    godot::Transform3D get_cradle_contact() const;
    godot::Vector3 get_gangway_top() const; // 跳板顶（甲板边缘，全局）
    godot::Vector3 get_gangway_foot() const; // 跳板脚（坪上，全局）
    godot::Vector3 get_bench_position() const; // 画样台跟前（坪上，全局）
};
