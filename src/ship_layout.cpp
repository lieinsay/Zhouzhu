#include "ship_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {

// 推重比 → 航速：默认小艇（动力 1 / 质量 2.1）恰好是切片一的 32 m/s
constexpr double SPEED_PER_RATIO = 67.2;
constexpr double SPEED_CAP = 60.0;
constexpr double CLIMB_PER_RATIO = 21.0;
constexpr double CLIMB_CAP = 16.0;
constexpr double YAW_RATE_TWO_SECTIONS = 0.75; // 两节船的转向率；船越大转得越慢

constexpr double BOW_M = 2.4; // 船头楔伸出最前一排多少

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

} // namespace

namespace modules {

const ModuleDef DEFS[] = {
    { ModuleKind::BRIDGE, "bridge", "驾驶区", 1.0, 0.0, 0 },
    { ModuleKind::STORAGE, "storage", "储存区", 1.2, 0.0, 4 },
    { ModuleKind::POWER, "power", "动力区", 1.1, 1.0, 0 },
};

const ModuleDef &def(ModuleKind p_kind) {
    return DEFS[size_t(p_kind)];
}

const ModuleDef *by_id(const std::string &p_id) {
    for (const ModuleDef &d : DEFS) {
        if (p_id == d.id) {
            return &d;
        }
    }
    return nullptr;
}

} // namespace modules

ShipLayout::ShipLayout(std::vector<Module> p_modules) :
        modules(std::move(p_modules)) {
    sort();
}

void ShipLayout::sort() {
    std::stable_sort(modules.begin(), modules.end(), sort_key_less);
}

const Module *ShipLayout::at(const Cell &p_cell) const {
    for (const Module &m : modules) {
        if (m.at == p_cell) {
            return &m;
        }
    }
    return nullptr;
}

int ShipLayout::column_height(int p_x, int p_z) const {
    int height = 0;
    for (const Module &m : modules) {
        if (m.at.x == p_x && m.at.z == p_z) {
            height = std::max(height, m.at.y + 1);
        }
    }
    return height;
}

int ShipLayout::min_x() const {
    int v = 0;
    for (const Module &m : modules) {
        v = std::min(v, m.at.x);
    }
    return v;
}

int ShipLayout::max_x() const {
    int v = 0;
    for (const Module &m : modules) {
        v = std::max(v, m.at.x);
    }
    return v;
}

int ShipLayout::min_z() const {
    int v = 0;
    for (const Module &m : modules) {
        v = std::min(v, m.at.z);
    }
    return v;
}

int ShipLayout::max_z() const {
    int v = 0;
    for (const Module &m : modules) {
        v = std::max(v, m.at.z);
    }
    return v;
}

