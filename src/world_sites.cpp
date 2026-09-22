#include "world_sites.h"

#include <cstdlib>
#include <sstream>

namespace {

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

bool to_int(const std::string &p_text, int &r_value) {
    double v = 0.0;
    if (!to_number(p_text, v)) {
        return false;
    }
    r_value = int(v);
    return double(r_value) == v;
}

} // namespace

const SiteDock *WorldSites::dock_by_id(int p_id) const {
    for (const SiteDock &d : docks) {
        if (d.id == p_id) {
            return &d;
        }
    }
    return nullptr;
}

const SiteSettlement *WorldSites::village_by_id(int p_id) const {
    for (const SiteSettlement &s : settlements) {
        if (!s.hamlet && s.id == p_id) {
            return &s;
        }
    }
    return nullptr;
}

std::vector<WorldIsland> WorldSites::terrain_islands() const {
    std::vector<WorldIsland> out;
    for (const SiteIsland &isl : islands) {
        WorldIsland w;
        w.id = isl.id;
        w.rim_m = isl.rim_m;
        w.cliff_m = isl.cliff_m;
        w.keel_m = isl.keel_m;
        w.max_shore_m = isl.max_shore_m;
        out.push_back(w);
    }
    return out;
}

bool parse_world_sites(const std::string &p_text, WorldSites &r_sites, std::string &r_error) {
    r_sites = WorldSites();
    std::istringstream in(p_text);
    std::string line;
    int number = 0;
    std::vector<SiteSettlement> hamlets;
    while (std::getline(in, line)) {
        number++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = split_tabs(line);
        const std::string where = "第 " + std::to_string(number) + " 行";
        bool ok = true;
        if (f[0] == "island" && f.size() >= 6) {
            SiteIsland isl;
            ok = to_int(f[1], isl.id) && to_number(f[2], isl.rim_m) && to_number(f[3], isl.cliff_m) && to_number(f[4], isl.keel_m) && to_number(f[5], isl.max_shore_m);
            r_sites.islands.push_back(isl);
        } else if (f[0] == "dock" && f.size() >= 6) {
            SiteDock d;
            int main = 0;
            ok = to_int(f[1], d.id) && to_int(f[2], d.island) && to_number(f[3], d.x) && to_number(f[4], d.z) && to_int(f[5], main);
            d.main = main != 0;
            r_sites.docks.push_back(d);
        } else if ((f[0] == "village" || f[0] == "hamlet") && f.size() >= 7) {
            SiteSettlement s;
            int seat = 0;
            ok = to_int(f[1], s.id) && to_int(f[2], s.island) && to_number(f[3], s.x) && to_number(f[4], s.z) && to_int(f[5], s.households) && to_int(f[6], seat);
            s.seat = seat != 0;
            s.hamlet = f[0] == "hamlet";
            s.name = f.size() >= 8 ? f[7] : std::string();
            (s.hamlet ? hamlets : r_sites.settlements).push_back(s);
        } else {
            r_error = where + "认不出：" + f[0];
            return false;
        }
        if (!ok) {
            r_error = where + "有字段不是数：" + line;
            return false;
        }
    }
    r_sites.settlements.insert(r_sites.settlements.end(), hamlets.begin(), hamlets.end());
    return true;
}

std::vector<DockPlan> plan_world_docks(const WorldTerrain &p_terrain, const WorldSites &p_sites, const DockRules &p_rules) {
    std::vector<DockPlan> plans;
    plans.reserve(p_sites.docks.size());
    for (const SiteDock &d : p_sites.docks) {
        plans.push_back(plan_world_dock(p_terrain, d.x, d.z, p_rules));
    }
    return plans;
}

VillagePlan plan_world_village(const WorldTerrain &p_terrain, const SiteSettlement &p_site, uint64_t p_seed, const WorldVillageRules &p_rules) {
    const WorldSiteGround ground(p_terrain, p_site.x, p_site.z, p_rules.village.max_radius_m, p_rules.ground);
    VillageRequest request;
    request.households = p_site.households;
    request.seat = p_site.seat;
    // 村与散户的 id 各自从 1 起：散户另起一条种子流
    request.seed = p_seed * 1000003ull + uint64_t(p_site.id) * 2ull + (p_site.hamlet ? 1ull : 0ull);
    return plan_village(ground, request, p_rules.site, p_rules.village);
}
