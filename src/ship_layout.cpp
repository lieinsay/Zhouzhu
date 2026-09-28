#include "ship_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// 推重比 → 航速：默认小艇（帆 1 / 质量 2.1）恰好是切片一的 32 m/s
constexpr double SPEED_PER_RATIO = 67.2;
constexpr double SPEED_CAP = 60.0;
constexpr double CLIMB_PER_RATIO = 21.0;
constexpr double CLIMB_CAP = 16.0;
constexpr double YAW_RATE_TWO_SECTIONS = 0.75; // 两节船的转向率；船越大转得越慢

constexpr double BOW_M = 3.4; // 船头楔伸出最前一排多少

const int N4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }; // (dx, dz)

bool sort_key_less(const Module &a, const Module &b) {
    if (a.at.y != b.at.y) {
        return a.at.y < b.at.y;
    }
    if (a.at.z != b.at.z) {
        return a.at.z < b.at.z;
    }
    return a.at.x < b.at.x;
}

std::string fmt(const char *p_format, double p_value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), p_format, p_value);
    return buffer;
}

} // namespace

namespace modules {

//  种类              id            名字        占格 x y z  质量  推力  船舱 货位   最上 最下  最多 少不了                   拆得   能进   能摆   样子
const ModuleDef DEFS[] = {
    { ModuleKind::BRIDGE, "bridge", "驾驶区", 1, 1, 1, 1.0, 0.0, 0, false, true, false, 1, "没有驾驶区，没处掌舵", false, false, false, ModuleLook::HELM },
    { ModuleKind::STORAGE, "storage", "储存区", 1, 1, 1, 1.2, 0.0, 12, true, false, false, 0, nullptr, true, true, false, ModuleLook::CARGO },
    { ModuleKind::SAIL, "sail", "帆", 1, 1, 1, 1.1, 1.0, 0, false, true, false, 0, nullptr, true, false, false, ModuleLook::SAIL },
    { ModuleKind::FLOATSTONE, "floatstone", "浮石舱", 1, 1, 1, 1.4, 0.7, 0, false, false, true, 0, nullptr, true, false, false, ModuleLook::FLOATSTONE },
    { ModuleKind::DECK, "deck", "空节", 1, 1, 1, 0.6, 0.0, 0, false, false, false, 0, nullptr, true, true, true, ModuleLook::BARE },
    { ModuleKind::LONG_SAIL, "long_sail", "双桅帆", 1, 1, 2, 2.0, 2.4, 0, false, true, false, 0, nullptr, true, false, false, ModuleLook::SAIL },
    { ModuleKind::TALL_HOLD, "tall_hold", "双层货舱", 1, 2, 1, 2.2, 0.0, 30, true, false, false, 0, nullptr, true, true, false, ModuleLook::CARGO },
    { ModuleKind::WIDE_DECK, "wide_deck", "宽空节", 2, 1, 1, 1.1, 0.0, 0, false, false, false, 0, nullptr, true, true, true, ModuleLook::BARE },
};
static_assert(sizeof(DEFS) / sizeof(DEFS[0]) == size_t(ModuleKind::COUNT), "节点表与 ModuleKind 一一对应");

const ModuleDef &def(ModuleKind p_kind) {
    return DEFS[size_t(p_kind)];
}

const ModuleDef *by_id(const std::string &p_id) {
    if (p_id == "power") {
        return &def(ModuleKind::SAIL); // 旧布局里的「动力区」就是帆
    }
    for (const ModuleDef &d : DEFS) {
        if (p_id == d.id) {
            return &d;
        }
    }
    return nullptr;
}

int cells_of(const ModuleDef &p_def) {
    return p_def.size_x * p_def.size_y * p_def.size_z;
}

std::string shape_name(const ModuleDef &p_def) {
    const int n = cells_of(p_def);
    if (n == 1) {
        return "单节";
    }
    if (n == 2) {
        return p_def.size_z == 2 ? "前后双节" : (p_def.size_y == 2 ? "上下双节" : "左右双节");
    }
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%d×%d×%d 节", p_def.size_x, p_def.size_y, p_def.size_z);
    return buffer;
}

std::vector<std::string> rule_tags(const ModuleDef &p_def) {
    std::vector<std::string> tags;
    if (p_def.top_only) {
        tags.push_back("只能在最上面");
    }
    if (p_def.bottom_only) {
        tags.push_back("只能在最下面一层");
    }
    if (p_def.max_count == 1) {
        tags.push_back("只能有一个");
    } else if (p_def.max_count > 1) {
        tags.push_back("最多 " + std::to_string(p_def.max_count) + " 个");
    }
    if (!p_def.removable) {
        tags.push_back("拆不得");
    }
    return tags;
}

std::string function_text(const ModuleDef &p_def) {
    std::string text;
    auto add = [&text](const std::string &p_part) {
        text += (text.empty() ? "" : " · ") + p_part;
    };
    if (p_def.look == ModuleLook::HELM) {
        add("舵在这儿");
    }
    if (p_def.thrust > 0.0) {
        add(fmt("推力 %.1f", p_def.thrust));
    }
    if (p_def.bay > 0) {
        add("船舱 " + std::to_string(p_def.bay) + " 格");
    }
    if (p_def.cargo_deck) {
        add("货位 " + std::to_string(4 * cells_of(p_def)));
    }
    if (text.empty()) {
        text = p_def.furnish ? "没有功能：能摆东西" : "没有功能";
    }
    if (p_def.enterable) {
        add(p_def.size_y > 1 ? "下层能进" : "盖住了能进");
    }
    return text;
}

} // namespace modules

