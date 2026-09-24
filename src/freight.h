#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "inventory.h"
#include "settlement_model.h"
#include "world_sites.h"

// 跑货：村仓、土产、托运委托与货箱账。不含 Godot 头；除了货箱账（CargoBook，纯数据）都是无状态的函数。
//
// 船在村里的船台上落地靠港（land_berth.h），所以托运不看码头：村（不算散户）都能托运、都能收货。
// 村仓：村子的仓只是账本里的一个数（VillageState::store）；托运在船台的跳板脚前接、交（land_berth::POST_Z），货堆摆在跳板脚边。
// 土产：每村一样（麻布 / 陶罐 / 漆器，按种子挑），开账时按户数记进仓账；托运的就是它，不动村里的粮。
// 委托：从一个村运到另一座岛上最近的村，几箱、每箱几份、运费多少。
// 货箱账：玩家接下的那一单、手上抱着的一箱、船上每个货位上的箱子；打包从起运村的仓里扣、交货进目的村的仓、交齐了付运费。
// 动作都返回空串表示做了，否则是不做的原因（给玩家看的话）。

struct FreightRules {
    double max_route_m = 40000.0; // 委托最远开到多远的村
    int crates = 2; // 一单几箱
    int per_crate = 5; // 一箱几份
    int fee_base = 8; // 运费：每箱 fee_base + fee_per_km × 航程
    double fee_per_km = 1.0;
    int specialty_per_house = 2; // 开账时土产每户几份
};

// 能托运的一处（一个村）
struct FreightPort {
    int site = -1; // sites.settlements 下标
    int site_id = 0; // 村号
    std::string name; // UTF-8
    int island = 0;
    double x = 0.0, z = 0.0; // 村心
};

namespace freight {

// 这处村落的土产（物品 id）；p_seed 是岛群铺村的种子
const char *specialty_of(uint64_t p_seed, int p_site_id, bool p_hamlet);
int specialty_stock(int p_households, const FreightRules &p_rules);
// 货箱盖板的颜色（sRGB 0–1）：一样货一种颜色，远远就认得出装的是什么
void crate_color(const std::string &p_item, float &r_r, float &r_g, float &r_b);

} // namespace freight

// 所有能托运的村（按 sites.settlements 的顺序，不含散户）
std::vector<FreightPort> plan_freight_ports(const WorldSites &p_sites);
int find_port(const std::vector<FreightPort> &p_ports, int p_site); // 按 sites.settlements 下标找；不是村 −1

// 村仓在哪座船台上（清单下标，第一座船台）；村里没有船台 −1
int freight_post_building(const SettlementLayout &p_layout);

struct FreightOrder {
    int from = -1, to = -1; // 村的下标（plan_freight_ports）
    std::string item;
    int crates = 0;
    int per_crate = 0;
    int fee = 0; // 铜钱
    double km = 0.0; // 两村村心之间的直线
    bool valid() const { return from >= 0 && to >= 0 && crates > 0; }
};

// 在 p_from 开一单：运 p_item 到别的岛上最近的村（max_route_m 内）；仓里不够 crates 箱就少开几箱。
// 开不出来返回 false，r_why 说为什么
bool offer_freight(const std::vector<FreightPort> &p_ports, int p_from, const Inventory &p_store, const std::string &p_item, const FreightRules &p_rules,
        FreightOrder &r_order, std::string &r_why);

struct Crate {
    std::string item;
    int count = 0;
    bool empty() const { return count <= 0; }
};

class CargoBook {
    FreightOrder order;
    bool active = false;
    int packed = 0; // 打包了几箱（含已交的）
    int delivered = 0;
    Crate hand;
    std::vector<Crate> slots; // 船上的货位；空箱 = 空位

public:
    bool is_active() const { return active; }
    const FreightOrder &get_order() const { return order; }
    int get_packed() const { return packed; }
    int get_delivered() const { return delivered; }
    bool is_holding() const { return !hand.empty(); }
    const Crate &get_hand() const { return hand; }
    const std::vector<Crate> &get_slots() const { return slots; }
    int aboard() const; // 船上几箱
    int free_slots() const;

    // 船改了：货位数跟着变。少了的货位上有箱子就不改、返回 false
    bool set_slot_count(int p_count);

    // 在村 p_port 的村仓前按 E：
    //   手上抱着这单的箱子、又在起运村 → 放回仓里（一箱都不剩在外面、也没交过，这单就退了）；
    //   没接单 → 接下 p_offer 并打包一箱；接了这单、在起运村、还有没打包的 → 再打包一箱。打包从 r_store 扣货。
    std::string pack(int p_port, const FreightOrder &p_offer, Inventory &r_store);
    // 在目的村的村仓前交手上那一箱：进 r_store；交齐了把运费放进 r_purse、这单结了（r_done = true）
    std::string deliver(int p_port, Inventory &r_store, Inventory &r_purse, bool &r_done);
    // 船上货位：手上的箱子放到第 p_slot 个货位 / 从那儿抱起来
    std::string stow(int p_slot);
    std::string lift(int p_slot);
};
