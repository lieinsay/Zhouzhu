#pragma once

#include <vector>

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/string.hpp>

// 开发者营造面板（拓展切片十一，god 下 F8 开合，右上那一栏）：只负责画。上层（编排）把每一行的字、光标在哪行、手上拿着哪行、笔刷多大递进来，
// 它列成：标题、每一行（光标那行带 ▶，手上的那行描亮）、按键提示。自己不存状态、不读输入。
class DevWorksPanel : public godot::PanelContainer {
    GDCLASS(DevWorksPanel, godot::PanelContainer)

    godot::VBoxContainer *column = nullptr;
    godot::Label *title = nullptr;
    std::vector<godot::Label *> rows;
    godot::Label *hint = nullptr;

    void ensure_built();

protected:
    static void _bind_methods();

public:
    // p_rows：每一行的字；p_cursor = 光标那行；p_held = 手上拿着的那行（−1 没有）；p_brush = 挖垒的笔刷（n × n）
    void refresh(const std::vector<godot::String> &p_rows, int p_cursor, int p_held, int p_brush);
};
