#include "farm_model.h"

#include <algorithm>
#include <cmath>

int FarmSurvey::arable_count() const {
    return int(std::count(arability.begin(), arability.end(), uint8_t(Arability::ARABLE)));
}

FarmSurvey survey_farmland(const IslandField &f, double p_dock_apron_m, const FarmRules &p_rules) {
    FarmSurvey s;
    s.n = f.n;
    s.arability.assign(size_t(f.n) * f.n, uint8_t(Arability::NO_GROUND));
    s.moisture.assign(size_t(f.n) * f.n, 0.0f);
    const double diagonal = f.cell_m * std::sqrt(2.0);

    for (int i = 0; i < f.n - 1; i++) {
        for (int j = 0; j < f.n - 1; j++) {
            if (!f.quad_solid(i, j)) {
                continue;
            }
            const size_t corners[4] = { f.index(i, j), f.index(i, j + 1), f.index(i + 1, j), f.index(i + 1, j + 1) };
            Arability verdict = Arability::ARABLE;
            if (!f.has_river()) {
                verdict = Arability::TOO_DRY;
            } else {
                double lo = 1e18, hi = -1e18, dist = 0.0, trail = 1e18;
                bool wet = false;
                for (size_t k : corners) {
                    lo = std::min(lo, double(f.height[k]));
                    hi = std::max(hi, double(f.height[k]));
                    dist += 0.25 * f.river_dist[k];
                    wet = wet || f.river_depth[k] > 0.0f;
                    trail = f.has_trail() ? std::min(trail, double(f.trail_dist[k])) : trail;
                }
                const double cx = f.coord(j) + 0.5 * f.cell_m, cz = f.coord(i) + 0.5 * f.cell_m;
                if (wet) {
                    verdict = Arability::WATER;
                } else if (std::hypot(cx - f.dock.x, cz - f.dock.z) <= p_dock_apron_m + p_rules.dock_margin_m) {
                    verdict = Arability::DOCK;
                } else if (trail <= p_rules.trail_margin_m) {
                    verdict = Arability::TRAIL;
                } else if (dist > p_rules.max_river_m) {
                    verdict = Arability::TOO_DRY;
                } else if ((hi - lo) / diagonal > p_rules.max_slope) {
                    verdict = Arability::TOO_STEEP;
                }
                s.moisture[s.index(i, j)] = float(std::clamp(1.0 - dist / p_rules.max_river_m, 0.0, 1.0));
            }
            s.arability[s.index(i, j)] = uint8_t(verdict);
        }
    }
    return s;
}

Plot FarmModel::plot_at(int p_i, int p_j) const {
    const auto it = plots.find(key(p_i, p_j));
    return it == plots.end() ? Plot() : it->second;
}

FarmAction FarmModel::available_action(int p_i, int p_j, bool p_has_seed) const {
    switch (plot_at(p_i, p_j).state) {
        case PlotState::WILD:
            return survey.at(p_i, p_j) == Arability::ARABLE ? FarmAction::TILL : FarmAction::NONE;
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
        const int i = int(cell >> 16), j = int(cell & 0xFFFF);
        const int before = stage_of(plot);
        const double moisture = survey.moisture[survey.index(i, j)];
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

int FarmModel::stage_of(const Plot &p_plot) {
    if (p_plot.state == PlotState::RIPE) {
        return STAGES - 1;
    }
    return p_plot.growth < 0.25 ? 0 : (p_plot.growth < 0.6 ? 1 : 2);
}
