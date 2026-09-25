#include "resource_sites.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "wild_landing.h"
#include "world_ground.h"

namespace {

constexpr double TAU = 6.283185307179586;
constexpr double SLOPE_ARM_M = 3.0; // 坡度：前后左右各这么远取高差
constexpr double LANDMARK_SHORE_M = 4.0; // 地标离崖边至少这么远（崖洞的样子摆在崖顶）
constexpr double LANDMARK_VILLAGE_M = 10.0; // 地标让开村子（估的半径）外这么远

std::vector<std::string> split(const std::string &p_line, char p_sep) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t at = p_line.find(p_sep, start);
        fields.push_back(p_line.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) {
            break;
        }
        start = at + 1;
    }
    return fields;
}

bool to_number(const std::string &p_text, double &r_value) {
    char *end = nullptr;
    r_value = std::strtod(p_text.c_str(), &end);
    return end != p_text.c_str() && *end == '\0';
}

// 村子估的圆（settlement_guess_radius），按 x 排好：问一点在不在村里只看 x 附近那一条
class VillageDisks {
    struct Disk {
        double x, z, r;
    };
    std::vector<Disk> disks;
    double max_r = 0.0;

public:
    explicit VillageDisks(const WorldSites &p_sites) {
        disks.reserve(p_sites.settlements.size());
        for (const SiteSettlement &s : p_sites.settlements) {
            disks.push_back({ s.x, s.z, settlement_guess_radius(s) });
            max_r = std::max(max_r, disks.back().r);
        }
        std::sort(disks.begin(), disks.end(), [](const Disk &a, const Disk &b) { return a.x < b.x; });
    }
    bool hit(double p_x, double p_z, double p_margin) const {
        const double reach = max_r + p_margin;
        auto it = std::lower_bound(disks.begin(), disks.end(), p_x - reach, [](const Disk &d, double v) { return d.x < v; });
        for (; it != disks.end() && it->x <= p_x + reach; ++it) {
            const double r = it->r + p_margin;
            const double dx = p_x - it->x, dz = p_z - it->z;
            if (dx * dx + dz * dz <= r * r) {
                return true;
            }
        }
        return false;
    }
};

// 前后左右 3 m 的高差 / 6 m；取不到（崖边）返回 false
bool grade_at(const WorldTerrain &p_terrain, double p_x, double p_z, double &r_grade) {
    double e = 0.0, w = 0.0, n = 0.0, so = 0.0;
    if (!p_terrain.surface(p_x + SLOPE_ARM_M, p_z, e) || !p_terrain.surface(p_x - SLOPE_ARM_M, p_z, w) || !p_terrain.surface(p_x, p_z - SLOPE_ARM_M, n) ||
            !p_terrain.surface(p_x, p_z + SLOPE_ARM_M, so)) {
        return false;
    }
    r_grade = std::max(std::abs(e - w), std::abs(n - so)) / (2.0 * SLOPE_ARM_M);
    return true;
}

bool spot_ok(const WorldTerrain &p_terrain, const VillageDisks &p_villages, double p_x, double p_z, const ResourceSiteRules &p_rules, const char **r_why) {
    const char *why = nullptr;
    const TerrainSample s = p_terrain.sample(p_x, p_z);
    const WorldGround ground(p_terrain, 0.0, 0.0);
    double grade = 0.0;
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
    } else if (p_villages.hit(p_x, p_z, p_rules.village_margin_m)) {
        why = "在村子里";
    } else if (!grade_at(p_terrain, p_x, p_z, grade)) {
        why = "离崖边太近";
    } else if (grade > p_rules.max_grade) {
        why = "太陡";
    }
    if (r_why) {
        *r_why = why;
    }
    return why == nullptr;
}

double reach(const WorldTerrain &p_terrain, const VillageDisks &p_villages, double p_x, double p_z, const ResourceSiteRules &p_rules, double &r_x, double &r_z,
        double &r_yaw) {
    const ShipLayout &ship = resource_reach_ship();
    const WorldLandingGround land(p_terrain);
    const LandingRules landing;
    for (double r = 20.0; r <= p_rules.reach_m + 1e-9; r += 10.0) {
        for (int a = 0; a < 12; a++) {
            const double x = p_x + r * std::cos(a * TAU / 12.0), z = p_z + r * std::sin(a * TAU / 12.0);
            double y = 0.0;
            if (p_villages.hit(x, z, 10.0) || !land.height(x, z, y)) {
                continue;
            }
            for (double yaw : { 0.0, 0.25 * TAU }) {
                if (landing_corners_ok(land, ship, x, z, yaw, landing) && plan_wild_landing(land, ship, x, y + 20.0, z, yaw, landing, p_x, p_z).ok) {
                    r_x = x, r_z = z, r_yaw = yaw;
                    return r;
                }
            }
        }
    }
    return -1.0;
}

