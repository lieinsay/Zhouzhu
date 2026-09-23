#include "interior_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>

using namespace house_dims;

namespace {

constexpr double DOOR_CLEAR = 1.2; // 外门往里要空着多深
constexpr double DOORWAY_CLEAR = 0.9; // 门洞两侧各要空着多深
constexpr double WALL_REACH = 0.8; // 光标离墙内皮这么近算「在墙边」
constexpr double CORNER_GAP = 0.25; // 门窗离墙角至少这么多
constexpr double OPENING_GAP = 0.3; // 同一面墙上两个开口之间
constexpr double PARTITION_HIT = 0.35; // 光标离隔墙中线这么近算点中
constexpr double SNAP_TO_WALL = 0.3; // 隔墙端点离墙这么近就接上去
constexpr double MIN_PARTITION = 1.0;
constexpr double GRID = 0.25; // 走得通的格
constexpr double UNREACHABLE_M2 = 0.75;
constexpr double EPS = 1e-6;

InteriorRect rect_around(double p_x, double p_z, double p_sx, double p_sz) {
    return InteriorRect{ p_x - 0.5 * p_sx, p_z - 0.5 * p_sz, p_x + 0.5 * p_sx, p_z + 0.5 * p_sz };
}

// 梯子上的一条带：沿梯板 ± p_half，沿「人站那侧」从 p_out0 到 p_out1
InteriorRect ladder_band(const LadderSpot &p_ladder, double p_half, double p_out0, double p_out1) {
    double ox = 0.0, oz = 0.0;
    ladder_outward(p_ladder.quarter, ox, oz);
    const double ax = std::abs(oz), az = std::abs(ox);
    double x0 = 1e18, z0 = 1e18, x1 = -1e18, z1 = -1e18;
    for (int a = -1; a <= 1; a += 2) {
        for (double out : { p_out0, p_out1 }) {
            const double x = p_ladder.x + a * p_half * ax + out * ox, z = p_ladder.z + a * p_half * az + out * oz;
            x0 = std::min(x0, x), z0 = std::min(z0, z), x1 = std::max(x1, x), z1 = std::max(z1, z);
        }
    }
    return InteriorRect{ x0, z0, x1, z1 };
}

InteriorRect interior_rect(const BuildingDef &p_def, double p_inset = 0.0) {
    const double hx = interior_half_x(p_def) - p_inset, hz = interior_half_z(p_def) - p_inset;
    return InteriorRect{ -hx, -hz, hx, hz };
}

bool inside(const InteriorRect &p_outer, const InteriorRect &p_inner) {
    return p_inner.x0 >= p_outer.x0 - EPS && p_inner.z0 >= p_outer.z0 - EPS && p_inner.x1 <= p_outer.x1 + EPS && p_inner.z1 <= p_outer.z1 + EPS;
}

bool horizontal(const Partition &p_partition) {
    return std::abs(p_partition.z1 - p_partition.z0) < EPS;
}

// 点到隔墙中线的距离、落点沿墙的坐标
double partition_distance(const Partition &p_partition, double p_x, double p_z, double &r_along) {
    if (horizontal(p_partition)) {
        const double lo = std::min(p_partition.x0, p_partition.x1), hi = std::max(p_partition.x0, p_partition.x1);
        r_along = p_x;
        const double dx = p_x < lo ? lo - p_x : (p_x > hi ? p_x - hi : 0.0);
        return std::hypot(dx, p_z - p_partition.z0);
    }
    const double lo = std::min(p_partition.z0, p_partition.z1), hi = std::max(p_partition.z0, p_partition.z1);
    r_along = p_z;
    const double dz = p_z < lo ? lo - p_z : (p_z > hi ? p_z - hi : 0.0);
    return std::hypot(dz, p_x - p_partition.x0);
}

InteriorRect partition_rect(const Partition &p_partition) {
    return InteriorRect{ std::min(p_partition.x0, p_partition.x1) - (horizontal(p_partition) ? 0.0 : 0.5 * PARTITION_T),
        std::min(p_partition.z0, p_partition.z1) - (horizontal(p_partition) ? 0.5 * PARTITION_T : 0.0),
        std::max(p_partition.x0, p_partition.x1) + (horizontal(p_partition) ? 0.0 : 0.5 * PARTITION_T),
        std::max(p_partition.z0, p_partition.z1) + (horizontal(p_partition) ? 0.5 * PARTITION_T : 0.0) };
}

// 门洞落在哪段隔墙上（同层、离中线 0.15 m 内）；没有 −1
int partition_of(const HouseTemplate &p_template, const Doorway &p_doorway) {
    for (size_t k = 0; k < p_template.partitions.size(); k++) {
        const Partition &p = p_template.partitions[k];
        double along = 0.0;
        if (p.level == p_doorway.level && partition_distance(p, p_doorway.x, p_doorway.z, along) < 0.15) {
            return int(k);
        }
    }
    return -1;
}

InteriorRect doorway_clear_rect(const HouseTemplate &p_template, const Doorway &p_doorway) {
    const int k = partition_of(p_template, p_doorway);
    const bool across_z = k < 0 || horizontal(p_template.partitions[size_t(k)]); // 横墙上的门洞，人沿 z 穿过
    return across_z ? rect_around(p_doorway.x, p_doorway.z, DOORWAY_W, 2 * DOORWAY_CLEAR + PARTITION_T)
                    : rect_around(p_doorway.x, p_doorway.z, 2 * DOORWAY_CLEAR + PARTITION_T, DOORWAY_W);
}

InteriorRect door_clear_rect(const BuildingDef &p_def, const Opening &p_door) {
    double ax = 0.0, az = 0.0, ox = 0.0, oz = 0.0;
    side_axes(p_door.side, ax, az, ox, oz);
    double x0 = 0.0, z0 = 0.0, x1 = 0.0, z1 = 0.0;
    Opening edge = p_door;
    opening_point(p_def, edge, 0.0, x0, z0);
    opening_point(p_def, edge, DOOR_CLEAR, x1, z1);
    const double half = 0.5 * p_door.width + 0.1;
    return InteriorRect{ std::min(x0, x1) - half * ax, std::min(z0, z1) - half * az, std::max(x0, x1) + half * ax, std::max(z0, z1) + half * az };
}

// 一层里的东西（p_skip_* 是检查某一样自己时要当它不在的那一个）
struct LevelThings {
    std::vector<std::pair<InteriorRect, std::string>> solid; // 挡人：家具、隔墙、梯板
    std::vector<std::pair<InteriorRect, std::string>> keep_clear; // 要空着：门前、门洞两侧、梯脚、梯口、落脚处
};

struct Skip {
    int item = -1, ladder = -1, opening = -1, doorway = -1, partition = -1;
};

// p_walking：只要挡人的（走得通的格用）；否则家具都算（放东西时席子上也不叠别的）
LevelThings gather(const HouseTemplate &p_template, const BuildingDef &p_def, int p_level, const Skip &p_skip, bool p_walking = false) {
    LevelThings t;
    for (size_t k = 0; k < p_template.items.size(); k++) {
        const Furnishing &f = p_template.items[k];
        const FurnitureDef *def = furniture::by_id(f.kind);
        if (int(k) != p_skip.item && f.level == p_level && !(p_walking && def && !def->solid)) {
            t.solid.push_back({ furnishing_rect(f), def ? def->name : f.kind });
        }
    }
    for (size_t k = 0; k < p_template.partitions.size(); k++) {
        if (int(k) != p_skip.partition && p_template.partitions[k].level == p_level) {
            for (const InteriorRect &piece : partition_pieces(p_template, int(k))) {
                t.solid.push_back({ piece, "隔墙" });
            }
        }
    }
    for (size_t k = 0; k < p_template.ladders.size(); k++) {
        const LadderSpot &l = p_template.ladders[k];
        if (int(k) == p_skip.ladder) {
            continue;
        }
        if (l.level == p_level) {
            t.solid.push_back({ ladder_board_rect(l), "梯子" });
            t.keep_clear.push_back({ ladder_foot_rect(l), "梯脚" });
        } else if (l.level + 1 == p_level) {
            t.keep_clear.push_back({ ladder_hatch_rect(l), "梯口" });
            t.keep_clear.push_back({ ladder_landing_rect(l), "梯口的落脚处" });
        }
    }
    for (size_t k = 0; k < p_template.openings.size(); k++) {
        const Opening &o = p_template.openings[k];
        if (int(k) != p_skip.opening && !o.window && o.level == p_level) {
            t.keep_clear.push_back({ door_clear_rect(p_def, o), "门" });
        }
    }
    for (size_t k = 0; k < p_template.doorways.size(); k++) {
        const Doorway &d = p_template.doorways[k];
        if (int(k) != p_skip.doorway && d.level == p_level) {
            t.keep_clear.push_back({ doorway_clear_rect(p_template, d), "门洞" });
        }
    }
    return t;
}

std::string level_check(const BuildingDef &p_def, int p_level) {
    if (p_level < 0 || p_level >= p_def.levels) {
        return std::string("这座只有 ") + std::to_string(p_def.levels) + " 层";
    }
    return std::string();
}

std::string check_item(const HouseTemplate &p_template, const BuildingDef &p_def, const Furnishing &p_item, int p_self) {
    const FurnitureDef *def = furniture::by_id(p_item.kind);
    if (!def) {
        return "没有这种家具：" + p_item.kind;
    }
    std::string why = level_check(p_def, p_item.level);
    if (!why.empty()) {
        return why;
    }
    const InteriorRect r = furnishing_rect(p_item);
    if (!inside(interior_rect(p_def), r)) {
        return std::string(def->name) + "放不下：伸到墙外去了";
    }
    Skip skip;
    skip.item = p_self;
    const LevelThings things = gather(p_template, p_def, p_item.level, skip);
    for (const auto &s : things.solid) {
        if (r.overlaps(s.first)) {
            return "压着" + s.second + "了";
        }
    }
    for (const auto &c : things.keep_clear) {
        if (r.overlaps(c.first)) {
            return "挡着" + c.second + "了";
        }
    }
    return std::string();
}

std::string check_opening(const HouseTemplate &p_template, const BuildingDef &p_def, const Opening &p_opening, int p_self) {
    std::string why = level_check(p_def, p_opening.level);
    if (!why.empty()) {
        return why;
    }
    const char *what = p_opening.window ? "窗" : "门";
    if (!p_opening.window && p_opening.level > 0) {
        return "楼上开门一脚踩空：门只开在底层";
    }
    if (std::abs(p_opening.at) + 0.5 * p_opening.width > 0.5 * side_length(p_def, p_opening.side) - WALL_T - CORNER_GAP + EPS) {
        return std::string(what) + "离墙角太近";
    }
    for (size_t k = 0; k < p_template.openings.size(); k++) {
        const Opening &o = p_template.openings[k];
        if (int(k) != p_self && o.side == p_opening.side && o.level == p_opening.level &&
                std::abs(o.at - p_opening.at) < 0.5 * (o.width + p_opening.width) + OPENING_GAP - EPS) {
            return std::string("和") + (o.window ? "窗" : "门") + "挨得太近";
        }
    }
    if (!p_opening.window) {
        Skip skip;
        skip.opening = p_self;
        const LevelThings things = gather(p_template, p_def, p_opening.level, skip);
        const InteriorRect clear = door_clear_rect(p_def, p_opening);
        for (const auto &s : things.solid) {
            if (clear.overlaps(s.first)) {
                return "门里头有" + s.second + "挡着";
            }
        }
    }
    return std::string();
}

std::string check_doorway(const HouseTemplate &p_template, const BuildingDef &p_def, const Doorway &p_doorway, int p_self) {
    const int k = partition_of(p_template, p_doorway);
    if (k < 0) {
        return "门洞要开在隔墙上";
    }
    const Partition &p = p_template.partitions[size_t(k)];
    const double lo = horizontal(p) ? std::min(p.x0, p.x1) : std::min(p.z0, p.z1), hi = horizontal(p) ? std::max(p.x0, p.x1) : std::max(p.z0, p.z1);
    const double along = horizontal(p) ? p_doorway.x : p_doorway.z;
    if (along - 0.5 * DOORWAY_W < lo - EPS || along + 0.5 * DOORWAY_W > hi + EPS) {
        return "这段隔墙开不下门洞";
    }
    for (size_t j = 0; j < p_template.doorways.size(); j++) {
        const Doorway &d = p_template.doorways[j];
        if (int(j) != p_self && d.level == p_doorway.level && std::hypot(d.x - p_doorway.x, d.z - p_doorway.z) < DOORWAY_W + 0.2) {
            return "这里已经有门洞了";
        }
    }
    Skip skip;
    skip.doorway = p_self;
    skip.partition = k; // 隔墙自己不算挡
    const LevelThings things = gather(p_template, p_def, p_doorway.level, skip);
    const InteriorRect clear = doorway_clear_rect(p_template, p_doorway);
    for (const auto &s : things.solid) {
        if (clear.overlaps(s.first)) {
            return "门洞两边有" + s.second + "挡着";
        }
    }
    return std::string();
}

std::string check_partition(const HouseTemplate &p_template, const BuildingDef &p_def, const Partition &p_partition, int p_self) {
    std::string why = level_check(p_def, p_partition.level);
    if (!why.empty()) {
        return why;
    }
    const double length = std::hypot(p_partition.x1 - p_partition.x0, p_partition.z1 - p_partition.z0);
    if (length < MIN_PARTITION - EPS) {
        return "隔墙太短（至少 1 m）";
    }
    const InteriorRect r = partition_rect(p_partition);
    if (!inside(interior_rect(p_def, -0.5 * PARTITION_T), r)) {
        return "隔墙伸到墙外去了";
    }
    for (size_t k = 0; k < p_template.partitions.size(); k++) {
        const Partition &o = p_template.partitions[k];
        if (int(k) == p_self || o.level != p_partition.level || horizontal(o) != horizontal(p_partition)) {
            continue;
        }
        const bool same_line = horizontal(o) ? std::abs(o.z0 - p_partition.z0) < PARTITION_T : std::abs(o.x0 - p_partition.x0) < PARTITION_T;
        if (same_line && partition_rect(o).overlaps(r)) {
            return "和已有的隔墙重了";
        }
    }
    Skip skip;
    skip.partition = p_self;
    LevelThings things = gather(p_template, p_def, p_partition.level, skip);
    for (const auto &s : things.solid) {
        if (s.second != "隔墙" && r.overlaps(s.first)) {
            return "穿过" + s.second + "了";
        }
    }
    for (const auto &c : things.keep_clear) {
        if (c.second != "门洞" && r.overlaps(c.first)) {
            return "挡着" + c.second + "了";
        }
    }
    return std::string();
}

std::string check_ladder(const HouseTemplate &p_template, const BuildingDef &p_def, const LadderSpot &p_ladder, int p_self) {
    std::string why = level_check(p_def, p_ladder.level);
    if (!why.empty()) {
        return why;
    }
    if (p_ladder.level + 1 >= p_def.levels) {
        return "顶层没有再往上的地方";
    }
    const InteriorRect room = interior_rect(p_def);
    const InteriorRect board = ladder_board_rect(p_ladder), foot = ladder_foot_rect(p_ladder), hatch = ladder_hatch_rect(p_ladder), landing = ladder_landing_rect(p_ladder);
    if (!inside(room, board) || !inside(room, foot) || !inside(room, hatch) || !inside(room, landing)) {
        return "梯子伸到墙外去了：离墙远一点，人站的那侧朝屋里";
    }
    Skip skip;
    skip.ladder = p_self;
    const LevelThings here = gather(p_template, p_def, p_ladder.level, skip);
    for (const InteriorRect &r : { board, foot }) {
        for (const auto &s : here.solid) {
            if (r.overlaps(s.first)) {
                return "梯脚被" + s.second + "挡着";
            }
        }
        for (const auto &c : here.keep_clear) {
            if (r.overlaps(c.first)) {
                return "梯脚占着" + c.second + "了";
            }
        }
    }
    const LevelThings above = gather(p_template, p_def, p_ladder.level + 1, skip);
    for (const InteriorRect &r : { hatch, landing }) {
        for (const auto &s : above.solid) {
            if (r.overlaps(s.first)) {
                return "楼上梯口被" + s.second + "挡着";
            }
        }
        for (const auto &c : above.keep_clear) {
            if (r.overlaps(c.first)) {
                return "楼上梯口占着" + c.second + "了";
            }
        }
    }
    return std::string();
}

std::string format_area(double p_m2) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", p_m2);
    return buffer;
}

} // namespace

