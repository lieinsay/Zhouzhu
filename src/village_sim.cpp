#include "village_sim.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "crops.h"

namespace {

constexpr double INF = std::numeric_limits<double>::infinity();
constexpr double EPS = 1e-9;

// 钟点：分界点那一秒本身算分界之后（歇晌的第一秒已经该回家了），浮点舍入别让它差一点落回前面
double hour_at(double p_seconds, const DayRules &p_day) {
    return read_clock(p_seconds + 1e-6, p_day).hour;
}

double path_left(const PersonState &p_person) {
    double len = 0.0, x = p_person.mind.x, z = p_person.mind.z;
    for (size_t k = p_person.next; k < p_person.path.size(); k++) {
        len += std::hypot(p_person.path[k].x - x, p_person.path[k].z - z);
        x = p_person.path[k].x;
        z = p_person.path[k].z;
    }
    return len;
}

// 离下一件事（到了、干完、该出门）还有几秒；在家的钟点里在屋里的人没有事
double time_to_event(const PersonState &p_person, bool p_home) {
    switch (p_person.phase) {
        case FolkPhase::INDOORS:
            return p_home ? INF : std::max(0.0, p_person.timer);
        case FolkPhase::WALKING:
            return (p_person.estimated ? p_person.walk_left : path_left(p_person)) / std::max(0.05, p_person.speed);
        case FolkPhase::WORKING:
            return std::max(0.0, p_person.timer);
    }
    return INF;
}

void step_person(PersonState &r_person, double p_dt, bool p_home) {
    switch (r_person.phase) {
        case FolkPhase::INDOORS:
            if (!p_home) {
                r_person.timer -= p_dt;
            }
            break;
        case FolkPhase::WORKING:
            r_person.timer -= p_dt;
            break;
        case FolkPhase::WALKING: {
            double budget = r_person.speed * p_dt;
            VillagerMind &m = r_person.mind;
            if (r_person.estimated) {
                // 估着走：沿直线往目标挪，按「还剩的路」的比例（绕路也算在里面）
                const double step = std::min(budget, r_person.walk_left);
                if (r_person.walk_left > EPS) {
                    const double f = step / r_person.walk_left;
                    m.x += (r_person.task.x - m.x) * f;
                    m.z += (r_person.task.z - m.z) * f;
                }
                r_person.walk_left -= step;
                if (r_person.walk_left <= EPS) {
                    r_person.walk_left = 0.0;
                    m.x = r_person.task.x;
                    m.z = r_person.task.z;
                }
                break;
            }
            while (budget > 0.0 && r_person.next < r_person.path.size()) {
                const NavPoint &target = r_person.path[r_person.next];
                const double dx = target.x - m.x, dz = target.z - m.z;
                const double dist = std::hypot(dx, dz);
                if (dist <= budget) {
                    m.x = target.x;
                    m.z = target.z;
                    budget -= dist;
                    r_person.next++;
                } else {
                    m.x += dx / dist * budget;
                    m.z += dz / dist * budget;
                    budget = 0.0;
                }
            }
        } break;
    }
}

bool due(const PersonState &p_person, bool p_home) {
    return time_to_event(p_person, p_home) <= EPS;
}

VillageView make_view(const VillageState &p_state, const VillageWorld &p_world, double p_seconds) {
    VillageView v;
    v.plots = p_world.fields ? p_world.plots : nullptr;
    v.water = p_world.water;
    v.layout = p_world.layout;
    v.nav = p_world.nav;
    v.ground = p_world.ground;
    v.fields = p_world.fields;
    v.claims = &p_state.claims;
    v.taken = p_world.taken;
    v.arable = p_state.arable.rows > 0 ? &p_state.arable : nullptr;
    v.store = &p_state.store;
    v.water_stock = p_state.water;
    v.water_cap = p_state.water_cap;
    v.day = p_world.day;
    v.work = &p_world.rules->work;
    v.nav_rules = &p_world.rules->nav;
    v.hour = hour_at(p_seconds, *p_world.day);
    v.now = p_seconds;
    return v;
}

bool is_grain(const std::string &p_item) {
    for (int k = 0; k < catalog::CROP_COUNT; k++) {
        if (p_item == catalog::CROPS[k].product) {
            return true;
        }
    }
    return false;
}

void pour_water(VillageState &r_state, double p_amount) {
    const double room = std::max(0.0, r_state.water_cap - r_state.water);
    const double in = std::min(room, p_amount);
    r_state.water += in;
    r_state.tally.water_in += in;
}

void enter_home(VillageState &r_state, PersonState &r_person) {
    // 背着的粮顺手放进仓里（天黑了不再跑一趟），水倒进缸里
    VillagerMind &m = r_person.mind;
    if (m.carrying == "water") {
        pour_water(r_state, m.carry_count);
    } else if (!m.carrying.empty()) {
        r_state.store.add(m.carrying, m.carry_count);
    }
    m.carrying.clear();
    m.carry_count = 0;
    r_person.phase = FolkPhase::INDOORS;
    r_person.path.clear();
    r_person.next = 0;
    r_person.estimated = false;
    r_person.walk_left = 0.0;
    r_person.timer = r_person.rng.uniform(1.0, 25.0);
}

void remove_claim(VillageState &r_state, const VillageWorld &p_world, int p_i, int p_j) {
    for (size_t k = 0; k < r_state.claims.size(); k++) {
        if (r_state.claims[k].i == p_i && r_state.claims[k].j == p_j) {
            r_state.claims.erase(r_state.claims.begin() + long(k));
            if (p_world.taken) {
                p_world.taken->remove(p_i, p_j);
            }
            return;
        }
    }
}

// 原地歇一阵（找不到路之后）
void wait_here(PersonState &r_person, const WorkRules &p_work) {
    Task &t = r_person.task;
    t.kind = TaskKind::WAIT;
    t.x = r_person.mind.x;
    t.z = r_person.mind.z;
    t.face_x = r_person.mind.x;
    t.face_z = r_person.mind.z + 1.0;
    t.seconds = p_work.wait_s;
    r_person.phase = FolkPhase::WORKING;
    r_person.timer = t.seconds;
    r_person.path.clear();
    r_person.estimated = false;
}

// 出发去干眼下这件事：近处找路，远处估路长
void set_out(VillageState &r_state, const VillageWorld &p_world, PersonState &r_person) {
    const Task &t = r_person.task;
    r_person.next = 0;
    r_person.path.clear();
    if (!p_world.nav) {
        r_person.estimated = true;
        r_person.walk_left = std::hypot(t.x - r_person.mind.x, t.z - r_person.mind.z) * p_world.rules->walk_detour;
        r_person.phase = FolkPhase::WALKING;
        return;
    }
    r_person.estimated = false;
    r_person.walk_left = 0.0;
    r_person.path = nav_find_path(*p_world.nav, p_world.rules->nav, r_person.mind.x, r_person.mind.z, t.x, t.z);
    if (!r_person.path.empty()) {
        r_person.phase = FolkPhase::WALKING;
        return;
    }
    r_state.tally.path_failures++;
    if (t.kind == TaskKind::TILL) {
        for (Claim &c : r_state.claims) {
            if (c.i == t.cell_i && c.j == t.cell_j) {
                c.owner = -1; // 够不着：记下来，谁也别再挑它
            }
        }
    }
    if (t.kind == TaskKind::GO_HOME) {
        enter_home(r_state, r_person); // 回不了家也不能在外面过夜
        return;
    }
    wait_here(r_person, p_world.rules->work);
}

// 决策下一件事并出发
void assign(VillageState &r_state, const VillageWorld &p_world, int p_index, double p_seconds) {
    PersonState &p = r_state.people[size_t(p_index)];
    p.task = decide_task(p.mind, make_view(r_state, p_world, p_seconds), p.rng);
    if (p.task.kind == TaskKind::TILL) {
        r_state.claims.push_back({ p.task.cell_i, p.task.cell_j, p_index }); // 先占上，免得两个人挑中同一块
        if (p_world.taken) {
            p_world.taken->add(p.task.cell_i, p.task.cell_j);
        }
    }
    set_out(r_state, p_world, p);
}

// 干完了：把结果落下去
void finish(VillageState &r_state, const VillageWorld &p_world, PersonState &r_person) {
    const Task &t = r_person.task;
    VillagerMind &m = r_person.mind;
    FieldBook *fields = p_world.fields;
    bool done = true;
    switch (t.kind) {
        case TaskKind::TILL:
            done = fields && fields->available(t.cell_i, t.cell_j, false) == FarmAction::TILL;
            if (done) {
                fields->till(t.cell_i, t.cell_j);
            } else {
                remove_claim(r_state, p_world, t.cell_i, t.cell_j); // 让人抢先垦了（或地形换了）：这块不算他的
            }
            break;
        case TaskKind::SOW: {
            const CropDef *crop = catalog::crop_of_seed(t.seed);
            done = fields && crop && fields->available(t.cell_i, t.cell_j, true) == FarmAction::SOW && r_state.store.remove(t.seed, 1);
            if (done) {
                fields->sow(t.cell_i, t.cell_j, crop);
                r_state.tally.sown++;
            }
        } break;
        case TaskKind::HARVEST: {
            done = fields && fields->available(t.cell_i, t.cell_j, false) == FarmAction::HARVEST;
            if (done) {
                const Harvest got = fields->harvest(t.cell_i, t.cell_j);
                if (got.item && got.count > 0) {
                    m.carrying = got.item;
                    m.carry_count = got.count;
                    r_state.tally.harvested += got.count;
                }
            }
        } break;
        case TaskKind::DELIVER:
            if (m.carrying == "water") {
                pour_water(r_state, m.carry_count);
            } else if (!m.carrying.empty()) {
                r_state.store.add(m.carrying, m.carry_count);
            }
            m.carrying.clear();
            m.carry_count = 0;
            break;
        case TaskKind::FETCH_WATER:
            m.carrying = "water";
            m.carry_count = 1;
            break;
        default:
            break;
    }
    if (done) {
        r_state.tally.done[int(t.kind)]++;
    }
}

// 开饭：每人吃 meal_grain 份粮（仓里给农夫留够种子），全村按人头用水
void eat(VillageState &r_state, const SimRules &p_rules) {
    int farmers = 0;
    for (const PersonState &p : r_state.people) {
        farmers += p.mind.who.job == Job::FARMER ? 1 : 0;
    }
    const int reserve = farmers * p_rules.seed_reserve_per_farmer;
    auto grain_in_store = [&r_state]() {
        int n = 0;
        for (const ItemStack &s : r_state.store.get_stacks()) {
            n += is_grain(s.item) ? s.count : 0;
        }
        return n;
    };
    int available = grain_in_store() - reserve;
    for (size_t k = 0; k < r_state.people.size(); k++) {
        bool fed = true;
        for (int g = 0; g < p_rules.meal_grain; g++) {
            if (available <= 0) {
                fed = false;
                break;
            }
            // 吃存得最多的那样
            std::string most;
            int count = 0;
            for (const ItemStack &s : r_state.store.get_stacks()) {
                if (is_grain(s.item) && s.count > count) {
                    count = s.count;
                    most = s.item;
                }
            }
            r_state.store.remove(most, 1);
            r_state.tally.eaten++;
            available--;
        }
        r_state.tally.hungry += fed ? 0 : 1;
    }
    const double need = p_rules.meal_water * double(r_state.people.size());
    const double used = std::min(r_state.water, need);
    r_state.water -= used;
    r_state.tally.water_used += used;
}

// 一个人眼下的事到了点：到了 / 干完 / 该出门。一个人一次可能连着好几件（到了就开始干），给个上限免得死循环
void handle_due(VillageState &r_state, const VillageWorld &p_world, int p_index, double p_seconds, bool p_home) {
    for (int guard = 0; guard < 8; guard++) {
        PersonState &p = r_state.people[size_t(p_index)];
        if (!due(p, p_home)) {
            return;
        }
        switch (p.phase) {
            case FolkPhase::INDOORS:
                assign(r_state, p_world, p_index, p_seconds);
                break;
            case FolkPhase::WALKING:
                p.path.clear();
                p.next = 0;
                p.estimated = false;
                p.walk_left = 0.0;
                if (p.task.kind == TaskKind::GO_HOME) {
                    enter_home(r_state, p);
                } else {
                    p.phase = FolkPhase::WORKING;
                    p.timer = p.task.seconds;
                }
                break;
            case FolkPhase::WORKING:
                finish(r_state, p_world, p);
                assign(r_state, p_world, p_index, p_seconds);
                break;
        }
    }
}

} // namespace

