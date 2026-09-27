#include "placeables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "crops.h"

namespace props {

const PropDef PROPS[] = {
    { "workbench", 1.4, 0.8, 0.9, PropLook::WORKBENCH, 0, int(Station::WORKBENCH), 0.0 },
    { "kiln", 1.6, 1.6, 1.8, PropLook::KILN, 0, int(Station::KILN), 0.0 },
    { "chest", 1.0, 0.6, 0.7, PropLook::CHEST, 18, -1, 0.0 },
    { "torch", 0.3, 0.3, 1.6, PropLook::TORCH, 0, -1, 9.0 },
    { "fence", 2.0, 0.3, 1.1, PropLook::FENCE, 0, -1, 0.0 },
};
const int PROP_COUNT = int(sizeof(PROPS) / sizeof(PROPS[0]));

const PropDef *find(const std::string &p_item) {
    for (int k = 0; k < PROP_COUNT; k++) {
        if (p_item == PROPS[k].item) {
            return &PROPS[k];
        }
    }
    return nullptr;
}

} // namespace props

namespace {

constexpr double PI = 3.14159265358979323846;

double wrap_angle(double p_a) {
    p_a = std::fmod(p_a + PI, 2.0 * PI);
    if (p_a < 0.0) {
        p_a += 2.0 * PI;
    }
    return p_a - PI;
}

bool boxes_touch(double p_ax, double p_az, double p_ahx, double p_ahz, double p_bx, double p_bz, double p_bhx, double p_bhz, double p_touch) {
    return std::abs(p_ax - p_bx) < p_ahx + p_bhx + p_touch - 1e-9 && std::abs(p_az - p_bz) < p_ahz + p_bhz + p_touch - 1e-9;
}

} // namespace

void prop_half_extent(const PropDef &p_def, int p_quarter, double &r_half_x, double &r_half_z) {
    const bool turned = (((p_quarter % 4) + 4) % 4) % 2 == 1;
    r_half_x = 0.5 * (turned ? p_def.depth : p_def.width);
    r_half_z = 0.5 * (turned ? p_def.width : p_def.depth);
}

double quarter_yaw(int p_quarter) {
    return (((p_quarter % 4) + 4) % 4) * 0.5 * PI;
}

int facing_quarter(double p_look_yaw, int p_turns) {
    const int q = int(std::lround(wrap_angle(p_look_yaw) / (0.5 * PI)));
    return (((q + p_turns) % 4) + 4) % 4;
}

PlacedProp *Placements::find(int p_id) {
    for (PlacedProp &p : props) {
        if (p.id == p_id) {
            return &p;
        }
    }
    return nullptr;
}

const PlacedProp *Placements::find(int p_id) const {
    return const_cast<Placements *>(this)->find(p_id);
}

int Placements::overlapping(const PropDef &p_def, double p_x, double p_z, int p_quarter, double p_touch_m, int p_ignore) const {
    double hx = 0, hz = 0;
    prop_half_extent(p_def, p_quarter, hx, hz);
    for (const PlacedProp &p : props) {
        const PropDef *def = props::find(p.item);
        if (!def || p.id == p_ignore) {
            continue;
        }
        double bx = 0, bz = 0;
        prop_half_extent(*def, p.quarter, bx, bz);
        if (boxes_touch(p_x, p_z, hx, hz, p.x, p.z, bx, bz, p_touch_m)) {
            return p.id;
        }
    }
    return -1;
}

int Placements::add(const std::string &p_item, const PlaceCheck &p_check) {
    const PropDef *def = props::find(p_item);
    PlacedProp p;
    p.id = next_id++;
    p.item = p_item;
    p.x = p_check.x;
    p.y = p_check.y;
    p.z = p_check.z;
    p.low = p_check.low;
    p.quarter = p_check.quarter;
    p.contents = ItemSlots(def ? def->slots : 0);
    props.push_back(p);
    revision++;
    return p.id;
}

void Placements::assign(const std::vector<PlacedProp> &p_props) {
    props = p_props;
    next_id = 1;
    for (const PlacedProp &p : props) {
        next_id = std::max(next_id, p.id + 1);
    }
    revision++;
}

void Placements::shift(const std::function<void(double &, double &, double &, double &)> &p_move) {
    for (PlacedProp &p : props) {
        p_move(p.x, p.y, p.z, p.yaw_extra);
    }
    if (!props.empty()) {
        revision++;
    }
}