std::vector<Cell> footprint(const Cell &p_anchor, ModuleKind p_kind) {
    const ModuleDef &d = modules::def(p_kind);
    std::vector<Cell> out;
    out.reserve(size_t(modules::cells_of(d)));
    for (int dy = 0; dy < d.size_y; dy++) {
        for (int dz = 0; dz < d.size_z; dz++) {
            for (int dx = 0; dx < d.size_x; dx++) {
                out.push_back({ p_anchor.x + dx, p_anchor.y + dy, p_anchor.z + dz });
            }
        }
    }
    return out;
}

std::vector<Cell> module_cells(const Module &p_module) {
    return footprint(p_module.at, p_module.kind);
}

bool module_covers(const Module &p_module, const Cell &p_cell) {
    const ModuleDef &d = modules::def(p_module.kind);
    const int dx = p_cell.x - p_module.at.x, dy = p_cell.y - p_module.at.y, dz = p_cell.z - p_module.at.z;
    return dx >= 0 && dx < d.size_x && dy >= 0 && dy < d.size_y && dz >= 0 && dz < d.size_z;
}

ShipLayout::ShipLayout() {
    grid.fill(-1);
}

ShipLayout::ShipLayout(std::vector<Module> p_modules) :
        modules(std::move(p_modules)) {
    reindex();
}

int ShipLayout::grid_index(const Cell &p_cell) {
    if (!cell_in_range(p_cell)) {
        return -1;
    }
    return (p_cell.y * GRID_Z + (p_cell.z + MAX_Z)) * GRID_X + (p_cell.x + MAX_X);
}

void ShipLayout::reindex() {
    std::stable_sort(modules.begin(), modules.end(), sort_key_less);
    grid.fill(-1);
    overlapping = false;
    cell_total = 0;
    for (size_t k = 0; k < modules.size(); k++) {
        for (const Cell &c : module_cells(modules[k])) {
            cell_total++;
            const int i = grid_index(c);
            if (i < 0) {
                continue;
            }
            if (grid[size_t(i)] >= 0) {
                overlapping = true;
                continue;
            }
            grid[size_t(i)] = int8_t(k);
        }
    }
}

