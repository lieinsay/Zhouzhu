#pragma once

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "house_template.h"
#include "interior_overlay.h"
#include "interior_panel.h"
#include "interior_rules.h"
#include "play_notice.h"
#include "player.h"
#include "settlement.h"

// 室内编辑（开发者）：在世界里的一座房子上改它用的那套房屋模板——门窗、隔墙与门洞、家具、梯子，改一下这座（和用同一套模板的每一座）就地重画。
// 与场景无关的玩法部件（不是节点）：改哪座由编排递进来（布置模式里光标下那座，或人站在里面的那座）。
// 光标在建筑自己的局部坐标里按 0.25 m 取整；规矩全在 interior_rules 里，这里只转述原因。
// 每改成一步就把模板换进模板库（TemplateLibrary::put），F5 写回 game/buildings/<外壳>/<款式>.txt；
// N 另存为一套新模板（不参与自动挑），这座改用它（清单里记上款式，F5 一并存）。开着的时候那座被剖开：藏起顶与编辑层以上。
class InteriorEdit {
    bool open = false;
    Settlement *target = nullptr;
    int index = -1;
    const BuildingDef *def = nullptr;
    HouseTemplate work; // 正在改的那套（改成一步就换进库里）
    InteriorReport report; // 这套的毛病（改一步重算一次）
    double cursor_x = 0.0, cursor_z = 0.0; // 局部，连续移动
    int level = 0;
    int tool = 0;
    int quarter = 0;
    bool wall_started = false;
    double wall_x = 0.0, wall_z = 0.0;
    InteriorPanel *panel = nullptr;
    InteriorOverlay *overlay = nullptr;

    void commit(); // 换进模板库、重算毛病
    godot::Transform3D building_transform() const;

public:
    static constexpr double CURSOR_SPEED = 3.0; // m/s；按住 Shift 三倍
    static constexpr double CURSOR_SPRINT = 3.0;
    static constexpr double SNAP = 0.25;
    static constexpr double ZOOM_STEP = 0.85;
    static constexpr double MOUSE_SENSITIVITY = 0.004;

    void setup(InteriorPanel *p_panel, InteriorOverlay *p_overlay);
    bool is_open() const { return open; }
    // 在 p_settlement 的第 p_index 座上开；没有模板库、没有这座就拒绝（p_say 说为什么）
    bool open_on(Settlement *p_settlement, int p_index, const Say &p_say);
    void close(Player *p_player);
    void browse(double p_delta, bool p_interact, const Say &p_say);
    void refresh();
    void orbit(double p_dx, double p_dy);
    void zoom(bool p_in);

    double snapped_x() const;
    double snapped_z() const;
    godot::Vector3 get_cursor_global() const;
    godot::String get_tool_name() const { return godot::String::utf8(interior_tool_name(tool).c_str()); }
    int get_level() const { return level; }
    godot::String get_template_id() const { return godot::String::utf8(work.id.c_str()); }
    double get_camera_yaw() const { return overlay ? overlay->get_camera_yaw() : 0.0; }
    Settlement *get_target() const { return target; }
    int get_index() const { return index; }
};
