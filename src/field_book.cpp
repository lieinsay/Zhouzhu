#include "field_book.h"

#include <cmath>

bool ArableMask::any() const {
    for (uint8_t b : bits) {
        if (b) {
            return true;
        }
    }
    return false;
}

ArableMask survey_arable(const FarmModel &p_land, double p_x, double p_z, double p_radius) {
    ArableMask m;
    const PlotLattice &l = p_land.get_lattice();
    int ci = 0, cj = 0;
    l.cell_of(p_x, p_z, ci, cj);
    const int reach = int(std::ceil(p_radius / l.cell_m)) + 1;
    m.i0 = ci - reach;
    m.j0 = cj - reach;
    m.rows = m.cols = 2 * reach + 1;
    m.bits.assign((size_t(m.rows) * size_t(m.cols) + 7) / 8, 0);
    for (int i = m.i0; i < m.i0 + m.rows; i++) {
        for (int j = m.j0; j < m.j0 + m.cols; j++) {
            if (std::hypot(l.center_x(j) - p_x, l.center_z(i) - p_z) <= p_radius + l.cell_m && l.contains(i, j) && p_land.land_at(i, j) == Arability::ARABLE) {
                m.set(i, j);
            }
        }
    }
    return m;
}

int HeldFields::find(int p_i, int p_j) const {
    for (size_t k = 0; k < held.size(); k++) {
        if (held[k].i == p_i && held[k].j == p_j) {
            return int(k);
        }
    }
    return -1;
}

Plot HeldFields::plot_at(int p_i, int p_j) const {
    const int k = find(p_i, p_j);
    return k >= 0 ? held[size_t(k)].plot : Plot();
}

FarmAction HeldFields::available(int p_i, int p_j, bool p_has_seed) const {
    const int k = find(p_i, p_j);
    if (k < 0) {
        const bool arable = mask && mask->covers(p_i, p_j) ? mask->arable(p_i, p_j) : (land && land->land_at(p_i, p_j) == Arability::ARABLE);
        const bool free = land && arable && land->plot_at(p_i, p_j).state == PlotState::WILD;
        return free ? FarmAction::TILL : FarmAction::NONE;
    }
    switch (held[size_t(k)].plot.state) {
        case PlotState::WILD:
            return FarmAction::TILL;
        case PlotState::TILLED:
            return p_has_seed ? FarmAction::SOW : FarmAction::NONE;
        case PlotState::GROWING:
            return FarmAction::NONE;
        case PlotState::RIPE:
            return FarmAction::HARVEST;
    }
    return FarmAction::NONE;
}

bool HeldFields::till(int p_i, int p_j) {
    if (available(p_i, p_j, false) != FarmAction::TILL) {
        return false;
    }
    const int k = find(p_i, p_j);
    HeldPlot plot;
    plot.i = p_i;
    plot.j = p_j;
    plot.plot = { PlotState::TILLED, 0.0, nullptr };
    plot.moisture = land ? land->survey(p_i, p_j).moisture : 0.0f;
    if (k >= 0) {
        held[size_t(k)] = plot;
    } else {
        held.push_back(plot);
    }
    return true;
}

bool HeldFields::sow(int p_i, int p_j, const CropDef *p_crop) {
    const int k = find(p_i, p_j);
    if (!p_crop || k < 0 || available(p_i, p_j, true) != FarmAction::SOW) {
        return false;
    }
    held[size_t(k)].plot = { PlotState::GROWING, 0.0, p_crop };
    return true;
}

Harvest HeldFields::harvest(int p_i, int p_j) {
    const int k = find(p_i, p_j);
    if (k < 0 || available(p_i, p_j, false) != FarmAction::HARVEST) {
        return Harvest();
    }
    Plot &plot = held[size_t(k)].plot;
    const Harvest got = { plot.crop->product, plot.crop->yield };
    plot = { PlotState::TILLED, 0.0, nullptr };
    return got;
}

void HeldFields::grow(double p_hours, double p_season) {
    for (HeldPlot &h : held) {
        if (h.plot.state != PlotState::GROWING) {
            continue;
        }
        h.plot.growth += p_hours * growth_rate(h.moisture, h.plot.crop, rules, p_season);
        if (h.plot.growth >= 1.0) {
            h.plot.growth = 1.0;
            h.plot.state = PlotState::RIPE;
        }
    }
}