bool InteriorRect::contains(double p_x, double p_z, double p_margin) const {
    return p_x >= x0 - p_margin && p_x <= x1 + p_margin && p_z >= z0 - p_margin && p_z <= z1 + p_margin;
}

bool InteriorRect::overlaps(const InteriorRect &p_other, double p_gap) const {
    return x0 < p_other.x1 + p_gap - EPS && p_other.x0 < x1 + p_gap - EPS && z0 < p_other.z1 + p_gap - EPS && p_other.z0 < z1 + p_gap - EPS;
}

// ---------------------------------------------------------------- 工具板

int interior_tool_count() {
    return 4 + furniture::COUNT;
}

InteriorTool interior_tool(int p_index) {
    const int k = ((p_index % interior_tool_count()) + interior_tool_count()) % interior_tool_count();
    InteriorTool tool;
    if (k < 4) {
        static const InteriorToolKind KINDS[4] = { InteriorToolKind::WALL, InteriorToolKind::DOOR, InteriorToolKind::WINDOW, InteriorToolKind::LADDER };
        tool.kind = KINDS[k];
    } else {
        tool.kind = InteriorToolKind::FURNITURE;
        tool.furniture = k - 4;
    }
    return tool;
}

std::string interior_tool_name(int p_index) {
    const InteriorTool tool = interior_tool(p_index);
    switch (tool.kind) {
        case InteriorToolKind::WALL:
            return "隔墙";
        case InteriorToolKind::DOOR:
            return "门";
        case InteriorToolKind::WINDOW:
            return "窗";
        case InteriorToolKind::LADDER:
            return "梯子";
        case InteriorToolKind::FURNITURE:
            return furniture::DEFS[tool.furniture].name;
    }
    return std::string();
}

