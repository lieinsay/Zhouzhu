#pragma once

#include <cstdint>
#include <vector>

// 一座浮空岛的高度场。skyisle-gen island/terrain.py（5.2 岛内地形）的游戏尺度移植：
// 那边 100 m 一格、岛以 km² 计，这里 2.5 m 一格、岛以公顷计，特征尺度全按等效半径取，所以算法照搬即可。
// 本文件不含 Godot 头：输入参数，输出一份值，没有状态。
//
// 局部坐标：x 向东、z 向南、y 向上，原点在栅格中心；节点 (i, j) 在 (x = coord(j), z = coord(i))。

enum class IslandAge {
    YOUNG, // 火山锥，坡陡，放射状沟谷
    MID, // 切割后的山脊与谷地
    OLD, // 低缓台地与宽谷
};

struct IslandParams {
    uint64_t seed = 1;
    double area_m2 = 120000.0;
    double cell_m = 2.5;
    double elongation = 1.3; // 长轴 = R·√e，短轴 = R/√e
    double theta = 0.0; // 长轴方位（弧度，自 +x 转向 +z）
    double age = 0.5; // 0 新 → 1 老；< 0.3 锥，< 0.65 脊，其余台地
    double rim_m = 0.0; // 岸缘地面高度
    double peak_m = 60.0; // 峰高（最高节点恰为此值）
    double cliff_m = 22.0; // 崖高：岸缘到岛底边
    double keel_m = 110.0; // 岛底最深处在岸缘之下多少
    double dock_bearing = 0.0; // 码头在岛的哪个方位（弧度，同 theta 的转向）
    double dock_apron_m = 20.0; // 码头后面压平的停机坪半径
    double pier_min_m = 9.0; // 栈桥最短伸出多少
};

struct IslandDock {
    double x = 0.0, z = 0.0; // 岸缘点（栈桥根）
    double y = 0.0; // 停机坪与栈桥面的高度
    double out_x = 1.0, out_z = 0.0; // 指向岛外的单位向量
    double pier_m = 9.0; // 栈桥自岸缘点向外伸多长：保证泊位上的船身碰不到两侧的岸
};

struct IslandField {
    int n = 0; // n × n 个节点
    double cell_m = 1.0;
    double rim_m = 0.0;
    double peak_m = 0.0;
    IslandAge age_class = IslandAge::MID;
    std::vector<uint8_t> mask; // 1 = 陆地
    std::vector<float> height; // 顶面 y（掩膜外无意义）
    std::vector<float> inside; // 岸线 0 → 腹地 1 的「向内程度」
    std::vector<float> bottom; // 岛底 y
    IslandDock dock;

    size_t index(int p_i, int p_j) const { return size_t(p_i) * size_t(n) + size_t(p_j); }
    double coord(int p_k) const { return (p_k - (n - 1) * 0.5) * cell_m; }
    double half_extent() const { return (n - 1) * 0.5 * cell_m; }
    bool in_mask(int p_i, int p_j) const {
        return p_i >= 0 && p_j >= 0 && p_i < n && p_j < n && mask[index(p_i, p_j)] != 0;
    }
    // 四个节点都在掩膜里的格子才有地面（与网格的铺法一致）
    bool quad_solid(int p_i, int p_j) const {
        return in_mask(p_i, p_j) && in_mask(p_i + 1, p_j) && in_mask(p_i, p_j + 1) && in_mask(p_i + 1, p_j + 1);
    }
    int land_count() const;
    // 双线性取地面高度；该处没有地面则返回 false
    bool sample_height(double p_x, double p_z, double &r_y) const;
};

IslandAge island_age_class(double p_age);

IslandField generate_island_field(const IslandParams &p_params);