// 一处采场找采掘面与堆场（与别处无关，可以并行）
struct Placement {
    bool found = false, reached = false;
    ResourceSite site; // 只填了采掘面、堆场、泊地
};

Placement place_working(const WorldTerrain &p_terrain, const VillageDisks &p_villages, const ResourceWorking &p_working, const ResourceSiteRules &p_rules) {
    // 采掘面：采场格心起一圈圈往外找，旁边还得放得下堆场；还得够得着船（reach_m 内野泊得下）。
    // 最近的那处够不着就接着往外找——山里的采石场常在陡坡上，赋存是一大片，采掘面挪到船落得下的那一边；
    // 试了 reach_tries 处都够不着，就用最近的那处（体检报出来）
    Placement out;
    std::vector<std::pair<double, double>> missed; // 试过、够不着船的采掘面
    int tries = 0;
    bool done = false;
    for (double r = 0.0; r <= p_rules.search_m + 1e-9 && !done; r += p_rules.step_m) {
        const int count = r <= 0.0 ? 1 : std::max(6, int(std::lround(TAU * r / p_rules.step_m)));
        for (int a = 0; a < count && !done; a++) {
            const double ang = TAU * a / count;
            const double fx = p_working.x + r * std::cos(ang), fz = p_working.z + r * std::sin(ang);
            if (!spot_ok(p_terrain, p_villages, fx, fz, p_rules, nullptr)) {
                continue;
            }
            double px = 0.0, pz = 0.0;
            bool pile = false;
            for (int b = 0; b < 8 && !pile; b++) {
                const double pang = TAU * b / 8.0;
                px = fx + p_rules.pile_m * std::cos(pang), pz = fz + p_rules.pile_m * std::sin(pang);
                pile = spot_ok(p_terrain, p_villages, px, pz, p_rules, nullptr);
            }
            if (!pile) {
                continue;
            }
            if (!out.found) {
                out.site.face_x = fx, out.site.face_z = fz;
                out.site.pile_x = px, out.site.pile_z = pz;
                out.found = true;
            }
            bool skip = false;
            for (const auto &m : missed) {
                skip = skip || std::hypot(m.first - fx, m.second - fz) < p_rules.reach_skip_m;
            }
            if (skip) {
                continue;
            }
            double lx = 0.0, lz = 0.0, lyaw = 0.0;
            const double got = reach(p_terrain, p_villages, fx, fz, p_rules, lx, lz, lyaw);
            if (got >= 0.0) {
                out.site.face_x = fx, out.site.face_z = fz;
                out.site.pile_x = px, out.site.pile_z = pz;
                out.site.reach_m = got;
                out.site.land_x = lx, out.site.land_z = lz, out.site.land_yaw = lyaw;
                out.reached = true;
                done = true;
            } else {
                missed.emplace_back(fx, fz);
                done = ++tries >= p_rules.reach_tries;
            }
        }
    }
    return out;
}

// 岛底的洞（生成器的「底面洞」）：开在浮岛的底面上，人走不到
bool underside_cave(const ResourceDeposit &p_deposit) {
    return p_deposit.kind == "cave" && p_deposit.subtype.find("底面") != std::string::npos;
}

// 地标找一处摆样子的地：点起一圈圈往外找，有地、不是水、不在坪上村里、离崖边几米、坡不太陡
bool place_landmark(const WorldTerrain &p_terrain, const VillageDisks &p_villages, const ResourceDeposit &p_deposit, const ResourceSiteRules &p_rules, double &r_x,
        double &r_z) {
    const WorldGround ground(p_terrain, 0.0, 0.0);
    for (double r = 0.0; r <= p_rules.landmark_search_m + 1e-9; r += p_rules.step_m) {
        const int count = r <= 0.0 ? 1 : std::max(6, int(std::lround(TAU * r / p_rules.step_m)));
        for (int a = 0; a < count; a++) {
            const double x = p_deposit.x + r * std::cos(TAU * a / count), z = p_deposit.z + r * std::sin(TAU * a / count);
            const TerrainSample s = p_terrain.sample(x, z);
            double grade = 0.0;
            if (!s.land || s.shore < LANDMARK_SHORE_M || ground.water_at(x, z) != WaterKind::NONE || ground.on_dock(x, z, p_rules.dock_margin_m) ||
                    ground.on_yard(x, z, p_rules.dock_margin_m) || p_villages.hit(x, z, LANDMARK_VILLAGE_M) || !grade_at(p_terrain, x, z, grade) ||
                    grade > p_rules.landmark_grade) {
                continue;
            }
            r_x = x, r_z = z;
            return true;
        }
    }
    return false;
}

} // namespace

