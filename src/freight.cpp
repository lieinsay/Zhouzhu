#include "freight.h"

#include <algorithm>
#include <cmath>

#include "crops.h"
#include "villager_model.h"

namespace {

const char *SPECIALTIES[] = { "cloth", "pottery", "lacquer" };

constexpr double POST_OUT_M = 2.2; // 村仓门前人站在门外多远（村民在 1.8 m 处进出，让开一点）
constexpr double PILE_SIDE_M = 3.0; // 货箱堆在门边：沿墙挪开多远
constexpr double PILE_OUT_M = 1.2;

std::string name_of(const std::string &p_item) {
    return catalog::item_name(p_item);
}

} // namespace

namespace freight {

const char *specialty_of(uint64_t p_seed, int p_site_id, bool p_hamlet) {
    uint64_t h = p_seed * 0x9E3779B97F4A7C15ull ^ (uint64_t(p_site_id) * 2ull + (p_hamlet ? 1ull : 0ull)) * 0xBF58476D1CE4E5B9ull;
    h ^= h >> 31;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 29;
    return SPECIALTIES[h % (sizeof(SPECIALTIES) / sizeof(SPECIALTIES[0]))];
}

int specialty_stock(int p_households, const FreightRules &p_rules) {
    return std::max(0, p_households) * p_rules.specialty_per_house;
}

} // namespace freight

std::vector<FreightPort> plan_freight_ports(const WorldSites &p_sites, const std::vector<DockPlan> &p_docks, const FreightRules &p_rules) {
    std::vector<FreightPort> out;
    for (size_t s = 0; s < p_sites.settlements.size(); s++) {
        const SiteSettlement &v = p_sites.settlements[s];
        if (v.hamlet) {
            continue;
        }
        int best = -1;
        double best_d = p_rules.port_radius_m;
        for (size_t d = 0; d < p_sites.docks.size() && d < p_docks.size(); d++) {
            if (!p_docks[d].ok || p_sites.docks[d].island != v.island) {
                continue;
            }
            const double dist = std::hypot(p_sites.docks[d].x - v.x, p_sites.docks[d].z - v.z);
            if (dist <= best_d) {
                best_d = dist;
                best = int(d);
            }
        }
        if (best < 0) {
            continue;
        }
        FreightPort p;
        p.site = int(s);
        p.site_id = v.id;
        p.name = v.name;
        p.island = v.island;
        p.dock = best;
        p.dock_id = p_sites.docks[size_t(best)].id;
        p.dock_x = p_docks[size_t(best)].x;
        p.dock_z = p_docks[size_t(best)].z;
        p.x = v.x;
        p.z = v.z;
        out.push_back(p);
    }
    return out;
}

int find_port(const std::vector<FreightPort> &p_ports, int p_site) {
    for (size_t k = 0; k < p_ports.size(); k++) {
        if (p_ports[k].site == p_site) {
            return int(k);
        }
    }
    return -1;
}

int freight_post_building(const SettlementLayout &p_layout, double p_cx, double p_cz) {
    int best = -1;
    int best_rank = 3;
    double best_d = 1e18;
    const std::vector<Building> &items = p_layout.get_items();
    for (size_t k = 0; k < items.size(); k++) {
        const Building &b = items[k];
        const int rank = b.kind == "granary" ? 0 : (b.kind == "house" ? 1 : 3);
        if (rank >= 3) {
            continue;
        }
        const double d = std::hypot(b.x - p_cx, b.z - p_cz);
        if (rank < best_rank || (rank == best_rank && d < best_d)) {
            best_rank = rank;
            best_d = d;
            best = int(k);
        }
    }
    return best;
}

void freight_post_spot(const Building &p_building, double &r_x, double &r_z, double &r_pile_x, double &r_pile_z, double &r_pile_yaw) {
    const NavPoint door = building_door(p_building, POST_OUT_M);
    r_x = door.x;
    r_z = door.z;
    double ux = 0, uz = 0, vx = 0, vz = 0;
    building_axes(p_building.yaw, ux, uz, vx, vz);
    const NavPoint wall = building_door(p_building, PILE_OUT_M);
    r_pile_x = wall.x + ux * PILE_SIDE_M;
    r_pile_z = wall.z + uz * PILE_SIDE_M;
    r_pile_yaw = p_building.yaw;
}

