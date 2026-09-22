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
constexpr const char *REMOVE = "remove"; // 改装时拆掉光标那一节
constexpr const char *KIND_1 = "kind_1"; // 改装时选模块：驾驶区
constexpr const char *KIND_2 = "kind_2"; // 储存区
constexpr const char *KIND_3 = "kind_3"; // 动力区
constexpr const char *KIND_4 = "kind_4"; // 布置时选第四种建筑
constexpr const char *BUILD_MODE = "build_mode"; // 开发者：开合布置模式
constexpr const char *ROTATE_LEFT = "rotate_left"; // 布置时把建筑转向
constexpr const char *ROTATE_RIGHT = "rotate_right";
constexpr const char *SAVE = "save"; // 布置时把清单存盘
constexpr const char *MAP = "map"; // 开合岛群地图
constexpr const char *SWITCH_MAP = "switch_map"; // 开发者：开合换地图清单
constexpr const char *GOD_MODE = "god_mode"; // 开发者：开关 god 模式
constexpr const char *GEAR_UP = "gear_up"; // 掌舵时升一档
constexpr const char *GEAR_DOWN = "gear_down"; // 掌舵时降一档
constexpr const char *FLY_DOWN = "fly_down"; // god 模式下往下飞（Shift 已是加速，不能再用来降）

// 幂等：已有的动作不重复注册
void register_all();

} // namespace input_actions
