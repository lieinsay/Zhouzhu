#pragma once

#include <vector>

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

#include "ship_layout.h"

// 改装面板：只负责画。上层（Voyage）把船的布局、光标位置、船况递进来，它自船头到船尾一行一节列出来，
// 光标那行描亮，底下一行船况（航速 / 升速 / 转向 / 货舱），飞不起来的船说明为什么。自己不存状态、不读输入。
class RefitPanel : public godot::PanelContainer {
    GDCLASS(RefitPanel, godot::PanelContainer)

    godot::VBoxContainer *column = nullptr;
    godot::Label *title = nullptr;
    godot::Label *stats_label = nullptr;
    godot::Label *hint = nullptr;
    std::vector<godot::Label *> rows;

    void ensure_built();

protected:
    static void _bind_methods();

public:
    void refresh(const ShipLayout &p_layout, int p_cursor, const ShipStats &p_stats);
};