const ShipLayout &resource_reach_ship() {
    static const ShipLayout ship = layout_from_ids({ "0,0,-2:storage", "0,0,-1:power", "0,0,0:bridge" });
    return ship;
}

double resource_reach(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules) {
    double x = 0.0, z = 0.0, yaw = 0.0;
    return reach(p_terrain, VillageDisks(p_sites), p_x, p_z, p_rules, x, z, yaw);
}

double resource_reach(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules, double &r_x, double &r_z,
        double &r_yaw) {
    return reach(p_terrain, VillageDisks(p_sites), p_x, p_z, p_rules, r_x, r_z, r_yaw);
}

const ResourceDeposit *ResourceData::deposit_by_id(int p_id) const {
    for (const ResourceDeposit &d : deposits) {
        if (d.id == p_id) {
            return &d;
        }
    }
    return nullptr;
}

const ResourceOccurrence *ResourceData::occurrence_by_id(int p_id) const {
    for (const ResourceOccurrence &o : occurrences) {
        if (o.id == p_id) {
            return &o;
        }
    }
    return nullptr;
}

bool parse_resources(const std::string &p_text, ResourceData &r_data, std::string &r_error) {
    r_data = ResourceData();
    std::istringstream in(p_text);
    std::string line;
    int number = 0;
    auto fail = [&](const char *p_what) {
        r_error = "第 " + std::to_string(number) + " 行：" + p_what;
        return false;
    };
    while (std::getline(in, line)) {
        number++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::vector<std::string> f = split(line, '\t');
        double id = 0.0, island = 0.0, x = 0.0, z = 0.0;
        const bool head = f.size() >= 6 && to_number(f[1], id) && to_number(f[3], island) && to_number(f[4], x) && to_number(f[5], z);
        if (f[0] == "deposit") {
            ResourceDeposit d;
            if (f.size() < 10 || !head || !to_number(f[6], d.area_km2) || (f[9] != "point" && f[9] != "patch")) {
                return fail("deposit 要 10 列（deposit id kind island x z area_km2 grade subtype form），form 是 point / patch");
            }
            d.id = int(id), d.kind = f[2], d.island = int(island), d.x = x, d.z = z;
            d.grade = deposits::grade_of(f[7]);
            d.subtype = f[8];
            d.form = f[9] == "patch" ? DepositForm::PATCH : DepositForm::POINT;
            r_data.deposits.push_back(d);
        } else if (f[0] == "occurrence") {
            ResourceOccurrence o;
            if (f.size() < 11 || !head || !to_number(f[6], o.area_km2) || !to_number(f[9], o.length_km) || !to_number(f[10], o.axis_deg)) {
                return fail("occurrence 要 11 列（occurrence id kind island x z area_km2 grade subtype length_km axis_deg）");
            }
            o.id = int(id), o.kind = f[2], o.island = int(island), o.x = x, o.z = z;
            o.grade = deposits::grade_of(f[7]);
            o.subtype = f[8];
            r_data.occurrences.push_back(o);
        } else if (f[0] == "working") {
            ResourceWorking w;
            double occurrence = 0.0;
            if (f.size() < 10 || !head || !to_number(f[6], occurrence) || !to_number(f[7], w.grade)) {
                return fail("working 要 10 列（working id kind island x z occurrence grade subtype villages）");
            }
            w.id = int(id), w.kind = f[2], w.island = int(island), w.x = x, w.z = z;
            w.occurrence = int(occurrence);
            w.subtype = f[8];
            if (f[9] != "-") {
                for (const std::string &v : split(f[9], ',')) {
                    double vid = 0.0;
                    if (!to_number(v, vid)) {
                        return fail("working 的 villages 是逗号分隔的村号，没有写 -");
                    }
                    w.villages.push_back(int(vid));
                }
            }
            r_data.workings.push_back(w);
        }
        // 将来多出来的行类型：不认得就跳过
    }
    return true;
}

