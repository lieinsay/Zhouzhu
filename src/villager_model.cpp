#include "villager_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <queue>

#include "crops.h"

// ================================================================ 导航格

bool NavGrid::cell_of(double p_x, double p_z, int &r_r, int &r_c) const {
    r_c = int(std::floor((p_x - x0) / cell_m));
    r_r = int(std::floor((p_z - z0) / cell_m));
    return in_range(r_r, r_c);
}

bool NavGrid::walkable_at(double p_x, double p_z) const {
    int r = 0, c = 0;
    return cell_of(p_x, p_z, r, c) && walkable(r, c);
}

bool NavGrid::dry_at(double p_x, double p_z) const {
    int r = 0, c = 0;
    return cell_of(p_x, p_z, r, c) && at(r, c) == DRY;
}

int NavGrid::walkable_count() const {
    int n = 0;
    for (uint8_t w : walk) {
        n += w != BLOCKED ? 1 : 0;
    }
    return n;
}

bool surface_height(const IslandField &p_field, double p_x, double p_z, double &r_y) {
    const double ext = p_field.half_extent();
    const double fx = (p_x + ext) / p_field.cell_m, fz = (p_z + ext) / p_field.cell_m;
    const int j = int(std::floor(fx)), i = int(std::floor(fz));
    if (!p_field.quad_solid(i, j)) {
        return false;
    }
    const double u = fx - j, v = fz - i; // u 沿 j（x），v 沿 i（z）
    const double h00 = p_field.height[p_field.index(i, j)], h01 = p_field.height[p_field.index(i, j + 1)];
    const double h10 = p_field.height[p_field.index(i + 1, j)], h11 = p_field.height[p_field.index(i + 1, j + 1)];
    // 与地面网格同样的切法：三角 (00, 01, 10) 与 (01, 11, 10)
    r_y = u + v <= 1.0 ? h00 + (h01 - h00) * u + (h10 - h00) * v : h11 + (h10 - h11) * (1.0 - u) + (h01 - h11) * (1.0 - v);
    return true;
}

namespace {

// 该处（岛坐标）的水深：取最近的高度场节点
double water_depth_at(const IslandField &p_field, double p_x, double p_z) {
    if (p_field.river_depth.empty()) {
        return 0.0;
    }
    const double ext = p_field.half_extent();
    const int j = int(std::lround((p_x + ext) / p_field.cell_m)), i = int(std::lround((p_z + ext) / p_field.cell_m));
    if (i < 0 || j < 0 || i >= p_field.n || j >= p_field.n) {
        return 0.0;
    }
    return p_field.river_depth[p_field.index(i, j)];
}

} // namespace

bool IslandNavGround::walk_height(double p_x, double p_z, double &r_y) const {
    return surface_height(field, p_x, p_z, r_y);
}

double IslandNavGround::water_depth(double p_x, double p_z) const {
    return water_depth_at(field, p_x, p_z);
}

