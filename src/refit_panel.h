#pragma once

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ship_layout.h"

// 改装面板（装配视角旁边那一栏）：只负责画。上层（Voyage）把光标格、那里是什么 / 能不能放及原因、
// 选中的模块、船况递进来，它列成：光标一行、三种模块的调色板（选中的描亮）、船况两行、按键提示。
// 自己不存状态、不读输入。
class RefitPanel : public godot::PanelContainer {
    GDCLASS(RefitPanel, godot::PanelContainer)

    godot::VBoxContainer *column = nullptr;
    godot::Label *title = nullptr;
    godot::Label *cursor_label = nullptr;
    godot::Label *cursor_status = nullptr;
    godot::Label *palette_title = nullptr;
    godot::Label *palette[int(ModuleKind::COUNT)] = { nullptr, nullptr, nullptr };
    godot::Label *stats_label = nullptr;
    godot::Label *hint = nullptr;

    void ensure_built();

protected:
    static void _bind_methods();

public:
    void refresh(const Cell &p_cursor, const godot::String &p_cursor_status, bool p_cursor_ok, ModuleKind p_kind, const ShipStats &p_stats);
};
