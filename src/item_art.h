#pragma once

#include <map>
#include <string>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

// 绑定 JSON 经 FileAccess 读取，纹理由 ResourceLoader 读取，兼容 PCK。
// Godot 资源缓存让不同控件的同一路径共享纹理身份。
class ItemArt {
    bool loaded = false;
    godot::Dictionary bindings;
    std::map<std::string, godot::Ref<godot::Texture2D>> textures;
    godot::Dictionary drawn;
    void load();

public:
    godot::Ref<godot::Texture2D> texture(const std::string &p_item);
    bool draw(godot::Control *p_control, const godot::Rect2 &p_rect, const std::string &p_item, float p_alpha = 1.0f);
    godot::Dictionary inspect(const godot::String &p_item);
};