NavGrid build_nav_grid(const NavGround &p_ground, double p_x0, double p_z0, int p_cols, int p_rows, const SettlementLayout &p_layout, const NavRules &p_rules) {
    NavGrid g;
    if (p_cols <= 0 || p_rows <= 0) {
        return g;
    }
    g.cell_m = p_rules.cell_m;
    g.x0 = p_x0;
    g.z0 = p_z0;
    g.cols = p_cols;
    g.rows = p_rows;
    g.walk.assign(size_t(g.cols) * size_t(g.rows), NavGrid::BLOCKED);
    g.y.assign(g.walk.size(), 0.0f);
    for (int r = 0; r < g.rows; r++) {
        for (int c = 0; c < g.cols; c++) {
            const NavPoint p = g.center(r, c);
            double y = 0.0;
            if (!p_ground.walk_height(p.x, p.z, y)) {
                continue;
            }
            g.y[g.index(r, c)] = float(y);
            g.walk[g.index(r, c)] = p_ground.water_depth(p.x, p.z) > 0.05 ? NavGrid::WET : NavGrid::DRY;
        }
    }
    // 房子占的地（外扩）走不了：只刷每座房子外接框里的格
    for (const Building &b : p_layout.get_items()) {
        const BuildingDef *def = buildings::by_id(b.kind);
        if (!def) {
            continue;
        }
        const double reach = 0.5 * std::hypot(def->width_m, def->depth_m) + p_rules.building_margin_m + g.cell_m;
        const int r0 = std::max(0, int(std::floor((b.z - reach - g.z0) / g.cell_m)));
        const int c0 = std::max(0, int(std::floor((b.x - reach - g.x0) / g.cell_m)));
        const int r1 = std::min(g.rows - 1, int(std::floor((b.z + reach - g.z0) / g.cell_m)));
        const int c1 = std::min(g.cols - 1, int(std::floor((b.x + reach - g.x0) / g.cell_m)));
        for (int r = r0; r <= r1; r++) {
            for (int c = c0; c <= c1; c++) {
                const NavPoint p = g.center(r, c);
                if (footprint_contains(*def, b, p.x, p.z, p_rules.building_margin_m)) {
                    g.walk[g.index(r, c)] = NavGrid::BLOCKED;
                }
            }
        }
    }
    return g;
}

NavGrid build_nav_grid(const IslandField &p_field, const SettlementLayout &p_layout, const NavRules &p_rules) {
    if (p_field.n < 2) {
        return NavGrid();
    }
    const double ext = p_field.half_extent();
    const int cells = int(std::floor(2.0 * ext / p_rules.cell_m));
    return build_nav_grid(IslandNavGround(p_field), -ext, -ext, cells, cells, p_layout, p_rules);
}

bool nav_nearest_walkable(const NavGrid &p_grid, double p_x, double p_z, double p_max_m, bool p_dry_only, NavPoint &r_point) {
    if (p_grid.cols == 0) {
        return false;
    }
    int r0 = 0, c0 = 0;
    p_grid.cell_of(p_x, p_z, r0, c0);
    const int reach = int(std::ceil(p_max_m / p_grid.cell_m));
    double best = std::numeric_limits<double>::infinity();
    for (int r = r0 - reach; r <= r0 + reach; r++) {
        for (int c = c0 - reach; c <= c0 + reach; c++) {
            const uint8_t w = p_grid.at(r, c);
            if (w == NavGrid::BLOCKED || (p_dry_only && w != NavGrid::DRY)) {
                continue;
            }
            const NavPoint p = p_grid.center(r, c);
            const double d = std::hypot(p.x - p_x, p.z - p_z);
            if (d <= p_max_m && d < best) {
                best = d;
                r_point = p;
            }
        }
    }
    return best < std::numeric_limits<double>::infinity();
}

namespace {

bool step_ok(const NavGrid &p_grid, const NavRules &p_rules, int p_ra, int p_ca, int p_rb, int p_cb) {
    if (!p_grid.walkable(p_rb, p_cb)) {
        return false;
    }
    const double run = p_grid.cell_m * std::hypot(double(p_ra - p_rb), double(p_ca - p_cb));
    const double rise = std::abs(double(p_grid.y[p_grid.index(p_ra, p_ca)]) - double(p_grid.y[p_grid.index(p_rb, p_cb)]));
    return rise <= p_rules.max_grade * run + 1e-9;
}

// 直线上逐格走一遍；p_allow_wet = 线上能不能有水格
bool line_clear(const NavGrid &p_grid, const NavRules &p_rules, double p_x0, double p_z0, double p_x1, double p_z1, bool p_allow_wet) {
    int r = 0, c = 0;
    if (!p_grid.cell_of(p_x0, p_z0, r, c) || !p_grid.walkable(r, c)) {
        return false;
    }
    const double len = std::hypot(p_x1 - p_x0, p_z1 - p_z0);
    const int steps = std::max(1, int(std::ceil(len / (p_grid.cell_m * 0.25))));
    for (int k = 1; k <= steps; k++) {
        const double t = double(k) / steps;
        int nr = 0, nc = 0;
        if (!p_grid.cell_of(p_x0 + (p_x1 - p_x0) * t, p_z0 + (p_z1 - p_z0) * t, nr, nc)) {
            return false;
        }
        if (nr == r && nc == c) {
            continue;
        }
        if (!p_allow_wet && p_grid.at(nr, nc) == NavGrid::WET) {
            return false;
        }
        // 斜着跨格：两侧的格也得走得了，免得从房角切过去
        if (nr != r && nc != c && (!step_ok(p_grid, p_rules, r, c, nr, c) || !step_ok(p_grid, p_rules, r, c, r, nc))) {
            return false;
        }
        if (!step_ok(p_grid, p_rules, r, c, nr, nc)) {
            return false;
        }
        r = nr;
        c = nc;
    }
    return true;
}

} // namespace