int ShipLayout::module_index_at(const Cell &p_cell) const {
    const int i = grid_index(p_cell);
    return i < 0 ? -1 : grid[size_t(i)];
}

const Module *ShipLayout::at(const Cell &p_cell) const {
    const int k = module_index_at(p_cell);
    return k < 0 ? nullptr : &modules[size_t(k)];
}

std::vector<CellPart> ShipLayout::cells() const {
    std::vector<CellPart> out;
    out.reserve(size_t(cell_total));
    for (size_t k = 0; k < modules.size(); k++) {
        for (const Cell &c : module_cells(modules[k])) {
            out.push_back({ c, int(k) });
        }
    }
    return out;
}

int ShipLayout::count(ModuleKind p_kind) const {
    int n = 0;
    for (const Module &m : modules) {
        n += m.kind == p_kind ? 1 : 0;
    }
    return n;
}

int ShipLayout::column_height(int p_x, int p_z) const {
    for (int y = MAX_LEVELS - 1; y >= 0; y--) {
        if (has({ p_x, y, p_z })) {
            return y + 1;
        }
    }
    return 0;
}

int ShipLayout::min_x() const {
    int v = 0;
    for (const CellPart &c : cells()) {
        v = std::min(v, c.at.x);
    }
    return v;
}

int ShipLayout::max_x() const {
    int v = 0;
    for (const CellPart &c : cells()) {
        v = std::max(v, c.at.x);
    }
    return v;
}

int ShipLayout::min_z() const {
    int v = 0;
    for (const CellPart &c : cells()) {
        v = std::min(v, c.at.z);
    }
    return v;
}

int ShipLayout::max_z() const {
    int v = 0;
    for (const CellPart &c : cells()) {
        v = std::max(v, c.at.z);
    }
    return v;
}

int ShipLayout::levels() const {
    int v = 0;
    for (const CellPart &c : cells()) {
        v = std::max(v, c.at.y + 1);
    }
    return v;
}

const Module *ShipLayout::bridge() const {
    for (const Module &m : modules) {
        if (m.kind == ModuleKind::BRIDGE) {
            return &m;
        }
    }
    return nullptr;
}

void ShipLayout::put(const Module &p_module) {
    modules.push_back(p_module);
    reindex();
}

void ShipLayout::erase(const Cell &p_cell) {
    const int k = module_index_at(p_cell);
    if (k < 0) {
        return;
    }
    modules.erase(modules.begin() + k);
    reindex();
}

bool ShipLayout::operator==(const ShipLayout &p_other) const {
    if (modules.size() != p_other.modules.size()) {
        return false;
    }
    for (size_t k = 0; k < modules.size(); k++) {
        if (modules[k].at != p_other.modules[k].at || modules[k].kind != p_other.modules[k].kind) {
            return false;
        }
    }
    return true;
}

ShipLayout default_layout() {
    return ShipLayout({ { { 0, 0, -1 }, ModuleKind::SAIL }, { { 0, 0, 0 }, ModuleKind::BRIDGE } });
}

double cell_center_x(int p_x) {
    return p_x * CELL_WIDTH_M;
}

double cell_deck_y(int p_y) {
    return p_y * CELL_HEIGHT_M;
}

double cell_center_z(int p_z) {
    return p_z * CELL_LENGTH_M;
}

double port_edge_x(const ShipLayout &p_layout) {
    int x = 0;
    for (int k = -MAX_X; k <= 0; k++) {
        if (p_layout.has({ k, 0, 0 })) {
            x = k;
            break;
        }
    }
    return cell_center_x(x) - 0.5 * CELL_WIDTH_M;
}

double starboard_edge_x(const ShipLayout &p_layout) {
    int x = 0;
    for (int k = MAX_X; k >= 0; k--) {
        if (p_layout.has({ k, 0, 0 })) {
            x = k;
            break;
        }
    }
    return cell_center_x(x) + 0.5 * CELL_WIDTH_M;
}

