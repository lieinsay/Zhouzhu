#pragma once

#include "dock_site.h"
#include "map_ground.h"
#include "yard_site.h"

// 平板岛：一座圆形、顶面全平的岛，一条笔直的溪横穿过去，一个码头、码头旁一块船坞坪。不含 Godot 头。
// 它是「新地图最少要实现什么」的样板（见 README）：只回答地面的几个问题、给出码头与船坞坪，所有玩法就都跑得起来。
// 坐标是岛的局部坐标（原点在岛心，顶面 y = 0）。码头与船坞坪用的是与别的地图同一套 dock_site / yard_site。

struct FlatIslandSpec {
    double radius_m = 160.0;
    double dock_bearing = 0.0; // 码头在岛的哪个方位（弧度，自 +x 转向 +z）
    // 溪：从 (x0, z0) 到 (x1, z1) 的一段直线，水面半宽
    double stream_x0 = -60.0, stream_z0 = -160.0, stream_x1 = -60.0, stream_z1 = 160.0;
    double stream_half_width_m = 2.0;
    double apron_m = 20.0; // 停机坪半径
};

struct FlatIsland {
    FlatIslandSpec spec;
    DockPlan dock;
    YardPlan yard;
};

// 按规格算出码头（dock_site）与船坞坪（yard_site）
FlatIsland lay_flat_island(const FlatIslandSpec &p_spec);

class FlatGround : public MapGround {
    const FlatIsland &island;

    double stream_distance(double p_x, double p_z) const;

public:
    explicit FlatGround(const FlatIsland &p_island) :
            island(p_island) {}
    bool land(double p_x, double p_z) const;
    bool site_height(double p_x, double p_z, double &r_y) const override;
    bool walk_height(double p_x, double p_z, double &r_y) const override { return site_height(p_x, p_z, r_y); }
    WaterKind water_at(double p_x, double p_z) const override;
    double water_distance(double p_x, double p_z) const override { return stream_distance(p_x, p_z); }
    void water_lines(double p_x0, double p_z0, double p_x1, double p_z1, std::vector<WaterLine> &r_lines) const override;
    bool on_dock(double p_x, double p_z, double p_margin) const override;
    bool on_yard(double p_x, double p_z, double p_margin) const override { return island.yard.contains(p_x, p_z, p_margin); }
};
