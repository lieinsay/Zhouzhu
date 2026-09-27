#pragma once

#include <cstdint>
#include <memory>

#include <godot_cpp/classes/node.hpp>

#include "job_pool.h"

// 共用的后台任务池（行星计划 P3，docs/PLAN-PLANET.md 第 4.4 节）：一个场景一个，所有岛群（GroupWorld）、远处的岛群（FarGroups）、
// 行星贴图（PlanetGlobe）的后台活都进这一个池，按离焦点 / 相机的米数排先后。
// 每个岛群领一条通道（acquire_lane，1–15；0 留给行星级的活），同一种活在不同的群里号撞了也分得开（job_pool.h）；
// 群拆掉之前自己 drain_lane。池本身用 shared_ptr 交出去：拆场景时谁后走谁收线程，做法里拿着的地形都在各群拆之前等完。
// 场景里没有它（甲乙两岛、平板岛、单独跑一个 World 的工具）时，GroupWorld 自己开一个池（兼容原来「World 拥有任务池」）。
// 进树（_enter_tree）时就开线程、进组，比谁的 _ready 都早。
class PlanetJobs : public godot::Node {
    GDCLASS(PlanetJobs, godot::Node)

    int threads = 4;
    std::shared_ptr<JobPool> pool;
    uint32_t lanes_used = 1; // 位图：第 0 条留给行星级的活

protected:
    static void _bind_methods();

public:
    static constexpr const char *GROUP = "planet_jobs";

    void _enter_tree() override;

    // 场景里的那一个（p_from 须在树里）；没有返回 nullptr
    static PlanetJobs *find(const godot::Node *p_from);

    std::shared_ptr<JobPool> share_pool();
    JobPool &get_pool() { return *share_pool(); }
    // 领一条通道（1–15）；领完了返回 −1
    int acquire_lane();
    void release_lane(int p_lane);
    int get_lanes_in_use() const;

    // 给脚本、测试：排着几件（各种、各通道加起来）、正在做几件
    int get_queued() const;
    int get_running() const;

    void set_threads(int p_value) { threads = p_value; }
    int get_threads() const { return threads; }
};
