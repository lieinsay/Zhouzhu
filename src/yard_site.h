#pragma once

#include "dock_site.h"

// 船坞坪：码头旁沿岸的一块矩形平地，与停机坪同高，船坞（坞架、跳板、画样台）搭在上面。
// 游戏尺度的岛（IslandField）与真实比例的岛群（WorldTerrain）共用这一套。不含 Godot 头，无状态。
//
// 选址：岸线不直，先在码头两侧各试一遍，从岸缘往岛内一米一米加进深，找矩形（四周再留 land_margin）整个落在「能凿坪的地」上的
// 最小进深，取更浅的那一侧；进深超过 max_inland 就不建。能凿坪的地由调用方定（岛：陆地；岛群：陆地且不压溪涧、水面——河路都绕开它）。
// 凿坪：矩形内全平（坪高），矩形外按固定坡度挖方 / 填方接回原地形（yard_carve）。
//
// 局部坐标 (u, v)：u 沿长轴（顺着岸，中心为 0），v 沿短轴指向岛内（中心为 0）。

struct YardPlan {
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

struct YardRules {
    double length_m = 60.0; // 沿岸：够 7 格（7 m）长的船加船头楔
    double width_m = 28.0; // 向岛内：跳板与画样台在岛这边，3 格宽的船往海那边伸（可以悬出坪外）
    double max_inland_m = 25.0; // 岸线退得比这还深就不建
    double gap_m = 6.0; // 坪与码头中线（栈桥、出生点、路头所在）之间留的空
    double land_margin_m = 2.0; // 坪四周还得有这么宽的地
    double cut_slope = 1.0; // 坪外挖方 45°
    double fill_slope = 0.8;
};

// 码头 p_dock（岸缘点、外法线、坪高）旁的船坞坪；找不到 present = false。p_ground 只用 land()
YardPlan plan_yard(const DockGround &p_ground, const DockPlan &p_dock, const YardRules &p_rules);

// 凿坪后该点的地面高：p_height 是凿之前的高
double yard_carve(const YardPlan &p_yard, const YardRules &p_rules, double p_x, double p_z, double p_height);
