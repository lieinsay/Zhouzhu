#include "settlement_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr double PI = 3.14159265358979323846;

double normalize_yaw(double p_yaw) {
    double y = std::fmod(p_yaw, 2.0 * PI);
    if (y < 0.0) {
        y += 2.0 * PI;
    }
    return y;
}

// 占地的九个勘察点：四角、四边中点、中心（岛坐标）
void survey_points(const BuildingDef &p_def, double p_x, double p_z, double p_yaw, double r_x[9], double r_z[9]) {
    double ux = 0.0, uz = 0.0, vx = 0.0, vz = 0.0;
    building_axes(p_yaw, ux, uz, vx, vz);
    const double hw = 0.5 * p_def.width_m, hd = 0.5 * p_def.depth_m;
    int k = 0;
    for (int a = -1; a <= 1; a++) {
        for (int b = -1; b <= 1; b++) {
            r_x[k] = p_x + a * hw * ux + b * hd * vx;
            r_z[k] = p_z + a * hw * uz + b * hd * vz;
            k++;
        }
    }
}

// 把矩形投到一根轴上：中心的投影 ± 半长
void project(const Building &p_b, const BuildingDef &p_def, double p_grow, double p_ax, double p_az, double &r_lo, double &r_hi) {
    double ux = 0.0, uz = 0.0, vx = 0.0, vz = 0.0;
    building_axes(p_b.yaw, ux, uz, vx, vz);
    const double center = p_b.x * p_ax + p_b.z * p_az;
    const double reach = (0.5 * p_def.width_m + p_grow) * std::abs(ux * p_ax + uz * p_az) + (0.5 * p_def.depth_m + p_grow) * std::abs(vx * p_ax + vz * p_az);
    r_lo = center - reach;
    r_hi = center + reach;
}

std::string format_number(double p_value, int p_decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", p_decimals, p_value);
    // 去掉 "-0.00" 这种负零
    std::string text(buffer);
    if (text[0] == '-' && text.find_first_not_of("-0.") == std::string::npos) {
        text.erase(0, 1);
    }
    return text;
}

} // namespace

bool SettlementLayout::operator==(const SettlementLayout &p_other) const {
    if (items.size() != p_other.items.size()) {
        return false;
    }
    for (size_t k = 0; k < items.size(); k++) {
        const Building &a = items[k], &b = p_other.items[k];
        if (a.kind != b.kind || std::abs(a.x - b.x) > 1e-6 || std::abs(a.z - b.z) > 1e-6 || std::abs(normalize_yaw(a.yaw) - normalize_yaw(b.yaw)) > 1e-6) {
            return false;
        }
    }
    return true;
}

void building_axes(double p_yaw, double &r_ux, double &r_uz, double &r_vx, double &r_vz) {
    const double c = std::cos(p_yaw), s = std::sin(p_yaw);
    r_ux = c;
    r_uz = -s;
    r_vx = s;
    r_vz = c;
}

void footprint_corners(const BuildingDef &p_def, double p_x, double p_z, double p_yaw, double p_margin, double r_x[4], double r_z[4]) {
    double ux = 0.0, uz = 0.0, vx = 0.0, vz = 0.0;
    building_axes(p_yaw, ux, uz, vx, vz);
    const double hw = 0.5 * p_def.width_m + p_margin, hd = 0.5 * p_def.depth_m + p_margin;
    const int su[4] = { -1, 1, 1, -1 }, sv[4] = { 1, 1, -1, -1 };
    for (int k = 0; k < 4; k++) {
        r_x[k] = p_x + su[k] * hw * ux + sv[k] * hd * vx;
        r_z[k] = p_z + su[k] * hw * uz + sv[k] * hd * vz;
    }
}

bool footprint_contains(const BuildingDef &p_def, const Building &p_building, double p_px, double p_pz, double p_margin) {
    double ux = 0.0, uz = 0.0, vx = 0.0, vz = 0.0;
    building_axes(p_building.yaw, ux, uz, vx, vz);
    const double dx = p_px - p_building.x, dz = p_pz - p_building.z;
    const double u = dx * ux + dz * uz, v = dx * vx + dz * vz;
    return std::abs(u) <= 0.5 * p_def.width_m + p_margin && std::abs(v) <= 0.5 * p_def.depth_m + p_margin;
}