bool nav_line_clear(const NavGrid &p_grid, const NavRules &p_rules, double p_x0, double p_z0, double p_x1, double p_z1) {
    return line_clear(p_grid, p_rules, p_x0, p_z0, p_x1, p_z1, true);
}

std::vector<NavPoint> nav_find_path(const NavGrid &p_grid, const NavRules &p_rules, double p_x0, double p_z0, double p_x1, double p_z1) {
    std::vector<NavPoint> out;
    NavPoint start, goal;
    if (!nav_nearest_walkable(p_grid, p_x0, p_z0, 6.0, false, start) || !nav_nearest_walkable(p_grid, p_x1, p_z1, 6.0, false, goal)) {
        return out;
    }
    // 终点本身走得了就去终点本身（不吸到格心），否则去吸到的格心
    const NavPoint target = p_grid.walkable_at(p_x1, p_z1) ? NavPoint{ p_x1, p_z1 } : goal;
    int sr = 0, sc = 0, gr = 0, gc = 0;
    p_grid.cell_of(start.x, start.z, sr, sc);
    p_grid.cell_of(goal.x, goal.z, gr, gc);
    if (sr == gr && sc == gc) {
        out.push_back(target);
        return out;
    }

    const size_t count = p_grid.walk.size();
    std::vector<float> g_cost(count, std::numeric_limits<float>::infinity());
    std::vector<int> parent(count, -1);
    std::vector<uint8_t> closed(count, 0);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    auto heuristic = [&](int p_r, int p_c) {
        const double dr = std::abs(p_r - gr), dc = std::abs(p_c - gc);
        return float((std::max(dr, dc) + (std::sqrt(2.0) - 1.0) * std::min(dr, dc)) * p_grid.cell_m);
    };
    const int start_k = int(p_grid.index(sr, sc)), goal_k = int(p_grid.index(gr, gc));
    g_cost[size_t(start_k)] = 0.0f;
    open.push({ heuristic(sr, sc), start_k });
    static const int DR[8] = { -1, 1, 0, 0, -1, -1, 1, 1 };
    static const int DC[8] = { 0, 0, -1, 1, -1, 1, -1, 1 };
    bool found = false;
    while (!open.empty()) {
        const int k = open.top().second;
        open.pop();
        if (closed[size_t(k)]) {
            continue;
        }
        closed[size_t(k)] = 1;
        if (k == goal_k) {
            found = true;
            break;
        }
        const int r = k / p_grid.cols, c = k % p_grid.cols;
        for (int d = 0; d < 8; d++) {
            const int nr = r + DR[d], nc = c + DC[d];
            if (!p_grid.in_range(nr, nc) || !step_ok(p_grid, p_rules, r, c, nr, nc)) {
                continue;
            }
            if (d >= 4 && (!step_ok(p_grid, p_rules, r, c, nr, c) || !step_ok(p_grid, p_rules, r, c, r, nc))) {
                continue; // 斜走不切角
            }
            const int nk = int(p_grid.index(nr, nc));
            if (closed[size_t(nk)]) {
                continue;
            }
            double step = p_grid.cell_m * (d >= 4 ? std::sqrt(2.0) : 1.0);
            if (p_grid.at(nr, nc) == NavGrid::WET) {
                step *= p_rules.water_cost;
            }
            const float ng = g_cost[size_t(k)] + float(step);
            if (ng < g_cost[size_t(nk)]) {
                g_cost[size_t(nk)] = ng;
                parent[size_t(nk)] = k;
                open.push({ ng + heuristic(nr, nc), nk });
            }
        }
    }
    if (!found) {
        return out;
    }
    std::vector<int> cells;
    for (int k = goal_k; k != -1; k = parent[size_t(k)]) {
        cells.push_back(k);
    }
    std::reverse(cells.begin(), cells.end());
    // 拉直：从当前点往前看，能直走到的最远一格就是下一个路点。水里的格子不跨（A* 为了少蹚水绕的路要留着）
    auto point_of = [&](int p_k) { return p_grid.center(p_k / p_grid.cols, p_k % p_grid.cols); };
    size_t i = 0;
    while (i + 1 < cells.size()) {
        const NavPoint from = point_of(cells[i]);
        const bool from_wet = p_grid.walk[size_t(cells[i])] == NavGrid::WET;
        size_t best = i + 1;
        for (size_t j = i + 2; j < cells.size(); j++) {
            const NavPoint to = point_of(cells[j]);
            const bool wet_end = from_wet || p_grid.walk[size_t(cells[j])] == NavGrid::WET;
            if (!line_clear(p_grid, p_rules, from.x, from.z, to.x, to.z, wet_end)) {
                break;
            }
            best = j;
        }
        out.push_back(point_of(cells[best]));
        i = best;
    }
    out.back() = target;
    return out;
}