int ShipLayout::levels() const {
    int v = 0;
    for (const Module &m : modules) {
        v = std::max(v, m.at.y + 1);
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
    erase(p_module.at);
    modules.push_back(p_module);
    sort();
}

void ShipLayout::erase(const Cell &p_cell) {
    modules.erase(std::remove_if(modules.begin(), modules.end(), [&](const Module &m) { return m.at == p_cell; }), modules.end());
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
    return ShipLayout({ { { 0, 0, -1 }, ModuleKind::POWER }, { { 0, 0, 0 }, ModuleKind::BRIDGE } });
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
    for (const Module &m : p_layout.get_modules()) {
        if (m.at.y == 0 && m.at.z == 0) {
            x = std::min(x, m.at.x);
        }
    }
    return cell_center_x(x) - 0.5 * CELL_WIDTH_M;
}

double starboard_edge_x(const ShipLayout &p_layout) {
    int x = 0;
    for (const Module &m : p_layout.get_modules()) {
        if (m.at.y == 0 && m.at.z == 0) {
            x = std::max(x, m.at.x);
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

ShipStats layout_stats(const ShipLayout &p_layout) {
    ShipStats s;
    s.sections = p_layout.size();
    for (const Module &m : p_layout.get_modules()) {
        const ModuleDef &d = modules::def(m.kind);
        s.mass += d.mass;
        s.thrust += d.thrust;
        s.hold += d.hold;
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
    if (p_layout.size() > MAX_MODULES) {
        return "最多 " + std::to_string(MAX_MODULES) + " 节";
    }
    for (const Module &m : p_layout.get_modules()) {
        if (!cell_in_range(m.at)) {
            return "超出了船坞能造的范围";
        }
    }
    if (!p_layout.has({ 0, 0, 0 })) {
        return "舷门那一格不能空";
    }
    int bridges = 0;
    for (const Module &m : p_layout.get_modules()) {
        if (m.kind == ModuleKind::BRIDGE) {
            bridges++;
            if (p_layout.has({ m.at.x, m.at.y + 1, m.at.z })) {
                return "驾驶区顶上不能再搭";
            }
        }
        if (m.at.y > 0 && !p_layout.has({ m.at.x, m.at.y - 1, m.at.z })) {
            return "悬空了：下面没有节撑着";
        }
    }
    if (bridges == 0) {
        return "没有驾驶区，没处掌舵";
    }
    if (bridges > 1) {
        return "驾驶区只能有一个";
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
    for (const Module &m : p_layout.get_modules()) {
        if (m.at.y == 0 && m.at.z == 0) {
            gate_min = std::min(gate_min, m.at.x);
            gate_max = std::max(gate_max, m.at.x);
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

namespace {

std::string try_apply(ShipLayout &p_layout, const ShipLayout &p_next) {
    const std::string problem = layout_problem(p_next);
    if (problem.empty()) {
        p_layout = p_next;
    }
    return problem;
}

} // namespace

std::string layout_place(ShipLayout &p_layout, const Cell &p_cell, ModuleKind p_kind) {
    if (!cell_in_range(p_cell)) {
        return "超出了船坞能造的范围";
    }
    if (p_layout.has(p_cell)) {
        return "这一格已经有节了";
    }
    ShipLayout next = p_layout;
    next.put({ p_cell, p_kind });
    return try_apply(p_layout, next);
}

std::string layout_set_kind(ShipLayout &p_layout, const Cell &p_cell, ModuleKind p_kind) {
    const Module *m = p_layout.at(p_cell);
    if (!m) {
        return "这一格没有节";
    }
    if (m->kind == p_kind) {
        return std::string();
    }
    if (m->kind == ModuleKind::BRIDGE) {
        return "驾驶区只有这一个，换不得";
    }
    if (p_kind == ModuleKind::BRIDGE) {
        return "已经有驾驶区了";
    }
    ShipLayout next = p_layout;
    next.put({ p_cell, p_kind });
    return try_apply(p_layout, next);
}

std::string layout_remove(ShipLayout &p_layout, const Cell &p_cell) {
    const Module *m = p_layout.at(p_cell);
    if (!m) {
        return "这一格没有节";
    }
    if (m->kind == ModuleKind::BRIDGE) {
        return "驾驶区拆不得";
    }
    if (p_layout.has({ p_cell.x, p_cell.y + 1, p_cell.z })) {
        return "上面还搭着节，先拆上面的";
    }
    ShipLayout next = p_layout;
    next.erase(p_cell);
    return try_apply(p_layout, next);
}

std::vector<Cell> layout_candidates(const ShipLayout &p_layout) {
    std::vector<Cell> out;
    for (int y = 0; y < MAX_LEVELS; y++) {
        for (int z = -MAX_Z; z <= MAX_Z; z++) {
            for (int x = -MAX_X; x <= MAX_X; x++) {
                const Cell cell{ x, y, z };
                if (p_layout.has(cell)) {
                    continue;
                }
                // 只看贴着已有节的格子，省得整个格子空间都试一遍
                bool adjacent = p_layout.has({ x, y - 1, z }) || p_layout.has({ x, y + 1, z });
                for (const auto &d : N4) {
                    adjacent = adjacent || p_layout.has({ x + d[0], y, z + d[1] });
                }
                if (!adjacent) {
                    continue;
                }
                ShipLayout trial = p_layout;
                if (layout_place(trial, cell, ModuleKind::STORAGE).empty()) {
                    out.push_back(cell);
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
