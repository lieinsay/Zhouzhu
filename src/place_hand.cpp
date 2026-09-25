#include "place_hand.h"

#include <cmath>

#include <godot_cpp/classes/input.hpp>

#include "crops.h"
#include "input_actions.h"
#include "settlement.h"
#include "ship.h"

using namespace godot;

namespace {

constexpr double MAX_STEP_M = 1.6; // 虚影落处的地面比脚下高 / 低过这么多就够不着（人站在栈桥、屋里）
constexpr double SITE_CLEAR_M = 3.0; // 资源点的采掘面、堆场周围这么宽留着（人站着干活、打箱；堆场的货箱只是摆设、没碰撞）

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

// 碰着的实物是什么：房子、船，还是别的
String obstacle_name(Object *p_hit) {
    for (Node *node = Object::cast_to<Node>(p_hit); node; node = node->get_parent()) {
        if (Object::cast_to<Settlement>(node)) {
            return String(U"房子挡着");
        }
        if (Object::cast_to<Ship>(node)) {
            return String(U"船挡着");
        }
    }
    return String(U"有东西挡着");
}

} // namespace

void PlaceHand::setup(PlacedProps *p_view) {
    view = p_view;
    placed = Placements();
    view->sync(placed);
}

void PlaceHand::stand_down() {
    ghost_on = false;
    ghost_item.clear();
    if (view) {
        view->hide_ghost();
    }
}

void PlaceHand::update_ghost(MapHost *p_map, Player *p_player, const PropDef &p_def, bool p_on_ship) {
    const Vector3 at = p_player->get_global_position();
    ghost = plan_placement(MapLandingGround(p_map), p_def, at.x, at.z, p_player->get_look_yaw(), turns, placed, rules);
    if (ghost.ok && p_on_ship) {
        ghost.ok = false;
        ghost.why = "船上摆不了：下船摆在地上";
    } else if (ghost.ok && std::abs(ghost.y - at.y) > MAX_STEP_M) {
        ghost.ok = false;
        ghost.why = "够不着那儿的地";
    }
    if (ghost.ok) {
        double hx = 0.0, hz = 0.0;
        prop_half_extent(p_def, ghost.quarter, hx, hz);
        const double reach = SITE_CLEAR_M + std::max(hx, hz);
        const Vector3 center(real_t(ghost.x), real_t(ghost.y), real_t(ghost.z));
        const ResourceSpot spot = p_map->map_resource_near(center, reach);
        if (spot.site >= 0) {
            const bool pile = Vector2(center.x - spot.pile.x, center.z - spot.pile.z).length() <= reach;
            ghost.ok = false;
            ghost.why = pile ? "这是资源点的堆场，别占着" : "这是资源点的采掘面，别占着";
        }
    }
    if (ghost.ok) {
        TypedArray<RID> exclude;
        exclude.push_back(p_player->get_rid());
        if (Object *hit = view->obstacle(p_def, ghost, exclude)) {
            ghost.ok = false;
            ghost.why = play::to_std(obstacle_name(hit));
        }
    }
    ghost_on = true;
    ghost_item = p_def.item;
    view->show_ghost(ghost_item, ghost);
}

void PlaceHand::look(MapHost *p_map, Player *p_player, PackHand &p_pack, bool p_on_ship) {
    const Vector3 at = p_player->get_global_position();
    facing_id = placed.facing(at.x, at.y, at.z, p_player->get_look_yaw(), rules);
    const PropDef *holding = props::find(p_pack.held_item());
    if (holding) {
        Input *input = Input::get_singleton();
        turns += input->is_action_just_pressed(input_actions::ROTATE_RIGHT) ? 1 : 0;
        turns -= input->is_action_just_pressed(input_actions::ROTATE_LEFT) ? 1 : 0;
        turns = ((turns % 4) + 4) % 4;
        update_ghost(p_map, p_player, *holding, p_on_ship);
    } else if (ghost_on) {
        stand_down();
    }
    view->sync(placed);
}

bool PlaceHand::claims(const PackHand &p_pack, bool p_interact, bool p_remove) const {
    return (p_remove && facing_id >= 0) || (p_interact && ((facing_id >= 0 && is_openable(facing_id)) || props::placeable(p_pack.held_item())));
}