bool offer_freight(const std::vector<FreightPort> &p_ports, int p_from, const Inventory &p_store, const std::string &p_item, const FreightRules &p_rules,
        FreightOrder &r_order, std::string &r_why) {
    r_order = FreightOrder();
    if (p_from < 0 || p_from >= int(p_ports.size())) {
        r_why = "这里不是港口村，没人托运";
        return false;
    }
    const FreightPort &from = p_ports[size_t(p_from)];
    int to = -1;
    double best = p_rules.max_route_m;
    for (size_t k = 0; k < p_ports.size(); k++) {
        const FreightPort &p = p_ports[k];
        if (p.island == from.island) {
            continue;
        }
        const double d = std::hypot(p.dock_x - from.dock_x, p.dock_z - from.dock_z);
        if (d <= best) {
            best = d;
            to = int(k);
        }
    }
    if (to < 0) {
        r_why = "近处别的岛上没有港口村，托运不出去";
        return false;
    }
    const int per_crate = std::max(1, p_rules.per_crate);
    const int crates = std::min(p_rules.crates, p_store.count(p_item) / per_crate);
    if (crates <= 0) {
        r_why = "村仓里的" + name_of(p_item) + "不够一箱，眼下没有托运";
        return false;
    }
    r_order.from = p_from;
    r_order.to = to;
    r_order.item = p_item;
    r_order.crates = crates;
    r_order.per_crate = per_crate;
    r_order.km = best / 1000.0;
    r_order.fee = crates * (p_rules.fee_base + int(std::lround(p_rules.fee_per_km * r_order.km)));
    r_why.clear();
    return true;
}

// ---------------------------------------------------------------- 货箱账

int CargoBook::aboard() const {
    int n = 0;
    for (const Crate &c : slots) {
        n += c.empty() ? 0 : 1;
    }
    return n;
}

int CargoBook::free_slots() const {
    return int(slots.size()) - aboard();
}

bool CargoBook::set_slot_count(int p_count) {
    p_count = std::max(0, p_count);
    for (size_t k = size_t(p_count); k < slots.size(); k++) {
        if (!slots[k].empty()) {
            return false;
        }
    }
    slots.resize(size_t(p_count));
    return true;
}

std::string CargoBook::pack(int p_port, const FreightOrder &p_offer, Inventory &r_store) {
    if (is_holding()) {
        if (active && p_port == order.from) {
            r_store.add(hand.item, hand.count);
            hand = Crate();
            packed--;
            if (packed == 0 && delivered == 0) {
                active = false; // 一箱都没出去：这单退了
            }
            return std::string();
        }
        return "手上已经抱着一箱了";
    }
    if (!active) {
        if (!p_offer.valid() || p_offer.from != p_port) {
            return "这里眼下没有托运";
        }
        if (!r_store.has(p_offer.item, p_offer.per_crate)) {
            return "村仓里的" + name_of(p_offer.item) + "不够一箱";
        }
        order = p_offer;
        active = true;
        packed = 0;
        delivered = 0;
    } else if (p_port != order.from) {
        return "手上还有一单没送完";
    } else if (packed >= order.crates) {
        return "这单的 " + std::to_string(order.crates) + " 箱都打包了";
    }
    if (!r_store.remove(order.item, order.per_crate)) {
        return "村仓里的" + name_of(order.item) + "不够一箱了";
    }
    hand.item = order.item;
    hand.count = order.per_crate;
    packed++;
    return std::string();
}

std::string CargoBook::deliver(int p_port, Inventory &r_store, Inventory &r_purse, bool &r_done) {
    r_done = false;
    if (!is_holding()) {
        return "手上没有货";
    }
    if (!active || p_port != order.to) {
        return "这箱不是送这儿的";
    }
    r_store.add(hand.item, hand.count);
    hand = Crate();
    delivered++;
    if (delivered >= order.crates) {
        r_purse.add("coin", order.fee);
        active = false;
        r_done = true;
    }
    return std::string();
}

std::string CargoBook::stow(int p_slot) {
    if (!is_holding()) {
        return "手上没有货";
    }
    if (p_slot < 0 || p_slot >= int(slots.size())) {
        return "这里不是货位";
    }
    if (!slots[size_t(p_slot)].empty()) {
        return "这个货位上已经有一箱了";
    }
    slots[size_t(p_slot)] = hand;
    hand = Crate();
    return std::string();
}

std::string CargoBook::lift(int p_slot) {
    if (is_holding()) {
        return "手上已经抱着一箱了";
    }
    if (p_slot < 0 || p_slot >= int(slots.size()) || slots[size_t(p_slot)].empty()) {
        return "这个货位是空的";
    }
    hand = slots[size_t(p_slot)];
    slots[size_t(p_slot)] = Crate();
    return std::string();
}