Inventory village_starting_store() {
    Inventory store;
    store.add("millet", 8);
    store.add("bean", 4);
    return store;
}

void found_village(VillageState &r_state, const VillageWorld &p_world, bool p_has_farm, bool p_has_water, int p_per_house, int p_max_folk, uint64_t p_seed,
        const Inventory &p_store, double p_now) {
    // 换了清单重招：旧的认领从全图认领表里撤掉（地还在农田里，是垦过的荒地）
    if (p_world.taken) {
        for (const Claim &c : r_state.claims) {
            p_world.taken->remove(c.i, c.j);
        }
    }
    ArableMask arable = std::move(r_state.arable); // 周围哪些地能垦不随清单变
    r_state = VillageState();
    r_state.arable = std::move(arable);
    const SettlementLayout &layout = *p_world.layout;
    const SimRules &rules = *p_world.rules;
    std::vector<Resident> residents = assign_residents(layout, p_has_farm, p_has_water, p_per_house, p_seed);
    if (p_max_folk > 0 && int(residents.size()) > p_max_folk) {
        residents.resize(size_t(p_max_folk)); // 封顶：其余的算在屋里
    }
    r_state.layout_sig = layout_signature(layout);
    r_state.founded = true;
    r_state.clock = p_now;
    r_state.deployed = p_world.nav != nullptr;
    r_state.store = p_store;
    r_state.water_cap = std::max(rules.vat_min, rules.vat_per_person * double(residents.size()));
    r_state.water = 0.5 * r_state.water_cap;
    const Rng base(p_seed * 0x9E3779B97F4A7C15ull + 0x51A7ull);
    r_state.people.resize(residents.size());
    for (int k = 0; k < int(residents.size()); k++) {
        PersonState &p = r_state.people[size_t(k)];
        p.mind.index = k;
        p.mind.who = residents[size_t(k)];
        p.rng = base.fork(uint64_t(k));
        p.speed = rules.walk_speed * p.rng.uniform(0.85, 1.15);
        NavPoint door = building_door(layout.at(p.mind.who.home));
        NavPoint snapped;
        if (p_world.nav && nav_nearest_walkable(*p_world.nav, door.x, door.z, 6.0, true, snapped)) {
            door = snapped;
        }
        p.mind.x = door.x;
        p.mind.z = door.z;
        p.task = Task();
        p.task.kind = TaskKind::GO_HOME;
        p.task.building = p.mind.who.home;
        p.task.x = door.x;
        p.task.z = door.z;
        p.phase = FolkPhase::INDOORS;
        p.timer = p.rng.uniform(0.5, 12.0); // 出门错开些
    }
    // 挑水的去哪打水只看家门与水：开账时就找好（远处开账在后台线程里，找水要沿水边一点点探地形）
    VillageView view = make_view(r_state, p_world, p_now);
    for (PersonState &p : r_state.people) {
        VillagerMind &m = p.mind;
        if (m.who.job != Job::CARRIER) {
            continue;
        }
        NavPoint door = building_door(layout.at(m.who.home)), snapped;
        if (p_world.nav && nav_nearest_walkable(*p_world.nav, door.x, door.z, 6.0, true, snapped)) {
            door = snapped; // 与决策时的门前站位一致
        }
        NavPoint stand, water;
        m.water_spot = pick_water_spot(view, door.x, door.z, stand, water) ? 1 : 0;
        m.water_x = stand.x;
        m.water_z = stand.z;
        m.water_face_x = water.x;
        m.water_face_z = water.z;
    }
}

