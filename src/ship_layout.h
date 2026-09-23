#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 空艇的布局与船况。不含 Godot 头；名字是 UTF-8（两个编译目标都开了 /utf-8）。
//
// 一艘空艇是三维格子里的一堆「节」：每节占一格（长 7 m × 高 3.8 m × 宽 7 m，按人的尺度定：甲板上能走开、舱房像间屋子），格子用 (x, y, z) 指——
// x 左舷 −1 … 右舷 +1，y 第 0 层（船体那一层）… 第 2 层，z 船头 −3 … 船尾 +3。每节是一种模块：
// 驾驶区（舵在这儿，恰好一个，上面不能再搭）、储存区（货舱）、动力区（浮石与帆，出推力）。
// 舷门固定在格 (0, 0, 0) 那一排（z = 0、第 0 层）的两头；泊位、坞位、跳板都接舷门，所以船怎么改都不用挪码头。
//
// 规则（layout_problem 一一说明）：在范围内、不超上限；舷门格 (0,0,0) 不能空；恰好一个驾驶区且顶上没东西；
// 上层的节下面得有节撑着；列与列面连相连（每一列从舷门那一列走得到——高度不同的相邻列之间会有梯子）；
// 舷门那一排是最宽的一排（不然靠不上码头），且它两头的格子上面不搭（跳板要接开放的甲板）。
//
// 船况由布局算出：推力来自动力区，质量每节都有；航速、升速随推重比，转向率随节数；货舱格数是储存区之和。

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

struct Cell {
    int x = 0, y = 0, z = 0;
    bool operator==(const Cell &p_other) const { return x == p_other.x && y == p_other.y && z == p_other.z; }
    bool operator!=(const Cell &p_other) const { return !(*this == p_other); }
};

struct Module {
    Cell at;
    ModuleKind kind = ModuleKind::STORAGE;
};

constexpr double CELL_LENGTH_M = 7.0; // z
constexpr double CELL_WIDTH_M = 7.0; // x
constexpr double CELL_HEIGHT_M = 3.8; // y
constexpr int MAX_X = 1; // |x| ≤ 1
constexpr int MAX_LEVELS = 3; // y ∈ [0, 2]
constexpr int MAX_Z = 3; // |z| ≤ 3
constexpr int MAX_MODULES = 12;

// 布局：一堆节，按 (y, z, x) 排序，便于比较与序列化
class ShipLayout {
    std::vector<Module> modules;

    void sort();

public:
    ShipLayout() = default;
    explicit ShipLayout(std::vector<Module> p_modules);

    const std::vector<Module> &get_modules() const { return modules; }
    int size() const { return int(modules.size()); }
    bool empty() const { return modules.empty(); }
    const Module *at(const Cell &p_cell) const; // 没有返回 nullptr
    bool has(const Cell &p_cell) const { return at(p_cell) != nullptr; }
    // 某一列（x, z）有几层；没有这一列为 0
    int column_height(int p_x, int p_z) const;
    // 边界（没有节时全 0）
    int min_x() const;
    int max_x() const;
    int min_z() const;
    int max_z() const;
    int levels() const; // 最高列的层数
    const Module *bridge() const;

    // 不检查合法性的直接改动（编辑函数在检查后调用）
    void put(const Module &p_module); // 同格覆盖
    void erase(const Cell &p_cell);

    bool operator==(const ShipLayout &p_other) const;
    bool operator!=(const ShipLayout &p_other) const { return !(*this == p_other); }
};

ShipLayout default_layout(); // 动力区 (0,0,−1) + 驾驶区 (0,0,0)：一艘跑船的小艇

// 格子中心的局部坐标（甲板面在格底：y = 层 × CELL_HEIGHT_M）
double cell_center_x(int p_x);
double cell_deck_y(int p_y);
double cell_center_z(int p_z);
// 舷门那一排两头的船边：左舷边 x（负）与右舷边 x（正）
double port_edge_x(const ShipLayout &p_layout);
double starboard_edge_x(const ShipLayout &p_layout);
double bow_z(const ShipLayout &p_layout); // 最前一排的前缘
double stern_z(const ShipLayout &p_layout);

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

ShipStats layout_stats(const ShipLayout &p_layout);

// 货位：每节储存区 4 个（两舷各前后两个，中间留走道），货箱就放在这些位置上；总数 = 船况的 hold。
// 按布局里节的顺序排、每节内 左前 / 左后 / 右前 / 右后，所以改船不动的节，货位编号不变。
// 坐标是船的局部坐标，y 是那一节的甲板面。open：这一节是顶层的开放甲板，人够得着；被上面盖住的舱体里的货位眼下还装卸不了。
constexpr double HOLD_SLOT_SIDE_M = 2.2; // 货位中心离节中线
constexpr double HOLD_SLOT_ALONG_M = 1.6; // 货位中心离节中心（前后）
struct HoldSlot {
    Cell cell;
    double x = 0.0, y = 0.0, z = 0.0;
    bool open = true;
};
std::vector<HoldSlot> hold_slots(const ShipLayout &p_layout);

// 梯子：从低的那一列的甲板（第 from_level 层顶，即 from_level 层数）爬到高的那一列顶上。
// 挂在高列面向低列的那面墙上；低列 (low_x, low_z) 可以是船外（列高 0）的格子？不——只在两列都有节时才有梯子。
struct Ladder {
    int low_x = 0, low_z = 0; // 梯脚所在的列
    int high_x = 0, high_z = 0; // 梯子靠着的列
    int from_level = 0; // 梯脚甲板是第几层顶（= 低列的层数）
    int to_level = 0; // 梯顶甲板（= 高列的层数）
};
std::vector<Ladder> layout_ladders(const ShipLayout &p_layout);

// 合法性：空串 = 合法；否则原因
std::string layout_problem(const ShipLayout &p_layout);
bool cell_in_range(const Cell &p_cell);

// 编辑：返回空串表示做了；否则原因，布局不动
std::string layout_place(ShipLayout &p_layout, const Cell &p_cell, ModuleKind p_kind); // 空格放一节
std::string layout_set_kind(ShipLayout &p_layout, const Cell &p_cell, ModuleKind p_kind); // 换已有节的类型
std::string layout_remove(ShipLayout &p_layout, const Cell &p_cell);
// 眼下能放一节储存区的空格（改装视角里画成半透明的候选格）
std::vector<Cell> layout_candidates(const ShipLayout &p_layout);

// 与脚本 / 场景文件之间的转换："x,y,z:kind" 字符串序列
std::vector<std::string> layout_ids(const ShipLayout &p_layout);
std::string module_id(const Module &p_module);
// 认不出的条目跳过；r_ok 报告是否全都认识
ShipLayout layout_from_ids(const std::vector<std::string> &p_ids, bool *r_ok = nullptr);
bool parse_module_id(const std::string &p_text, Module &r_module);
