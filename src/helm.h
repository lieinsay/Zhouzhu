#pragma once

#include <cmath>
#include <vector>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include "player.h"
#include "ship.h"

// 掌舵：上舵 → 飞（换档）→ 叫靠泊（船自己滑到泊位上）→ 离舵下船。甲乙两岛（Voyage）与岛群（WorldScene）共用。
// 野泊（wild_landing.h）也是一种泊位：接触线是落地位姿的舷门线，编排按船此刻的位置当场算好递进来；
// 码头、船坞、船台在够得着的距离内时先靠它们，没有才野泊。野泊不了不拦着离舵，只在提示里说为什么（wild_note）。
// 不是节点，是编排者肚子里的一个部件：状态（谁在受控、在不在滑向泊位）在这里，但每一步由编排者在自己的物理帧里推一下，
// 泊位候选也由编排者当场给——它知道此刻有哪些码头 / 船坞，这里不认识 Island、World。
// 上舵前该收起的面板、该放下的活，是编排者的事；这里只管人、船、相机的交接。

struct Berth {
    godot::Transform3D contact; // 接触线（见 dock_builder::berth_contact）
    bool yard = false; // 是坞位（入坞）不是泊位（靠泊）
    bool land = false; // 是村里的船台（落地靠港）
    int id = -1; // 编排者自己认的号（码头号），这里不用
    godot::String refusal; // 非空：这里停不下这条船（原因）
    bool wild = false; // 是野泊（就地落在平地上，自己放舷梯）
    int gangway_side = 0; // 野泊：舷梯放哪一舷（+1 右舷、−1 左舷）
    double gangway_run = 0.0, gangway_rise = 0.0; // 野泊：舷梯的水平长、落差
};

class Helm {
public:
    enum State {
        ON_FOOT,
        PILOTING,
        MOORING,
    };
    enum Event {
        NONE,
        MOOR_STARTED, // 叫了靠泊 / 入坞，船开始滑
        DISEMBARKED, // 人下了船
        STILL_MOVING, // 想离舵，船还没停稳
        REFUSED, // 叫了靠泊，那儿停不下这条船（原因在 get_refusal）
    };

    double helm_range = 2.2; // 离舵多近能上舵
    double dock_range = 45.0; // 离泊位多近能叫靠泊
    double leave_helm_speed = 3.0; // 半空中离舵：船速须低于此
    godot::String wild_note; // 编排每帧写：此刻为什么野泊不了（空 = 没什么可说）
    // 编排每帧写（行星计划 P7）：此刻离不了舵的原因（空 = 离得了）——船降到云海以下，人下去就掉进云里 / 海里：
    // 「海上无处落脚」。这时按 E 不离舵、不靠泊（近处也没有码头），只说这一句
    godot::String no_footing;
    double sea_y = NAN; // 有行星数据时海面的 y（航行 HUD 报离海面多高）；没有是 NaN

private:
    Ship *ship = nullptr;
    Player *player = nullptr;
    State state = ON_FOOT;
    Berth moor_berth; // 正在滑向的那一个
    godot::String refusal;

public:
    void attach(Ship *p_ship, Player *p_player);

    State get_state() const { return state; }
    bool is_mooring_to_yard() const { return state == MOORING && moor_berth.yard; }
    const Berth &get_moor_berth() const { return moor_berth; }
    const godot::String &get_refusal() const { return refusal; }
    bool near_helm() const;

    void board();
    void disembark();
    // 掌舵 / 滑行中每个物理帧调：人坐稳在舵旁；R/F 换档；p_interact 时在泊位附近叫靠泊，否则停稳了就离舵
    Event step(bool p_interact, const std::vector<Berth> &p_berths);
    // dock_range 内最近的泊位（码头、船坞、船台优先；都没有才是野泊）；没有返回 false
    bool moor_target(const std::vector<Berth> &p_berths, Berth &r_berth) const;

    // 航行 HUD：「航速 · 档位 · 高度」一行，「<目标名> 距离 · 方位 · 高差」一行
    godot::String course_text(const godot::Transform3D &p_target, const godot::String &p_target_name) const;
    // 掌舵时的操作提示（泊位附近换成「按 E 靠泊 / 入坞 / 落上船台 / 离舵」，停不下就说为什么）
    godot::String piloting_prompt(const std::vector<Berth> &p_berths) const;
};