bool resource_spot_ok(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules, const char **r_why) {
    return spot_ok(p_terrain, VillageDisks(p_sites), p_x, p_z, p_rules, r_why);
}

ResourcePlan plan_resource_sites(const WorldTerrain &p_terrain, const WorldSites &p_sites, const ResourceData &p_data, const ResourceSiteRules &p_rules) {
    ResourcePlan plan;
    const VillageDisks villages(p_sites);
    // 各处找采掘面、地标找地互不相干，分给几条线程并行找（找完再按号定并不并，所以结果与线程数无关）
    const std::vector<ResourceWorking> &workings = p_data.workings;
    const std::vector<ResourceDeposit> &points = p_data.deposits;
    std::vector<Placement> placed(workings.size());
    std::vector<char> landmark_ok(points.size(), 0);
    std::vector<std::pair<double, double>> landmark_at(points.size());
    std::atomic<size_t> next{ 0 };
    const size_t total = workings.size() + points.size();
    auto work = [&]() {
        for (size_t k = next++; k < total; k = next++) {
            if (k < workings.size()) {
                const DepositKind *kind = deposits::kind(workings[k].kind);
                if (kind && deposits::has_working(*kind)) {
                    placed[k] = place_working(p_terrain, villages, workings[k], p_rules);
                }
            } else {
                const size_t d = k - workings.size();
                const DepositKind *kind = deposits::kind(points[d].kind);
                if (kind && deposits::is_landmark(*kind) && !underside_cave(points[d])) {
                    landmark_ok[d] = place_landmark(p_terrain, villages, points[d], p_rules, landmark_at[d].first, landmark_at[d].second) ? 1 : 0;
                }
            }
        }
    };
    const unsigned threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; t++) {
        pool.emplace_back(work);
    }
    work();
    for (std::thread &t : pool) {
        t.join();
    }

    std::vector<std::pair<double, double>> kept; // 已落地的采场（并掉更晚的近邻按它量）
    for (size_t k = 0; k < workings.size(); k++) {
        const ResourceWorking &w = workings[k];
        const DepositKind *kind = deposits::kind(w.kind);
        if (!kind || !deposits::has_working(*kind)) {
            continue;
        }
        bool near = false;
        for (const auto &p : kept) {
            near = near || std::hypot(p.first - w.x, p.second - w.z) < p_rules.merge_m;
        }
        if (near) {
            plan.merged++;
            continue;
        }
        if (!placed[k].found) {
            if (villages.hit(w.x, w.z, p_rules.village_margin_m)) {
                plan.in_village++;
            } else {
                plan.failed.push_back(w.id);
            }
            continue;
        }
        if (!placed[k].reached) {
            plan.unreachable.push_back(w.id);
        }
        ResourceSite site = placed[k].site;
        p_terrain.surface(site.face_x, site.face_z, site.face_y);
        p_terrain.surface(site.pile_x, site.pile_z, site.pile_y);
        const ResourceOccurrence *occurrence = p_data.occurrence_by_id(w.occurrence);
        site.working = int(k);
        site.id = w.id;
        site.kind = kind;
        site.subtype = w.subtype;
        site.item = deposits::product_of(*kind, w.subtype);
        site.name = deposits::site_name(*kind, w.subtype);
        site.island = w.island;
        site.grade = occurrence ? occurrence->grade : deposits::GRADE_MID;
        site.grade_value = w.grade;
        site.cap = deposits::working_cap(*kind, w.grade);
        site.villages = w.villages;
        plan.sites.push_back(site);
        kept.emplace_back(w.x, w.z);
    }

    for (size_t d = 0; d < points.size(); d++) {
        const DepositKind *kind = deposits::kind(points[d].kind);
        if (!kind || !deposits::is_landmark(*kind)) {
            continue;
        }
        if (underside_cave(points[d])) {
            plan.underside++;
            continue;
        }
        if (!landmark_ok[d]) {
            plan.landmarks_failed.push_back(points[d].id);
            continue;
        }
        ResourceLandmark mark;
        mark.deposit = int(d);
        mark.id = points[d].id;
        mark.kind = kind;
        mark.subtype = points[d].subtype;
        mark.name = deposits::site_name(*kind, points[d].subtype);
        mark.island = points[d].island;
        mark.x = landmark_at[d].first;
        mark.z = landmark_at[d].second;
        p_terrain.surface(mark.x, mark.z, mark.y);
        plan.landmarks.push_back(mark);
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
