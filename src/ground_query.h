#pragma once

// 地面查询接口：纯虚，不含 Godot 头。
// 盖房子（settlement_model 的勘察）与村民走路（villager_model 的导航格）只问「这里地面多高、压着什么」，
// 不在乎背后是一座游戏尺度的 IslandField 还是真实比例岛群的 WorldTerrain——各自写一个适配器即可。
// 坐标由适配器自己定（岛的局部坐标 / 村子的局部坐标），接口只管 (x, z) → 值。

// 一个点压着的禁区：rank 越小越要紧（勘察一次只说最要紧的一条），−1 = 没压着
struct SiteBlock {
    int rank = -1;
    const char *reason = nullptr; // UTF-8 静态串
};

class SiteGround {
public:
    virtual ~SiteGround() = default;
    // 勘察用的地面高；没有地面返回 false
    virtual bool site_height(double p_x, double p_z, double &r_y) const = 0;
    // 该点压着的禁区（河、停机坪、路、溪涧……）
    virtual SiteBlock site_block(double p_x, double p_z) const { return SiteBlock(); }
};

class NavGround {
public:
    virtual ~NavGround() = default;
    // 人脚下的地面高（应与碰撞网格一致）；没有地面返回 false
    virtual bool walk_height(double p_x, double p_z, double &r_y) const = 0;
    // 该点上方的水深；不在水下为 0
    virtual double water_depth(double p_x, double p_z) const { return 0.0; }
};
