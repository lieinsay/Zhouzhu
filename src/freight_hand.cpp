#include "freight_hand.h"

#include "crops.h"

using namespace godot;

namespace {

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

String item_name(const std::string &p_item) {
    return utf8(catalog::item_name(p_item));
}

} // namespace

String FreightHand::port_name(const MapHost *p_map, int p_port) const {
    const std::vector<FreightPort> &ports = p_map->map_freight_ports();
    if (p_port < 0 || p_port >= int(ports.size())) {
        return String();
    }
    const FreightPort &p = ports[size_t(p_port)];
    return vformat(String(U"%s（岛 #%d）"), utf8(p.name), p.island);
}

void FreightHand::look_around(MapHost *p_map, Ship *p_ship, Player *p_player) {
    const Vector3 at = p_player->get_global_position();
    post = p_map->map_freight_post_near(at, post_range);
    offer = FreightOrder();
    offer_why.clear();
    if (post.port >= 0 && !book.is_active()) {
        if (post.store) {
            offer_freight(p_map->map_freight_ports(), post.port, *post.store, post.specialty, rules, offer, offer_why);
        } else {
            offer_why = "村仓的账还没开，等一会儿";
        }
    }
    slot = -1;
    if (p_ship) {
        // 船改过：货位数跟上（改船前船上已经卸空了，见编排）
        if (int(book.get_slots().size()) != p_ship->get_hold_slot_count()) {
            book.set_slot_count(p_ship->get_hold_slot_count());
        }
        slot = p_ship->find_hold_slot(at, slot_range);
    }
}

void FreightHand::sync_player(Player *p_player) const {
    float r = 0, g = 0, b = 0;
    freight::crate_color(book.get_hand().item, r, g, b);
    p_player->set_carrying(book.is_holding(), Color(r, g, b));
}

bool FreightHand::step(MapHost *p_map, Ship *p_ship, Player *p_player, PackHand &p_pack, bool p_interact, const Say &p_say) {
    look_around(p_map, p_ship, p_player);
    if (!p_interact) {
        return false;
    }
    // 货位：抱着就放上去，空手就抱起来
    if (slot >= 0) {
        const bool filled = !book.get_slots()[size_t(slot)].empty();
        if (book.is_holding() || filled) {
            const std::string problem = book.is_holding() ? book.stow(slot) : book.lift(slot);
            if (!problem.empty()) {
                p_say(utf8(problem));
                return true;
            }
            const Crate &crate = book.get_slots()[size_t(slot)];
            p_ship->set_cargo(slot, utf8(crate.item));
            sync_player(p_player);
            p_say(book.is_holding() ? String(U"抱起一箱") : vformat(String(U"放上货位（船上 %d 箱）"), book.aboard()));
            return true;
        }
    }
    if (post.port < 0) {
        return false;
    }
    // 村仓：交货、打包、放回
    if (!post.store) {
        p_say(utf8(offer_why));
        return true;
    }
    if (book.is_holding() && book.is_active() && post.port == book.get_order().to) {
        const FreightOrder order = book.get_order();
        bool done = false;
        const std::string problem = book.deliver(post.port, *post.store, p_pack.items(), done);
        if (!problem.empty()) {
            p_say(utf8(problem));
            return true;
        }
        sync_player(p_player);
        p_pack.refresh_panel();
        p_say(done ? vformat(String(U"交齐了：%s %d 箱进了%s的村仓，得运费 %d 文"), item_name(order.item), order.crates, post.name, order.fee)
                   : vformat(String(U"交了一箱（%d / %d）"), book.get_delivered(), order.crates));
        return true;
    }
    const bool was_active = book.is_active(), was_holding = book.is_holding();
    const std::string problem = book.pack(post.port, offer, *post.store);
    if (!problem.empty()) {
        p_say(utf8(problem == "这里眼下没有托运" && !offer_why.empty() ? offer_why : problem));
        return true;
    }
    sync_player(p_player);
    const FreightOrder &order = book.get_order();
    if (was_holding) {
        p_say(book.is_active() ? String(U"放回村仓了") : String(U"放回村仓了，这单退了"));
    } else if (!was_active) {
        p_say(vformat(String(U"接下托运：%s %d 箱送%s，打包了第一箱"), item_name(order.item), order.crates, port_name(p_map, order.to)));
    } else {
        p_say(vformat(String(U"打包了第 %d 箱（共 %d 箱）"), book.get_packed(), order.crates));
    }
    return true;
}

String FreightHand::prompt(const MapHost *p_map) const {
    if (slot >= 0) {
        const bool filled = !book.get_slots()[size_t(slot)].empty();
        if (book.is_holding()) {
            return filled ? String(U"这个货位上已经有一箱了") : String(U"按 E 把货箱放上货位");
        }
        if (filled) {
            return String(U"按 E 抱起这箱");
        }
    }
    if (post.port < 0) {
        return book.is_holding() ? String(U"抱着货箱 · 慢走，不能跑跳") : String();
    }
    const String here = String(U"村仓 · ") + post.name + String(U" · ");
    if (book.is_active()) {
        const FreightOrder &order = book.get_order();
        if (post.port == order.to) {
            return here + (book.is_holding() ? vformat(String(U"按 E 交货（%s %d 份）"), item_name(order.item), order.per_crate) : String(U"把船上的货箱抱来这里交"));
        }
        if (post.port == order.from) {
            if (book.is_holding()) {
                return here + String(U"按 E 把这箱放回村仓");
            }
            if (book.get_packed() < order.crates) {
                return here + vformat(String(U"按 E 再打包一箱（还剩 %d 箱）"), order.crates - book.get_packed());
            }
            return here + String(U"这单都打包了，装船送去") + port_name(p_map, order.to);
        }
        return here + String(U"手上还有一单没送完");
    }
    if (book.is_holding()) {
        return here + String(U"手上已经抱着一箱了");
    }
    if (!offer.valid()) {
        return here + utf8(offer_why);
    }
    return here + vformat(String(U"托运 %s %d 箱（每箱 %d 份）到%s，%s km，运费 %d 文 · 按 E 接下并打包一箱"), item_name(offer.item), offer.crates, offer.per_crate,
                          port_name(p_map, offer.to), String::num(offer.km, 1), offer.fee);
}

String FreightHand::status_text(const MapHost *p_map) const {
    if (!book.is_active()) {
        return book.is_holding() ? String(U"抱着一箱") : String();
    }
    const FreightOrder &order = book.get_order();
    return vformat(String(U"托运 %s %d 箱 → %s · 手上 %d · 船上 %d · 已交 %d · 运费 %d 文"), item_name(order.item), order.crates, port_name(p_map, order.to),
            book.is_holding() ? 1 : 0, book.aboard(), book.get_delivered(), order.fee);
}