// ================================================================ 钟点

ClockReading read_clock(double p_seconds, const DayRules &p_rules) {
    ClockReading c;
    const double days = std::max(0.0, p_seconds) / p_rules.day_seconds;
    c.day = int(std::floor(days));
    c.hour = (days - c.day) * 24.0;
    if (c.hour >= 24.0) {
        c.hour = 0.0;
        c.day++;
    }
    return c;
}

bool is_home_hour(double p_hour, const DayRules &p_rules) {
    return p_hour < p_rules.work_start || p_hour >= p_rules.work_end || (p_hour >= p_rules.lunch_start && p_hour < p_rules.lunch_end);
}

const char *shichen_name(double p_hour) {
    static const char *NAMES[12] = { "子时", "丑时", "寅时", "卯时", "辰时", "巳时", "午时", "未时", "申时", "酉时", "戌时", "亥时" };
    const double shifted = std::fmod(p_hour + 1.0 + 24.0, 24.0);
    return NAMES[std::min(11, int(shifted / 2.0))];
}

std::string clock_text(double p_seconds, const DayRules &p_rules) {
    const ClockReading c = read_clock(p_seconds, p_rules);
    const int minutes = int(std::floor(c.hour * 60.0 + 1e-6));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s · 第 %d 日 · %02d:%02d", shichen_name(c.hour), c.day + 1, minutes / 60, minutes % 60);
    return buf;
}

// ================================================================ 职业与住户

