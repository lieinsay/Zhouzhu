#pragma once

#include <vector>

#include <godot_cpp/classes/input_event.hpp>

#include "play_notice.h"
#include "player.h"
#include "refit_overlay.h"
#include "refit_panel.h"
#include "refit_pick.h"
#include "ship.h"
#include "ship_layout.h"
#include "shipyard.h"

// 改装：船开进船坞后，在画样台按 E 进装配视角改船。与场景无关的玩法部件（不是节点）：
// 有哪些船坞由编排递进来（地图宿主说有哪些），这里只管「离哪座画样台够近、船在不在那座坞里」以及装配视角里的鼠标：
//   · 在左边的节点卡片上按住左键拖到船上：船上亮起这种节点眼下能放的格子，指着亮格出黄绿的虚影，松手就放上；
//     指着不亮的地方是红色虚影、面板说为什么，松手不放；拖回面板上松手算放回去。在卡片上点一下（不拖）是「拿着」，再点船上放、
//     再点那张卡片或按右键 / Esc 放下；
//   · 没拖东西时点船上的节点就选中它（白框），按 X 或面板上的「拆除」拆掉整个节点（复合节一起拆）；点空处取消选中；
//   · 右键按住拖着转视角（拖着节点时右键是放下），滚轮拉远拉近。
// 每改一下船立刻重拼并在坞里重新坐定；船舱里放着的东西装不下就不改。开着的时候人站不站住、鼠标放不放出来由编排定。
class RefitBench {
    bool open = false;
    Shipyard *yard = nullptr; // 开着时在改的是哪座坞里的船
    RefitPanel *panel = nullptr;
    RefitOverlay *overlay = nullptr;

    int drag_kind = -1; // 拖着 / 拿着的节点（ModuleKind）；−1 没有
    bool drag_sticky = false; // 点了一下卡片拿着的：下一次左键落下
    int press_card = -1; // 按下左键时在哪张卡片上
    bool left_card = false; // 按下之后离开过那张卡片（是拖，不是点）
    bool has_selected = false;
    Cell selected; // 选中的节点的锚点
    bool orbiting = false;
    godot::Vector2 mouse;
    bool mouse_known = false;

    RefitHover hover; // 鼠标指着的：船上的节点 / 拖着的节点的落点
    RefitPanel::Hit panel_hit; // 鼠标在面板的哪儿
    double clock = 0.0; // 亮格一呼一吸

    // 每种节点眼下的落点：布局变了才重算
    ShipLayout spots_for;
    bool spots_valid = false;
    std::vector<Cell> spots[int(ModuleKind::COUNT)];

    void update_spots(const ShipLayout &p_layout);
    void update_hover(const Ship *p_ship);
    RefitHover pick_at(const ShipLayout &p_layout, const godot::Vector2 &p_screen) const;
    void drop(Ship *p_ship, const Say &p_say, int p_bay_in_use);
    void put_down();
    bool apply(Ship *p_ship, const ShipLayout &p_next, const Say &p_say, int p_bay_in_use);
    void remove_selected(Ship *p_ship, const Say &p_say, int p_bay_in_use);

public:
    static constexpr double IN_YARD_TOLERANCE = 0.5; // 船舷离坞位接触线这么近就算停在坞里
    static constexpr double ORBIT_SENSITIVITY = 0.006;

    void setup(RefitPanel *p_panel, RefitOverlay *p_overlay);

    // p_yards 里离 p_at 不超过 p_range 的那座画样台；没有返回 nullptr
    static Shipyard *bench_near(const std::vector<Shipyard *> &p_yards, const godot::Vector3 &p_at, double p_range);
    // 船停在这座坞的坞位上（改得了）
    static bool ship_in(const Ship *p_ship, const Shipyard *p_yard);

    bool is_open() const { return open; }
    // 在 p_yard 的画样台开改装：船不在坞里就拒绝（p_say 说为什么），返回是否开了
    bool open_at(Shipyard *p_yard, Ship *p_ship, const Say &p_say);
    void close(Player *p_player);
    // 开着时的鼠标事件（编排转过来）：用掉了返回 true。p_bay_in_use：船舱里放着东西的格数——改完的船舱比它小就不改
    bool on_mouse(const godot::Ref<godot::InputEvent> &p_event, Ship *p_ship, const Say &p_say, int p_bay_in_use = 0);
    // 开着时每个物理帧：X 拆选中的节点
    void browse(Ship *p_ship, const Say &p_say, int p_bay_in_use = 0);
    // Esc：拖着 / 拿着节点就放下（返回 true），没有返回 false
    bool cancel_drag();
    void refresh(const Ship *p_ship, double p_delta);

    int get_drag_kind() const { return drag_kind; }
    bool get_selected(Cell &r_anchor) const;
    const RefitHover &get_hover() const { return hover; }
    std::vector<Cell> glow_now() const; // 眼下亮着的格子
    // 测试拿它点鼠标（视口坐标；没有返回 (−1, −1)）：某张卡片、拆除钮；
    // 拖着 p_kind 时屏幕上哪一点指着锚点 p_anchor 那个落点（能不能放都算）；没拖东西时哪一点指着锚点在 p_anchor 的节点
    godot::Vector2 card_center(ModuleKind p_kind) const;
    godot::Vector2 remove_center() const;
    godot::Vector2 aim_spot(const Ship *p_ship, const Cell &p_anchor, ModuleKind p_kind) const;
    godot::Vector2 aim_node(const Ship *p_ship, const Cell &p_anchor) const;
};