// ---------------------------------------------------------------- 形状

InteriorRect furnishing_rect(const Furnishing &p_item) {
    const FurnitureDef *def = furniture::by_id(p_item.kind);
    double sx = 0.5, sz = 0.5;
    if (def) {
        furniture_extent(*def, p_item.quarter, sx, sz);
    }
    return rect_around(p_item.x, p_item.z, sx, sz);
}

InteriorRect ladder_board_rect(const LadderSpot &p_ladder) {
    return ladder_band(p_ladder, 0.5 * LADDER_W, -0.05, 0.05);
}

InteriorRect ladder_foot_rect(const LadderSpot &p_ladder) {
    return ladder_band(p_ladder, 0.6, 0.05, 1.1);
}

InteriorRect ladder_hatch_rect(const LadderSpot &p_ladder) {
    return ladder_band(p_ladder, 0.5 * HATCH_ALONG, -0.1, HATCH_DEPTH);
}

InteriorRect ladder_landing_rect(const LadderSpot &p_ladder) {
    return ladder_band(p_ladder, 0.6, -1.0, -0.1);
}

std::vector<InteriorRect> partition_pieces(const HouseTemplate &p_template, int p_partition) {
    const Partition &p = p_template.partitions[size_t(p_partition)];
    const bool h = horizontal(p);
    const double lo = h ? std::min(p.x0, p.x1) : std::min(p.z0, p.z1), hi = h ? std::max(p.x0, p.x1) : std::max(p.z0, p.z1);
    std::vector<std::pair<double, double>> gaps;
    for (const Doorway &d : p_template.doorways) {
        if (partition_of(p_template, d) == p_partition) {
            const double along = h ? d.x : d.z;
            gaps.push_back({ along - 0.5 * DOORWAY_W, along + 0.5 * DOORWAY_W });
        }
    }
    std::sort(gaps.begin(), gaps.end());
    std::vector<InteriorRect> pieces;
    double from = lo;
    auto emit = [&](double a, double b) {
        if (b - a > 0.01) {
            pieces.push_back(h ? InteriorRect{ a, p.z0 - 0.5 * PARTITION_T, b, p.z0 + 0.5 * PARTITION_T } : InteriorRect{ p.x0 - 0.5 * PARTITION_T, a, p.x0 + 0.5 * PARTITION_T, b });
        }
    };
    for (const auto &g : gaps) {
        emit(from, std::max(from, g.first));
        from = std::max(from, g.second);
    }
    emit(from, hi);
    return pieces;
}

