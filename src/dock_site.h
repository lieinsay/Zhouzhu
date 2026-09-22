#pragma once

// 码头几何：游戏尺度的 IslandField 与真实比例的 WorldTerrain 共用的一套。不含 Godot 头，无状态。
//
// 给定一个起点和一个指向岛外的方向，自外向内找岸缘点（栈桥根）——它和它身后 back_count 格都得是陆地，免得落在细岬上；
// 再看泊位两侧 ±berth_half_length 的岸线最远伸出多少，定栈桥伸多长（船身平行于岸，碰不到两侧的岸）；
// 停机坪高度 = 岸缘点周围半个坪半径内格点地面的平均。压平：坪半径以内一半全平，外一半渐回原地形（dock_pad_weight）。
// 泊位约定（BERTH）：栈桥头外 berth_gap 处、比坪高 pier_lift 的一道接触线，X 指向栈桥（岛内），Z 顺着岸；
// 船把朝栈桥那一侧的舷边贴上去（Ship::place_at / moor_at），多宽的船都靠得上。

class DockGround {
public:
    virtual ~DockGround() = default;
    virtual bool land(double p_x, double p_z) const = 0;
    // 原地形的地面高（压平之前）；没有地面返回 false
    virtual bool ground(double p_x, double p_z, double &r_y) const = 0;
};

struct DockRules {
    double step_m = 1.25; // 找岸缘、量岸线的步长
    double back_m = 2.5; // 岸缘点身后逐格检查的间距
    int back_count = 4;
    double apron_m = 20.0; // 停机坪半径
    double pier_min_m = 9.0; // 栈桥最短伸出多少
    double pier_clearance_m = 3.0; // 比两侧岸线最远处再多伸这么多
    double berth_half_length_m = 30.0; // 泊位两侧要空出的岸线（半个最长的船身还多一点：7 格 × 7 m + 船头楔）
    double reach_probe_m = 60.0; // 量岸线时往外看多远
    // 平均坪高取样的格点：(lattice_x0 + j·lattice_m, lattice_z0 + i·lattice_m)
    double lattice_m = 2.5;
    double lattice_x0 = 0.0, lattice_z0 = 0.0;
};

struct DockPlan {
    bool ok = false; // 找到了岸缘点
    double x = 0.0, z = 0.0; // 岸缘点（栈桥根）
    double y = 0.0; // 停机坪与栈桥面的高度
    double out_x = 1.0, out_z = 0.0; // 指向岛外的单位向量
    double pier_m = 9.0; // 栈桥自岸缘点向外伸多长
    double apron_m = 20.0; // 停机坪半径（压平用）
};

// 起点 (p_x0, p_z0) 沿 out 方向 t ∈ (0, p_t_max] 自外向内找岸缘点；找不到时 ok = false、岸缘点取起点
DockPlan plan_dock(const DockGround &p_ground, double p_x0, double p_z0, double p_out_x, double p_out_z, double p_t_max, const DockRules &p_rules);

// 压平权重 ∈ [0, 1]：1 = 全按坪高，0 = 原地形
double dock_pad_weight(const DockPlan &p_plan, double p_x, double p_z);

// 沿伸出方向 p_along_out、比坪高 p_up 的那一点
void dock_point(const DockPlan &p_plan, double p_along_out, double p_up, double &r_x, double &r_y, double &r_z);

// 泊位接触线的原点（栈桥头外 p_gap、坪上 p_lift）；方向：X = (−out_x, 0, −out_z)，Z 顺着岸
void dock_berth(const DockPlan &p_plan, double p_gap, double p_lift, double &r_x, double &r_y, double &r_z);
