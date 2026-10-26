#pragma once

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "buildings.h"
#include "item_art.h"

// 布置模式的面板（右上那一栏）：只负责画。上层（Voyage）把在哪座岛、光标的岛内坐标与朝向、
// 那里能不能盖及原因、选中的建筑种类、清单里有几座、存到哪儿、有没有没存的改动递进来，
// 它列成：标题一行、光标一行、状态一行、四种建筑的调色板（选中的描亮）、房屋模板一行（V / Z 换）、清单与存盘一行、按键提示。
// 自己不存状态、不读输入。
class BuildPanel : public godot::PanelContainer {
    GDCLASS(BuildPanel, godot::PanelContainer)

    godot::VBoxContainer *column = nullptr;
    godot::Label *title = nullptr;
    godot::Label *cursor_label = nullptr;
    godot::Label *status_label = nullptr;
    godot::Label *palette_title = nullptr;
    godot::Label *palette[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    godot::Label *style_label = nullptr;
    godot::Label *file_label = nullptr;
    godot::Label *hint = nullptr;
    ItemArt art;

    void ensure_built();

protected:
    static void _bind_methods();

public:
    godot::Dictionary get_asset_art(const godot::String &p_asset) { return art.inspect_asset(p_asset); }
    void refresh(const godot::String &p_island, const godot::Vector3 &p_local_cursor, double p_yaw_deg, const godot::String &p_status, bool p_ok,
            int p_kind, int p_count, const godot::String &p_layout_path, bool p_dirty, const godot::String &p_style);
};
