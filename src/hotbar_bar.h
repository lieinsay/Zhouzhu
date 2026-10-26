#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/control.hpp>

#include "inventory.h"
#include "item_art.h"

// 快捷栏（屏幕底部常驻的 9 格）：只负责画。背包的前 9 格、选中哪格由上层递进来；
// 选中那格描亮，格子上面写手上拿着的是什么。
class HotbarBar : public godot::Control {
    GDCLASS(HotbarBar, godot::Control)

    std::vector<ItemStack> slots;
    int selected = 0;
    ItemArt art;

protected:
    static void _bind_methods();

public:
    godot::Dictionary get_item_art(const godot::String &p_item) { return art.inspect(p_item); }
    HotbarBar();
    void _draw() override;
    void refresh(const std::vector<ItemStack> &p_slots, int p_selected);
};
