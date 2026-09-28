#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// 空艇的布局与船况。不含 Godot 头；名字是 UTF-8（两个编译目标都开了 /utf-8）。
//
// 一艘空艇是三维格子里拼起来的一堆「节点」。格子的一格叫一「节」（长 7 m × 高 3.8 m × 宽 7 m，按人的尺度定：甲板上能走开、舱房像间屋子），
// 格子用 (x, y, z) 指——x 左舷 −1 … 右舷 +1，y 第 0 层（船体那一层）… 第 2 层，z 船头 −3 … 船尾 +3。
// 节点占一节或几节：单节，或复合节（前后双节占 z 方向两节、上下双节占 y 方向两节、左右双节占 x 方向两节）。
// 节点用锚点（占的格子里 x、y、z 都最小的那一格）加种类记；种类的占格、规矩、功能都在节点表（modules::DEFS）里：
//   驾驶区（单节，舵在这儿，恰好一个、只能在最上面、拆不得）、储存区（单节，货位 + 船舱）、帆（单节，只能在最上面，出推力）、
//   浮石舱（单节，只能在最下面一层，推力小些但能压在别的节下面）、空节（单节，没有功能，空着留给玩家摆东西）、
//   双桅帆（前后双节，只能在最上面）、双层货舱（上下双节）、宽空节（左右双节，没有功能）。
// 舷门固定在格 (0, 0, 0) 那一排（z = 0、第 0 层）的两头；泊位、坞位、跳板都接舷门，所以船怎么改都不用挪码头。
//
// 规矩（layout_problem 一一说明原因）：在范围内、不超 12 节；节点不重叠；舷门格 (0,0,0) 不能空；
// 节点表的规矩——最多几个 / 少不了、只能在最下面一层、只能在最上面（顶上不能再搭）；
// 上层的节下面得有节撑着；列与列面连相连（每一列从舷门那一列走得到——高度不同的相邻列之间会有梯子）；
// 舷门那一排是最宽的一排（不然靠不上码头），且它两头的格子上面不搭（跳板要接开放的甲板）。
//
// 船况由布局算出：推力来自帆与浮石舱，质量每个节点都有；航速、升速随推重比，转向率随节数；货位与船舱来自货舱类节点。

enum class ModuleKind : uint8_t {
    BRIDGE, // 驾驶区
    STORAGE, // 储存区
    SAIL, // 帆
    FLOATSTONE, // 浮石舱
    DECK, // 空节
    LONG_SAIL, // 双桅帆（前后双节）
    TALL_HOLD, // 双层货舱（上下双节）
    WIDE_DECK, // 宽空节（左右双节）
    COUNT,
};

// 节点在船上拼成什么样（Ship 按它摆陈设）
enum class ModuleLook : uint8_t {
    HELM, // 舵柱、舵轮、舵楼顶
    CARGO, // 开放的格子上四个货位的垫木；被盖住是开舱口的货舱
    SAIL, // 两舷各一根侧桅一面帆；多节的是贯通的长帆
    FLOATSTONE, // 船底吊着浮石；开放是透光的格栅，被盖住墙上透光
    BARE, // 什么也不摆：空甲板镶一圈浅色边，被盖住是空舱
};

struct ModuleDef {
    ModuleKind kind;
    const char *id; // 布局条目里用的："bridge" / "storage" / "sail" …
    const char *name; // 显示名
    int size_x, size_y, size_z; // 占几节：左右、上下、前后
    double mass; // 整个节点
    double thrust;
    int bay; // 船舱格数（开背包时列在上面的容器格子）
    bool cargo_deck; // 每一节甲板上 4 个货位（摆货箱）
    bool top_only; // 只能在最上面：顶上不能再搭
    bool bottom_only; // 只能在最下面一层
    int max_count; // 最多几个；0 = 不限
    const char *required; // 少不了时缺了的原因；nullptr = 可以没有
    bool removable;
    ModuleLook look;
};

namespace modules {

extern const ModuleDef DEFS[]; // 按 ModuleKind 排
const ModuleDef &def(ModuleKind p_kind);
const ModuleDef *by_id(const std::string &p_id); // 没有返回 nullptr；旧名 "power" 认作帆
int cells_of(const ModuleDef &p_def); // 占几节

// 面板上的说法（UTF-8）
std::string shape_name(const ModuleDef &p_def); // 单节 / 前后双节 / 上下双节 / 左右双节
std::vector<std::string> rule_tags(const ModuleDef &p_def); // 只能在最上面 / 只能在最下面一层 / 只能有一个 / 拆不得
std::string function_text(const ModuleDef &p_def); // 舵在这儿 / 推力 1.0 / 船舱 12 格 · 货位 4 / 没有功能：空着留给摆东西

} // namespace modules

struct Cell {
    int x = 0, y = 0, z = 0;
    bool operator==(const Cell &p_other) const { return x == p_other.x && y == p_other.y && z == p_other.z; }
    bool operator!=(const Cell &p_other) const { return !(*this == p_other); }
};

struct Module {
    Cell at; // 锚点：占的格子里 x、y、z 都最小的那一格
    ModuleKind kind = ModuleKind::STORAGE;
};

