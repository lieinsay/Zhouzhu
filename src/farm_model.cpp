#include "farm_model.h"

#include <algorithm>
#include <cmath>

PlotSurvey survey_plot(const MapGround &p_ground, const PlotLattice &p_lattice, int p_i, int p_j, const FarmRules &p_rules) {
    PlotSurvey s;
    if (!p_lattice.contains(p_i, p_j)) {
        return s;
    }
    const int corners[4][2] = { { p_i, p_j }, { p_i, p_j + 1 }, { p_i + 1, p_j }, { p_i + 1, p_j + 1 } };
    double lo = 1e18, hi = -1e18, dist = 0.0;
    bool wet = false, trail = false;
    for (const auto &c : corners) {
        PlotNode node;
        if (!p_ground.plot_node(p_lattice, c[0], c[1], node) || !node.ground) {
            return s;
        }
        lo = std::min(lo, node.y);
        hi = std::max(hi, node.y);
        dist += 0.25 * node.water_m;
        wet = wet || node.wet;
        trail = trail || p_ground.on_trail(p_lattice.node_x(c[1]), p_lattice.node_z(c[0]), p_rules.trail_margin_m);
    }
    const double cx = p_lattice.center_x(p_j), cz = p_lattice.center_z(p_i);
    const double diagonal = p_lattice.cell_m * std::sqrt(2.0);
    const bool near_water = dist <= p_rules.max_river_m;
    const bool fertile = p_ground.fertile(cx, cz);
    s.land = Arability::ARABLE;
    if (wet) {
        s.land = Arability::WATER;
    } else if (p_ground.on_dock(cx, cz, p_rules.dock_margin_m)) {
        s.land = Arability::DOCK;
    } else if (p_ground.on_yard(cx, cz, p_rules.dock_margin_m)) {
        s.land = Arability::YARD;
    } else if (trail) {
        s.land = Arability::TRAIL;
    } else if (!near_water && !fertile) {
        s.land = Arability::TOO_DRY;
    } else if ((hi - lo) / diagonal > p_rules.max_slope) {
        s.land = Arability::TOO_STEEP;
    }
    const double from_water = near_water ? std::clamp(1.0 - dist / p_rules.max_river_m, 0.0, 1.0) : 0.0;
    s.moisture = float(std::max(from_water, fertile ? p_rules.fertile_moisture : 0.0));
    return s;
}

void FarmModel::set_rules(const FarmRules &p_rules) {
    rules = p_rules;
    surveyed.clear();
}

PlotSurvey FarmModel::survey(int p_i, int p_j) const {
    if (!ground) {
        return PlotSurvey();
    }
    const uint64_t k = key(p_i, p_j);
    const auto found = surveyed.find(k);
    if (found != surveyed.end()) {
        return found->second;
    }
    const PlotSurvey s = survey_plot(*ground, lattice, p_i, p_j, rules);
    surveyed.emplace(k, s);
    return s;
}

bool FarmModel::any_arable_near(double p_x, double p_z, double p_radius, int p_step) const {
    int ci = 0, cj = 0;
    lattice.cell_of(p_x, p_z, ci, cj);
    const int reach = int(std::ceil(p_radius / lattice.cell_m));
    const int step = std::max(1, p_step);
    // 一圈圈往外：第 r 圈是 max(|di|, |dj|) = r 的那些格
    for (int r = 0; r <= reach; r += step) {
        for (int di = -r; di <= r; di += step) {
            for (int dj = -r; dj <= r; dj += step) {
                if (std::max(std::abs(di), std::abs(dj)) != r) {
                    continue;
                }
                const int i = ci + di, j = cj + dj;
                if (std::hypot(lattice.center_x(j) - p_x, lattice.center_z(i) - p_z) > p_radius || !lattice.contains(i, j)) {
                    continue;
                }
                if (land_at(i, j) == Arability::ARABLE) {
                    return true;
                }
            }
        }
    }
    return false;
}

Plot FarmModel::plot_at(int p_i, int p_j) const {
    const auto it = plots.find(key(p_i, p_j));
    return it == plots.end() ? Plot() : it->second;
}

FarmAction FarmModel::available_action(int p_i, int p_j, bool p_has_seed) const {
    switch (plot_at(p_i, p_j).state) {
        case PlotState::WILD:
            return land_at(p_i, p_j) == Arability::ARABLE ? FarmAction::TILL : FarmAction::NONE;
        case PlotState::TILLED:
            return p_has_seed ? FarmAction::SOW : FarmAction::NONE;
        case PlotState::GROWING:
            return FarmAction::NONE;
        case PlotState::RIPE:
            return FarmAction::HARVEST;
    }
    return FarmAction::NONE;
}

bool FarmModel::till(int p_i, int p_j) {
    if (available_action(p_i, p_j, false) != FarmAction::TILL) {
        return false;
    }
    plots[key(p_i, p_j)] = { PlotState::TILLED, 0.0, nullptr };
    return true;
}

bool FarmModel::sow(int p_i, int p_j, const CropDef *p_crop) {
    if (!p_crop || available_action(p_i, p_j, true) != FarmAction::SOW) {
        return false;
    }
    plots[key(p_i, p_j)] = { PlotState::GROWING, 0.0, p_crop };
    return true;
}

Harvest FarmModel::harvest(int p_i, int p_j) {
    if (available_action(p_i, p_j, false) != FarmAction::HARVEST) {
        return Harvest();
    }
    Plot &plot = plots[key(p_i, p_j)];
    const Harvest got = { plot.crop->product, plot.crop->yield };
    plot = { PlotState::TILLED, 0.0, nullptr }; // 收完还是垦好的地
    return got;
}

std::vector<int> FarmModel::advance(double p_delta) {
    std::vector<int> changed;
    for (auto &[cell, plot] : plots) {
        if (plot.state != PlotState::GROWING) {
            continue;
        }
        const int i = key_i(cell), j = key_j(cell);
        const int before = stage_of(plot);
        const double moisture = survey(i, j).moisture;
        const double rate = rules.dry_growth + (1.0 - rules.dry_growth) * moisture;
        const double factor = plot.crop ? plot.crop->grow_factor : 1.0;
        plot.growth += p_delta * rate / std::max(1e-6, rules.grow_seconds * factor);
        if (plot.growth >= 1.0) {
            plot.growth = 1.0;
            plot.state = PlotState::RIPE;
        }
        if (stage_of(plot) != before) {
            changed.push_back(i);
            changed.push_back(j);
        }
    }
    return changed;
}

int FarmModel::count(PlotState p_state) const {
    int total = 0;
    for (const auto &entry : plots) {
        total += entry.second.state == p_state ? 1 : 0;
    }
    return total;
}

std::vector<int> FarmModel::plot_cells() const {
    std::vector<int> cells;
    for (const auto &entry : plots) {
        cells.push_back(key_i(entry.first));
        cells.push_back(key_j(entry.first));
    }
    return cells;
}

int FarmModel::stage_of(const Plot &p_plot) {
    if (p_plot.state == PlotState::RIPE) {
        return STAGES - 1;
    }
    return p_plot.growth < 0.25 ? 0 : (p_plot.growth < 0.6 ? 1 : 2);
}
