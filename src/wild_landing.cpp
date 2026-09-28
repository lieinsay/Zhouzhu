#include "wild_landing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double HULL_HALF_W = 2.6; // 船体宽 5.2 m（Ship 的 HULL_W），吊在每节甲板下面
constexpr double BOW_REACH_M = 4.5; // 船头楔伸出最前一排多远

struct Frame {
    double x, z, c, s;
    // 船的局部 (lx, lz) → 世界 xz（Godot：绕 y 转 yaw，X 轴 = (cos, −sin)，Z 轴 = (sin, cos)）
    void to_world(double p_lx, double p_lz, double &r_x, double &r_z) const {
        r_x = x + p_lx * c + p_lz * s;
        r_z = z - p_lx * s + p_lz * c;
    }
};

// 船底下要查的一块块矩形（船的局部坐标）：第 0 层每节的船体，加船头楔
template <typename F>
void for_each_hull_rect(const ShipLayout &p_layout, F p_fn) {
    const int front = p_layout.min_z();
    for (const CellPart &c : p_layout.cells()) {
        if (c.at.y != 0) {
            continue;
        }
        const double cx = cell_center_x(c.at.x), cz = cell_center_z(c.at.z);
        p_fn(cx - HULL_HALF_W, cx + HULL_HALF_W, cz - 0.5 * CELL_LENGTH_M, cz + 0.5 * CELL_LENGTH_M);
        if (c.at.z == front) {
            p_fn(cx - HULL_HALF_W, cx + HULL_HALF_W, cz - 0.5 * CELL_LENGTH_M - BOW_REACH_M, cz - 0.5 * CELL_LENGTH_M);
        }
    }
}

std::string fmt(const char *p_format, double p_value) {
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), p_format, p_value);
    return buffer;
}

// 一舷的舷梯：沿外法线每 run_step 取一次地面，找第一处落得下脚的地方
struct Gangway {
    bool ok = false;
    std::string why;
    double run = 0.0, rise = 0.0, foot_x = 0.0, foot_z = 0.0, foot_y = 0.0;
};

Gangway try_gangway(const LandingGround &p_ground, const LandingRules &p_rules, double p_deck_y, double p_door_x, double p_door_z, double p_out_x, double p_out_z) {
    Gangway g;
    const int n = int(std::floor(p_rules.max_run_m / p_rules.run_step_m + 1e-9)) + 1;
    std::vector<double> y(size_t(n), 0.0);
    std::vector<char> has(size_t(n), 0);
    for (int i = 0; i < n; i++) {
        const double t = i * p_rules.run_step_m;
        has[size_t(i)] = p_ground.height(p_door_x + p_out_x * t, p_door_z + p_out_z * t, y[size_t(i)]) ? 1 : 0;
    }
    g.why = "舷梯那边落不到地上";
    for (int i = 0; i < n; i++) {
        const double run = i * p_rules.run_step_m;
        if (run < p_rules.min_run_m - 1e-9) {
            continue;
        }
        if (!has[size_t(i)]) {
            g.why = "舷梯那边是崖外";
            continue;
        }
        const double fx = p_door_x + p_out_x * run, fz = p_door_z + p_out_z * run;
        if (p_ground.water(fx, fz)) {
            g.why = "舷梯落脚处是水";
            continue;
        }
        const double rise = p_deck_y - y[size_t(i)];
        if (rise < 0.3) {
            g.why = "舷梯那边的地比甲板还高";
            continue;
        }
        if (rise / run > p_rules.max_slope) {
            g.why = "舷梯那边地太低，梯子太陡";
            continue;
        }
        bool clear = true;
        for (int j = 0; j < i && clear; j++) {
            const double t = j * p_rules.run_step_m;
            if (t > run - p_rules.foot_free_m) {
                break;
            }
            const double plank = p_deck_y - rise * t / run;
            clear = !has[size_t(j)] || y[size_t(j)] <= plank - p_rules.plank_clear_m;
        }
        if (!clear) {
            g.why = "舷梯那边的地面顶着梯板";
            continue;
        }
        g.ok = true;
        g.why.clear();
        g.run = run;
        g.rise = rise;
        g.foot_x = fx, g.foot_z = fz, g.foot_y = y[size_t(i)];
        return g;
    }
    return g;
}

} // namespace