bool PlaceHand::engaged() const {
    return ghost_on || (facing_id >= 0 && is_openable(facing_id));
}

PlaceHand::Step PlaceHand::act(PackHand &p_pack, bool p_interact, bool p_remove, const Say &p_say) {
    Step out;
    const PropDef *holding = props::find(p_pack.held_item());
    if (p_remove && facing_id >= 0) {
        out.used = true;
        const PlacedProp *prop = placed.find(facing_id);
        const std::string problem = pickup_problem(*prop);
        if (!problem.empty()) {
            p_say(utf8(problem));
        } else if (p_pack.items().room_for(prop->item) < 1) {
            p_say(String(U"背包满了，收不回来"));
        } else {
            const std::string item = prop->item;
            placed.remove(facing_id);
            p_pack.items().put(item, 1);
            p_pack.refresh_bar();
            facing_id = -1;
            p_say(String(U"收起了") + utf8(catalog::item_name(item)));
        }
    } else if (p_interact && facing_id >= 0 && is_openable(facing_id) && !(holding && ghost_on && ghost.ok)) {
        out.used = true; // 虚影绿着就是要摆；不然看着木箱 / 工作台 / 窑，E 是打开
        out.open = facing_id;
    } else if (p_interact && holding) {
        out.used = true;
        if (!ghost_on || ghost_item != holding->item) {
            p_say(String(U"摆不了：") + utf8(catalog::item_name(holding->item)) + String(U"只能摆在地上"));
        } else if (!ghost.ok) {
            p_say(String(U"摆不了：") + utf8(ghost.why));
        } else if (p_pack.items().take_at(p_pack.get_selected(), 1) == 1) {
            placed.add(holding->item, ghost);
            p_pack.refresh_bar();
            p_say(String(U"摆下了") + utf8(catalog::item_name(holding->item)));
            if (!props::find(p_pack.held_item())) {
                stand_down(); // 手上那格用完了
            }
        }
    }
    view->sync(placed);
    return out;
}

unsigned PlaceHand::stations_near(const Vector3 &p_at) const {
    return placed.stations_near(p_at.x, p_at.y, p_at.z, rules.station_range_m);
}

ItemSlots *PlaceHand::container(int p_id) {
    PlacedProp *prop = placed.find(p_id);
    return prop && prop->contents.size() > 0 ? &prop->contents : nullptr;
}

bool PlaceHand::is_openable(int p_id) const {
    const PlacedProp *prop = placed.find(p_id);
    const PropDef *def = prop ? props::find(prop->item) : nullptr;
    return def && (def->slots > 0 || def->station >= 0);
}

String PlaceHand::title_of(int p_id) const {
    const PlacedProp *prop = placed.find(p_id);
    if (!prop) {
        return String();
    }
    const String name = utf8(catalog::item_name(prop->item));
    return prop->contents.size() > 0 ? name + vformat(String(U"（%d 格）"), prop->contents.size()) : name;
}

String PlaceHand::prompt(const PackHand &p_pack) const {
    const PlacedProp *prop = facing_id >= 0 ? placed.find(facing_id) : nullptr;
    const PropDef *def = prop ? props::find(prop->item) : nullptr;
    const String name = prop ? utf8(catalog::item_name(prop->item)) : String();
    const String item = utf8(catalog::item_name(ghost_item));
    if (ghost_on && ghost.ok) {
        return String(U"按 E 摆下") + item + String(U" · Q/R 转向");
    }
    if (def && def->slots > 0) {
        return name + vformat(String(U" · 放着 %d / %d 格 · 按 E 打开 · X 收起"), prop->contents.used(), prop->contents.size());
    }
    if (def && def->station >= 0) {
        return name + String(U" · 按 E 打开背包合成（3 m 内都算在它旁边）· X 收起");
    }
    if (ghost_on) {
        return String(U"摆不了") + item + String(U"：") + utf8(ghost.why) + String(U" · Q/R 转向");
    }
    if (def) {
        return name + String(U" · X 收起");
    }
    return String();
}
