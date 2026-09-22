#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "dock_site.h"
#include "world_grid.h"

// 岛群的连续地形：在任意 (x, z) 上给出有没有地、顶面多高、岛底多深、是什么地表。
// 大形全听栅格（Catmull-Rom 双三次），栅格量不到的 100 m 以下细节由确定性噪声补上：
//   陆地 ⇔ 岸距 + 岸线噪声 > 0（岸线不再是 100 m 的锯齿）
//   顶面 = 栅格高程 + 细节噪声（按地表调幅：岩多草少、田最平）
//   岛底 = 岸缘 − 崖高 − 龙骨 × √向内程度（倒锥，与 skyisle-gen 的岛体一致）
//   停机坪（set_pads）：顶面按 dock_pad_weight 拉向坪高——码头后面那一片是平的
// 同一点无论哪一级网格来取，结果都一样，所以不同精度的地块拼得上。
// 停机坪在建第一块地面之前设好、之后不再变；除此之外没有状态，取样线程安全。

struct WorldIsland {
    int id = 0;
    double rim_m = 0.0; // 岸缘高
    double cliff_m = 20.0; // 岸缘到岛底边的崖高
    double keel_m = 100.0; // 岛底最深处在岸缘之下多少
    double max_shore_m = 1000.0; // 岛上最深的岸距
};

struct TerrainSample {
    bool land = false;
    double top = 0.0; // 顶面 y
    double bottom = 0.0; // 岛底 y
    double shore = 0.0; // 含岸线噪声的岸距（米），陆地为正
    int island = 0; // 最近的岛
    int landcover = 0; // 最近格的地表类
    bool stream = false;
};

class WorldTerrain {
    const WorldGrid *grid = nullptr;
    std::vector<WorldIsland> islands; // 按 id 下标
    uint64_t seed = 0;
    std::vector<DockPlan> pads;
    std::unordered_map<int64_t, std::vector<int>> pad_buckets; // 桶 → 压着这个桶的坪

public:
    static constexpr double PAD_BUCKET_M = 256.0;

    static constexpr double COAST_AMP_M = 28.0; // 岸线噪声幅度：岸线最多进退这么多
    static constexpr double COAST_FEATURE_M = 90.0;

    WorldTerrain() = default;
    // p_grid 须比本对象活得久
    WorldTerrain(const WorldGrid &p_grid, const std::vector<WorldIsland> &p_islands, uint64_t p_seed);

    const WorldGrid &get_grid() const { return *grid; }
    bool valid() const { return grid && !grid->empty(); }
    const WorldIsland &island_info(int p_id) const;

    double coast_noise(double p_x, double p_z) const; // 约 [-1, 1]
    double detail(double p_x, double p_z) const; // 米
    TerrainSample sample(double p_x, double p_z) const;
    // 不算停机坪的原地形（码头选址就在它上面量）
    TerrainSample sample_raw(double p_x, double p_z) const;

    // 停机坪：只收 ok 的；建地面之前调
    void set_pads(const std::vector<DockPlan> &p_pads);
    const std::vector<DockPlan> &get_pads() const { return pads; }
    // 只问有没有地（比 sample 省：不算高度）
    bool is_land(double p_x, double p_z) const;
    // 只要顶面（没有地返回 false）
    bool surface(double p_x, double p_z, double &r_y) const;
};

// 无存储的值噪声：整点上的随机值由坐标哈希得出，任意远都能取样
double hash_noise(uint64_t p_seed, double p_x, double p_z); // 单层，约 [-1, 1]
double hash_fbm(uint64_t p_seed, double p_x, double p_z, double p_feature, int p_octaves, double p_persistence = 0.5);