double bow_z(const ShipLayout &p_layout) {
    return cell_center_z(p_layout.min_z()) - 0.5 * CELL_LENGTH_M - BOW_M;
}

double stern_z(const ShipLayout &p_layout) {
    return cell_center_z(p_layout.max_z()) + 0.5 * CELL_LENGTH_M;
}

std::vector<HoldSlot> hold_slots(const ShipLayout &p_layout) {
    std::vector<HoldSlot> out;
    for (const Module &m : p_layout.get_modules()) {
        if (!modules::def(m.kind).cargo_deck) {
            continue;
        }
        const bool enterable = modules::def(m.kind).enterable;
        for (const Cell &c : module_cells(m)) {
            const bool open = enterable || !p_layout.has({ c.x, c.y + 1, c.z }); // 能进的舱里也够得着
            for (int side = -1; side <= 1; side += 2) {
                for (int end = -1; end <= 1; end += 2) {
                    HoldSlot slot;
                    slot.cell = c;
                    slot.x = cell_center_x(c.x) + side * HOLD_SLOT_SIDE_M;
                    slot.y = cell_deck_y(c.y);
                    slot.z = cell_center_z(c.z) + end * HOLD_SLOT_ALONG_M;
                    slot.open = open;
                    out.push_back(slot);
                }
            }
        }
    }
    return out;
}

ShipStats layout_stats(const ShipLayout &p_layout) {
    ShipStats s;
    s.sections = p_layout.cell_count();
    for (const Module &m : p_layout.get_modules()) {
        const ModuleDef &d = modules::def(m.kind);
        s.mass += d.mass;
        s.thrust += d.thrust;
        s.bay += d.bay;
        s.hold += d.cargo_deck ? 4 * modules::cells_of(d) : 0;
    }
    if (s.sections == 0 || s.thrust <= 0.0) {
        return s;
    }
    const double ratio = s.thrust / s.mass;
    s.max_speed = std::min(SPEED_CAP, SPEED_PER_RATIO * ratio);
    s.max_climb = std::min(CLIMB_CAP, CLIMB_PER_RATIO * ratio);
    s.max_yaw_rate = YAW_RATE_TWO_SECTIONS * std::sqrt(2.0 / s.sections);
    return s;
}

bool layout_contains(const ShipLayout &p_layout, double p_x, double p_y, double p_z, double p_head_m) {
    const int x = int(std::lround(p_x / CELL_WIDTH_M)), z = int(std::lround(p_z / CELL_LENGTH_M));
    const int h = p_layout.column_height(x, z);
    return h > 0 && p_y >= -1.0 && p_y <= cell_deck_y(h - 1) + p_head_m; // 顶层开放甲板在第 h − 1 层的格底
}

std::vector<Ladder> layout_ladders(const ShipLayout &p_layout) {
    std::vector<Ladder> ladders;
    for (int z = -MAX_Z; z <= MAX_Z; z++) {
        for (int x = -MAX_X; x <= MAX_X; x++) {
            const int low = p_layout.column_height(x, z);
            if (low == 0) {
                continue;
            }
            for (const auto &d : N4) {
                const int high = p_layout.column_height(x + d[0], z + d[1]);
                if (high > low) {
                    ladders.push_back({ x, z, x + d[0], z + d[1], low, high });
                }
            }
        }
    }
    return ladders;
}

bool cell_in_range(const Cell &p_cell) {
    return std::abs(p_cell.x) <= MAX_X && p_cell.y >= 0 && p_cell.y < MAX_LEVELS && std::abs(p_cell.z) <= MAX_Z;
}