namespace {

const JobDef JOBS[] = {
    { Job::FARMER, "farmer", "农" },
    { Job::CARRIER, "carrier", "挑水" },
    { Job::WATCHMAN, "watchman", "守望" },
    { Job::IDLER, "idler", "闲" },
};

const TaskDef TASKS[] = {
    { TaskKind::GO_HOME, "go_home", "回家" },
    { TaskKind::TILL, "till", "开荒" },
    { TaskKind::SOW, "sow", "播种" },
    { TaskKind::HARVEST, "harvest", "收割" },
    { TaskKind::TEND, "tend", "锄草" },
    { TaskKind::DELIVER, "deliver", "送东西" },
    { TaskKind::FETCH_WATER, "fetch_water", "打水" },
    { TaskKind::WATCH, "watch", "守望" },
    { TaskKind::PATROL, "patrol", "巡村" },
    { TaskKind::CHAT, "chat", "串门" },
    { TaskKind::WAIT, "wait", "歇着" },
};

const char *NAMES[] = {
    "阿禾", "石头", "满仓", "春娘", "黑牛", "秋生", "杏儿", "大柱", "小满", "桂枝",
    "长庚", "谷雨", "阿苗", "铁锁", "二丫", "青禾", "来福", "巧姑", "山根", "月娥",
    "麦生", "豆豆", "老槐", "菱花", "水生", "阿蒲", "冬至", "兰芝", "狗剩", "莲心",
};
const int NAME_COUNT = int(sizeof(NAMES) / sizeof(NAMES[0]));

} // namespace

namespace villagers {

const JobDef &job(Job p_job) {
    return JOBS[int(p_job)];
}

const JobDef *job_by_id(const std::string &p_id) {
    for (const JobDef &def : JOBS) {
        if (p_id == def.id) {
            return &def;
        }
    }
    return nullptr;
}

const TaskDef &task(TaskKind p_kind) {
    return TASKS[int(p_kind)];
}

} // namespace villagers

std::vector<Resident> assign_residents(const SettlementLayout &p_layout, bool p_has_farm, bool p_has_river, int p_per_house, uint64_t p_seed) {
    std::vector<Resident> out;
    const std::vector<Building> &items = p_layout.get_items();
    int hall = -1;
    for (int k = 0; k < int(items.size()); k++) {
        const Building &b = items[size_t(k)];
        if (b.kind == "house") {
            for (int m = 0; m < p_per_house; m++) {
                Resident r;
                r.home = k;
                r.job = m == 0 ? (p_has_farm ? Job::FARMER : Job::IDLER) : m == 1 ? (p_has_river ? Job::CARRIER : Job::IDLER) : Job::IDLER;
                out.push_back(r);
            }
        } else if (b.kind == "hall") {
            if (hall < 0) {
                hall = k;
            }
            Resident r;
            r.home = k;
            out.push_back(r);
        }
    }
    for (int k = 0; k < int(items.size()); k++) {
        const Building &tower = items[size_t(k)];
        if (tower.kind != "tower") {
            continue;
        }
        int home = hall;
        if (home < 0) {
            double best = std::numeric_limits<double>::infinity();
            for (int h = 0; h < int(items.size()); h++) {
                const double d = std::hypot(items[size_t(h)].x - tower.x, items[size_t(h)].z - tower.z);
                if (items[size_t(h)].kind == "house" && d < best) {
                    best = d;
                    home = h;
                }
            }
        }
        if (home < 0) {
            continue;
        }
        Resident r;
        r.home = home;
        r.job = Job::WATCHMAN;
        r.post = k;
        out.push_back(r);
    }
    // 名字：按种子洗一遍名字表，依次取；人多过表就加后缀
    std::vector<int> order(static_cast<size_t>(NAME_COUNT), 0);
    for (int k = 0; k < NAME_COUNT; k++) {
        order[size_t(k)] = k;
    }
    Rng rng(p_seed ^ 0x6E616D6573ull);
    for (int k = NAME_COUNT - 1; k > 0; k--) {
        std::swap(order[size_t(k)], order[size_t(rng.range(0, k + 1))]);
    }
    static const char *SUFFIX[] = { "", "二", "三", "四", "五" };
    for (size_t k = 0; k < out.size(); k++) {
        out[k].name = std::string(NAMES[order[k % size_t(NAME_COUNT)]]) + SUFFIX[std::min<size_t>(4, k / size_t(NAME_COUNT))];
    }
    return out;
}