bool nearest_wall(const BuildingDef &p_def, double p_x, double p_z, double p_reach, WallSide &r_side, double &r_at) {
    const double hx = interior_half_x(p_def), hz = interior_half_z(p_def);
    const double d[4] = { hz - p_z, hx - p_x, hz + p_z, hx + p_x }; // 到正面、右、背面、左墙内皮
    int best = -1;
    for (int k = 0; k < 4; k++) {
        const bool along_x = k == 0 || k == 2;
        const double along = along_x ? p_x : p_z, span = along_x ? hx : hz;
        if (std::abs(along) > span + EPS || d[k] > p_reach || d[k] < -WALL_T - EPS) {
            continue;
        }
        if (best < 0 || d[k] < d[best]) {
            best = k;
        }
    }
    if (best < 0) {
        return false;
    }
    r_side = WallSide(best);
    r_at = best == 0 || best == 2 ? p_x : p_z;
    return true;
}

void opening_span(const BuildingDef &p_def, const Opening &p_opening, double &r_y0, double &r_y1) {
    const double h = level_height(p_def);
    if (p_opening.window) {
        r_y0 = WINDOW_SILL;
        r_y1 = std::min(WINDOW_TOP, h - 0.3);
    } else {
        r_y0 = 0.0;
        r_y1 = std::min(DOOR_H, h - 0.4);
    }
}