std::string layout_problem(const ShipLayout &p_layout) {
    if (p_layout.empty()) {
        return "船至少要有一节";
    }
    if (p_layout.cell_count() > MAX_CELLS) {
        return "最多 " + std::to_string(MAX_CELLS) + " 节";
    }
    const std::vector<CellPart> cells = p_layout.cells();
    for (const CellPart &c : cells) {
        if (!cell_in_range(c.at)) {
            return "超出了船坞能造的范围";
        }
    }
    if (p_layout.has_overlap()) {
        return "这里已经有节了";
    }
    if (!p_layout.has({ 0, 0, 0 })) {
        return "舷门那一格不能空";
    }

    // 节点表的规矩：数目、只能在最下面一层、只能在最上面
    for (const ModuleDef &d : modules::DEFS) {
        const int n = p_layout.count(d.kind);
        if (d.max_count > 0 && n > d.max_count) {
            return std::string(d.name) + (d.max_count == 1 ? "只能有一个" : "最多 " + std::to_string(d.max_count) + " 个");
        }
        if (d.required && n == 0) {
            return d.required;
        }
    }
    for (const Module &m : p_layout.get_modules()) {
        const ModuleDef &d = modules::def(m.kind);
        if (d.bottom_only && m.at.y != 0) {
            return std::string(d.name) + "只能在最下面一层";
        }
    }
    for (const CellPart &c : cells) {
        if (c.at.y > 0 && !p_layout.has({ c.at.x, c.at.y - 1, c.at.z })) {
            return "悬空了：下面没有节撑着";
        }
    }
    for (const CellPart &c : cells) {
        const Module &m = p_layout.get_modules()[size_t(c.module)];
        const ModuleDef &d = modules::def(m.kind);
        if (!d.top_only) {
            continue;
        }
        const int above = p_layout.module_index_at({ c.at.x, c.at.y + 1, c.at.z });
        if (above >= 0 && above != c.module) {
            return std::string(d.name) + "只能在最上面：顶上不能再搭";
        }
    }

    // 列与列面连相连：从舷门那一列走遍所有列
    std::vector<uint8_t> seen(size_t((2 * MAX_X + 1) * (2 * MAX_Z + 1)), 0);
    auto index = [](int x, int z) { return size_t((z + MAX_Z) * (2 * MAX_X + 1) + (x + MAX_X)); };
    std::vector<std::pair<int, int>> stack = { { 0, 0 } };
    seen[index(0, 0)] = 1;
    int reached = 0;
    while (!stack.empty()) {
        const auto [x, z] = stack.back();
        stack.pop_back();
        reached++;
        for (const auto &d : N4) {
            const int nx = x + d[0], nz = z + d[1];
            if (std::abs(nx) > MAX_X || std::abs(nz) > MAX_Z || seen[index(nx, nz)] || p_layout.column_height(nx, nz) == 0) {
                continue;
            }
            seen[index(nx, nz)] = 1;
            stack.push_back({ nx, nz });
        }
    }
    int columns = 0;
    for (int z = -MAX_Z; z <= MAX_Z; z++) {
        for (int x = -MAX_X; x <= MAX_X; x++) {
            columns += p_layout.column_height(x, z) > 0 ? 1 : 0;
        }
    }
    if (reached < columns) {
        return "有的节没和船身连上";
    }

    // 舷门那一排最宽，且两头的格子上面不搭
    int gate_min = 0, gate_max = 0;
    for (const CellPart &c : cells) {
        if (c.at.y == 0 && c.at.z == 0) {
            gate_min = std::min(gate_min, c.at.x);
            gate_max = std::max(gate_max, c.at.x);
        }
    }
    if (p_layout.min_x() < gate_min || p_layout.max_x() > gate_max) {
        return "舷门那一排得是最宽的一排，不然靠不上码头";
    }
    if (p_layout.column_height(gate_min, 0) > 1 || p_layout.column_height(gate_max, 0) > 1) {
        return "舷门两头的格子上面不能搭：跳板要接甲板";
    }
    return std::string();
}