NavPoint building_door(const Building &p_building, double p_out_m) {
    const BuildingDef *def = buildings::by_id(p_building.kind);
    const double depth = def ? def->depth_m : 0.0;
    double ux = 0, uz = 0, vx = 0, vz = 0;
    building_axes(p_building.yaw, ux, uz, vx, vz);
    const double out = 0.5 * depth + 0.6 + p_out_m; // 0.6：台基比墙脚外扩的那一圈
    return { p_building.x + vx * out, p_building.z + vz * out };
}

// ================================================================ 决策

NavPoint plot_center(const IslandField &p_field, int p_i, int p_j) {
    return { p_field.coord(p_j) + 0.5 * p_field.cell_m, p_field.coord(p_i) + 0.5 * p_field.cell_m };
}

namespace {

bool claimed(const VillageView &p_view, int p_i, int p_j) {
    for (const Claim &c : *p_view.claims) {
        if (c.i == p_i && c.j == p_j) {
            return true;
        }
    }
    return false;
}

// 门前站位：吸到最近的干地
NavPoint door_of(const VillageView &p_view, int p_building) {
    const Building &b = p_view.layout->at(p_building);
    NavPoint door = building_door(b);
    NavPoint snapped;
    if (p_view.nav && nav_nearest_walkable(*p_view.nav, door.x, door.z, 6.0, true, snapped)) {
        door = snapped;
    }
    return door;
}

Task at_building(TaskKind p_kind, const VillageView &p_view, int p_building, double p_seconds) {
    Task t;
    t.kind = p_kind;
    t.building = p_building;
    const NavPoint door = door_of(p_view, p_building);
    t.x = door.x;
    t.z = door.z;
    const Building &b = p_view.layout->at(p_building);
    t.face_x = b.x; // 面朝房子
    t.face_z = b.z;
    t.seconds = p_seconds;
    return t;
}

Task at_plot(TaskKind p_kind, const VillagerMind &p_mind, const VillageView &p_view, int p_i, int p_j, double p_seconds) {
    Task t;
    t.kind = p_kind;
    t.cell_i = p_i;
    t.cell_j = p_j;
    const NavPoint c = plot_center(*p_view.field, p_i, p_j);
    t.x = c.x;
    t.z = c.z;
    // 面朝走来的方向（站在地里往前锄）
    double dx = c.x - p_mind.x, dz = c.z - p_mind.z;
    const double len = std::hypot(dx, dz);
    if (len < 1e-6) {
        dx = 1.0;
        dz = 0.0;
    } else {
        dx /= len;
        dz /= len;
    }
    t.face_x = c.x + dx;
    t.face_z = c.z + dz;
    t.seconds = p_seconds;
    return t;
}

int first_of_kind(const SettlementLayout &p_layout, const char *p_kind) {
    for (int k = 0; k < p_layout.size(); k++) {
        if (p_layout.at(k).kind == p_kind) {
            return k;
        }
    }
    return -1;
}

// 除了 p_except 之外随便挑一座；只有它自己就返回 −1
int random_other(const SettlementLayout &p_layout, int p_except, Rng &p_rng) {
    if (p_layout.size() < 2) {
        return -1;
    }
    int k = p_rng.range(0, p_layout.size() - 1);
    if (k >= p_except && p_except >= 0) {
        k++;
    }
    return k;
}

Task idle_task(const VillagerMind &p_mind, const VillageView &p_view, Rng &p_rng) {
    const WorkRules &w = *p_view.work;
    const int other = random_other(*p_view.layout, p_mind.who.home, p_rng);
    if (other >= 0) {
        return at_building(TaskKind::CHAT, p_view, other, p_rng.uniform(w.chat_min_s, w.chat_max_s));
    }
    return at_building(TaskKind::WAIT, p_view, p_mind.who.home, w.wait_s);
}

// 仓里挑一样种子：份数最多的那样；没有返回空
std::string pick_seed(const Inventory &p_store) {
    std::string best;
    int most = 0;
    for (const ItemStack &s : p_store.get_stacks()) {
        if (catalog::is_seed(s.item) && s.count > most) {
            most = s.count;
            best = s.item;
        }
    }
    return best;
}

} // namespace

