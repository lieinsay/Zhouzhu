#pragma once

#include <godot_cpp/variant/string.hpp>

#include "freight.h"
#include "map_host.h"
#include "pack_hand.h"
#include "play_notice.h"
#include "player.h"
#include "ship.h"

// 跑货：村仓（船台的跳板脚前）接委托、打包一箱抱起来，走上跳板放到货位上；飞到目的村的船台，从货位上抱起来，走下跳板交货，交齐了拿运费。
// 与场景无关的玩法部件（不是节点），编排持有、步行时每帧推一下。账在货箱账（CargoBook，纯数据）里；
// 村仓在哪、仓账是哪本由地图宿主答（map_freight_post_near），货位在哪、画不画箱子问船，抱着箱子走得慢由 Player 管。
// 按 E 的先后：够得着村仓或货位、这一下又有事可做，就归它；否则编排再问画样台、舵、农事。
class FreightHand {
    CargoBook book;
    FreightRules rules;
    FreightPost post; // 这一帧够得着的村仓（port = −1：没有）
    FreightOrder offer; // 这处村仓眼下开得出的一单（没有时 why 说为什么）
    std::string offer_why;
    int slot = -1; // 这一帧够得着的货位
    double post_range = 2.5; // 离村仓门前多近算够得着
    double slot_range = 1.6; // 离货位中心多近

    void look_around(MapHost *p_map, Ship *p_ship, Player *p_player);
    void sync_player(Player *p_player) const;

public:
    // 每个物理帧（步行、没开面板时）。按了 E 且被它用了返回 true（编排就别再拿这一下 E 干别的）
    bool step(MapHost *p_map, Ship *p_ship, Player *p_player, PackHand &p_pack, bool p_interact, const Say &p_say);

    bool is_holding() const { return book.is_holding(); }
    int cargo_aboard() const { return book.aboard(); }
    bool is_active() const { return book.is_active(); }
    const CargoBook &get_book() const { return book; }
    // 这单要送去的村（map_freight_ports 的下标）；没接单为 −1
    int destination_port() const { return book.is_active() ? book.get_order().to : -1; }
    godot::String port_name(const MapHost *p_map, int p_port) const;

    // 底部提示：够得着村仓 / 货位时能干什么；无事可说返回空
    godot::String prompt(const MapHost *p_map) const;
    // HUD：「托运 麻布 2 箱 → 村127（岛 #4）· 手上 1 · 船上 1 · 已交 0 · 运费 30 文」；没接单时空
    godot::String status_text(const MapHost *p_map) const;
};
