#pragma once

#include <vector>

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/string.hpp>

// 整座样式的面板（切片十一，G 开合，右上那一栏）：只负责画。上层（编排）把每套样式的名字与要几件什么、手上拿着第几套递进来，
// 它列成：标题、每套一行（1–9 选，手上的那套描亮）、按键提示。自己不存状态、不读输入。
class StylePanel : public godot::PanelContainer {
    GDCLASS(StylePanel, godot::PanelContainer)

    godot::VBoxContainer *column = nullptr;
    godot::Label *title = nullptr;
    std::vector<godot::Label *> rows;
    godot::Label *hint = nullptr;

    void ensure_built();

protected:
    static void _bind_methods();

public:
    // p_names 与 p_bills 一一对应；p_selected = 手上拿着的那套（−1 没有）
    void refresh(const std::vector<godot::String> &p_names, const std::vector<godot::String> &p_bills, int p_selected);
};