bool footprints_overlap(const Building &p_a, const Building &p_b, double p_gap) {
    const BuildingDef *da = buildings::by_id(p_a.kind), *db = buildings::by_id(p_b.kind);
    if (!da || !db) {
        return false;
    }
    // 分离轴：两座各自的两根轴，四根里有一根投影不相交就没压着
    const Building *both[2] = { &p_a, &p_b };
    for (const Building *b : both) {
        double ux = 0.0, uz = 0.0, vx = 0.0, vz = 0.0;
        building_axes(b->yaw, ux, uz, vx, vz);
        const double axes[2][2] = { { ux, uz }, { vx, vz } };
        for (const auto &axis : axes) {
            double a_lo = 0.0, a_hi = 0.0, b_lo = 0.0, b_hi = 0.0;
            project(p_a, *da, 0.5 * p_gap, axis[0], axis[1], a_lo, a_hi);
            project(p_b, *db, 0.5 * p_gap, axis[0], axis[1], b_lo, b_hi);
            if (a_hi < b_lo || b_hi < a_lo) {
                return false;
            }
        }
    }
    return true;
}

SiteReport survey_site(const SiteGround &p_ground, const BuildingDef &p_def, double p_x, double p_z, double p_yaw,
        const SettlementLayout &p_others, const SiteRules &p_rules, int p_ignore) {
    SiteReport r;
    double px[9], pz[9];
    survey_points(p_def, p_x, p_z, p_yaw, px, pz);

    double lo = 1e18, hi = -1e18;
    SiteBlock block;
    for (int k = 0; k < 9; k++) {
        double ground = 0.0;
        if (!p_ground.site_height(px[k], pz[k], ground)) {
            r.reason = "这里没有地面：占地伸到岛外去了";
            return r;
        }
        lo = std::min(lo, ground);
        hi = std::max(hi, ground);
        const SiteBlock here = p_ground.site_block(px[k], pz[k]);
        if (here.rank >= 0 && (block.rank < 0 || here.rank < block.rank)) {
            block = here;
        }
    }
    r.ground_min = lo;
    r.ground_max = hi;
    r.floor_y = hi + p_def.plinth_m;
    r.plinth_bottom = lo - p_rules.bury_m;

    if (block.rank >= 0) {
        r.reason = block.reason;
    } else {
        // 压着别的建筑比坡陡先说：挪开就好，坡是挪不动的
        const Building here{ p_def.id, p_x, p_z, p_yaw };
        for (int k = 0; k < p_others.size() && r.reason.empty(); k++) {
            if (k == p_ignore) {
                continue;
            }
            const Building &other = p_others.at(k);
            if (footprints_overlap(here, other, p_rules.gap_m)) {
                const BuildingDef *od = buildings::by_id(other.kind);
                r.reason = std::string("和") + (od ? od->name : other.kind.c_str()) + "挨得太近";
            }
        }
        if (r.reason.empty() && hi - lo > p_rules.max_rise_m) {
            r.reason = "坡太陡，台基架不平（高差 " + format_number(hi - lo, 1) + " m）";
        }
    }
    return r;
}

std::string settlement_place(SettlementLayout &p_layout, const SiteGround &p_ground, const std::string &p_kind,
        double p_x, double p_z, double p_yaw, const SiteRules &p_rules) {
    const BuildingDef *def = buildings::by_id(p_kind);
    if (!def) {
        return "没有这种建筑：" + p_kind;
    }
    const SiteReport report = survey_site(p_ground, *def, p_x, p_z, p_yaw, p_layout, p_rules);
    if (!report.ok()) {
        return report.reason;
    }
    p_layout.add(Building{ p_kind, p_x, p_z, normalize_yaw(p_yaw) });
    return std::string();
}

int settlement_find(const SettlementLayout &p_layout, double p_px, double p_pz) {
    for (int k = p_layout.size() - 1; k >= 0; k--) {
        const Building &b = p_layout.at(k);
        const BuildingDef *def = buildings::by_id(b.kind);
        if (def && footprint_contains(*def, b, p_px, p_pz)) {
            return k;
        }
    }
    return -1;
}

std::string building_id(const Building &p_building) {
    return p_building.kind + "@" + format_number(p_building.x, 2) + "," + format_number(p_building.z, 2) + "," + format_number(normalize_yaw(p_building.yaw) * 180.0 / PI, 1);
}