bool pick_wild_plot(const VillageView &p_view, double p_home_x, double p_home_z, int &r_i, int &r_j) {
    if (!p_view.farm || !p_view.field || !p_view.nav) {
        return false;
    }
    const IslandField &f = *p_view.field;
    const double radius = p_view.work->field_radius_m;
    const double ext = f.half_extent();
    const int i0 = std::max(0, int((p_home_z - radius + ext) / f.cell_m)), i1 = std::min(f.n - 2, int((p_home_z + radius + ext) / f.cell_m));
    const int j0 = std::max(0, int((p_home_x - radius + ext) / f.cell_m)), j1 = std::min(f.n - 2, int((p_home_x + radius + ext) / f.cell_m));
    double best = std::numeric_limits<double>::infinity();
    for (int i = i0; i <= i1; i++) {
        for (int j = j0; j <= j1; j++) {
            const NavPoint c = plot_center(f, i, j);
            const double d = std::hypot(c.x - p_home_x, c.z - p_home_z);
            if (d > radius || d >= best) {
                continue;
            }
            if (p_view.farm->available_action(i, j, false) != FarmAction::TILL || claimed(p_view, i, j)) {
                continue;
            }
            // 格心与四角都得是干地：挨着房子（导航格里被让出去的那圈）的不开
            const double h = 0.5 * f.cell_m - 0.05;
            if (!p_view.nav->dry_at(c.x, c.z) || !p_view.nav->dry_at(c.x - h, c.z - h) || !p_view.nav->dry_at(c.x + h, c.z - h) ||
                    !p_view.nav->dry_at(c.x - h, c.z + h) || !p_view.nav->dry_at(c.x + h, c.z + h)) {
                continue;
            }
            best = d;
            r_i = i;
            r_j = j;
        }
    }
    return best < std::numeric_limits<double>::infinity();
}

bool pick_water_spot(const VillageView &p_view, double p_x, double p_z, NavPoint &r_stand, NavPoint &r_water) {
    if (!p_view.field || !p_view.nav || p_view.field->river.size() < 2) {
        return false;
    }
    const std::vector<RiverPoint> &river = p_view.field->river;
    double best = std::numeric_limits<double>::infinity();
    for (size_t k = 0; k < river.size(); k++) {
        const RiverPoint &a = river[k == 0 ? 0 : k - 1];
        const RiverPoint &b = river[k == 0 ? 1 : k];
        double tx = b.x - a.x, tz = b.z - a.z;
        const double len = std::hypot(tx, tz);
        if (len < 1e-6) {
            continue;
        }
        tx /= len;
        tz /= len;
        const RiverPoint &p = river[k];
        for (double side : { -1.0, 1.0 }) {
            // 往岸上找第一块干地：水边上
            for (double out = p.half_width; out <= p.half_width + 4.0; out += 0.5) {
                const double sx = p.x - tz * side * out, sz = p.z + tx * side * out;
                if (!p_view.nav->dry_at(sx, sz)) {
                    continue;
                }
                const double d = std::hypot(sx - p_x, sz - p_z);
                if (d < best) {
                    best = d;
                    r_stand = { sx, sz };
                    r_water = { p.x, p.z };
                }
                break;
            }
        }
    }
    return best < std::numeric_limits<double>::infinity();
}