// ---------------------------------------------------------------- 点中、放、拆

InteriorHit interior_find(const HouseTemplate &p_template, const BuildingDef &p_def, double p_x, double p_z, int p_level) {
    InteriorHit hit;
    for (int k = int(p_template.items.size()) - 1; k >= 0; k--) {
        const Furnishing &f = p_template.items[size_t(k)];
        if (f.level == p_level && furnishing_rect(f).contains(p_x, p_z)) {
            hit.kind = InteriorHit::ITEM;
            hit.index = k;
            return hit;
        }
    }
    for (int k = int(p_template.ladders.size()) - 1; k >= 0; k--) {
        const LadderSpot &l = p_template.ladders[size_t(k)];
        if (l.level == p_level && (ladder_board_rect(l).contains(p_x, p_z, 0.3) || ladder_foot_rect(l).contains(p_x, p_z))) {
            hit.kind = InteriorHit::LADDER;
            hit.index = k;
            return hit;
        }
    }
    for (int k = int(p_template.doorways.size()) - 1; k >= 0; k--) {
        const Doorway &d = p_template.doorways[size_t(k)];
        if (d.level == p_level && std::hypot(d.x - p_x, d.z - p_z) <= 0.6) {
            hit.kind = InteriorHit::DOORWAY;
            hit.index = k;
            return hit;
        }
    }
    WallSide side = WallSide::FRONT;
    double at = 0.0;
    if (nearest_wall(p_def, p_x, p_z, WALL_REACH, side, at)) {
        for (int k = int(p_template.openings.size()) - 1; k >= 0; k--) {
            const Opening &o = p_template.openings[size_t(k)];
            if (o.level == p_level && o.side == side && std::abs(o.at - at) <= 0.5 * o.width + 0.1) {
                hit.kind = InteriorHit::OPENING;
                hit.index = k;
                return hit;
            }
        }
    }
    for (int k = int(p_template.partitions.size()) - 1; k >= 0; k--) {
        const Partition &p = p_template.partitions[size_t(k)];
        double along = 0.0;
        if (p.level == p_level && partition_distance(p, p_x, p_z, along) <= PARTITION_HIT) {
            hit.kind = InteriorHit::PARTITION;
            hit.index = k;
            return hit;
        }
    }
    return hit;
}

std::string interior_hit_name(const HouseTemplate &p_template, const InteriorHit &p_hit) {
    switch (p_hit.kind) {
        case InteriorHit::ITEM: {
            const FurnitureDef *def = furniture::by_id(p_template.items[size_t(p_hit.index)].kind);
            return def ? def->name : p_template.items[size_t(p_hit.index)].kind;
        }
        case InteriorHit::LADDER:
            return "梯子";
        case InteriorHit::DOORWAY:
            return "门洞";
        case InteriorHit::OPENING: {
            const Opening &o = p_template.openings[size_t(p_hit.index)];
            return std::string(side_label(o.side)) + "的" + (o.window ? "窗" : "门");
        }
        case InteriorHit::PARTITION:
            return "隔墙";
        case InteriorHit::NONE:
            break;
    }
    return std::string();
}

