#include "farm_hand.h"

#include <cmath>

#include <godot_cpp/classes/input.hpp>

#include "crops.h"
#include "input_actions.h"

using namespace godot;

namespace {

constexpr int PROGRESS_CELLS = 8;

const Color TARGET_READY(1.0f, 0.96f, 0.72f, 0.95f); // 这一格有活可干
const Color TARGET_IDLE(1.0f, 1.0f, 1.0f, 0.4f); // 是块田，但眼下没什么可做

// 每样活要按住多久
double work_seconds(Farm::Action p_action) {
    switch (p_action) {
        case Farm::ACTION_TILL:
            return 1.5;
        case Farm::ACTION_SOW:
            return 0.6;
        case Farm::ACTION_HARVEST:
            return 0.8;
        case Farm::ACTION_NONE:
            break;
    }
    return 0.0;
}

String work_name(Farm::Action p_action) {
    switch (p_action) {
        case Farm::ACTION_TILL:
            return String(U"开垦");
        case Farm::ACTION_SOW:
            return String(U"播种");
        case Farm::ACTION_HARVEST:
            return String(U"收获");
        case Farm::ACTION_NONE:
            break;
    }
    return String();
}

} // namespace

Farm *FarmHand::live() const {
    return farm && ObjectDB::get_instance(farm_id) == farm ? farm : nullptr;
}

void FarmHand::stand_down() {
    work_action = Farm::ACTION_NONE;
    work_cell = Farm::NO_CELL;
    work_time = 0.0;
    target_cell = Farm::NO_CELL;
    target_action = Farm::ACTION_NONE;
    if (Farm *f = live()) {
        f->set_highlight(Farm::NO_CELL, Color());
    }
}

FarmHand::Worked FarmHand::step(Farm *p_farm, Player *p_player, PackHand &p_pack, double p_reach, double p_delta, bool p_interact, const Say &p_say) {
    Worked worked;
    if (p_farm != farm || !live()) {
        stand_down(); // 换了一片农田（或走出了农田）：旧的描边收起，活作废
        farm = p_farm;
        farm_id = p_farm ? p_farm->get_instance_id() : ObjectID();
    }
    if (!farm) {
        return worked;
    }
    target_cell = Farm::NO_CELL;
    if (p_player->is_on_floor()) {
        const Vector3 ahead = Basis(Vector3(0, 1, 0), real_t(p_player->get_look_yaw())).xform(Vector3(0, 0, real_t(-p_reach)));
        target_cell = farm->cell_at(p_player->get_global_position() + ahead);
    }
    target_action = farm->available_action(target_cell, p_pack.has_seed_in_hand());

    // 描边只给「与农事有关」的格子：有活可干的，或者已经是田的
    if (target_action != Farm::ACTION_NONE) {
        farm->set_highlight(target_cell, TARGET_READY);
    } else if (target_cell != Farm::NO_CELL && farm->get_plot(target_cell) != Farm::PLOT_WILD) {
        farm->set_highlight(target_cell, TARGET_IDLE);
    } else {
        farm->set_highlight(Farm::NO_CELL, Color());
    }

    if (p_interact && target_action == Farm::ACTION_HARVEST) {
        // 收成放不下就不收（一格地至多收 yield 份）
        const CropDef *crop = catalog::crop(play::to_std(farm->get_crop(target_cell)));
        if (crop && p_pack.items().room_for(crop->product) < crop->yield) {
            p_say(String(U"背包满了，收了也没地方放：先腾出一格"));
            return worked;
        }
    }
    if (p_interact) {
        if (target_action != Farm::ACTION_NONE) {
            work_cell = target_cell;
            work_action = target_action;
            work_time = 0.0;
            p_player->set_facing_yaw(p_player->get_look_yaw());
        } else if (target_cell != Farm::NO_CELL) {
            p_say(refusal(p_pack));
        }
    }
    if (work_action == Farm::ACTION_NONE) {
        return worked;
    }
    if (!Input::get_singleton()->is_action_pressed(input_actions::INTERACT) || target_cell != work_cell || target_action != work_action) {
        work_action = Farm::ACTION_NONE;
        work_cell = Farm::NO_CELL;
        work_time = 0.0;
        return worked;
    }
    work_time += p_delta;
    if (work_time < work_seconds(work_action)) {
        return worked;
    }
    finish(p_pack, p_say);
    worked.done = true;
    worked.cell = work_cell;
    worked.action = work_action;
    // 下一样活要重新按 E：免得垦完顺手就把种子撒了
    work_action = Farm::ACTION_NONE;
    work_cell = Farm::NO_CELL;
    work_time = 0.0;
    return worked;
}

