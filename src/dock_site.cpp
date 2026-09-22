#include "dock_site.h"

#include <algorithm>
#include <cmath>

namespace {

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

double smoothstep(double t) {
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

DockPlan plan_dock(const DockGround &p_ground, double p_x0, double p_z0, double p_out_x, double p_out_z, double p_t_max, const DockRules &p_rules) {
    DockPlan plan;
    plan.out_x = p_out_x;
    plan.out_z = p_out_z;
    plan.apron_m = p_rules.apron_m;
    const double step = p_rules.step_m;

    // 岸缘点：它和它身后 back_count 格都得是陆地，免得落在一根细岬上
    double t_rim = 0.0;
    for (double t = p_t_max; t > 0.0; t -= step) {
        bool solid = true;
        for (int back = 0; back <= p_rules.back_count && solid; back++) {
            const double tb = t - back * p_rules.back_m;
            solid = p_ground.land(p_x0 + tb * p_out_x, p_z0 + tb * p_out_z);
        }
        if (solid) {
            t_rim = t;
            plan.ok = true;
            break;
        }
    }
    plan.x = p_x0 + t_rim * p_out_x;
    plan.z = p_z0 + t_rim * p_out_z;

    // 泊位上的船平行于岸：看这一段岸线最远伸出多少
    const double lat_x = -p_out_z, lat_z = p_out_x;
    double reach = 0.0;
    for (double s = -p_rules.berth_half_length_m; s <= p_rules.berth_half_length_m; s += step) {
        for (double t = 0.0; t <= p_rules.reach_probe_m; t += step) {
            if (p_ground.land(plan.x + t * p_out_x + s * lat_x, plan.z + t * p_out_z + s * lat_z)) {
                reach = std::max(reach, t);
            }
        }
    }
    plan.pier_m = std::max(p_rules.pier_min_m, reach + p_rules.pier_clearance_m);

    // 停机坪高度 = 岸缘点周围半个半径内格点地面的平均（行、列都自小到大，与逐格扫一遍的次序相同）
    const double radius = 0.5 * p_rules.apron_m;
    const double cell = p_rules.lattice_m;
    const int i0 = int(std::floor((plan.z - radius - p_rules.lattice_z0) / cell)) - 1;
    const int i1 = int(std::ceil((plan.z + radius - p_rules.lattice_z0) / cell)) + 1;
    const int j0 = int(std::floor((plan.x - radius - p_rules.lattice_x0) / cell)) - 1;
    const int j1 = int(std::ceil((plan.x + radius - p_rules.lattice_x0) / cell)) + 1;
    double sum = 0.0;
    int count = 0;
    for (int i = i0; i <= i1; i++) {
        for (int j = j0; j <= j1; j++) {
            const double x = p_rules.lattice_x0 + j * cell, z = p_rules.lattice_z0 + i * cell;
            double y = 0.0;
            if (std::hypot(x - plan.x, z - plan.z) <= radius && p_ground.land(x, z) && p_ground.ground(x, z, y)) {
                sum += y;
                count++;
            }
        }
    }
    double rim_y = 0.0;
    plan.y = count > 0 ? sum / count : (p_ground.ground(plan.x, plan.z, rim_y) ? rim_y : 0.0);
    return plan;
}

double dock_pad_weight(const DockPlan &p_plan, double p_x, double p_z) {
    const double d = std::hypot(p_x - p_plan.x, p_z - p_plan.z) / p_plan.apron_m;
    // 半径以内一半全平，外一半渐回原地形
    return smoothstep(clamp01(2.0 * (1.0 - d)));
}

void dock_point(const DockPlan &p_plan, double p_along_out, double p_up, double &r_x, double &r_y, double &r_z) {
    r_x = p_plan.x + p_along_out * p_plan.out_x;
    r_y = p_plan.y + p_up;
    r_z = p_plan.z + p_along_out * p_plan.out_z;
}

void dock_berth(const DockPlan &p_plan, double p_gap, double p_lift, double &r_x, double &r_y, double &r_z) {
    dock_point(p_plan, p_plan.pier_m + p_gap, p_lift, r_x, r_y, r_z);
}
