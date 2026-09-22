#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 空艇的布局与船况。不含 Godot 头；名字是 UTF-8（两个编译目标都开了 /utf-8）。
//
// 一艘空艇是自船头到船尾的一串「节」，每节是一种模块：驾驶区（舵在这儿，恰好一个）、储存区（货舱）、动力区（浮石与帆）。
// 布局只是一个模块序列；几何怎么拼、船飞得多快，都从它算出来：
//   · 各节沿船长排开，船头朝 −z。舷门固定在 z = 0 的两舷：前面 count/2 节在舷门之前，其余在之后。
//     泊位、坞位、跳板都指舷门，所以船变长变短都不用挪码头。
//   · 船况：推力来自动力区，质量每节都有；航速、升速随推重比，转向率随节数；货舱格数是储存区之和。
// 编辑（换类型 / 加一节 / 拆掉）不合法就什么都不动，并说明原因。

enum class ModuleKind : uint8_t {
    BRIDGE, // 驾驶区
    STORAGE, // 储存区
    POWER, // 动力区
    COUNT,
};

struct ModuleDef {
    ModuleKind kind;
    const char *id; // "bridge" / "storage" / "power"
    const char *name; // 显示名
    double mass;
    double thrust; // 动力区才有
    int hold; // 货舱格数
};

namespace modules {

extern const ModuleDef DEFS[]; // 按 ModuleKind 排
const ModuleDef &def(ModuleKind p_kind);
const ModuleDef *by_id(const std::string &p_id); // 没有返回 nullptr

} // namespace modules

using ShipLayout = std::vector<ModuleKind>; // 自船头到船尾

constexpr double SECTION_LENGTH_M = 5.0;
constexpr int MAX_SECTIONS = 5;

struct ShipStats {
    int sections = 0;
    double mass = 0.0;
    double thrust = 0.0;
    int hold = 0;
    double max_speed = 0.0; // m/s
    double max_climb = 0.0;
    double max_yaw_rate = 0.0; // rad/s
    bool can_fly() const { return thrust > 0.0; }
};

ShipLayout default_layout(); // 动力区 + 驾驶区：一艘跑船的小艇

ShipStats layout_stats(const ShipLayout &p_layout);

// 舷门之前有几节
int fore_sections(int p_count);
// 第 p_index 节的 z 起点（船头一侧）；这一节占 [z0, z0 + SECTION_LENGTH_M)
double section_front_z(const ShipLayout &p_layout, int p_index);
double layout_bow_z(const ShipLayout &p_layout); // 最前一节的前缘
double layout_stern_z(const ShipLayout &p_layout); // 最后一节的后缘
int bridge_index(const ShipLayout &p_layout); // 没有驾驶区返回 -1

// 合法性：空串 = 合法；否则原因
std::string layout_problem(const ShipLayout &p_layout);

// 编辑：返回空串表示做了；否则原因，布局不动
std::string layout_set_kind(ShipLayout &p_layout, int p_index, ModuleKind p_kind);
std::string layout_cycle_kind(ShipLayout &p_layout, int p_index, int p_direction); // 换成下一种 / 上一种类型
std::string layout_insert_after(ShipLayout &p_layout, int p_index, ModuleKind p_kind); // p_index = -1 插在船头
std::string layout_remove(ShipLayout &p_layout, int p_index);

// 与脚本 / 场景文件之间的转换：模块 id 序列
std::vector<std::string> layout_ids(const ShipLayout &p_layout);
// 认不出的 id 跳过；r_ok 报告是否全都认识
ShipLayout layout_from_ids(const std::vector<std::string> &p_ids, bool *r_ok = nullptr);
