#include "earthworks.h"

#include "terrain_edits.h"

bool earth_digs_with(const std::string &p_item) {
    return p_item == "wood_shovel" || p_item == "stone_pick";
}

bool earth_raises_with(const std::string &p_item) {
    return p_item == "earth";
}

EarthCheck plan_earthwork(const EarthFacts &p_facts, int p_step, EarthAction p_action, const std::string &p_held, bool p_free, const EarthRules &p_rules) {
    EarthCheck out;
    out.before = p_step;
    out.after = p_step + (p_action == EarthAction::DIG ? -1 : 1);
    auto refuse = [&](const std::string &p_why) {
        out.ok = false;
        out.why = p_why;
        return out;
    };
    if (!p_facts.solid) {
        return refuse("崖边上动不了土");
    }
    if (p_facts.shore < p_rules.min_shore_m) {
        return refuse("离岸太近，动了要塌");
    }
    if (!p_facts.blocked.empty()) {
        return refuse(p_facts.blocked);
    }
    if (p_action == EarthAction::DIG) {
        if (p_step <= -p_rules.max_down) {
            return refuse("挖不下去了：最深挖到原地面下 " + std::to_string(int(p_rules.max_down * EditGrid::STEP_M)) + " 米");
        }
        // 这一级：垒上来的土挖回去是土；原地形往下，这一级的顶在土层以下就是石
        out.stone = p_step <= 0 && -p_step * EditGrid::STEP_M >= p_facts.soil_depth - 1e-9;
        out.item = out.stone ? "stone" : "earth";
        if (p_free) {
            out.seconds = p_rules.free_s;
        } else if (out.stone) {
            if (p_held != "stone_pick") {
                return refuse("挖到石头了：要石镐");
            }
            out.seconds = p_rules.pick_stone_s;
        } else if (p_held == "wood_shovel") {
            out.seconds = p_rules.shovel_s;
        } else if (p_held == "stone_pick") {
            out.seconds = p_rules.pick_soil_s;
        } else {
            return refuse("要木锹或石镐才挖得动");
        }
    } else {
        if (p_step >= p_rules.max_up) {
            return refuse("垒不上去了：最高垒到原地面上 " + std::to_string(int(p_rules.max_up * EditGrid::STEP_M)) + " 米");
        }
        if (p_facts.under_feet) {
            return refuse("站在上面垒不了：先让开");
        }
        if (!p_free && !earth_raises_with(p_held)) {
            return refuse("手上要拿着土");
        }
        out.item = "earth";
        out.seconds = 0.0;
    }
    out.ok = true;
    return out;
}
