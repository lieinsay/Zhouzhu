#include "resource_sites.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "world_ground.h"

namespace {

constexpr double TAU = 6.283185307179586;
constexpr double SLOPE_ARM_M = 3.0; // 坡度：前后左右各这么远取高差

std::vector<std::string> split_tabs(const std::string &p_line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t tab = p_line.find('\t', start);
        fields.push_back(p_line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) {
            break;
        }
        start = tab + 1;
    }
    return fields;
}

bool to_number(const std::string &p_text, double &r_value) {
    char *end = nullptr;
    r_value = std::strtod(p_text.c_str(), &end);
    return end != p_text.c_str() && *end == '\0';
}

bool in_village(const WorldSites &p_sites, double p_x, double p_z, double p_margin) {
    for (const SiteSettlement &s : p_sites.settlements) {
        const double r = settlement_guess_radius(s) + p_margin;
        const double dx = p_x - s.x, dz = p_z - s.z;
        if (dx * dx + dz * dz <= r * r) {
            return true;
        }
    }
    return false;
}

} // namespace

bool parse_resource_deposits(const std::string &p_text, std::vector<ResourceDeposit> &r_deposits, std::string &r_error) {
    r_deposits.clear();
    std::istringstream in(p_text);
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        number++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = split_tabs(line);
        if (f[0] != "deposit") {
            continue; // 将来多出来的行类型：不认得就跳过
        }
        ResourceDeposit d;
        double id = 0.0, island = 0.0;
        if (f.size() < 9 || !to_number(f[1], id) || !to_number(f[3], island) || !to_number(f[4], d.x) || !to_number(f[5], d.z) || !to_number(f[6], d.area_km2)) {
            r_error = "第 " + std::to_string(number) + " 行：deposit 要 9 列（deposit id kind island x z area_km2 grade subtype）";
            return false;
        }
        d.id = int(id);
        d.kind = f[2];
        d.island = int(island);
        d.grade = deposits::grade_of(f[7]);
        d.subtype = f[8];
        r_deposits.push_back(d);
    }
    return true;
}

bool resource_spot_ok(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules, const char **r_why) {
    const char *why = nullptr;
    const TerrainSample s = p_terrain.sample(p_x, p_z);
    const WorldGround ground(p_terrain, 0.0, 0.0);
    if (!s.land) {
        why = "没有地";
    } else if (s.shore < p_rules.min_shore_m) {
        why = "离崖边太近";
    } else if (ground.water_at(p_x, p_z) != WaterKind::NONE) {
        why = "是水";
    } else if (ground.on_floodplain(p_x, p_z)) {
        why = "是河滩";
    } else if (ground.on_dock(p_x, p_z, p_rules.dock_margin_m) || ground.on_yard(p_x, p_z, p_rules.dock_margin_m)) {
        why = "在码头的坪上";
    } else if (in_village(p_sites, p_x, p_z, p_rules.village_margin_m)) {
        why = "在村子里";
    } else {
        double e = 0.0, w = 0.0, n = 0.0, so = 0.0;
        if (!p_terrain.surface(p_x + SLOPE_ARM_M, p_z, e) || !p_terrain.surface(p_x - SLOPE_ARM_M, p_z, w) || !p_terrain.surface(p_x, p_z - SLOPE_ARM_M, n) ||
                !p_terrain.surface(p_x, p_z + SLOPE_ARM_M, so)) {
            why = "离崖边太近";
        } else if (std::max(std::abs(e - w), std::abs(n - so)) / (2.0 * SLOPE_ARM_M) > p_rules.max_grade) {
            why = "太陡";
        }
    }
    if (r_why) {
        *r_why = why;
    }
    return why == nullptr;
}

