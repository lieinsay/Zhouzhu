#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

#include "inventory.h"

// 背包面板：只负责画。上层（Voyage）把背包内容、光标位置、手持物品递进来，它列成一行行
// 「名字 ×数量」，种子标出来、手持的标「手持」、光标那行描亮。自己不存状态、不读输入。
class BackpackPanel : public godot::PanelContainer {
    GDCLASS(BackpackPanel, godot::PanelContainer)

    godot::VBoxContainer *column = nullptr;
    godot::Label *title = nullptr;
    godot::Label *hint = nullptr;
    std::vector<godot::Label *> rows;

    void ensure_built();

protected:
    static void _bind_methods();

public:
    void refresh(const Inventory &p_pack, int p_cursor, const std::string &p_held);
};
