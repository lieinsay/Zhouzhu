#pragma once

#include <vector>

#include "play_notice.h"
#include "player.h"
#include "refit_overlay.h"
#include "refit_panel.h"
#include "ship.h"
#include "ship_layout.h"
#include "shipyard.h"

// 改装：船开进船坞后，在画样台按 E 进装配视角改船。与场景无关的玩法部件（不是节点）：
// 有哪些船坞由编排递进来（地图宿主说有哪些），这里只管「离哪座画样台够近、船在不在那座坞里、光标在哪一格、选了哪种模块」
// 以及装配视角与面板。W/S 前后、A/D 左右、空格/Shift 上下挪光标，1/2/3 选模块，E 放一节或换类型，X 拆掉；
// 每改一下船立刻重拼并在坞里重新坐定。开着的时候人站不站住由编排定。
class RefitBench {
    bool open = false;
    Shipyard *yard = nullptr; // 开着时在改的是哪座坞里的船
    Cell cursor;
    ModuleKind kind = ModuleKind::STORAGE;
    RefitPanel *panel = nullptr;
    RefitOverlay *overlay = nullptr;

    // 光标格眼下是什么 / 能不能放选中的模块；返回说明，r_state 给装配视角
    godot::String cursor_status(const Ship *p_ship, RefitOverlay::CursorState &r_state) const;

public:
    static constexpr double IN_YARD_TOLERANCE = 0.5; // 船舷离坞位接触线这么近就算停在坞里
    static constexpr double MOUSE_SENSITIVITY = 0.004;

    void setup(RefitPanel *p_panel, RefitOverlay *p_overlay);

    // p_yards 里离 p_at 不超过 p_range 的那座画样台；没有返回 nullptr
    static Shipyard *bench_near(const std::vector<Shipyard *> &p_yards, const godot::Vector3 &p_at, double p_range);
    // 船停在这座坞的坞位上（改得了）
    static bool ship_in(const Ship *p_ship, const Shipyard *p_yard);

    bool is_open() const { return open; }
    // 在 p_yard 的画样台开改装：船不在坞里就拒绝（p_say 说为什么），返回是否开了
    bool open_at(Shipyard *p_yard, Ship *p_ship, const Say &p_say);
    void close(Player *p_player);
    void browse(Ship *p_ship, bool p_interact, const Say &p_say);
    void refresh(const Ship *p_ship);
    void orbit(double p_dx, double p_dy);

    Cell get_cursor() const { return cursor; }
    ModuleKind get_kind() const { return kind; }
};