void advance_village(VillageState &r_state, const VillageWorld &p_world, double p_until) {
    if (!r_state.founded || p_until <= r_state.clock) {
        return;
    }
    const DayRules &day = *p_world.day;
    double t = r_state.clock;
    int stalled = 0; // 连着几步没往前走（有人一直「到点」却落不了地）：兜底往前挪一点，免得死循环
    while (t < p_until) {
        DayMark mark = DayMark::WORK_START;
        const double mark_at = next_day_mark(t, day, &mark);
        const bool home = is_home_hour(hour_at(t, day), day);
        double step_to = std::min(p_until, mark_at);
        for (const PersonState &p : r_state.people) {
            step_to = std::min(step_to, t + time_to_event(p, home));
        }
        if (step_to <= t && ++stalled > 32) {
            step_to = std::min(p_until, t + 1e-3);
        } else if (step_to > t) {
            stalled = 0;
        }
        const double dt = std::max(0.0, step_to - t);
        if (dt > 0.0) {
            if (p_world.fields) {
                p_world.fields->grow(game_hours(dt, day));
            }
            for (PersonState &p : r_state.people) {
                step_person(p, dt, home);
            }
        }
        t = step_to;
        r_state.clock = t;
        if (t >= mark_at) {
            // 分界点：开饭；到了在家的钟点，半路的掉头、干着的放下，都回家
            if (is_meal(mark)) {
                eat(r_state, *p_world.rules);
            }
            if (is_home_hour(hour_at(t, day), day)) {
                for (int k = 0; k < int(r_state.people.size()); k++) {
                    const PersonState &p = r_state.people[size_t(k)];
                    if (p.phase != FolkPhase::INDOORS && p.task.kind != TaskKind::GO_HOME) {
                        assign(r_state, p_world, k, t);
                    }
                }
            }
        }
        const bool home_now = is_home_hour(hour_at(t, day), day);
        for (int k = 0; k < int(r_state.people.size()); k++) {
            handle_due(r_state, p_world, k, t, home_now);
        }
    }
    r_state.clock = p_until;
}

