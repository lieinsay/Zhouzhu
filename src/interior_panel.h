#pragma once

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include "item_art.h"

// 室内编辑的面板（右上那一栏）：只负责画。上层把这些递进来：改的是哪套模板（名字、id、第几层 / 共几层）、光标、
// 光标处能不能放及原因、工具板（名字表与选中的那一样、朝向）、整套模板的毛病（走不到的地方……）、存到哪、有没有没存的改动。
// 自己不存状态、不读输入。
class InteriorPanel : public godot::PanelContainer {
    GDCLASS(InteriorPanel, godot::PanelContainer)

    godot::VBoxContainer *column = nullptr;
    godot::Label *title = nullptr;
    godot::Label *cursor_label = nullptr;
    godot::Label *status_label = nullptr;
    godot::Label *tools_label = nullptr;
    godot::Label *problems_label = nullptr;
    godot::Label *file_label = nullptr;
    godot::Label *hint = nullptr;
    godot::TextureRect *preview = nullptr;
    ItemArt art;

    void ensure_built();

protected:
    static void _bind_methods();

public:
    godot::Dictionary get_asset_art(const godot::String &p_asset) { return art.inspect_asset(p_asset); }
    struct View {
        godot::String name, id;
        godot::String preview_asset;
        int level = 0, levels = 1;
        double x = 0.0, z = 0.0;
        godot::String status;
        bool ok = true;
        godot::PackedStringArray tools;
        int tool = 0;
        int quarter = 0;
        bool drawing_wall = false;
        godot::PackedStringArray problems;
        godot::String path;
        bool dirty = false;
    };
    void refresh(const View &p_view);
};
