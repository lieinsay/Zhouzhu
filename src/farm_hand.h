#pragma once

#include <godot_cpp/variant/vector2i.hpp>

#include "farm.h"
#include "pack_hand.h"
#include "play_notice.h"
#include "player.h"

// 农事：步行时盯着视线前方的那一格，按下 E 起工，按住才算工时，干满了才落到地里——种子从背包扣、收成往背包放。
// 与场景无关的玩法部件（不是节点）：农田是哪一片，由编排每帧递进来（地图宿主说脚下是哪片）。
class FarmHand {
    Farm *farm = nullptr; // 上一帧盯着的那片农田（换了农田要收起旧的描边）
    godot::Vector2i target_cell = Farm::NO_CELL;
    Farm::Action target_action = Farm::ACTION_NONE;
    godot::Vector2i work_cell = Farm::NO_CELL;
    Farm::Action work_action = Farm::ACTION_NONE;
    double work_time = 0.0;

    void finish(PackHand &p_pack, const Say &p_say);
    godot::String refusal(const PackHand &p_pack) const;

public:
    struct Worked {
        bool done = false;
        godot::Vector2i cell;
        Farm::Action action = Farm::ACTION_NONE;
    };

    // 每个物理帧（步行、没开面板时）：p_farm 是脚下那片农田（没有为 nullptr），p_reach 是够得着身前多远
    Worked step(Farm *p_farm, Player *p_player, PackHand &p_pack, double p_reach, double p_delta, bool p_interact, const Say &p_say);
    // 放下手上的活、收起描边（开面板、上舵时）
    void stand_down();

    bool is_working() const { return work_action != Farm::ACTION_NONE; }
    Farm *get_farm() const { return farm; }
    // 底部提示：干活的进度、能干什么；无事可说返回空
    godot::String prompt(const PackHand &p_pack) const;
    // HUD：「田 N 块（M 块可收）」；还没有田就提示哪里能垦
    godot::String fields_text() const;
};