std::string interior_place_item(HouseTemplate &p_template, const BuildingDef &p_def, const std::string &p_kind, double p_x, double p_z, int p_quarter, int p_level) {
    Furnishing f;
    f.kind = p_kind;
    f.x = p_x;
    f.z = p_z;
    f.quarter = ((p_quarter % 4) + 4) % 4;
    f.level = p_level;
    const std::string why = check_item(p_template, p_def, f, -1);
    if (why.empty()) {
        p_template.items.push_back(f);
    }
    return why;
}

std::string interior_place_opening(HouseTemplate &p_template, const BuildingDef &p_def, bool p_window, double p_x, double p_z, int p_level) {
    std::string why = level_check(p_def, p_level);
    if (!why.empty()) {
        return why;
    }
    WallSide side = WallSide::FRONT;
    double at = 0.0;
    if (nearest_wall(p_def, p_x, p_z, WALL_REACH, side, at)) {
        const Opening o{ p_window, side, at, p_window ? 1.2 : 1.3, p_level };
        why = check_opening(p_template, p_def, o, -1);
        if (why.empty()) {
            p_template.openings.push_back(o);
        }
        return why;
    }
    for (const Partition &p : p_template.partitions) {
        double along = 0.0;
        if (p.level != p_level || partition_distance(p, p_x, p_z, along) > PARTITION_HIT) {
            continue;
        }
        if (p_window) {
            return "隔墙上不开窗";
        }
        const Doorway d{ horizontal(p) ? p_x : p.x0, horizontal(p) ? p.z0 : p_z, p_level };
        why = check_doorway(p_template, p_def, d, -1);
        if (why.empty()) {
            p_template.doorways.push_back(d);
        }
        return why;
    }
    return std::string(p_window ? "窗" : "门") + "要开在墙上：把光标挪到墙边";
}

void interior_wall_snap(const BuildingDef &p_def, double p_x0, double p_z0, double p_x1, double p_z1, double &r_x0, double &r_z0, double &r_x1, double &r_z1) {
    const double hx = interior_half_x(p_def), hz = interior_half_z(p_def);
    const bool along_x = std::abs(p_x1 - p_x0) >= std::abs(p_z1 - p_z0);
    auto clamp_end = [](double p_v, double p_half) {
        p_v = std::clamp(p_v, -p_half, p_half);
        if (p_half - std::abs(p_v) <= SNAP_TO_WALL) {
            p_v = p_v < 0 ? -p_half : p_half; // 离墙不到 0.3 m：接到墙上
        }
        return p_v;
    };
    if (along_x) {
        const double z = std::clamp(p_z0, -hz + 0.5 * PARTITION_T, hz - 0.5 * PARTITION_T);
        double a = clamp_end(p_x0, hx), b = clamp_end(p_x1, hx);
        if (a > b) {
            std::swap(a, b);
        }
        r_x0 = a, r_x1 = b, r_z0 = z, r_z1 = z;
    } else {
        const double x = std::clamp(p_x0, -hx + 0.5 * PARTITION_T, hx - 0.5 * PARTITION_T);
        double a = clamp_end(p_z0, hz), b = clamp_end(p_z1, hz);
        if (a > b) {
            std::swap(a, b);
        }
        r_x0 = x, r_x1 = x, r_z0 = a, r_z1 = b;
    }
}

std::string interior_place_wall(HouseTemplate &p_template, const BuildingDef &p_def, double p_x0, double p_z0, double p_x1, double p_z1, int p_level) {
    Partition p;
    p.level = p_level;
    interior_wall_snap(p_def, p_x0, p_z0, p_x1, p_z1, p.x0, p.z0, p.x1, p.z1);
    const std::string why = check_partition(p_template, p_def, p, -1);
    if (why.empty()) {
        p_template.partitions.push_back(p);
    }
    return why;
}

std::string interior_place_ladder(HouseTemplate &p_template, const BuildingDef &p_def, double p_x, double p_z, int p_quarter, int p_level) {
    const LadderSpot l{ p_x, p_z, ((p_quarter % 4) + 4) % 4, p_level };
    const std::string why = check_ladder(p_template, p_def, l, -1);
    if (why.empty()) {
        p_template.ladders.push_back(l);
    }
    return why;
}

std::string interior_place_tool(HouseTemplate &p_template, const BuildingDef &p_def, int p_tool, double p_x, double p_z, int p_quarter, int p_level) {
    const InteriorTool tool = interior_tool(p_tool);
    switch (tool.kind) {
        case InteriorToolKind::WALL:
            return "隔墙要两个点：E 定起点，挪到终点再按 E";
        case InteriorToolKind::DOOR:
            return interior_place_opening(p_template, p_def, false, p_x, p_z, p_level);
        case InteriorToolKind::WINDOW:
            return interior_place_opening(p_template, p_def, true, p_x, p_z, p_level);
        case InteriorToolKind::LADDER:
            return interior_place_ladder(p_template, p_def, p_x, p_z, p_quarter, p_level);
        case InteriorToolKind::FURNITURE:
            return interior_place_item(p_template, p_def, furniture::DEFS[tool.furniture].id, p_x, p_z, p_quarter, p_level);
    }
    return std::string();
}