void deploy_village(VillageState &r_state, const VillageWorld &p_world, FarmModel *p_farm) {
    for (const HeldPlot &h : r_state.held) {
        if (!p_farm || !p_farm->put(h.i, h.j, h.plot)) {
            remove_claim(r_state, p_world, h.i, h.j);
        }
    }
    r_state.held.clear();
    for (PersonState &p : r_state.people) {
        NavPoint snapped;
        switch (p.phase) {
            case FolkPhase::INDOORS:
            case FolkPhase::WORKING:
                if (p_world.nav && nav_nearest_walkable(*p_world.nav, p.mind.x, p.mind.z, 6.0, false, snapped)) {
                    p.mind.x = snapped.x;
                    p.mind.z = snapped.z;
                }
                break;
            case FolkPhase::WALKING:
                set_out(r_state, p_world, p); // 从此刻站的地方重新找路
                break;
        }
    }
    r_state.deployed = p_world.nav != nullptr;
}

void collapse_village(VillageState &r_state, FarmModel *p_farm) {
    if (p_farm) {
        for (const Claim &c : r_state.claims) {
            if (c.owner < 0) {
                continue;
            }
            const Plot plot = p_farm->take(c.i, c.j);
            if (plot.state == PlotState::WILD) {
                continue; // 认领了还没垦
            }
            HeldPlot h;
            h.i = c.i;
            h.j = c.j;
            h.plot = plot;
            h.moisture = p_farm->survey(c.i, c.j).moisture;
            r_state.held.push_back(h);
        }
    }
    for (PersonState &p : r_state.people) {
        if (p.phase == FolkPhase::WALKING && !p.estimated) {
            p.walk_left = path_left(p);
            p.estimated = true;
            p.path.clear();
            p.next = 0;
        }
    }
    r_state.deployed = false;
}