WildLanding plan_wild_landing(const LandingGround &p_ground, const ShipLayout &p_layout, double p_x, double p_y, double p_z, double p_yaw, const LandingRules &p_rules,
        double p_prefer_x, double p_prefer_z) {
    WildLanding out;
    if (p_layout.empty()) {
        out.why = "船是空的";
        return out;
    }
    const Frame frame{ p_x, p_z, std::cos(p_yaw), std::sin(p_yaw) };

    // 船底下：第 0 层每节的船体，加船头楔
    double hi = -1e18, lo = 1e18;
    bool void_below = false;
    auto sample_rect = [&](double p_x0, double p_x1, double p_z0, double p_z1) {
        const int nx = std::max(1, int(std::ceil((p_x1 - p_x0) / p_rules.sample_m))), nz = std::max(1, int(std::ceil((p_z1 - p_z0) / p_rules.sample_m)));
        for (int i = 0; i <= nx; i++) {
            for (int j = 0; j <= nz; j++) {
                double wx = 0.0, wz = 0.0, y = 0.0;
                frame.to_world(p_x0 + (p_x1 - p_x0) * i / nx, p_z0 + (p_z1 - p_z0) * j / nz, wx, wz);
                if (!p_ground.height(wx, wz, y)) {
                    void_below = true;
                    continue;
                }
                hi = std::max(hi, y);
                lo = std::min(lo, y);
            }
        }
    };
    for_each_hull_rect(p_layout, sample_rect);
    if (void_below) {
        out.why = "船底下有一块悬空：离崖边远一点";
        return out;
    }
    out.ground_hi = hi;
    out.ground_lo = lo;
    if (hi - lo > p_rules.max_relief_m) {
        out.why = fmt("船底下地面高低差 %.1f m，太不平", hi - lo);
        return out;
    }
    out.deck_y = hi + p_rules.deck_lift_m;
    if (p_y - out.deck_y > p_rules.max_descent_m) {
        out.why = fmt("离地太高：再降 %.0f m 才能野泊", p_y - out.deck_y - p_rules.max_descent_m);
        return out;
    }

    // 两舷各试一道舷梯
    Gangway best;
    int best_side = 0;
    double best_door_x = 0.0, best_door_z = 0.0, best_out_x = 0.0, best_out_z = 0.0;
    std::string first_why;
    const bool prefer = !std::isnan(p_prefer_x) && !std::isnan(p_prefer_z);
    double best_score = -1e18;
    for (int side = 1; side >= -1; side -= 2) {
        const double edge = side > 0 ? starboard_edge_x(p_layout) : port_edge_x(p_layout);
        double door_x = 0.0, door_z = 0.0, ox = 0.0, oz = 0.0;
        frame.to_world(edge, 0.0, door_x, door_z);
        frame.to_world(side, 0.0, ox, oz);
        ox -= p_x, oz -= p_z;
        const Gangway g = try_gangway(p_ground, p_rules, out.deck_y, door_x, door_z, ox, oz);
        if (!g.ok) {
            if (first_why.empty()) {
                first_why = g.why;
            }
            continue;
        }
        // 朝着 prefer 的那一侧优先；否则梯子短的优先（一样短时先试的右舷留下）
        const double score = prefer ? ox * (p_prefer_x - door_x) + oz * (p_prefer_z - door_z) : -g.run;
        if (best_side == 0 || score > best_score + 1e-9) {
            best = g;
            best_side = side;
            best_score = score;
            best_door_x = door_x, best_door_z = door_z, best_out_x = ox, best_out_z = oz;
        }
    }
    if (best_side == 0) {
        out.why = "两舷都放不下舷梯：" + first_why;
        return out;
    }
    out.ok = true;
    out.side = best_side;
    out.run = best.run;
    out.rise = best.rise;
    out.door_x = best_door_x, out.door_z = best_door_z;
    out.out_x = best_out_x, out.out_z = best_out_z;
    out.foot_x = best.foot_x, out.foot_z = best.foot_z, out.foot_y = best.foot_y;
    return out;
}

bool landing_corners_ok(const LandingGround &p_ground, const ShipLayout &p_layout, double p_x, double p_z, double p_yaw, const LandingRules &p_rules) {
    if (p_layout.empty()) {
        return false;
    }
    const Frame frame{ p_x, p_z, std::cos(p_yaw), std::sin(p_yaw) };
    double hi = -1e18, lo = 1e18;
    bool ok = true;
    // 每块矩形的四角都是 plan_wild_landing 取样的点：这里不过，那边一定不过
    for_each_hull_rect(p_layout, [&](double p_x0, double p_x1, double p_z0, double p_z1) {
        for (int c = 0; c < 4 && ok; c++) {
            double wx = 0.0, wz = 0.0, y = 0.0;
            frame.to_world(c & 1 ? p_x1 : p_x0, c & 2 ? p_z1 : p_z0, wx, wz);
            if (!p_ground.height(wx, wz, y)) {
                ok = false;
                break;
            }
            hi = std::max(hi, y);
            lo = std::min(lo, y);
            ok = hi - lo <= p_rules.max_relief_m;
        }
    });
    return ok;
}