bool parse_building_id(const std::string &p_text, Building &r_building) {
    const size_t at = p_text.find('@');
    if (at == std::string::npos || at == 0) {
        return false;
    }
    const std::string kind = p_text.substr(0, at);
    if (!buildings::by_id(kind)) {
        return false;
    }
    double values[3] = { 0.0, 0.0, 0.0 };
    const char *cursor = p_text.c_str() + at + 1;
    for (int k = 0; k < 3; k++) {
        char *end = nullptr;
        values[k] = std::strtod(cursor, &end);
        if (end == cursor) {
            return false;
        }
        cursor = end;
        if (k < 2) {
            if (*cursor != ',') {
                return false;
            }
            cursor++;
        }
    }
    while (*cursor == ' ' || *cursor == '\r' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor != '\0') {
        return false;
    }
    r_building.kind = kind;
    r_building.x = values[0];
    r_building.z = values[1];
    r_building.yaw = normalize_yaw(values[2] * PI / 180.0);
    return true;
}

std::vector<std::string> settlement_ids(const SettlementLayout &p_layout) {
    std::vector<std::string> ids;
    for (const Building &b : p_layout.get_items()) {
        ids.push_back(building_id(b));
    }
    return ids;
}

SettlementLayout settlement_from_ids(const std::vector<std::string> &p_ids, bool *r_ok) {
    SettlementLayout layout;
    bool ok = true;
    for (const std::string &raw : p_ids) {
        const size_t begin = raw.find_first_not_of(" \t\r");
        if (begin == std::string::npos || raw[begin] == '#') {
            continue;
        }
        Building b;
        if (parse_building_id(raw.substr(begin), b)) {
            layout.add(b);
        } else {
            ok = false;
        }
    }
    if (r_ok) {
        *r_ok = ok;
    }
    return layout;
}

SettlementLayout apply_layout_edits(const SettlementLayout &p_base, const LayoutEdits &p_edits, int *r_unmatched) {
    std::vector<bool> gone(size_t(p_base.size()), false);
    int unmatched = 0;
    for (const std::string &id : p_edits.removed) {
        bool found = false;
        for (int k = 0; k < p_base.size() && !found; k++) {
            if (!gone[size_t(k)] && building_id(p_base.at(k)) == id) {
                gone[size_t(k)] = true;
                found = true;
            }
        }
        unmatched += found ? 0 : 1;
    }
    SettlementLayout out;
    for (int k = 0; k < p_base.size(); k++) {
        if (!gone[size_t(k)]) {
            out.add(p_base.at(k));
        }
    }
    for (const Building &b : p_edits.added) {
        out.add(b);
    }
    if (r_unmatched) {
        *r_unmatched = unmatched;
    }
    return out;
}

LayoutEdits diff_layouts(const SettlementLayout &p_base, const SettlementLayout &p_now) {
    const std::vector<std::string> base_ids = settlement_ids(p_base), now_ids = settlement_ids(p_now);
    std::vector<bool> used(now_ids.size(), false);
    LayoutEdits edits;
    for (const std::string &id : base_ids) {
        bool found = false;
        for (size_t k = 0; k < now_ids.size() && !found; k++) {
            if (!used[k] && now_ids[k] == id) {
                used[k] = true;
                found = true;
            }
        }
        if (!found) {
            edits.removed.push_back(id);
        }
    }
    for (size_t k = 0; k < now_ids.size(); k++) {
        if (!used[k]) {
            edits.added.push_back(p_now.at(int(k)));
        }
    }
    return edits;
}

std::vector<std::string> layout_edit_lines(const LayoutEdits &p_edits) {
    std::vector<std::string> lines;
    for (const std::string &id : p_edits.removed) {
        lines.push_back("-" + id);
    }
    for (const Building &b : p_edits.added) {
        lines.push_back(building_id(b));
    }
    return lines;
}

LayoutEdits parse_layout_edits(const std::vector<std::string> &p_lines, bool *r_ok) {
    LayoutEdits edits;
    bool ok = true;
    for (const std::string &raw : p_lines) {
        const size_t begin = raw.find_first_not_of(" \t\r");
        if (begin == std::string::npos || raw[begin] == '#') {
            continue;
        }
        const char sign = raw[begin];
        const std::string body = raw.substr(sign == '+' || sign == '-' ? begin + 1 : begin);
        Building b;
        if (!parse_building_id(body, b)) {
            ok = false;
        } else if (sign == '-') {
            edits.removed.push_back(building_id(b));
        } else {
            edits.added.push_back(b);
        }
    }
    if (r_ok) {
        *r_ok = ok;
    }
    return edits;
}
