#pragma once

// 输入动作表。启动时用 InputMap 注册，不写进 project.godot：
// 动作名只在这里出现一次，C++ 这边拼错了编译不过，测试也经同一层动作来驱动。
namespace input_actions {

constexpr const char *MOVE_FORWARD = "move_forward"; // 步行：前；掌舵：进车
constexpr const char *MOVE_BACK = "move_back"; // 步行：后；掌舵：倒车
constexpr const char *MOVE_LEFT = "move_left"; // 步行：左；掌舵：左舵
constexpr const char *MOVE_RIGHT = "move_right";
constexpr const char *JUMP = "jump";
constexpr const char *SPRINT = "sprint";
constexpr const char *ASCEND = "ascend";
constexpr const char *DESCEND = "descend";
constexpr const char *INTERACT = "interact";
constexpr const char *INVENTORY = "inventory"; // 开合背包

// 幂等：已有的动作不重复注册
void register_all();

} // namespace input_actions
