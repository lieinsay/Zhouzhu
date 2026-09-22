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

    // 河（见 island_river.h）。默认没有：不是每座岛都养得起一条河
    bool river = false;
    double river_half_width_m = 3.2; // 水面半宽
    double river_depth_m = 0.6; // 河心水深
    double river_freeboard_m = 0.35; // 河滩比水面高多少
    double river_flat_m = 9.0; // 每侧压平成河滩的宽度（能种地的就是这一条）
    bool trail = false; // 从停机坪修一条路到河滩（要有河）
    double trail_half_width_m = 2.5;

    // 船坞坪（见 IslandYard）。默认没有
    bool yard = false;
    double yard_length_m = 44.0; // 沿岸：够 7 格长的船加船头楔
    double yard_width_m = 20.0; // 向岛内：够 3 格宽的船加跳板与画样台
    double yard_max_inland_m = 25.0; // 岸线退得比这还深就不建
};

struct IslandDock {
    double x = 0.0, z = 0.0; // 岸缘点（栈桥根）
    double y = 0.0; // 停机坪与栈桥面的高度
    double out_x = 1.0, out_z = 0.0; // 指向岛外的单位向量
    double pier_m = 9.0; // 栈桥自岸缘点向外伸多长：保证泊位上的船身碰不到两侧的岸
};

// 船坞坪：停机坪旁沿岸的一块矩形平地，与停机坪同高，船坞（坞架、跳板、画样台）搭在上面。
// 局部坐标 (u, v)：u 沿长轴（顺着岸，自靠停机坪的一端量起，中心为 0），v 沿短轴指向岛内（中心为 0）。
struct IslandYard {
    bool present = false;
    double x = 0.0, z = 0.0; // 中心
    double ux = 1.0, uz = 0.0; // 长轴单位向量（沿岸）
    double vx = 0.0, vz = 1.0; // 短轴单位向量（指向岛内）
    double y = 0.0; // 坪面高
    double length = 0.0, width = 0.0;

    void to_local(double p_x, double p_z, double &r_u, double &r_v) const {
        r_u = (p_x - x) * ux + (p_z - z) * uz;
        r_v = (p_x - x) * vx + (p_z - z) * vz;
    }
    void from_local(double p_u, double p_v, double &r_x, double &r_z) const {
        r_x = x + p_u * ux + p_v * vx;
        r_z = z + p_u * uz + p_v * vz;
    }
    // 点到矩形外缘的水平距离；在矩形内为 0
    double outside_distance(double p_x, double p_z) const;
    bool contains(double p_x, double p_z, double p_margin = 0.0) const { return present && outside_distance(p_x, p_z) <= p_margin; }
};

// 河中线上的一点，自源头排到河口（岸缘）；水面沿程只降不升
struct RiverPoint {
    double x = 0.0, z = 0.0;
    double water = 0.0; // 水面 y
    double half_width = 0.0;
};

struct TrailPoint {
    double x = 0.0, z = 0.0;
    double y = 0.0; // 路面高
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
    IslandYard yard; // 没有船坞则 present = false
    std::vector<RiverPoint> river; // 没有河则为空
    std::vector<float> river_dist; // 到河中线的水平距离（m）；没有河则为空
    std::vector<float> river_depth; // 该节点上方的水深，不在水下为 0；没有河则为空
    std::vector<TrailPoint> trail; // 停机坪 → 河滩；没有路则为空
    std::vector<float> trail_dist; // 到路中线的水平距离（m）；没有路则为空

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
    bool has_river() const { return !river.empty(); }
    bool has_trail() const { return !trail.empty(); }
};

IslandAge island_age_class(double p_age);

IslandField generate_island_field(const IslandParams &p_params);
