#include "yard_site.h"

#include <algorithm>
#include <cmath>

double YardPlan::outside_distance(double p_x, double p_z) const {
    double u = 0.0, v = 0.0;
    to_local(p_x, p_z, u, v);
    const double du = std::max(0.0, std::abs(u) - 0.5 * length);
    const double dv = std::max(0.0, std::abs(v) - 0.5 * width);
    return std::hypot(du, dv);
}

YardPlan plan_yard(const DockGround &p_ground, const DockPlan &p_dock, const YardRules &p_rules) {
    YardPlan yard;
    const double lat_x = -p_dock.out_z, lat_z = p_dock.out_x;
    const double L = p_rules.length_m, W = p_rules.width_m;
    const double gap = p_rules.gap_m, margin = p_rules.land_margin_m;

    int best_side = 0;
    double best_inland = 1e18;
    for (int side = 1; side >= -1; side -= 2) {
        for (double inland = 1.0; inland <= p_rules.max_inland_m; inland += 1.0) {
            bool solid = true;
            for (double u = gap - margin; u <= gap + L + margin && solid; u += 1.0) {
                for (double v = inland - margin; v <= inland + W + margin && solid; v += 1.0) {
                    solid = p_ground.land(p_dock.x + side * u * lat_x - v * p_dock.out_x, p_dock.z + side * u * lat_z - v * p_dock.out_z);
                }
            }
            if (solid) {
                if (inland < best_inland) {
                    best_inland = inland;
                    best_side = side;
                }
                break;
            }
        }
    }
    if (best_side == 0) {
        return yard;
    }
    yard.present = true;
    yard.ux = best_side * lat_x;
    yard.uz = best_side * lat_z;
    yard.vx = -p_dock.out_x;
    yard.vz = -p_dock.out_z;
    yard.length = L;
    yard.width = W;
    yard.y = p_dock.y;
    yard.x = p_dock.x + (gap + 0.5 * L) * yard.ux + (best_inland + 0.5 * W) * yard.vx;
    yard.z = p_dock.z + (gap + 0.5 * L) * yard.uz + (best_inland + 0.5 * W) * yard.vz;
    return yard;
}

double yard_carve(const YardPlan &p_yard, const YardRules &p_rules, double p_x, double p_z, double p_height) {
    if (!p_yard.present) {
        return p_height;
    }
    const double beyond = p_yard.outside_distance(p_x, p_z);
    return beyond <= 0.0 ? p_yard.y : std::clamp(p_height, p_yard.y - p_rules.fill_slope * beyond, p_yard.y + p_rules.cut_slope * beyond);
}