void village_extent(const SettlementLayout &p_layout, double &r_cx, double &r_cz, double &r_reach) {
    r_cx = r_cz = r_reach = 0.0;
    if (p_layout.empty()) {
        return;
    }
    for (const Building &b : p_layout.get_items()) {
        r_cx += b.x / p_layout.size();
        r_cz += b.z / p_layout.size();
    }
    for (const Building &b : p_layout.get_items()) {
        r_reach = std::max(r_reach, std::hypot(b.x - r_cx, b.z - r_cz));
    }
}

bool village_has_farm(const FarmModel &p_land, double p_to_land_x, double p_to_land_z, const SettlementLayout &p_layout, const WorkRules &p_work) {
    if (p_layout.empty()) {
        return false;
    }
    double cx = 0.0, cz = 0.0, reach = 0.0;
    village_extent(p_layout, cx, cz, reach);
    return p_land.any_arable_near(cx + p_to_land_x, cz + p_to_land_z, reach + p_work.field_radius_m);
}

std::vector<WaterLine> village_water(const MapGround &p_ground, const SettlementLayout &p_layout, const WorkRules &p_work) {
    std::vector<WaterLine> out;
    if (p_layout.empty()) {
        return out;
    }
    double cx = 0.0, cz = 0.0, reach = 0.0;
    village_extent(p_layout, cx, cz, reach);
    std::vector<WaterLine> lines;
    const double far = reach + p_work.water_reach_m;
    p_ground.water_lines(cx - far, cz - far, cx + far, cz + far, lines);
    for (WaterLine &line : lines) {
        bool close = false;
        for (const WaterPoint &p : line) {
            close = close || std::hypot(p.x - cx, p.z - cz) <= far;
        }
        if (close) {
            out.push_back(std::move(line));
        }
    }
    return out;
}