ResourcePlan plan_resource_sites(const WorldTerrain &p_terrain, const WorldSites &p_sites, const std::vector<ResourceDeposit> &p_deposits, const ResourceSiteRules &p_rules) {
    ResourcePlan plan;
    std::vector<std::pair<double, double>> kept; // 已落地的矿点（并掉更晚的近邻按它量）
    for (size_t k = 0; k < p_deposits.size(); k++) {
        const ResourceDeposit &d = p_deposits[k];
        const DepositKind *kind = deposits::kind(d.kind);
        if (!kind || !deposits::gatherable(*kind)) {
            plan.landmarks++;
            continue;
        }
        bool near = false;
        for (const auto &p : kept) {
            near = near || std::hypot(p.first - d.x, p.second - d.z) < p_rules.merge_m;
        }
        if (near) {
            plan.merged++;
            continue;
        }
        // 采掘面：矿点起一圈圈往外找，旁边还得放得下堆场
        bool found = false;
        ResourceSite site;
        for (double r = 0.0; r <= p_rules.search_m + 1e-9 && !found; r += p_rules.step_m) {
            const int count = r <= 0.0 ? 1 : std::max(6, int(std::lround(TAU * r / p_rules.step_m)));
            for (int a = 0; a < count && !found; a++) {
                const double ang = TAU * a / count;
                const double fx = d.x + r * std::cos(ang), fz = d.z + r * std::sin(ang);
                if (!resource_spot_ok(p_terrain, p_sites, fx, fz, p_rules)) {
                    continue;
                }
                for (int b = 0; b < 8; b++) {
                    const double pang = TAU * b / 8.0;
                    const double px = fx + p_rules.pile_m * std::cos(pang), pz = fz + p_rules.pile_m * std::sin(pang);
                    if (!resource_spot_ok(p_terrain, p_sites, px, pz, p_rules)) {
                        continue;
                    }
                    site.face_x = fx, site.face_z = fz;
                    site.pile_x = px, site.pile_z = pz;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            if (in_village(p_sites, d.x, d.z, p_rules.village_margin_m)) {
                plan.in_village++;
            } else {
                plan.failed.push_back(d.id);
            }
            continue;
        }
        p_terrain.surface(site.face_x, site.face_z, site.face_y);
        p_terrain.surface(site.pile_x, site.pile_z, site.pile_y);
        site.deposit = int(k);
        site.id = d.id;
        site.kind = kind;
        site.subtype = d.subtype;
        site.item = deposits::product_of(*kind, d.subtype);
        site.name = deposits::site_name(*kind, d.subtype);
        site.island = d.island;
        site.grade = d.grade;
        site.area_km2 = d.area_km2;
        site.cap = deposits::stock_cap(*kind, d.grade, d.area_km2);
        plan.sites.push_back(site);
        kept.emplace_back(d.x, d.z);
    }
    return plan;
}

// ---------------------------------------------------------------- 存量账

double stock_amount(const ResourceStock &p_stock, int p_cap, double p_refill_hours, double p_now_hours) {
    if (p_stock.amount < 0.0) {
        return double(p_cap);
    }
    const double rate = p_refill_hours > 0.0 ? double(p_cap) / p_refill_hours : 1e18;
    return std::min(double(p_cap), p_stock.amount + rate * std::max(0.0, p_now_hours - p_stock.hour));
}

bool stock_take(ResourceStock &r_stock, int p_cap, double p_refill_hours, double p_now_hours, int p_count) {
    const double now = stock_amount(r_stock, p_cap, p_refill_hours, p_now_hours);
    if (now + 1e-9 < double(p_count)) {
        return false;
    }
    r_stock.amount = std::max(0.0, now - double(p_count));
    r_stock.hour = p_now_hours;
    return true;
}

double stock_hours_until(const ResourceStock &p_stock, int p_cap, double p_refill_hours, double p_now_hours, double p_amount) {
    const double now = stock_amount(p_stock, p_cap, p_refill_hours, p_now_hours);
    if (now + 1e-9 >= p_amount || p_cap <= 0) {
        return 0.0;
    }
    return (std::min(p_amount, double(p_cap)) - now) * p_refill_hours / double(p_cap);
}
