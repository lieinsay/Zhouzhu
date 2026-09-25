#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/control.hpp>

#include "inventory.h"

// 快捷栏（屏幕底部常驻的 9 格）：只负责画。背包的前 9 格、选中哪格由上层递进来；
// 选中那格描亮，格子上面写手上拿着的是什么。
class HotbarBar : public godot::Control {
    GDCLASS(HotbarBar, godot::Control)

    std::vector<ItemStack> slots;
    int selected = 0;

protected:
    static void _bind_methods();

public:
    HotbarBar();
    void _draw() override;
    void refresh(const std::vector<ItemStack> &p_slots, int p_selected);
};
