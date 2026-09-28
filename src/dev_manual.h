#pragma once

#include <string>
#include <vector>

// 开发者工具说明（拓展：Esc 暂停菜单）：游戏里每一样开发者工具的一条说明——哪个键、叫什么、在哪儿能用、怎么用。不含 Godot 头，无状态。
// 暂停菜单（PauseMenu）的「开发者工具说明」页照这张表列出来；单测查 input_actions.h 里每个开发者动作都有一条（加了新工具忘了写说明，单测就不过）。
// 键写的是 input_actions.cpp 里绑的键；动作名空的是没有独立动作、挂在别的界面里的工具（地图上左键瞬移、行星地图）。

struct DevManualEntry {
    const char *action; // 输入动作名（input_actions.h）；"" = 挂在别的界面里
    const char *keys; // 「F3」「B / F1」……
    const char *title;
    const char *where; // 在哪儿能用
    std::vector<std::string> lines; // 怎么用，一行一件事
};

// 全部开发者工具，按键的次序
const std::vector<DevManualEntry> &dev_manual();
// 开头那几行：开发者工具是什么、什么时候不理
const std::vector<std::string> &dev_manual_intro();
// 表里有没有这个动作
bool dev_manual_covers(const std::string &p_action);