bool Placements::remove(int p_id) {
    for (size_t k = 0; k < props.size(); k++) {
        if (props[k].id == p_id) {
            props.erase(props.begin() + ptrdiff_t(k));
            revision++;
            return true;
        }
    }
    return false;
}

double prop_distance(const PlacedProp &p_prop, double p_x, double p_z) {
    const PropDef *def = props::find(p_prop.item);
    double hx = 0, hz = 0;
    if (def) {
        prop_half_extent(*def, p_prop.quarter, hx, hz);
    }
    const double dx = std::max(0.0, std::abs(p_x - p_prop.x) - hx);
    const double dz = std::max(0.0, std::abs(p_z - p_prop.z) - hz);
    return std::sqrt(dx * dx + dz * dz);
}

int Placements::facing(double p_x, double p_y, double p_z, double p_look_yaw, const PlaceRules &p_rules) const {
    const double fx = -std::sin(p_look_yaw), fz = -std::cos(p_look_yaw);
    const double cone = std::cos(p_rules.pick_cone_deg * PI / 180.0);
    int best = -1;
    double best_d = 1e18;
    for (const PlacedProp &p : props) {
        const PropDef *def = props::find(p.item);
        if (!def || std::abs(p_y - p.y) > def->height + 2.0) {
            continue;
        }
        const double d = prop_distance(p, p_x, p_z);
        if (d > p_rules.pick_range_m) {
            continue;
        }
        // 站在它上面 / 贴着它时不看方向；否则中心要在身前扇面里
        const double cx = p.x - p_x, cz = p.z - p_z, len = std::sqrt(cx * cx + cz * cz);
        if (d > 0.05 && len > 1e-6 && (cx * fx + cz * fz) / len < cone) {
            continue;
        }
        if (d < best_d) {
            best_d = d;
            best = p.id;
        }
    }
    return best;
}

unsigned Placements::stations_near(double p_x, double p_y, double p_z, double p_range) const {
    unsigned bits = 0;
    for (const PlacedProp &p : props) {
        const PropDef *def = props::find(p.item);
        if (def && def->station >= 0 && std::abs(p_y - p.y) <= 3.0 && prop_distance(p, p_x, p_z) <= p_range) {
            bits |= station_bit(Station(def->station));
        }
    }
    return bits;
}

PlaceCheck plan_placement(const LandingGround &p_ground, const PropDef &p_def, double p_x, double p_z, double p_look_yaw, int p_turns,
        const Placements &p_placed, const PlaceRules &p_rules) {
    PlaceCheck c;
    c.quarter = facing_quarter(p_look_yaw, p_turns);
    double hx = 0, hz = 0;
    prop_half_extent(p_def, c.quarter, hx, hz);
    // 中心：视线前方，离人 gap_m 再加上物件朝人那一面的半个身子，按格对齐
    const double fx = -std::sin(p_look_yaw), fz = -std::cos(p_look_yaw);
    const double reach = p_rules.gap_m + std::abs(fx) * hx + std::abs(fz) * hz;
    const double g = std::max(0.05, p_rules.grid_m);
    c.x = std::round((p_x + fx * reach) / g) * g;
    c.z = std::round((p_z + fz * reach) / g) * g;

    double lo = 1e18, hi = -1e18;
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            const double sx = c.x + i * hx, sz = c.z + j * hz;
            double y = 0.0;
            if (!p_ground.height(sx, sz, y)) {
                c.why = "这儿没有地";
                return c;
            }
            if (p_ground.water(sx, sz)) {
                c.why = "水里摆不了";
                return c;
            }
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }
    c.y = hi;
    c.low = lo;
    if (hi - lo > p_rules.max_relief_m) {
        char text[64];
        std::snprintf(text, sizeof(text), "%.1f", hi - lo);
        c.why = std::string("地不平（高低差 ") + text + " m）";
        return c;
    }
    const int other = p_placed.overlapping(p_def, c.x, c.z, c.quarter, p_rules.touch_m);
    if (other >= 0) {
        c.why = "和" + catalog::item_name(p_placed.find(other)->item) + "挤在一起了";
        return c;
    }
    c.ok = true;
    return c;
}

std::string pickup_problem(const PlacedProp &p_prop) {
    if (!p_prop.contents.empty()) {
        return catalog::item_name(p_prop.item) + "里还有东西，先搬空";
    }
    return std::string();
}