bool interior_remove(HouseTemplate &p_template, const InteriorHit &p_hit) {
    switch (p_hit.kind) {
        case InteriorHit::ITEM:
            p_template.items.erase(p_template.items.begin() + p_hit.index);
            return true;
        case InteriorHit::LADDER:
            p_template.ladders.erase(p_template.ladders.begin() + p_hit.index);
            return true;
        case InteriorHit::DOORWAY:
            p_template.doorways.erase(p_template.doorways.begin() + p_hit.index);
            return true;
        case InteriorHit::OPENING:
            p_template.openings.erase(p_template.openings.begin() + p_hit.index);
            return true;
        case InteriorHit::PARTITION: {
            // 墙上的门洞跟着拆
            for (int k = int(p_template.doorways.size()) - 1; k >= 0; k--) {
                if (partition_of(p_template, p_template.doorways[size_t(k)]) == p_hit.index) {
                    p_template.doorways.erase(p_template.doorways.begin() + k);
                }
            }
            p_template.partitions.erase(p_template.partitions.begin() + p_hit.index);
            return true;
        }
        case InteriorHit::NONE:
            break;
    }
    return false;
}

// ---------------------------------------------------------------- 走得通

const char *level_label(int p_level) {
    static const char *LABELS[] = { "底层", "二层", "三层", "四层", "五层" };
    return p_level >= 0 && p_level < 5 ? LABELS[p_level] : "高层";
}