// 工时够了，活落到地里；种子从背包扣、收成往背包放，都在这一处
void FarmHand::finish(PackHand &p_pack, const Say &p_say) {
    switch (work_action) {
        case Farm::ACTION_TILL:
            farm->perform(work_cell, Farm::ACTION_TILL, String());
            break;
        case Farm::ACTION_SOW: {
            const CropDef *crop = catalog::crop_of_seed(p_pack.held_seed());
            if (crop && p_pack.items().take(p_pack.held_seed(), 1)) {
                farm->perform(work_cell, Farm::ACTION_SOW, String(crop->id));
            }
        } break;
        case Farm::ACTION_HARVEST: {
            const CropDef *crop = catalog::crop(play::to_std(farm->get_crop(work_cell)));
            const int gained = farm->perform(work_cell, Farm::ACTION_HARVEST, String());
            if (crop && gained > 0) {
                p_pack.items().put(crop->product, gained); // 起工前查过放得下
                p_say(vformat(String(U"收了 %d 份%s，放进了背包"), gained, PackHand::name_of(crop->product)));
            }
        } break;
        case Farm::ACTION_NONE:
            break;
    }
}

namespace {

// 天冷时补一句（时令倍数：冷了、雪盖着不长）
String season_note(double p_factor) {
    if (p_factor < 0.05) {
        return String(U" · 天冷，不长");
    }
    if (p_factor < 0.5) {
        return String(U" · 天冷，长得慢");
    }
    return String();
}

} // namespace

// 按了 E 却无事可做：说清楚为什么
String FarmHand::refusal(const PackHand &p_pack) const {
    const std::string &held = p_pack.held_seed();
    switch (farm->get_plot(target_cell)) {
        case Farm::PLOT_TILLED:
            return held.empty() ? String(U"手上没拿种子：Tab 打开背包把种子放上快捷栏，按数字键选中") : PackHand::name_of(held) + String(U"种用完了，去背包换一样");
        case Farm::PLOT_GROWING:
            return vformat(String(U"%s还没熟（长了 %d%%%s）"), PackHand::name_of(play::to_std(farm->get_crop(target_cell))), int(std::lround(farm->get_growth(target_cell) * 100.0)),
                    season_note(farm->get_season_factor(target_cell)));
        case Farm::PLOT_RIPE:
        case Farm::PLOT_WILD:
            break;
    }
    switch (farm->get_land(target_cell)) {
        case Farm::LAND_WATER:
            return String(U"这是水里，垦不了");
        case Farm::LAND_TOO_DRY:
            return String(U"离水太远，引不到水，垦了也白垦");
        case Farm::LAND_TOO_STEEP:
            return String(U"坡太陡，垦不成地");
        case Farm::LAND_DOCK:
            return String(U"停机坪上不能垦");
        case Farm::LAND_TRAIL:
            return String(U"这是路，垦了就没法走了");
        case Farm::LAND_YARD:
            return String(U"这是船坞的坪，垦不得");
        case Farm::LAND_ARABLE:
        case Farm::LAND_NO_GROUND:
            break;
    }
    return String();
}

String FarmHand::prompt(const PackHand &p_pack) const {
    if (!live()) {
        return String();
    }
    const std::string &held = p_pack.held_seed();
    if (work_action != Farm::ACTION_NONE) {
        const int filled = int(PROGRESS_CELLS * work_time / work_seconds(work_action));
        return work_name(work_action) + String(U"中 ") + String(U"■").repeat(filled) + String(U"□").repeat(PROGRESS_CELLS - filled);
    }
    if (target_action == Farm::ACTION_SOW) {
        return String(U"按住 E 播") + PackHand::name_of(held) + String(U"（耗 1 份）");
    }
    if (target_action != Farm::ACTION_NONE) {
        return String(U"按住 E ") + work_name(target_action);
    }
    if (target_cell != Farm::NO_CELL && farm->get_plot(target_cell) == Farm::PLOT_TILLED) {
        return held.empty() ? String(U"手上没拿种子：把种子放上快捷栏、按数字键选中") : PackHand::name_of(held) + String(U"种用完了");
    }
    if (target_cell != Farm::NO_CELL && farm->get_plot(target_cell) == Farm::PLOT_GROWING) {
        return vformat(String(U"%s苗 · 长了 %d%%%s"), PackHand::name_of(play::to_std(farm->get_crop(target_cell))), int(std::lround(farm->get_growth(target_cell) * 100.0)),
                season_note(farm->get_season_factor(target_cell)));
    }
    return String();
}

String FarmHand::fields_text() const {
    if (!live()) {
        return String();
    }
    const int fields = farm->get_plot_count(Farm::PLOT_TILLED) + farm->get_plot_count(Farm::PLOT_GROWING) + farm->get_plot_count(Farm::PLOT_RIPE);
    if (fields == 0) {
        return String(U"水边的地、熟地可以开垦");
    }
    String text = vformat(String(U"田 %d 块"), fields);
    const int ripe = farm->get_plot_count(Farm::PLOT_RIPE);
    if (ripe > 0) {
        text += vformat(String(U"（%d 块可收）"), ripe);
    }
    return text;
}