Task decide_task(VillagerMind &r_mind, const VillageView &p_view, Rng &p_rng) {
    const WorkRules &w = *p_view.work;
    const Resident &who = r_mind.who;
    if (is_home_hour(p_view.hour, *p_view.day)) {
        return at_building(TaskKind::GO_HOME, p_view, who.home, 0.0);
    }
    const NavPoint home_door = door_of(p_view, who.home);
    switch (who.job) {
        case Job::FARMER: {
            if (!r_mind.carrying.empty()) {
                const int granary = first_of_kind(*p_view.layout, "granary");
                return at_building(TaskKind::DELIVER, p_view, granary >= 0 ? granary : who.home, w.deliver_s);
            }
            if (!p_view.farm || !p_view.field) {
                return idle_task(r_mind, p_view, p_rng);
            }
            int owned = 0;
            int ripe = -1, tilled = -1;
            std::vector<int> growing;
            double ripe_d = 1e18, tilled_d = 1e18;
            for (int k = 0; k < int(p_view.claims->size()); k++) {
                const Claim &c = (*p_view.claims)[size_t(k)];
                if (c.owner != r_mind.index) {
                    continue;
                }
                owned++;
                const NavPoint at = plot_center(*p_view.field, c.i, c.j);
                const double d = std::hypot(at.x - r_mind.x, at.z - r_mind.z);
                switch (p_view.farm->plot_at(c.i, c.j).state) {
                    case PlotState::RIPE:
                        if (d < ripe_d) {
                            ripe_d = d;
                            ripe = k;
                        }
                        break;
                    case PlotState::TILLED:
                        if (d < tilled_d) {
                            tilled_d = d;
                            tilled = k;
                        }
                        break;
                    case PlotState::GROWING:
                        growing.push_back(k);
                        break;
                    case PlotState::WILD:
                        break;
                }
            }
            if (ripe >= 0) {
                const Claim &c = (*p_view.claims)[size_t(ripe)];
                return at_plot(TaskKind::HARVEST, r_mind, p_view, c.i, c.j, w.harvest_s);
            }
            const std::string seed = p_view.store ? pick_seed(*p_view.store) : std::string();
            if (tilled >= 0 && !seed.empty()) {
                const Claim &c = (*p_view.claims)[size_t(tilled)];
                Task t = at_plot(TaskKind::SOW, r_mind, p_view, c.i, c.j, w.sow_s);
                t.seed = seed;
                return t;
            }
            int i = 0, j = 0;
            if (owned < w.plots_per_farmer && pick_wild_plot(p_view, home_door.x, home_door.z, i, j)) {
                return at_plot(TaskKind::TILL, r_mind, p_view, i, j, w.till_s);
            }
            if (!growing.empty()) {
                const Claim &c = (*p_view.claims)[size_t(growing[size_t(p_rng.range(0, int(growing.size())))])];
                return at_plot(TaskKind::TEND, r_mind, p_view, c.i, c.j, w.tend_s);
            }
            return idle_task(r_mind, p_view, p_rng);
        }
        case Job::CARRIER: {
            if (!r_mind.carrying.empty()) {
                const int hall = first_of_kind(*p_view.layout, "hall");
                return at_building(TaskKind::DELIVER, p_view, hall >= 0 ? hall : who.home, w.deliver_s);
            }
            NavPoint stand, water;
            if (pick_water_spot(p_view, home_door.x, home_door.z, stand, water)) {
                Task t;
                t.kind = TaskKind::FETCH_WATER;
                t.x = stand.x;
                t.z = stand.z;
                t.face_x = water.x;
                t.face_z = water.z;
                t.seconds = w.fetch_s;
                return t;
            }
            return idle_task(r_mind, p_view, p_rng);
        }
        case Job::WATCHMAN: {
            if (who.post < 0 || who.post >= p_view.layout->size()) {
                return idle_task(r_mind, p_view, p_rng);
            }
            if (r_mind.patrol_left > 0) {
                r_mind.patrol_left--;
                const int stop = random_other(*p_view.layout, who.post, p_rng);
                if (stop >= 0) {
                    return at_building(TaskKind::PATROL, p_view, stop, w.patrol_s);
                }
            }
            r_mind.patrol_left = w.patrol_stops;
            Task t = at_building(TaskKind::WATCH, p_view, who.post, w.watch_s);
            // 背对望楼、往外看
            t.face_x = t.x + (t.x - t.face_x);
            t.face_z = t.z + (t.z - t.face_z);
            return t;
        }
        case Job::IDLER:
            break;
    }
    return idle_task(r_mind, p_view, p_rng);
}
