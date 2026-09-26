#include "villager_model.h"

#include <algorithm>
#include <cmath>
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

std::vector<Resident> assign_residents(const SettlementLayout &p_layout, bool p_has_farm, bool p_has_water, int p_per_house, uint64_t p_seed) {
    std::vector<Resident> out;
    const std::vector<Building> &items = p_layout.get_items();
    int hall = -1;
    for (int k = 0; k < int(items.size()); k++) {
        const Building &b = items[size_t(k)];
        if (b.kind == "house") {
            for (int m = 0; m < p_per_house; m++) {
                Resident r;
                r.home = k;
                r.job = m == 0 ? (p_has_farm ? Job::FARMER : Job::IDLER) : m == 1 ? (p_has_water ? Job::CARRIER : Job::IDLER) : Job::IDLER;
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

NavPoint plot_center(const PlotLattice &p_plots, int p_i, int p_j) {
    return { p_plots.center_x(p_j), p_plots.center_z(p_i) };
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

// 门前站位：有导航格就吸到最近的干地
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
    const NavPoint c = plot_center(*p_view.plots, p_i, p_j);
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

// 除了 p_except 之外随便挑一座房屋（船台不算）；没有别的就返回 −1
int random_other(const SettlementLayout &p_layout, int p_except, Rng &p_rng) {
    std::vector<int> others;
    bool all_houses = true;
    for (int k = 0; k < p_layout.size(); k++) {
        const BuildingDef *def = buildings::by_id(p_layout.at(k).kind);
        const bool house = !def || def->house;
        all_houses = all_houses && house;
        if (house && k != p_except) {
            others.push_back(k);
        }
    }
    if (all_houses) {
        // 全是房屋：照原来的抽法（随机流与以前逐位一致）
        if (p_layout.size() < 2) {
            return -1;
        }
        int k = p_rng.range(0, p_layout.size() - 1);
        if (k >= p_except && p_except >= 0) {
            k++;
        }
        return k;
    }
    if (others.empty()) {
        return -1;
    }
    return others[size_t(p_rng.range(0, int(others.size())))];
}

Task idle_task(const VillagerMind &p_mind, const VillageView &p_view, Rng &p_rng) {
    const WorkRules &w = *p_view.work;
    const int other = random_other(*p_view.layout, p_mind.who.home, p_rng);
    if (other >= 0) {
        return at_building(TaskKind::CHAT, p_view, other, p_rng.uniform(w.chat_min_s, w.chat_max_s));
    }
    // 独门独户没处串门：在自家门前走走，找块站得住的地方歇一阵
    Task t = at_building(TaskKind::WAIT, p_view, p_mind.who.home, w.wait_s);
    for (int attempt = 0; attempt < 6; attempt++) {
        const double angle = p_rng.uniform(0.0, 6.283185307179586), dist = p_rng.uniform(w.stroll_min_m, w.stroll_max_m);
        const double x = t.x + std::sin(angle) * dist, z = t.z + std::cos(angle) * dist;
        if (view_dry_at(p_view, x, z)) {
            t.face_x = x + (x - t.x);
            t.face_z = z + (z - t.z);
            t.x = x;
            t.z = z;
            t.seconds = p_rng.uniform(w.chat_min_s, w.chat_max_s);
            break;
        }
    }
    return t;
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

bool view_dry_at(const VillageView &p_view, double p_x, double p_z) {
    if (p_view.nav) {
        return p_view.nav->dry_at(p_x, p_z);
    }
    double y = 0.0;
    if (!p_view.ground || !p_view.ground->walk_height(p_x, p_z, y) || p_view.ground->water_depth(p_x, p_z) > 0.05) {
        return false;
    }
    if (p_view.layout) {
        const double margin = p_view.nav_rules ? p_view.nav_rules->building_margin_m : NavRules().building_margin_m;
        for (const Building &b : p_view.layout->get_items()) {
            const BuildingDef *def = buildings::by_id(b.kind);
            if (def && std::hypot(b.x - p_x, b.z - p_z) <= 0.5 * std::hypot(def->width_m, def->depth_m) + margin && footprint_contains(*def, b, p_x, p_z, margin)) {
                return false;
            }
        }
    }
    return true;
}

bool pick_wild_plot(const VillageView &p_view, double p_home_x, double p_home_z, int &r_i, int &r_j) {
    if (!p_view.fields || !p_view.plots || (!p_view.nav && !p_view.ground)) {
        return false;
    }
    const PlotLattice &l = *p_view.plots;
    const double radius = p_view.work->field_radius_m;
    // 与 PlotLattice::cell_of 同一算式（岛上与旧的 (z − r + ext) / cell 逐位一致）
    auto index_of = [&l](double p_v, double p_zero) { return int(std::floor((p_v - p_zero + l.origin * l.cell_m) / l.cell_m)); };
    int i0 = index_of(p_home_z - radius, l.z0), i1 = index_of(p_home_z + radius, l.z0);
    int j0 = index_of(p_home_x - radius, l.x0), j1 = index_of(p_home_x + radius, l.x0);
    if (l.bounded) {
        i0 = std::max(i0, l.i_min);
        j0 = std::max(j0, l.j_min);
        i1 = std::min(i1, l.i_max - 1);
        j1 = std::min(j1, l.j_max - 1);
    }
    // 由近及远一格格试，第一格站得住的就是（等距的按行列先后，与逐格扫一遍取最近的结果一样）
    struct Candidate {
        double d;
        int i, j;
    };
    std::vector<Candidate> order;
    for (int i = i0; i <= i1; i++) {
        for (int j = j0; j <= j1; j++) {
            const NavPoint c = plot_center(l, i, j);
            const double d = std::hypot(c.x - p_home_x, c.z - p_home_z);
            if (d <= radius) {
                order.push_back({ d, i, j });
            }
        }
    }
    std::sort(order.begin(), order.end(), [](const Candidate &p_a, const Candidate &p_b) {
        return p_a.d != p_b.d ? p_a.d < p_b.d : (p_a.i != p_b.i ? p_a.i < p_b.i : p_a.j < p_b.j);
    });
    for (const Candidate &cand : order) {
        const int i = cand.i, j = cand.j;
        if (p_view.arable && p_view.arable->covers(i, j) && !p_view.arable->arable(i, j)) {
            continue; // 开账时勘察过：这格垦不了
        }
        const NavPoint c = plot_center(l, i, j);
        if (p_view.fields->available(i, j, false) != FarmAction::TILL || claimed(p_view, i, j) || (p_view.taken && p_view.taken->taken(i, j))) {
            continue;
        }
        // 格心与四角都得站得住：挨着房子（导航格里被让出去的那圈）的不开
        const double h = 0.5 * l.cell_m - 0.05;
        if (!view_dry_at(p_view, c.x, c.z) || !view_dry_at(p_view, c.x - h, c.z - h) || !view_dry_at(p_view, c.x + h, c.z - h) ||
                !view_dry_at(p_view, c.x - h, c.z + h) || !view_dry_at(p_view, c.x + h, c.z + h)) {
            continue;
        }
        r_i = i;
        r_j = j;
        return true;
    }
    return false;
}

bool pick_water_spot(const VillageView &p_view, double p_x, double p_z, NavPoint &r_stand, NavPoint &r_water) {
    if (!p_view.water || (!p_view.nav && !p_view.ground)) {
        return false;
    }
    double best = std::numeric_limits<double>::infinity();
    for (const WaterLine &line : *p_view.water) {
        if (line.size() < 2) {
            continue;
        }
        for (size_t k = 0; k < line.size(); k++) {
            const WaterPoint &a = line[k == 0 ? 0 : k - 1];
            const WaterPoint &b = line[k == 0 ? 1 : k];
            double tx = b.x - a.x, tz = b.z - a.z;
            const double len = std::hypot(tx, tz);
            if (len < 1e-6) {
                continue;
            }
            tx /= len;
            tz /= len;
            const WaterPoint &p = line[k];
            for (double side : { -1.0, 1.0 }) {
                // 往岸上找第一块干地：水边上
                for (double out = p.half_width; out <= p.half_width + 4.0; out += 0.5) {
                    const double sx = p.x - tz * side * out, sz = p.z + tx * side * out;
                    if (!view_dry_at(p_view, sx, sz)) {
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
    }
    return best < std::numeric_limits<double>::infinity();
}

Task decide_task(VillagerMind &r_mind, const VillageView &p_view, Rng &p_rng) {
    const WorkRules &w = *p_view.work;
    const Resident &who = r_mind.who;
    if (p_view.home > 0 || (p_view.home < 0 && is_home_hour(p_view.hour, *p_view.day))) {
        return at_building(TaskKind::GO_HOME, p_view, who.home, 0.0);
    }
    const NavPoint home_door = door_of(p_view, who.home);
    switch (who.job) {
        case Job::FARMER: {
            if (!r_mind.carrying.empty()) {
                const int granary = first_of_kind(*p_view.layout, "granary");
                return at_building(TaskKind::DELIVER, p_view, granary >= 0 ? granary : who.home, w.deliver_s);
            }
            if (!p_view.fields || !p_view.plots) {
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
                const NavPoint at = plot_center(*p_view.plots, c.i, c.j);
                const double d = std::hypot(at.x - r_mind.x, at.z - r_mind.z);
                switch (p_view.fields->plot_at(c.i, c.j).state) {
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
            const std::string seed = p_view.store && p_view.sowing ? pick_seed(*p_view.store) : std::string();
            if (tilled >= 0 && !seed.empty()) {
                const Claim &c = (*p_view.claims)[size_t(tilled)];
                Task t = at_plot(TaskKind::SOW, r_mind, p_view, c.i, c.j, w.sow_s);
                t.seed = seed;
                return t;
            }
            int i = 0, j = 0;
            if (owned < w.plots_per_farmer && p_view.sowing && p_view.now >= r_mind.wild_retry_at) {
                if (pick_wild_plot(p_view, home_door.x, home_door.z, i, j)) {
                    return at_plot(TaskKind::TILL, r_mind, p_view, i, j, w.till_s);
                }
                r_mind.wild_retry_at = p_view.now + w.wild_retry_s; // 家门口没荒地了：过一阵再看（别的村可能退了认领）
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
            if (p_view.water_cap > 0.0 && p_view.water_stock >= p_view.water_cap - 1e-9) {
                return idle_task(r_mind, p_view, p_rng); // 缸满了：歇着去串门
            }
            // 去哪打水只看家门与水，找一次记下（找水要沿水边一点点探，远处没有导航格时尤其费）
            if (r_mind.water_spot < 0) {
                NavPoint stand, water;
                r_mind.water_spot = pick_water_spot(p_view, home_door.x, home_door.z, stand, water) ? 1 : 0;
                r_mind.water_x = stand.x;
                r_mind.water_z = stand.z;
                r_mind.water_face_x = water.x;
                r_mind.water_face_z = water.z;
            }
            if (r_mind.water_spot > 0) {
                Task t;
                t.kind = TaskKind::FETCH_WATER;
                t.x = r_mind.water_x;
                t.z = r_mind.water_z;
                t.face_x = r_mind.water_face_x;
                t.face_z = r_mind.water_face_z;
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
