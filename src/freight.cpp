#include "freight.h"

#include <algorithm>
#include <cmath>

#include "crops.h"
#include "deposit_kinds.h"

namespace {

const char *SPECIALTIES[] = { "cloth", "pottery", "lacquer" };

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

void crate_color(const std::string &p_item, float &r_r, float &r_g, float &r_b) {
    catalog::item_color(p_item, r_r, r_g, r_b); // 土产、原料、合成的东西：颜色都在物品表里
}

} // namespace freight

std::vector<FreightPort> plan_freight_ports(const WorldSites &p_sites) {
    std::vector<FreightPort> out;
    for (size_t s = 0; s < p_sites.settlements.size(); s++) {
        const SiteSettlement &v = p_sites.settlements[s];
        if (v.hamlet) {
            continue;
        }
        FreightPort p;
        p.site = int(s);
        p.site_id = v.id;
        p.name = v.name;
        p.island = v.island;
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

int freight_post_building(const SettlementLayout &p_layout) {
    const std::vector<Building> &items = p_layout.get_items();
    for (size_t k = 0; k < items.size(); k++) {
        if (items[k].kind == "berth") {
            return int(k);
        }
    }
    return -1;
}

bool offer_freight(const std::vector<FreightPort> &p_ports, int p_from, const Inventory &p_store, const std::string &p_item, const FreightRules &p_rules,
        FreightOrder &r_order, std::string &r_why) {
    r_order = FreightOrder();
    if (p_from < 0 || p_from >= int(p_ports.size())) {
        r_why = "这里不是村子，没人托运";
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
        const double d = std::hypot(p.x - from.x, p.z - from.z);
        if (d <= best) {
            best = d;
            to = int(k);
        }
    }
    if (to < 0) {
        r_why = "近处别的岛上没有村子，托运不出去";
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

int CargoBook::aboard_consigned() const {
    int n = 0;
    for (const Crate &c : slots) {
        n += !c.empty() && c.consigned ? 1 : 0;
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
        if (hand.consigned && active && p_port == order.from) {
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
    hand.consigned = true;
    packed++;
    return std::string();
}

std::string CargoBook::deliver(int p_port, Inventory &r_store, ItemBag &r_purse, bool &r_done) {
    r_done = false;
    if (!is_holding()) {
        return "手上没有货";
    }
    if (!hand.consigned || !active || p_port != order.to) {
        return "这箱不是送这儿的";
    }
    if (delivered + 1 >= order.crates && r_purse.room_for("coin") < order.fee) {
        return "背包满了，放不下运费：先腾出一格";
    }
    r_store.add(hand.item, hand.count);
    hand = Crate();
    delivered++;
    if (delivered >= order.crates) {
        r_purse.put("coin", order.fee);
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

std::string CargoBook::pack_goods(const std::string &p_item, int p_per_crate, ItemBag &r_pack) {
    if (is_holding()) {
        return "手上已经抱着一箱了";
    }
    const int per_crate = std::max(1, p_per_crate);
    if (!r_pack.has(p_item, per_crate)) {
        return "背包里的" + name_of(p_item) + "不够一箱（" + std::to_string(r_pack.count(p_item)) + " / " + std::to_string(per_crate) + " 份）";
    }
    r_pack.take(p_item, per_crate);
    hand.item = p_item;
    hand.count = per_crate;
    hand.consigned = false;
    return std::string();
}

std::string CargoBook::unpack_goods(ItemBag &r_pack) {
    if (!is_holding()) {
        return "手上没有货";
    }
    if (hand.consigned) {
        return "这是托运的货，拆不得";
    }
    if (r_pack.room_for(hand.item) < hand.count) {
        return "背包放不下这箱" + name_of(hand.item) + "：先腾出地方";
    }
    r_pack.put(hand.item, hand.count);
    hand = Crate();
    return std::string();
}