std::string layout_place(ShipLayout &p_layout, const Cell &p_anchor, ModuleKind p_kind) {
    const std::vector<Cell> cells = footprint(p_anchor, p_kind);
    for (const Cell &c : cells) {
        if (!cell_in_range(c)) {
            return "超出了船坞能造的范围";
        }
    }
    for (const Cell &c : cells) {
        if (p_layout.has(c)) {
            return "这里已经有节了";
        }
    }
    ShipLayout next = p_layout;
    next.put({ p_anchor, p_kind });
    const std::string problem = layout_problem(next);
    if (problem.empty()) {
        p_layout = next;
    }
    return problem;
}

std::string layout_remove(ShipLayout &p_layout, const Cell &p_cell) {
    const int k = p_layout.module_index_at(p_cell);
    if (k < 0) {
        return "这一格没有节";
    }
    const Module m = p_layout.get_modules()[size_t(k)];
    const ModuleDef &d = modules::def(m.kind);
    if (!d.removable) {
        return std::string(d.name) + "拆不得";
    }
    for (const Cell &c : module_cells(m)) {
        const int above = p_layout.module_index_at({ c.x, c.y + 1, c.z });
        if (above >= 0 && above != k) {
            return "上面还搭着节，先拆上面的";
        }
    }
    ShipLayout next = p_layout;
    next.erase(m.at);
    const std::string problem = layout_problem(next);
    if (problem.empty()) {
        p_layout = next;
    }
    return problem;
}

std::vector<Cell> layout_placements(const ShipLayout &p_layout, ModuleKind p_kind) {
    std::vector<Cell> out;
    for (int y = 0; y < MAX_LEVELS; y++) {
        for (int z = -MAX_Z; z <= MAX_Z; z++) {
            for (int x = -MAX_X; x <= MAX_X; x++) {
                const Cell anchor{ x, y, z };
                // 只试贴着已有节的：不贴着的连不上船身，省得整个格子空间都试一遍
                bool touching = false;
                for (const Cell &c : footprint(anchor, p_kind)) {
                    touching = touching || p_layout.has({ c.x, c.y - 1, c.z }) || p_layout.has({ c.x, c.y + 1, c.z });
                    for (const auto &d : N4) {
                        touching = touching || p_layout.has({ c.x + d[0], c.y, c.z + d[1] });
                    }
                }
                if (!touching) {
                    continue;
                }
                ShipLayout trial = p_layout;
                if (layout_place(trial, anchor, p_kind).empty()) {
                    out.push_back(anchor);
                }
            }
        }
    }
    return out;
}

std::string module_id(const Module &p_module) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%d,%d,%d:%s", p_module.at.x, p_module.at.y, p_module.at.z, modules::def(p_module.kind).id);
    return buffer;
}

std::vector<std::string> layout_ids(const ShipLayout &p_layout) {
    std::vector<std::string> ids;
    ids.reserve(p_layout.get_modules().size());
    for (const Module &m : p_layout.get_modules()) {
        ids.push_back(module_id(m));
    }
    return ids;
}

bool parse_module_id(const std::string &p_text, Module &r_module) {
    const size_t colon = p_text.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    int x = 0, y = 0, z = 0;
    char tail = 0;
    if (std::sscanf(p_text.substr(0, colon).c_str(), "%d,%d,%d%c", &x, &y, &z, &tail) != 3) {
        return false;
    }
    const ModuleDef *d = modules::by_id(p_text.substr(colon + 1));
    if (!d) {
        return false;
    }
    r_module = { { x, y, z }, d->kind };
    return true;
}

ShipLayout layout_from_ids(const std::vector<std::string> &p_ids, bool *r_ok) {
    std::vector<Module> list;
    bool ok = true;
    for (const std::string &id : p_ids) {
        Module m;
        if (parse_module_id(id, m)) {
            list.push_back(m);
        } else {
            ok = false;
        }
    }
    if (r_ok) {
        *r_ok = ok;
    }
    return ShipLayout(list);
}
