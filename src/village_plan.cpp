#include "village_plan.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "rng.h"

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double BUCKET_M = 40.0; // 邻居查找的桶：比最大的建筑外接圆直径（堂屋约 19 m）加空隙还大
constexpr double SLOPE_PROBE_M = 4.0;

struct Candidate {
    double x = 0.0, z = 0.0;
    double distance = 0.0;
    int order = 0;
};

int64_t bucket_key(int p_i, int p_j) {
    return (int64_t(p_i) << 32) ^ int64_t(uint32_t(p_j));
}

int bucket_of(double p_v) {
    return int(std::floor(p_v / BUCKET_M));
}

} // namespace

VillagePlan plan_village(const SiteGround &p_ground, const VillageRequest &p_request, const SiteRules &p_site_rules, const VillageRules &p_rules) {
    VillagePlan plan;
    std::vector<const BuildingDef *> queue;
    if (p_request.seat) {
        queue.push_back(buildings::by_id("hall"));
        queue.push_back(buildings::by_id("granary"));
    }
    const BuildingDef *house = buildings::by_id("house");
    for (int k = 0; k < std::max(0, p_request.households); k++) {
        queue.push_back(house);
    }
    queue.erase(std::remove(queue.begin(), queue.end(), nullptr), queue.end());
    plan.wanted = int(queue.size());
    if (queue.empty()) {
        return plan;
    }

    // 格点：按种子转一个角度，每点抖一抖，由近及远
    Rng rng(p_request.seed * 0x9E3779B97F4A7C15ull + 0x5851F42D4C957F2Dull);
    const double angle = rng.uniform(0.0, 0.5 * PI);
    const double ca = std::cos(angle), sa = std::sin(angle);
    const int reach = int(std::ceil(p_rules.max_radius_m / p_rules.spacing_m)) + 1;
    std::vector<Candidate> candidates;
    for (int i = -reach; i <= reach; i++) {
        for (int j = -reach; j <= reach; j++) {
            const double lu = j * p_rules.spacing_m + rng.uniform(-p_rules.jitter_m, p_rules.jitter_m);
            const double lv = i * p_rules.spacing_m + rng.uniform(-p_rules.jitter_m, p_rules.jitter_m);
            // 村心那一格不抖：头一座（邑治的堂屋）正落在生成器定的点上
            const double u = (i == 0 && j == 0) ? 0.0 : lu, v = (i == 0 && j == 0) ? 0.0 : lv;
            Candidate c;
            c.x = p_request.x + u * ca - v * sa;
            c.z = p_request.z + u * sa + v * ca;
            c.distance = std::hypot(u, v);
            c.order = int(candidates.size());
            if (c.distance <= p_rules.max_radius_m) {
                candidates.push_back(c);
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.distance != b.distance ? a.distance < b.distance : a.order < b.order;
    });

    std::unordered_map<int64_t, std::vector<int>> buckets; // 已摆下的建筑按桶
    size_t next = 0;
    for (const Candidate &c : candidates) {
        if (next >= queue.size()) {
            break;
        }
        const BuildingDef &def = *queue[next];
        double h = 0.0, hx0 = 0.0, hx1 = 0.0, hz0 = 0.0, hz1 = 0.0;
        if (!p_ground.site_height(c.x, c.z, h) || !p_ground.site_height(c.x - SLOPE_PROBE_M, c.z, hx0) || !p_ground.site_height(c.x + SLOPE_PROBE_M, c.z, hx1) ||
                !p_ground.site_height(c.x, c.z - SLOPE_PROBE_M, hz0) || !p_ground.site_height(c.x, c.z + SLOPE_PROBE_M, hz1)) {
            continue;
        }
        const double gx = (hx1 - hx0) / (2.0 * SLOPE_PROBE_M), gz = (hz1 - hz0) / (2.0 * SLOPE_PROBE_M);
        double yaw = 0.0; // 门朝 (sin yaw, cos yaw)
        if (std::hypot(gx, gz) >= p_rules.flat_grade) {
            yaw = std::atan2(-gx, -gz); // 门朝下坡：面阔顺着等高线
        } else if (c.distance > 1.0) {
            yaw = std::atan2(p_request.x - c.x, p_request.z - c.z); // 平地门朝村心
        } else {
            yaw = angle;
        }

        // 只拿附近的建筑来比挨没挨着
        SettlementLayout neighbours;
        const int bi = bucket_of(c.z), bj = bucket_of(c.x);
        for (int di = -1; di <= 1; di++) {
            for (int dj = -1; dj <= 1; dj++) {
                const auto found = buckets.find(bucket_key(bi + di, bj + dj));
                if (found != buckets.end()) {
                    for (int index : found->second) {
                        neighbours.add(plan.layout.at(index));
                    }
                }
            }
        }
        for (double turn : { 0.0, 0.5 * PI }) {
            const double try_yaw = std::fmod(yaw + turn + 4.0 * PI, 2.0 * PI);
            if (!survey_site(p_ground, def, c.x, c.z, try_yaw, neighbours, p_site_rules).ok()) {
                continue;
            }
            buckets[bucket_key(bi, bj)].push_back(plan.layout.size());
            plan.layout.add(Building{ def.id, c.x, c.z, try_yaw });
            plan.radius = std::max(plan.radius, std::hypot(c.x - p_request.x, c.z - p_request.z) + 0.5 * std::hypot(def.width_m, def.depth_m));
            next++;
            break;
        }
    }
    plan.missing = int(queue.size() - next);
    return plan;
}
