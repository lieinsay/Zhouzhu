#include "ship_layout.h"

#include <algorithm>
#include <cmath>

namespace {

// 推重比 → 航速：默认小艇（动力 1 / 质量 2.1）恰好是切片一的 32 m/s
constexpr double SPEED_PER_RATIO = 67.2;
constexpr double SPEED_CAP = 60.0;
constexpr double CLIMB_PER_RATIO = 21.0;
constexpr double CLIMB_CAP = 16.0;
constexpr double YAW_RATE_TWO_SECTIONS = 0.75; // 两节船的转向率；船越长转得越慢

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

ShipLayout default_layout() {
    return { ModuleKind::POWER, ModuleKind::BRIDGE };
}

ShipStats layout_stats(const ShipLayout &p_layout) {
    ShipStats s;
    s.sections = int(p_layout.size());
    for (ModuleKind kind : p_layout) {
        const ModuleDef &d = modules::def(kind);
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

int fore_sections(int p_count) {
    return p_count / 2;
}

double section_front_z(const ShipLayout &p_layout, int p_index) {
    return (p_index - fore_sections(int(p_layout.size()))) * SECTION_LENGTH_M;
}

double layout_bow_z(const ShipLayout &p_layout) {
    return section_front_z(p_layout, 0);
}

double layout_stern_z(const ShipLayout &p_layout) {
    return section_front_z(p_layout, int(p_layout.size()));
}

int bridge_index(const ShipLayout &p_layout) {
    for (size_t k = 0; k < p_layout.size(); k++) {
        if (p_layout[k] == ModuleKind::BRIDGE) {
            return int(k);
        }
    }
    return -1;
}

std::string layout_problem(const ShipLayout &p_layout) {
    if (p_layout.empty()) {
        return "船至少要有一节";
    }
    if (int(p_layout.size()) > MAX_SECTIONS) {
        return "最多 " + std::to_string(MAX_SECTIONS) + " 节";
    }
    const int bridges = int(std::count(p_layout.begin(), p_layout.end(), ModuleKind::BRIDGE));
    if (bridges == 0) {
        return "没有驾驶区，没处掌舵";
    }
    if (bridges > 1) {
        return "驾驶区只能有一个";
    }
    return std::string();
}

namespace {

bool in_range(const ShipLayout &p_layout, int p_index) {
    return p_index >= 0 && p_index < int(p_layout.size());
}

// 试着换成新布局：不合法就不动
std::string try_apply(ShipLayout &p_layout, const ShipLayout &p_next) {
    const std::string problem = layout_problem(p_next);
    if (problem.empty()) {
        p_layout = p_next;
    }
    return problem;
}

} // namespace

std::string layout_set_kind(ShipLayout &p_layout, int p_index, ModuleKind p_kind) {
    if (!in_range(p_layout, p_index)) {
        return "没有这一节";
    }
    if (p_layout[size_t(p_index)] == p_kind) {
        return std::string();
    }
    if (p_layout[size_t(p_index)] == ModuleKind::BRIDGE) {
        return "驾驶区只有这一个，换不得";
    }
    if (p_kind == ModuleKind::BRIDGE) {
        return "已经有驾驶区了";
    }
    ShipLayout next = p_layout;
    next[size_t(p_index)] = p_kind;
    return try_apply(p_layout, next);
}

std::string layout_cycle_kind(ShipLayout &p_layout, int p_index, int p_direction) {
    if (!in_range(p_layout, p_index)) {
        return "没有这一节";
    }
    const int n = int(ModuleKind::COUNT);
    const int step = p_direction >= 0 ? 1 : n - 1;
    int kind = int(p_layout[size_t(p_index)]);
    std::string reason;
    for (int tries = 0; tries < n - 1; tries++) {
        kind = (kind + step) % n;
        reason = layout_set_kind(p_layout, p_index, ModuleKind(kind));
        if (reason.empty()) {
            return reason;
        }
    }
    return reason;
}

std::string layout_insert_after(ShipLayout &p_layout, int p_index, ModuleKind p_kind) {
    if (p_index < -1 || p_index >= int(p_layout.size())) {
        return "没有这一节";
    }
    ShipLayout next = p_layout;
    next.insert(next.begin() + (p_index + 1), p_kind);
    return try_apply(p_layout, next);
}

std::string layout_remove(ShipLayout &p_layout, int p_index) {
    if (!in_range(p_layout, p_index)) {
        return "没有这一节";
    }
    if (p_layout[size_t(p_index)] == ModuleKind::BRIDGE) {
        return "驾驶区拆不得";
    }
    ShipLayout next = p_layout;
    next.erase(next.begin() + p_index);
    return try_apply(p_layout, next);
}

std::vector<std::string> layout_ids(const ShipLayout &p_layout) {
    std::vector<std::string> ids;
    ids.reserve(p_layout.size());
    for (ModuleKind kind : p_layout) {
        ids.emplace_back(modules::def(kind).id);
    }
    return ids;
}

ShipLayout layout_from_ids(const std::vector<std::string> &p_ids, bool *r_ok) {
    ShipLayout layout;
    bool ok = true;
    for (const std::string &id : p_ids) {
        const ModuleDef *d = modules::by_id(id);
        if (d) {
            layout.push_back(d->kind);
        } else {
            ok = false;
        }
    }
    if (r_ok) {
        *r_ok = ok;
    }
    return layout;
}
