#include "cloud_band.h"

#include <algorithm>
#include <cmath>

namespace {

double smooth(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

CloudBandState cloud_band_at(double p_y, double p_sun_sin, const CloudBandRules &p_rules) {
    const CloudBandRules &r = p_rules;
    CloudBandState s;
    const double y = std::isfinite(p_y) ? p_y : 0.0;
    // 雾：顶上 40 m 渐浓到顶下 50 m，云底上 50 m 渐淡到底下 70 m
    const double from_top = smooth(r.top_y + r.top_soft_above_m, r.top_y - r.top_soft_below_m, y);
    const double above_base = smooth(r.base_y - r.base_soft_below_m, r.base_y + r.base_soft_above_m, y);
    s.inside = from_top * above_base;
    s.under = 1.0 - above_base;
    s.below = smooth(r.top_y - r.sky_switch_from_m, r.top_y - r.sky_switch_to_m, y);
    s.depth = std::clamp((r.top_y - y) / (r.top_y - r.base_y), 0.0, 1.0);
    // 直射：按走进的份与太阳高度（太阳越低走的路越长）；夜里太阳在地平线下，本来就没有
    const double sun_sin = std::max(r.min_sun_sin, p_sun_sin);
    s.sun = std::exp(-r.optical_depth * s.depth / sun_sin);
    // 漫射光：顶上 1，渐暗到云底下的 under_diffuse；太阳低时暗得快一点
    const double low = std::clamp(1.0 - p_sun_sin, 0.0, 1.0);
    const double d = 1.0 - std::pow(1.0 - s.depth, 1.0 + r.low_sun_darken * low);
    const double k = r.diffuse_k;
    const double fall = (std::exp(-k * d) - std::exp(-k)) / (1.0 - std::exp(-k));
    s.diffuse = r.under_diffuse + (1.0 - r.under_diffuse) * fall;
    s.turbulence = s.inside * r.turbulence_in + s.under * r.turbulence_under;
    s.sea_height = y - r.sea_y;
    return s;
}
