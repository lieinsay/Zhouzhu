#include "flat_ground.h"

#include <algorithm>
#include <cmath>

namespace {

class FlatDockGround : public DockGround {
    const FlatIslandSpec &spec;

public:
    explicit FlatDockGround(const FlatIslandSpec &p_spec) :
            spec(p_spec) {}
    bool land(double p_x, double p_z) const override { return std::hypot(p_x, p_z) <= spec.radius_m; }
    bool ground(double p_x, double p_z, double &r_y) const override {
        r_y = 0.0;
        return land(p_x, p_z);
    }
};

} // namespace

FlatIsland lay_flat_island(const FlatIslandSpec &p_spec) {
    FlatIsland island;
    island.spec = p_spec;
    DockRules rules;
    rules.apron_m = p_spec.apron_m;
    const FlatDockGround ground(p_spec);
    island.dock = plan_dock(ground, 0.0, 0.0, std::cos(p_spec.dock_bearing), std::sin(p_spec.dock_bearing), p_spec.radius_m + 10.0, rules);
    island.yard = plan_yard(ground, island.dock, YardRules());
    return island;
}

bool FlatGround::land(double p_x, double p_z) const {
    return std::hypot(p_x, p_z) <= island.spec.radius_m;
}

bool FlatGround::site_height(double p_x, double p_z, double &r_y) const {
    r_y = 0.0;
    return land(p_x, p_z);
}

double FlatGround::stream_distance(double p_x, double p_z) const {
    const FlatIslandSpec &s = island.spec;
    const double dx = s.stream_x1 - s.stream_x0, dz = s.stream_z1 - s.stream_z0;
    const double len2 = dx * dx + dz * dz;
    const double t = len2 > 0.0 ? std::clamp(((p_x - s.stream_x0) * dx + (p_z - s.stream_z0) * dz) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p_x - (s.stream_x0 + t * dx), p_z - (s.stream_z0 + t * dz));
}

WaterKind FlatGround::water_at(double p_x, double p_z) const {
    return stream_distance(p_x, p_z) <= island.spec.stream_half_width_m ? WaterKind::STREAM : WaterKind::NONE;
}

void FlatGround::water_lines(double, double, double, double, std::vector<WaterLine> &r_lines) const {
    const FlatIslandSpec &s = island.spec;
    WaterLine line;
    const int steps = 32;
    for (int k = 0; k <= steps; k++) {
        const double t = double(k) / steps;
        line.push_back({ s.stream_x0 + t * (s.stream_x1 - s.stream_x0), s.stream_z0 + t * (s.stream_z1 - s.stream_z0), s.stream_half_width_m });
    }
    r_lines.push_back(std::move(line));
}

bool FlatGround::on_dock(double p_x, double p_z, double p_margin) const {
    return island.dock.ok && std::hypot(p_x - island.dock.x, p_z - island.dock.z) <= island.spec.apron_m + p_margin;
}