// 一个节点占的格子，按 (y, z, x) 排
std::vector<Cell> module_cells(const Module &p_module);
std::vector<Cell> footprint(const Cell &p_anchor, ModuleKind p_kind);
bool module_covers(const Module &p_module, const Cell &p_cell);

constexpr double CELL_LENGTH_M = 7.0; // z
constexpr double CELL_WIDTH_M = 7.0; // x
constexpr double CELL_HEIGHT_M = 3.8; // y
constexpr int MAX_X = 1; // |x| ≤ 1
constexpr int MAX_LEVELS = 3; // y ∈ [0, 2]
constexpr int MAX_Z = 3; // |z| ≤ 3
constexpr int MAX_CELLS = 12; // 最多 12 节（复合节按占的节数算）

// 船上的一节：哪一格、属于第几个节点
struct CellPart {
    Cell at;
    int module = 0; // get_modules() 里的序号
};

// 布局：一堆节点，按锚点 (y, z, x) 排序，便于比较与序列化；另有一张格子占用表，查某一格 O(1)
class ShipLayout {
    static constexpr int GRID_X = 2 * MAX_X + 1, GRID_Z = 2 * MAX_Z + 1;
    static constexpr int GRID = GRID_X * GRID_Z * MAX_LEVELS;

    std::vector<Module> modules;
    std::array<int8_t, GRID> grid{}; // 占着这一格的节点序号；−1 空。越界的格子不记
    bool overlapping = false;
    int cell_total = 0;

    void reindex();
    static int grid_index(const Cell &p_cell); // 越界 −1

public:
    ShipLayout();
    explicit ShipLayout(std::vector<Module> p_modules);

    const std::vector<Module> &get_modules() const { return modules; }
    int node_count() const { return int(modules.size()); }
    int cell_count() const { return cell_total; } // 占的节数（复合节按节算）
    bool empty() const { return modules.empty(); }
    const Module *at(const Cell &p_cell) const; // 占着这一格的节点；没有返回 nullptr
    int module_index_at(const Cell &p_cell) const; // 没有返回 −1
    bool has(const Cell &p_cell) const { return at(p_cell) != nullptr; }
    bool has_overlap() const { return overlapping; }
    std::vector<CellPart> cells() const; // 每一节：按节点的顺序，节点内按 (y, z, x)
    int count(ModuleKind p_kind) const;
    // 某一列（x, z）有几层；没有这一列为 0
    int column_height(int p_x, int p_z) const;
    // 边界（没有节时全 0）
    int min_x() const;
    int max_x() const;
    int min_z() const;
    int max_z() const;
    int levels() const; // 最高列的层数
    const Module *bridge() const;

    // 不检查合法性的直接改动（编辑函数在检查后调用；重叠由 layout_problem 查出来）
    void put(const Module &p_module);
    void erase(const Cell &p_cell); // 拆掉占着这一格的节点

    bool operator==(const ShipLayout &p_other) const;
    bool operator!=(const ShipLayout &p_other) const { return !(*this == p_other); }
};

ShipLayout default_layout(); // 帆 (0,0,−1) + 驾驶区 (0,0,0)：一艘跑船的小艇

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
    int sections = 0; // 节数
    double mass = 0.0;
    double thrust = 0.0;
    int hold = 0; // 货位数
    int bay = 0; // 船舱格数
    double max_speed = 0.0; // m/s
    double max_climb = 0.0;
    double max_yaw_rate = 0.0; // rad/s
    bool can_fly() const { return thrust > 0.0; }
};

ShipStats layout_stats(const ShipLayout &p_layout);

// 船的局部坐标 (p_x, p_y, p_z) 在不在船上：落在布局某一列的格子里（水平），高度在船底之上、那一列顶层甲板之上 p_head_m 以内
bool layout_contains(const ShipLayout &p_layout, double p_x, double p_y, double p_z, double p_head_m = 2.5);

// 货位：货舱类节点的每一节 4 个（两舷各前后两个，中间留走道），货箱就放在这些位置上；总数 = 船况的 hold。
// 按布局里节点的顺序排、节点内按节 (y, z, x)、每节内 左前 / 左后 / 右前 / 右后，所以改船不动的节点，货位编号不变。
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
// 挂在高列面向低列的那面墙上；只在两列都有节时才有梯子。
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
std::string layout_place(ShipLayout &p_layout, const Cell &p_anchor, ModuleKind p_kind); // 以锚点放一个节点
std::string layout_remove(ShipLayout &p_layout, const Cell &p_cell); // 拆掉占着这一格的整个节点
// 眼下这种节点能放的全部锚点（改装时亮光提示的格子由它们占的格子并起来）
std::vector<Cell> layout_placements(const ShipLayout &p_layout, ModuleKind p_kind);

// 与脚本 / 场景文件之间的转换："x,y,z:kind" 字符串序列（x,y,z 是锚点）
std::vector<std::string> layout_ids(const ShipLayout &p_layout);
std::string module_id(const Module &p_module);
// 认不出的条目跳过；r_ok 报告是否全都认识
ShipLayout layout_from_ids(const std::vector<std::string> &p_ids, bool *r_ok = nullptr);
bool parse_module_id(const std::string &p_text, Module &r_module);
