#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dock_site.h"
#include "inventory.h"
#include "settlement_model.h"
#include "world_sites.h"

// 跑货：港口村、村仓、土产、托运委托与货箱账。不含 Godot 头；除了货箱账（CargoBook，纯数据）都是无状态的函数。
//
// 港口村：离最近的码头不超过 port_radius_m 的村（不算散户）；货从这个码头上下船。
// 村仓：村子的仓只是账本里的一个数（VillageState::store），普通村子没有仓房——有仓房就是仓房，
//       没有就是离村心最近的那户民居代管；托运在它门前接、在它门前交。
// 土产：每村一样（麻布 / 陶罐 / 漆器，按种子挑），开账时按户数记进仓账；托运的就是它，不动村里的粮。
// 委托：从一个港口村运到最近的另一座岛上的港口村，几箱、每箱几份、运费多少。
// 货箱账：玩家接下的那一单、手上抱着的一箱、船上每个货位上的箱子；打包从起运村的仓里扣、交货进目的村的仓、交齐了付运费。
// 动作都返回空串表示做了，否则是不做的原因（给玩家看的话）。

struct FreightRules {
    double port_radius_m = 1500.0; // 村心离码头的生成器点位多近算港口村
    double max_route_m = 40000.0; // 委托最远开到多远的港口
    int crates = 2; // 一单几箱
    int per_crate = 5; // 一箱几份
    int fee_base = 8; // 运费：每箱 fee_base + fee_per_km × 航程
    double fee_per_km = 1.0;
    int specialty_per_house = 2; // 开账时土产每户几份
};

struct FreightPort {
    int site = -1; // sites.settlements 下标
    int site_id = 0; // 村号
    std::string name; // UTF-8
    int island = 0;
    int dock = -1; // sites.docks 下标（= 地图的码头下标）
    int dock_id = 0;
    double dock_x = 0.0, dock_z = 0.0; // 码头的岸缘点
    double x = 0.0, z = 0.0; // 村心
};

namespace freight {

// 这处村落的土产（物品 id）；p_seed 是岛群铺村的种子
const char *specialty_of(uint64_t p_seed, int p_site_id, bool p_hamlet);
int specialty_stock(int p_households, const FreightRules &p_rules);

} // namespace freight

// 所有港口村（按 sites.settlements 的顺序）。p_docks 与 sites.docks 一一对应（找不到岸缘的码头不算）
std::vector<FreightPort> plan_freight_ports(const WorldSites &p_sites, const std::vector<DockPlan> &p_docks, const FreightRules &p_rules);
int find_port(const std::vector<FreightPort> &p_ports, int p_site); // 按 sites.settlements 下标找；不是港口 −1

// 村仓是哪一座（清单下标）：有仓房取离 (p_cx, p_cz) 最近的仓房，否则取最近的民居；都没有 −1
int freight_post_building(const SettlementLayout &p_layout, double p_cx, double p_cz);
// 村仓门前：人站着接货交货的那一点，和门边那堆货箱摆在哪、朝哪
void freight_post_spot(const Building &p_building, double &r_x, double &r_z, double &r_pile_x, double &r_pile_z, double &r_pile_yaw);

struct FreightOrder {
    int from = -1, to = -1; // 港口下标
    std::string item;
    int crates = 0;
    int per_crate = 0;
    int fee = 0; // 铜钱
    double km = 0.0; // 两个码头之间的直线
    bool valid() const { return from >= 0 && to >= 0 && crates > 0; }
};

// 在 p_from 开一单：运 p_item 到最近的别岛港口村（max_route_m 内）；仓里不够 crates 箱就少开几箱。
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

    // 在港口 p_port 的村仓前按 E：
    //   手上抱着这单的箱子、又在起运村 → 放回仓里（一箱都不剩在外面、也没交过，这单就退了）；
    //   没接单 → 接下 p_offer 并打包一箱；接了这单、在起运村、还有没打包的 → 再打包一箱。打包从 r_store 扣货。
    std::string pack(int p_port, const FreightOrder &p_offer, Inventory &r_store);
    // 在目的村的村仓前交手上那一箱：进 r_store；交齐了把运费放进 r_purse、这单结了（r_done = true）
    std::string deliver(int p_port, Inventory &r_store, Inventory &r_purse, bool &r_done);
    // 船上货位：手上的箱子放到第 p_slot 个货位 / 从那儿抱起来
    std::string stow(int p_slot);
    std::string lift(int p_slot);
};