InteriorReport interior_report(const HouseTemplate &p_template, const BuildingDef &p_def) {
    InteriorReport report;
    const int levels = std::max(1, p_def.levels);
    auto problem = [&](const std::string &p_text) { report.problems.push_back(p_text); };

    // 每一样单拿出来按放的规矩再查一遍（从文件读进来的模板没经过编辑器）
    for (size_t k = 0; k < p_template.items.size(); k++) {
        const std::string why = check_item(p_template, p_def, p_template.items[k], int(k));
        if (!why.empty()) {
            const FurnitureDef *def = furniture::by_id(p_template.items[k].kind);
            problem(std::string(level_label(p_template.items[k].level)) + "的" + (def ? def->name : p_template.items[k].kind) + "：" + why);
        }
    }
    for (size_t k = 0; k < p_template.openings.size(); k++) {
        const std::string why = check_opening(p_template, p_def, p_template.openings[k], int(k));
        if (!why.empty()) {
            problem(std::string(side_label(p_template.openings[k].side)) + "的" + (p_template.openings[k].window ? "窗" : "门") + "：" + why);
        }
    }
    for (size_t k = 0; k < p_template.partitions.size(); k++) {
        const std::string why = check_partition(p_template, p_def, p_template.partitions[k], int(k));
        if (!why.empty()) {
            problem(std::string(level_label(p_template.partitions[k].level)) + "的隔墙：" + why);
        }
    }
    for (size_t k = 0; k < p_template.doorways.size(); k++) {
        const std::string why = check_doorway(p_template, p_def, p_template.doorways[k], int(k));
        if (!why.empty()) {
            problem(std::string(level_label(p_template.doorways[k].level)) + "的门洞：" + why);
        }
    }
    for (size_t k = 0; k < p_template.ladders.size(); k++) {
        const std::string why = check_ladder(p_template, p_def, p_template.ladders[k], int(k));
        if (!why.empty()) {
            problem(std::string(level_label(p_template.ladders[k].level)) + "的梯子：" + why);
        }
    }

    bool any_door = false, front_door = false;
    for (const Opening &o : p_template.openings) {
        if (!o.window && o.level == 0) {
            any_door = true;
            front_door = front_door || o.side == WallSide::FRONT;
        }
    }
    if (!any_door) {
        problem("没有门：进不去");
    } else if (!front_door) {
        problem("正面没开门：村民认正面的门");
    }

    // 格：每层 cols × rows，格心站不站得住
    const double hx = interior_half_x(p_def), hz = interior_half_z(p_def);
    const int cols = std::max(1, int(std::floor(2 * hx / GRID))), rows = std::max(1, int(std::floor(2 * hz / GRID)));
    const double x0 = -0.5 * cols * GRID, z0 = -0.5 * rows * GRID;
    auto cx = [&](int i) { return x0 + (i + 0.5) * GRID; };
    auto cz = [&](int j) { return z0 + (j + 0.5) * GRID; };
    const size_t per_level = size_t(cols) * size_t(rows);
    std::vector<char> walkable(per_level * size_t(levels), 0), reached(per_level * size_t(levels), 0);
    auto index = [&](int l, int i, int j) { return size_t(l) * per_level + size_t(j) * size_t(cols) + size_t(i); };
    for (int l = 0; l < levels; l++) {
        const LevelThings things = gather(p_template, p_def, l, Skip(), true);
        std::vector<InteriorRect> holes;
        for (const LadderSpot &ladder : p_template.ladders) {
            if (ladder.level + 1 == l) {
                holes.push_back(ladder_hatch_rect(ladder));
            }
        }
        for (int j = 0; j < rows; j++) {
            for (int i = 0; i < cols; i++) {
                const double x = cx(i), z = cz(j);
                bool ok = std::abs(x) <= hx - PLAYER_R + EPS && std::abs(z) <= hz - PLAYER_R + EPS;
                for (size_t s = 0; ok && s < things.solid.size(); s++) {
                    ok = !things.solid[s].first.contains(x, z, PLAYER_R - EPS);
                }
                for (size_t h = 0; ok && h < holes.size(); h++) {
                    ok = !holes[h].contains(x, z);
                }
                walkable[index(l, i, j)] = ok ? 1 : 0;
            }
        }
    }
    std::deque<size_t> queue;
    auto seed_near = [&](int l, double x, double z, double radius) {
        bool any = false;
        for (int j = 0; j < rows; j++) {
            for (int i = 0; i < cols; i++) {
                const size_t k = index(l, i, j);
                if (walkable[k] && !reached[k] && std::hypot(cx(i) - x, cz(j) - z) <= radius) {
                    reached[k] = 1;
                    queue.push_back(k);
                    any = true;
                }
            }
        }
        return any;
    };
    auto seed_rect = [&](int l, const InteriorRect &r) {
        bool any = false;
        for (int j = 0; j < rows; j++) {
            for (int i = 0; i < cols; i++) {
                const size_t k = index(l, i, j);
                if (walkable[k] && r.contains(cx(i), cz(j))) {
                    any = true;
                    if (!reached[k]) {
                        reached[k] = 1;
                        queue.push_back(k);
                    }
                }
            }
        }
        return any;
    };
    for (const Opening &o : p_template.openings) {
        if (!o.window && o.level == 0) {
            double x = 0.0, z = 0.0;
            opening_point(p_def, o, PLAYER_R + 0.1, x, z);
            if (!seed_near(0, x, z, 0.5 * o.width)) {
                problem(std::string(side_label(o.side)) + "的门一进来就被挡住了");
            }
        }
    }
    // 梯子：梯脚与楼上落脚处是通的
    struct Link {
        int level;
        InteriorRect from;
        int to_level;
        InteriorRect to;
    };
    std::vector<Link> links;
    for (const LadderSpot &ladder : p_template.ladders) {
        if (ladder.level + 1 >= levels) {
            continue;
        }
        links.push_back({ ladder.level, ladder_foot_rect(ladder), ladder.level + 1, ladder_landing_rect(ladder) });
        links.push_back({ ladder.level + 1, ladder_landing_rect(ladder), ladder.level, ladder_foot_rect(ladder) });
    }
    std::vector<char> link_used(links.size(), 0);
    while (!queue.empty()) {
        const size_t k = queue.front();
        queue.pop_front();
        const int l = int(k / per_level), rest = int(k % per_level), i = rest % cols, j = rest / cols;
        const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; d++) {
            const int ni = i + di[d], nj = j + dj[d];
            if (ni < 0 || nj < 0 || ni >= cols || nj >= rows) {
                continue;
            }
            const size_t n = index(l, ni, nj);
            if (walkable[n] && !reached[n]) {
                reached[n] = 1;
                queue.push_back(n);
            }
        }
        for (size_t m = 0; m < links.size(); m++) {
            if (!link_used[m] && links[m].level == l && links[m].from.contains(cx(i), cz(j))) {
                link_used[m] = 1;
                seed_rect(links[m].to_level, links[m].to);
            }
        }
    }
    for (const LadderSpot &ladder : p_template.ladders) {
        if (ladder.level + 1 >= levels) {
            continue;
        }
        bool foot = false, landing = false;
        const InteriorRect fr = ladder_foot_rect(ladder), lr = ladder_landing_rect(ladder);
        for (int j = 0; j < rows; j++) {
            for (int i = 0; i < cols; i++) {
                foot = foot || (walkable[index(ladder.level, i, j)] && fr.contains(cx(i), cz(j)));
                landing = landing || (walkable[index(ladder.level + 1, i, j)] && lr.contains(cx(i), cz(j)));
            }
        }
        if (!foot) {
            problem(std::string(level_label(ladder.level)) + "的梯脚站不了人");
        }
        if (!landing) {
            problem(std::string(level_label(ladder.level)) + "的梯子上去没处落脚");
        }
    }
    for (int l = 0; l < levels; l++) {
        int free_cells = 0, lost = 0;
        for (size_t k = size_t(l) * per_level; k < size_t(l + 1) * per_level; k++) {
            free_cells += walkable[k];
            lost += walkable[k] && !reached[k] ? 1 : 0;
        }
        report.walkable_m2.push_back(free_cells * GRID * GRID);
        report.unreachable_m2.push_back(lost * GRID * GRID);
        if (lost * GRID * GRID > UNREACHABLE_M2) {
            problem(std::string(level_label(l)) + "有 " + format_area(lost * GRID * GRID) + " m² 走不到：隔墙上开个门洞，或挪开挡路的家具" +
                    (l > 0 ? "，或搭梯子上来" : ""));
        }
    }
    return report;
}
