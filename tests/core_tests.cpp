// 纯算法文件（不含 Godot 头的那几个）的单元测试。不引测试框架：一个可执行文件，失败即非零退出。
//
//   cmake --build build --config Debug --target zhouzhu_core_tests
//   ctest --test-dir build -C Debug --output-on-failure

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "cover_blend.h"
#include "crops.h"
#include "deposit_kinds.h"
#include "day_clock.h"
#include "field_book.h"
#include "dock_site.h"
#include "farm_model.h"
#include "flat_ground.h"
#include "flight_model.h"
#include "freight.h"
#include "building_parts.h"
#include "house_template.h"
#include "interior_rules.h"
#include "inventory.h"
#include "item_slots.h"
#include "island_field.h"
#include "island_ground.h"
#include "land_berth.h"
#include "map_ground.h"
#include "map_image.h"
#include "placeables.h"
#include "recipes.h"
#include "settlement_model.h"
#include "ship_layout.h"
#include "terrain_tile.h"
#include "value_noise.h"
#include "village_plan.h"
#include "village_sim.h"
#include "village_state.h"
#include "villager_model.h"
#include "resource_scatter.h"
#include "resource_sites.h"
#include "river_mesh.h"
#include "world_grid.h"
#include "world_ground.h"
#include "world_river.h"
#include "world_sites.h"
#include "world_ledger.h"
#include "wild_landing.h"
#include "world_terrain.h"

namespace {

int checks = 0;
int failures = 0;

void check(bool p_ok, const std::string &p_what) {
    checks++;
    if (!p_ok) {
        failures++;
        std::printf("  FAIL  %s\n", p_what.c_str());
    }
}

void check_near(double p_actual, double p_expected, double p_tol, const std::string &p_what) {
    check(std::abs(p_actual - p_expected) <= p_tol,
            p_what + "（得到 " + std::to_string(p_actual) + "，应为 " + std::to_string(p_expected) + " ± " + std::to_string(p_tol) + "）");
}

// 岛上的农田模型：地面与模型一起活着
struct IslandFarm {
    IslandGround ground;
    FarmModel model;
    IslandFarm(const IslandField &p_field, double p_apron, const FarmRules &p_rules) :
            ground(p_field, p_apron), model(&ground, ground.plot_lattice(), p_rules) {}
    int arable_count() const {
        const PlotLattice &l = model.get_lattice();
        int n = 0;
        for (int i = l.i_min; i < l.i_max; i++) {
            for (int j = l.j_min; j < l.j_max; j++) {
                n += model.land_at(i, j) == Arability::ARABLE ? 1 : 0;
            }
        }
        return n;
    }
};

// 整座岛的导航格（与 Island 宿主同一个窗口）
NavGrid island_nav(const IslandField &p_field, const SettlementLayout &p_layout, const NavRules &p_rules) {
    double x0 = 0.0, z0 = 0.0;
    int cols = 0, rows = 0;
    island_nav_window(p_field, p_rules, x0, z0, cols, rows);
    return build_nav_grid(IslandGround(p_field, 20.0), x0, z0, cols, rows, p_layout, p_rules);
}

uint64_t field_hash(const IslandField &f) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(uint64_t(f.n));
    for (size_t k = 0; k < f.mask.size(); k++) {
        mix(f.mask[k]);
        if (f.mask[k]) {
            mix(uint64_t(int64_t(std::llround(double(f.height[k]) * 1000.0))));
            mix(uint64_t(int64_t(std::llround(double(f.bottom[k]) * 1000.0))));
        }
    }
    return h;
}

// 掩膜是否只有一个 4 连通块
bool single_component(const IslandField &f) {
    std::vector<uint8_t> seen(f.mask.size(), 0);
    std::vector<int> stack;
    int components = 0;
    for (int start = 0; start < f.n * f.n; start++) {
        if (!f.mask[start] || seen[start]) {
            continue;
        }
        components++;
        stack.push_back(start);
        seen[start] = 1;
        while (!stack.empty()) {
            const int k = stack.back();
            stack.pop_back();
            const int i = k / f.n, j = k % f.n;
            const int nb[4][2] = { { i - 1, j }, { i + 1, j }, { i, j - 1 }, { i, j + 1 } };
            for (const auto &q : nb) {
                if (f.in_mask(q[0], q[1]) && !seen[f.index(q[0], q[1])]) {
                    seen[f.index(q[0], q[1])] = 1;
                    stack.push_back(int(f.index(q[0], q[1])));
                }
            }
        }
    }
    return components == 1;
}

void test_rng() {
    std::printf("rng\n");
    Rng a(42), b(42), c(43);
    bool same = true, differs = false, in_range = true;
    for (int k = 0; k < 1000; k++) {
        const uint64_t va = a.next_u64();
        same = same && va == b.next_u64();
        differs = differs || va != c.next_u64();
    }
    Rng d(7);
    double sum = 0.0;
    for (int k = 0; k < 20000; k++) {
        const double u = d.uniform();
        in_range = in_range && u >= 0.0 && u < 1.0;
        sum += u;
    }
    check(same, "同种子同序列");
    check(differs, "不同种子不同序列");
    check(in_range, "uniform() 落在 [0, 1)");
    check_near(sum / 20000.0, 0.5, 0.01, "uniform() 均值");
    Rng e(9);
    bool range_ok = true;
    for (int k = 0; k < 5000; k++) {
        const int v = e.range(9, 16);
        range_ok = range_ok && v >= 9 && v < 16;
    }
    check(range_ok, "range(9, 16) 落在 [9, 16)");
    check(Rng(1).fork(1).next_u64() != Rng(1).fork(2).next_u64(), "子流互不相同");
}

void test_noise() {
    std::printf("value_noise\n");
    Rng r1(5), r2(5);
    FractalNoise a(r1, -100, -100, 100, 100, 40.0, 4, 0.5);
    FractalNoise b(r2, -100, -100, 100, 100, 40.0, 4, 0.5);
    bool bounded = true, same = true;
    double max_jump = 0.0, lo = 1e9, hi = -1e9;
    for (int i = 0; i < 200; i++) {
        for (int j = 0; j < 200; j++) {
            const double x = -100 + j, z = -100 + i;
            const double v = a.sample(x, z);
            bounded = bounded && v >= -1.0 && v <= 1.0;
            same = same && v == b.sample(x, z);
            max_jump = std::max(max_jump, std::abs(v - a.sample(x + 0.1, z)));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    check(bounded, "取值在 [-1, 1]");
    check(same, "同种子同场");
    check(max_jump < 0.05, "连续：0.1 m 内的变化 < 0.05");
    check(hi - lo > 0.5, "不是常数场");
}

void test_island(const char *p_name, double p_age, IslandAge p_class) {
    std::printf("island_field（%s）\n", p_name);
    IslandParams p;
    p.seed = 1165;
    p.age = p_age;
    p.rim_m = 40.0;
    p.peak_m = 95.0;
    p.dock_bearing = 2.1;
    const IslandField f = generate_island_field(p);

    check(f.age_class == p_class, "岛龄类别");
    const double area = f.land_count() * p.cell_m * p.cell_m;
    check_near(area / p.area_m2, 1.0, 0.02, "面积 = 目标（< 2%）"); // 同 skyisle-gen 的 IS-area
    check(single_component(f), "只有一个连通块");

    double h_min = 1e9, h_max = -1e9, gap_min = 1e9;
    bool border_clear = true;
    for (int i = 0; i < f.n; i++) {
        for (int j = 0; j < f.n; j++) {
            if (!f.in_mask(i, j)) {
                continue;
            }
            border_clear = border_clear && i > 0 && j > 0 && i < f.n - 1 && j < f.n - 1;
            const double h = f.height[f.index(i, j)];
            h_min = std::min(h_min, h);
            h_max = std::max(h_max, h);
            gap_min = std::min(gap_min, h - f.bottom[f.index(i, j)]);
        }
    }
    check(border_clear, "岛不碰栅格边");
    check_near(h_max, p.peak_m, 0.01 * p.peak_m, "最高节点 = 峰高（< 1%）"); // IS-summit
    check(h_min >= p.rim_m - 1e-4, "地面不低于岸缘");
    check(gap_min >= 0.5 * p.cliff_m - 1e-4, "岛体处处至少半个崖高那么厚");

    // 码头
    const IslandDock &d = f.dock;
    check_near(std::hypot(d.out_x, d.out_z), 1.0, 1e-9, "码头朝向是单位向量");
    double y;
    check(f.sample_height(d.x - 3.0 * d.out_x, d.z - 3.0 * d.out_z, y), "岸缘点背后是地面");
    check(d.pier_m >= p.pier_min_m, "栈桥不短于下限");
    bool berth_clear = true;
    for (double s = -15.0; s <= 15.0; s += 1.0) {
        for (double t = d.pier_m; t <= d.pier_m + 8.0; t += 1.0) {
            berth_clear = berth_clear && !f.sample_height(d.x + t * d.out_x - s * d.out_z, d.z + t * d.out_z + s * d.out_x, y);
        }
    }
    check(berth_clear, "泊位（栈桥头外 8 m × 两侧 15 m）里没有陆地");
    double flat_err = 0.0;
    int flat_n = 0;
    for (int i = 0; i < f.n; i++) {
        for (int j = 0; j < f.n; j++) {
            if (f.in_mask(i, j) && std::hypot(f.coord(j) - d.x, f.coord(i) - d.z) <= 0.5 * p.dock_apron_m) {
                flat_err = std::max(flat_err, std::abs(f.height[f.index(i, j)] - d.y));
                flat_n++;
            }
        }
    }
    check(flat_n > 20, "停机坪上有地");
    check(flat_err < 1e-3, "停机坪内半径是平的");

    // 确定性
    check(field_hash(generate_island_field(p)) == field_hash(f), "同输入重跑，哈希一致"); // IS-det
    IslandParams q = p;
    q.seed = 1166;
    check(field_hash(generate_island_field(q)) != field_hash(f), "换种子，岛不同");
}

// 主场景里 IslandA 的参数，外加一条河
IslandParams river_island_params(double p_age) {
    IslandParams p;
    p.seed = 1165;
    p.age = p_age;
    p.peak_m = p_age < 0.65 ? 60.0 : 24.0;
    p.dock_bearing = -18.6 * 3.14159265358979323846 / 180.0;
    p.river = true;
    return p;
}

void test_river(const char *p_name, double p_age) {
    std::printf("island_river（%s）\n", p_name);
    const IslandParams p = river_island_params(p_age);
    const IslandField f = generate_island_field(p);
    IslandParams dry = p;
    dry.river = false;
    const IslandField f_dry = generate_island_field(dry);
    const double R = std::sqrt(p.area_m2 / 3.14159265358979323846);

    check(f_dry.river.empty() && f_dry.river_dist.empty(), "不要河的岛没有河");
    check(f.has_river(), "要河的岛有河");
    if (!f.has_river()) {
        return;
    }
    check(f.mask == f_dry.mask, "河不改岛的轮廓");

    // 河道：够长、水往低处流、绕开码头、在岸缘出岛
    const std::vector<RiverPoint> &river = f.river;
    double length = 0.0, dock_min = 1e18;
    bool downhill = true;
    for (size_t k = 0; k < river.size(); k++) {
        if (k > 0) {
            length += std::hypot(river[k].x - river[k - 1].x, river[k].z - river[k - 1].z);
            downhill = downhill && river[k].water <= river[k - 1].water;
        }
        dock_min = std::min(dock_min, std::hypot(river[k].x - f.dock.x, river[k].z - f.dock.z));
    }
    check(length >= 0.6 * R, "河长不短于 0.6 个等效半径（" + std::to_string(length) + " m）");
    check(downhill, "水面沿程只降不升");
    check(river.front().water - river.back().water > 0.5, "源头到河口有落差");
    check(dock_min >= p.dock_apron_m + p.river_flat_m + p.river_half_width_m, "河滩碰不到停机坪");
    const RiverPoint &last = river.back(), &before = river[river.size() - 2];
    const double ux = last.x - before.x, uz = last.z - before.z, ul = std::hypot(ux, uz);
    double y;
    check(f.sample_height(river[river.size() - 3].x, river[river.size() - 3].z, y), "河口以内是地面");
    check(!f.sample_height(last.x + 2.0 * p.cell_m * ux / ul, last.z + 2.0 * p.cell_m * uz / ul, y), "河口以外是云海");

    // 断面：河心在水下，两岸的滩在水上
    bool bed_wet = true, banks_dry = true;
    int banks_checked = 0;
    for (size_t k = 1; k + 2 < river.size(); k++) {
        const RiverPoint &a = river[k], &b = river[k + 1];
        if (!f.sample_height(a.x, a.z, y)) {
            continue;
        }
        bed_wet = bed_wet && y < a.water - 0.5 * p.river_depth_m;
        const double len = std::hypot(b.x - a.x, b.z - a.z);
        if ((a.water - b.water) / len > 0.05) {
            continue; // 跌水段：岸跟着水一起往下掉，一格之内就差出几十厘米，不拿它量滩高
        }
        const double off = a.half_width + 3.0;
        for (int side = -1; side <= 1; side += 2) {
            const double bx = a.x - side * off * (b.z - a.z) / len, bz = a.z + side * off * (b.x - a.x) / len;
            // 拐弯处这一点可能离另一段河更近，只看确实在「半宽 + 3 m」上的点
            const int i = int(std::lround((bz + f.half_extent()) / f.cell_m)), j = int(std::lround((bx + f.half_extent()) / f.cell_m));
            if (!f.in_mask(i, j) || std::abs(f.river_dist[f.index(i, j)] - off) > 1.5 || !f.sample_height(bx, bz, y)) {
                continue;
            }
            banks_checked++;
            banks_dry = banks_dry && y > a.water + 0.1;
        }
    }
    check(bed_wet, "河心在水下");
    check(banks_checked > 20 && banks_dry, "两岸的滩高出水面");

    // 刻河之后：停机坪仍平、峰高不变、岛体够厚、河边走得了人
    double flat_err = 0.0, h_max = -1e18, steepest = 0.0, gap_min = 1e18;
    for (int i = 0; i < f.n; i++) {
        for (int j = 0; j < f.n; j++) {
            if (!f.in_mask(i, j)) {
                continue;
            }
            const double h = f.height[f.index(i, j)];
            h_max = std::max(h_max, h);
            gap_min = std::min(gap_min, h - f.bottom[f.index(i, j)]);
            if (std::hypot(f.coord(j) - f.dock.x, f.coord(i) - f.dock.z) <= 0.5 * p.dock_apron_m) {
                flat_err = std::max(flat_err, std::abs(h - f.dock.y));
            }
            if (f.river_dist[f.index(i, j)] < 30.0) {
                if (f.in_mask(i, j + 1)) {
                    steepest = std::max(steepest, std::abs(f.height[f.index(i, j + 1)] - h) / f.cell_m);
                }
                if (f.in_mask(i + 1, j)) {
                    steepest = std::max(steepest, std::abs(f.height[f.index(i + 1, j)] - h) / f.cell_m);
                }
            }
        }
    }
    check(flat_err < 1e-3, "停机坪仍是平的");
    check_near(h_max, p.peak_m, 0.01 * p.peak_m, "峰高不变");
    check(gap_min >= 0.5 * p.cliff_m - 2.0, "河口切下去之后岛体仍够厚");
    check(steepest <= 1.0, "河边 30 m 内没有陡过 45° 的坡（最陡 " + std::to_string(steepest) + "）");

    check(field_hash(generate_island_field(p)) == field_hash(f), "同输入重跑，哈希一致");
    check(field_hash(f) != field_hash(f_dry), "有河与无河的岛不同");
}

void test_trail(const char *p_name, double p_age) {
    std::printf("island_river · 路（%s）\n", p_name);
    IslandParams p = river_island_params(p_age);
    const IslandField f_river = generate_island_field(p);
    p.trail = true;
    const IslandField f = generate_island_field(p);

    check(f_river.trail.empty() && f_river.trail_dist.empty(), "不要路的岛没有路");
    check(f.has_trail(), "要路的岛有路");
    if (!f.has_trail()) {
        return;
    }
    check(f.river.size() == f_river.river.size() && f.mask == f_river.mask, "修路不动河道与轮廓");

    const std::vector<TrailPoint> &trail = f.trail;
    check(std::hypot(trail.front().x - f.dock.x, trail.front().z - f.dock.z) <= 0.5 * p.dock_apron_m, "路从停机坪压平的那一圈里接出来");
    check_near(trail.front().y, f.dock.y, 0.05, "路头与停机坪齐平");
    {
        const TrailPoint &end = trail.back();
        const int i = int(std::lround((end.z + f.half_extent()) / f.cell_m)), j = int(std::lround((end.x + f.half_extent()) / f.cell_m));
        const double beyond_water = f.river_dist[f.index(i, j)] - p.river_half_width_m;
        check(f.in_mask(i, j) && f.river_depth[f.index(i, j)] == 0.0f && beyond_water > 0.0 && beyond_water < p.river_flat_m, "路尾落在河滩上");
    }

    // 走得上去：纵坡有上限；路面就是地面；路中线两侧 1.5 m 也大致是平的
    double max_grade = 0.0, surface_err = 0.0, cross_err = 0.0;
    bool dry = true;
    for (size_t k = 0; k + 1 < trail.size(); k++) {
        const TrailPoint &a = trail[k], &b = trail[k + 1];
        const double len = std::hypot(b.x - a.x, b.z - a.z);
        max_grade = std::max(max_grade, std::abs(b.y - a.y) / len);
        if (std::hypot(a.x - f.dock.x, a.z - f.dock.z) < p.dock_apron_m) {
            continue; // 停机坪里路不动土，那里本来就是平的
        }
        double y;
        if (f.sample_height(a.x, a.z, y)) {
            surface_err = std::max(surface_err, std::abs(y - a.y));
        }
        for (int side = -1; side <= 1; side += 2) {
            if (f.sample_height(a.x - side * 1.5 * (b.z - a.z) / len, a.z + side * 1.5 * (b.x - a.x) / len, y)) {
                cross_err = std::max(cross_err, std::abs(y - a.y));
            }
        }
        const int i = int(std::lround((a.z + f.half_extent()) / f.cell_m)), j = int(std::lround((a.x + f.half_extent()) / f.cell_m));
        dry = dry && f.in_mask(i, j) && f.river_depth[f.index(i, j)] == 0.0f;
    }
    check(max_grade <= 0.36, "路面纵坡不超过 0.36（最陡 " + std::to_string(max_grade) + "）");
    check(surface_err <= 0.35, "路面贴着地面（最大偏差 " + std::to_string(surface_err) + " m）");
    check(cross_err <= 0.8, "路中线两侧 1.5 m 内大致平（最大偏差 " + std::to_string(cross_err) + " m）");
    check(dry, "路不下水");

    double flat_err = 0.0;
    for (int i = 0; i < f.n; i++) {
        for (int j = 0; j < f.n; j++) {
            if (f.in_mask(i, j) && std::hypot(f.coord(j) - f.dock.x, f.coord(i) - f.dock.z) <= 0.5 * p.dock_apron_m) {
                flat_err = std::max(flat_err, std::abs(f.height[f.index(i, j)] - f.dock.y));
            }
        }
    }
    check(flat_err < 1e-3, "停机坪仍是平的");
    check(field_hash(generate_island_field(p)) == field_hash(f), "同输入重跑，哈希一致");

    const IslandFarm survey(f, p.dock_apron_m, FarmRules());
    int on_trail = 0;
    for (int i = 0; i < f.n - 1; i++) {
        for (int j = 0; j < f.n - 1; j++) {
            on_trail += survey.model.land_at(i, j) == Arability::TRAIL ? 1 : 0;
        }
    }
    check(on_trail > 0 && survey.arable_count() >= 60, "路上不能垦，路外的河滩照样能垦");
}

void test_farm() {
    std::printf("farm_model\n");
    const IslandParams p = river_island_params(0.8);
    const IslandField f = generate_island_field(p);
    FarmRules rules;
    const IslandFarm survey(f, p.dock_apron_m, rules);

    // 勘察
    int water = 0, dry = 0;
    bool arable_ok = true;
    int near_i = -1, near_j = -1, far_i = -1, far_j = -1, dry_i = -1, dry_j = -1;
    for (int i = 0; i < f.n - 1; i++) {
        for (int j = 0; j < f.n - 1; j++) {
            const Arability a = survey.model.land_at(i, j);
            water += a == Arability::WATER ? 1 : 0;
            dry += a == Arability::TOO_DRY ? 1 : 0;
            if (a == Arability::TOO_DRY) {
                dry_i = i;
                dry_j = j;
            }
            if (a != Arability::ARABLE) {
                continue;
            }
            const float m = survey.model.survey(i, j).moisture;
            arable_ok = arable_ok && f.quad_solid(i, j) && f.river_depth[f.index(i, j)] == 0.0f && m >= 0.0f && m <= 1.0f;
            if (near_i < 0 || m > survey.model.survey(near_i, near_j).moisture) {
                near_i = i;
                near_j = j;
            }
            if (far_i < 0 || m < survey.model.survey(far_i, far_j).moisture) {
                far_i = i;
                far_j = j;
            }
        }
    }
    check(survey.arable_count() >= 60, "河边有成片可垦的地（" + std::to_string(survey.arable_count()) + " 格）");
    check(water > 0 && dry > survey.arable_count(), "河里不能垦，离河远的地占多数");
    check(arable_ok, "可垦的格子都有地面、不在水里");
    check(survey.model.land_at(-1, 0) == Arability::NO_GROUND && survey.model.land_at(0, f.n - 1) == Arability::NO_GROUND, "界外算没有地面");
    IslandParams no_river = p;
    no_river.river = false;
    const IslandField dry_field = generate_island_field(no_river);
    check(IslandFarm(dry_field, p.dock_apron_m, rules).arable_count() == 0, "没有河的岛无地可垦");
    if (near_i < 0 || dry_i < 0) {
        return;
    }

    // 地块状态机
    const CropDef *millet = catalog::crop("millet");
    const CropDef *bean = catalog::crop("bean");
    FarmModel farm(&survey.ground, survey.ground.plot_lattice(), rules);
    check(farm.available_action(dry_i, dry_j, true) == FarmAction::NONE, "离河太远：不能开垦");
    check(!farm.till(dry_i, dry_j) && farm.plot_at(dry_i, dry_j).state == PlotState::WILD, "硬垦也垦不动");
    check(farm.available_action(near_i, near_j, false) == FarmAction::TILL, "荒地：开垦（不要种子）");
    check(!farm.sow(near_i, near_j, millet), "没垦的地不能直接播种");
    check(farm.till(near_i, near_j) && farm.plot_at(near_i, near_j).state == PlotState::TILLED, "开垦 → 已垦");
    check(farm.available_action(near_i, near_j, false) == FarmAction::NONE, "手上没种子：不能播种");
    check(farm.available_action(near_i, near_j, true) == FarmAction::SOW, "手上有种子：播种");
    check(!farm.sow(near_i, near_j, nullptr), "没说种什么：播不了");
    check(farm.harvest(near_i, near_j).count == 0, "空地没得收");
    check(farm.sow(near_i, near_j, millet), "播下粟");
    check(farm.plot_at(near_i, near_j).state == PlotState::GROWING && farm.plot_at(near_i, near_j).crop == millet, "生长中，地块记着种的是粟");
    check(farm.available_action(near_i, near_j, true) == FarmAction::NONE, "生长中：无事可做");

    farm.till(far_i, far_j);
    farm.sow(far_i, far_j, millet);
    int stage_changes = 0;
    double near_ripe_at = -1.0, far_ripe_at = -1.0;
    for (int step = 1; step <= 60 * 200 && far_ripe_at < 0.0; step++) {
        stage_changes += int(farm.advance(1.0 / 60.0).size()) / 2;
        if (near_ripe_at < 0.0 && farm.plot_at(near_i, near_j).state == PlotState::RIPE) {
            near_ripe_at = step / 60.0;
        }
        if (farm.plot_at(far_i, far_j).state == PlotState::RIPE) {
            far_ripe_at = step / 60.0;
        }
    }
    const double near_m = survey.model.survey(near_i, near_j).moisture, far_m = survey.model.survey(far_i, far_j).moisture;
    check_near(near_ripe_at, rules.grow_hours / (rules.dry_growth + (1 - rules.dry_growth) * near_m), 0.1, "河边的地按墒情算的时间成熟");
    check_near(far_ripe_at, rules.grow_hours / (rules.dry_growth + (1 - rules.dry_growth) * far_m), 0.1, "远处的地也是");
    check(near_ripe_at < far_ripe_at, "离河近的先熟");
    check(stage_changes == 2 * (FarmModel::STAGES - 1), "每格的外观阶段各变 STAGES − 1 次");
    check(farm.count(PlotState::RIPE) == 2 && farm.count(PlotState::GROWING) == 0, "两格都熟了");
    check(farm.available_action(near_i, near_j, false) == FarmAction::HARVEST, "成熟：收获");
    const Harvest got = farm.harvest(near_i, near_j);
    check(got.item && std::string(got.item) == "millet" && got.count == millet->yield, "收获得粟，数量按作物表");
    check(farm.plot_at(near_i, near_j).state == PlotState::TILLED && farm.plot_at(near_i, near_j).crop == nullptr, "收完还是垦好的地，能接着种");
    check(farm.harvest(near_i, near_j).count == 0, "收过的地再收没有");

    // 换种菽：同一块地，生长期按作物倍率拉长，收的是菽
    check(farm.sow(near_i, near_j, bean), "接着种菽");
    double bean_ripe_at = -1.0;
    for (int step = 1; step <= 60 * 400 && bean_ripe_at < 0.0; step++) {
        farm.advance(1.0 / 60.0);
        if (farm.plot_at(near_i, near_j).state == PlotState::RIPE) {
            bean_ripe_at = step / 60.0;
        }
    }
    check_near(bean_ripe_at, near_ripe_at * bean->grow_factor, 0.1, "菽的生长期是粟的 grow_factor 倍");
    const Harvest beans = farm.harvest(near_i, near_j);
    check(beans.item && std::string(beans.item) == "bean" && beans.count == bean->yield, "收下来是菽");
}

// 主场景里 IslandA 的参数 + 河 + 路 + 船坞
void test_yard(const char *p_name, double p_age) {
    std::printf("island_yard（%s）\n", p_name);
    IslandParams p = river_island_params(p_age);
    p.trail = true;
    p.yard = true;
    const IslandField f = generate_island_field(p);
    const YardPlan &y = f.yard;
    check(y.present, "有船坞坪");
    if (!y.present) {
        return;
    }
    check_near(std::hypot(y.ux, y.uz), 1.0, 1e-9, "长轴是单位向量");
    check_near(y.ux * y.vx + y.uz * y.vz, 0.0, 1e-9, "长短轴垂直");
    check_near(y.y, f.dock.y, 1e-9, "坪与停机坪同高");
    check(std::abs(y.vx + f.dock.out_x) < 1e-9 && std::abs(y.vz + f.dock.out_z) < 1e-9, "短轴指向岛内");

    // 坪内每个节点都是陆地、都平；坪外一圈也是陆地（坞架、跳板有地放）
    bool solid = true, flat = true, rim_solid = true;
    int inside = 0;
    for (int i = 0; i < f.n; i++) {
        for (int j = 0; j < f.n; j++) {
            const double x = f.coord(j), z = f.coord(i);
            const double beyond = y.outside_distance(x, z);
            if (beyond <= 0.0) {
                inside++;
                solid = solid && f.in_mask(i, j);
                flat = flat && f.in_mask(i, j) && std::abs(f.height[f.index(i, j)] - y.y) < 1e-6;
            } else if (beyond <= 1.5) {
                rim_solid = rim_solid && f.in_mask(i, j);
            }
        }
    }
    check(inside >= int(0.9 * y.length * y.width / (f.cell_m * f.cell_m)), "坪上的节点数与面积相称");
    check(solid, "坪内全是陆地");
    check(flat, "坪内是平的");
    check(rim_solid, "坪外 1.5 m 内也是陆地");

    // 坪挨着停机坪、不压到码头中线；出生点不在坪上
    double u = 0.0, v = 0.0;
    y.to_local(f.dock.x, f.dock.z, u, v);
    check(std::abs(u) > 0.5 * y.length + 5.0 && std::abs(u) < 0.5 * y.length + 7.0, "坪的近端离码头中线 5–7 m（路从这儿上山）");
    check(v < -0.5 * y.width, "坪在岸缘点之内（岛这一侧）");
    y.to_local(f.dock.x - 12.0 * f.dock.out_x, f.dock.z - 12.0 * f.dock.out_z, u, v);
    check(y.outside_distance(f.dock.x - 12.0 * f.dock.out_x, f.dock.z - 12.0 * f.dock.out_z) > 1.0, "出生点不在坪上");

    // 河与路都绕开
    double river_min = 1e18, trail_min = 1e18;
    for (const RiverPoint &pt : f.river) {
        river_min = std::min(river_min, y.outside_distance(pt.x, pt.z));
    }
    for (const TrailPoint &pt : f.trail) {
        trail_min = std::min(trail_min, y.outside_distance(pt.x, pt.z));
    }
    check(f.has_river() && river_min >= 15.0, "河中线离坪至少 15 m（得到 " + std::to_string(river_min) + "）");
    check(f.has_trail() && trail_min >= 4.0, "路中线离坪至少 4 m（得到 " + std::to_string(trail_min) + "）");

    // 坪外地形接回：坪缘外 2 m 内（河与路的土方都不碰的那一圈）的高差不超过挖方 / 填方坡度
    bool graded = true;
    for (int i = 0; i < f.n; i++) {
        for (int j = 0; j < f.n; j++) {
            const double beyond = y.outside_distance(f.coord(j), f.coord(i));
            if (f.in_mask(i, j) && beyond > 0.0 && beyond <= 2.0) {
                graded = graded && f.height[f.index(i, j)] <= y.y + 1.0 * beyond + 1e-6 && f.height[f.index(i, j)] >= y.y - 0.8 * beyond - 1e-6;
            }
        }
    }
    check(graded, "坪外按坡度接回原地形");

    // 没要船坞的岛没有；确定性
    IslandParams q = p;
    q.yard = false;
    check(!generate_island_field(q).yard.present, "不要船坞就没有");
    check(field_hash(generate_island_field(p)) == field_hash(f), "同输入重跑，哈希一致");
}

void test_ship_layout() {
    std::printf("ship_layout\n");
    const Cell GATE{ 0, 0, 0 };
    const ShipLayout small = default_layout();
    check(small.size() == 2 && small.at({ 0, 0, -1 }) && small.at({ 0, 0, -1 })->kind == ModuleKind::POWER && small.at(GATE)->kind == ModuleKind::BRIDGE, "默认小艇：动力区在舷门格前面，驾驶区在舷门格");
    check(layout_problem(small).empty(), "默认小艇合法");
    check(modules::by_id("bridge") && modules::by_id("storage") && modules::by_id("power") && !modules::by_id("cabin"), "按 id 查模块");
    check(std::string(modules::def(ModuleKind::STORAGE).name) == "储存区", "模块显示名");

    // 船况
    const ShipStats s0 = layout_stats(small);
    check(s0.sections == 2 && s0.can_fly() && s0.hold == 0, "小艇能飞、没货舱");
    check_near(s0.max_speed, 32.0, 0.05, "小艇航速 = 切片一的 32 m/s");
    check_near(s0.max_climb, 10.0, 0.05, "小艇升速 = 切片一的 10 m/s");
    check_near(s0.max_yaw_rate, 0.75, 1e-9, "小艇转向率 = 切片一的 0.75");
    ShipLayout cargo = small;
    check(layout_place(cargo, { 0, 0, 1 }, ModuleKind::STORAGE).empty() && cargo.size() == 3, "舷门格后面加一节储存区");
    const ShipStats s1 = layout_stats(cargo);
    check(s1.hold == 4 && s1.max_speed < s0.max_speed && s1.max_climb < s0.max_climb && s1.max_yaw_rate < s0.max_yaw_rate, "多一节货：有货舱，但更慢、更钝");
    ShipLayout strong = cargo;
    check(layout_place(strong, { 0, 0, -2 }, ModuleKind::POWER).empty() && layout_stats(strong).max_speed > s1.max_speed, "再加一节动力区：更快");
    ShipLayout dead({ { { 0, 0, 0 }, ModuleKind::BRIDGE }, { { 0, 0, 1 }, ModuleKind::STORAGE } });
    check(layout_problem(dead).empty() && !layout_stats(dead).can_fly() && layout_stats(dead).max_speed == 0.0, "没有动力区：合法，但飞不起来");

    // 几何
    check_near(cell_center_z(-1), -7.0, 1e-9, "格中心 z");
    check_near(cell_deck_y(2), 7.6, 1e-9, "第 2 层甲板高 7.6 m");
    check_near(bow_z(cargo), -10.5 - 3.4, 1e-9, "船头 = 最前一排前缘再伸出船头楔");
    check_near(stern_z(cargo), 10.5, 1e-9, "船尾 = 最后一排后缘");
    check_near(port_edge_x(cargo), -3.5, 1e-9, "一格宽的船：左舷边 −3.5");
    check_near(starboard_edge_x(cargo), 3.5, 1e-9, "一格宽的船：右舷边 +3.5");
    check(cargo.column_height(0, 1) == 1 && cargo.column_height(1, 0) == 0 && cargo.levels() == 1, "列高");

    // 往上搭、往两边搭
    ShipLayout tall = cargo;
    check(layout_place(tall, { 0, 1, 1 }, ModuleKind::STORAGE).empty() && tall.levels() == 2 && tall.column_height(0, 1) == 2, "储存区上面再搭一节");
    const std::vector<Ladder> ladders = layout_ladders(tall);
    check(ladders.size() == 1 && ladders[0].low_x == 0 && ladders[0].low_z == 0 && ladders[0].high_z == 1 && ladders[0].from_level == 1 && ladders[0].to_level == 2, "舷门格与高一层的邻列之间有一架梯子");
    check(layout_place(tall, { 0, 1, 0 }, ModuleKind::STORAGE) == "驾驶区顶上不能再搭", "驾驶区顶上不能搭");
    check(layout_place(tall, { -1, 1, 1 }, ModuleKind::STORAGE) == "悬空了：下面没有节撑着", "悬空被拒");
    check(layout_place(tall, { -1, 0, 1 }, ModuleKind::STORAGE) == "舷门那一排得是最宽的一排，不然靠不上码头", "先加宽舷门排");
    check(layout_place(tall, { -1, 0, 0 }, ModuleKind::STORAGE).empty(), "舷门排左舷加一节");
    check_near(port_edge_x(tall), -10.5, 1e-9, "左舷边到了 −10.5");
    check_near(starboard_edge_x(tall), 3.5, 1e-9, "右舷边还是 3.5");
    check(layout_place(tall, { -1, 0, 1 }, ModuleKind::POWER).empty(), "现在左舷后面也能加");
    check(layout_place(tall, { -1, 1, 0 }, ModuleKind::STORAGE) == "舷门两头的格子上面不能搭：跳板要接甲板", "舷门排两头不能往上搭");
    check(layout_remove(tall, { 0, 0, 1 }) == "上面还搭着节，先拆上面的", "拆支撑被拒");
    check(layout_remove(tall, { 0, 1, 1 }).empty() && tall.levels() == 1, "先拆上面的");
    check(layout_remove(tall, GATE) == "驾驶区拆不得", "驾驶区拆不得");
    check(layout_place(tall, { 1, 0, 1 }, ModuleKind::STORAGE) == "舷门那一排得是最宽的一排，不然靠不上码头", "右舷伸出去也不行");
    check(layout_place(tall, { 0, 0, 3 }, ModuleKind::STORAGE) == "有的节没和船身连上", "隔着空格不相连");
    check(layout_place(tall, { 0, 0, 4 }, ModuleKind::STORAGE) == "超出了船坞能造的范围", "越界");
    check(layout_place(tall, GATE, ModuleKind::STORAGE) == "这一格已经有节了", "占着的格放不了");

    // 换类型
    ShipLayout edit = cargo;
    check(!layout_set_kind(edit, GATE, ModuleKind::STORAGE).empty() && edit.at(GATE)->kind == ModuleKind::BRIDGE, "驾驶区换不得");
    check(!layout_set_kind(edit, { 0, 0, 1 }, ModuleKind::BRIDGE).empty(), "已有驾驶区，别的节换不成驾驶区");
    check(layout_set_kind(edit, { 0, 0, 1 }, ModuleKind::POWER).empty() && edit.at({ 0, 0, 1 })->kind == ModuleKind::POWER, "储存区换成动力区");
    check(layout_set_kind(edit, { 1, 1, 1 }, ModuleKind::POWER) == "这一格没有节", "空格换不了");

    // 上限与候选格
    ShipLayout full = small;
    for (int z = 1; z <= 3; z++) {
        for (int x = -1; x <= 1; x++) {
            if (full.size() < MAX_MODULES) {
                if (x != 0 && !full.has({ x, 0, 0 })) {
                    layout_place(full, { x, 0, 0 }, ModuleKind::STORAGE);
                }
                layout_place(full, { x, 0, z }, ModuleKind::STORAGE);
            }
        }
    }
    check(full.size() == MAX_MODULES && layout_problem(full).empty(), "堆到上限 " + std::to_string(MAX_MODULES) + " 节");
    check(layout_place(full, { 0, 1, 1 }, ModuleKind::STORAGE) == "最多 12 节" && full.size() == MAX_MODULES, "第 13 节加不上");
    const std::vector<Cell> cands = layout_candidates(small);
    bool cands_ok = !cands.empty();
    for (const Cell &c : cands) {
        ShipLayout trial = small;
        cands_ok = cands_ok && !small.has(c) && layout_place(trial, c, ModuleKind::STORAGE).empty();
    }
    check(cands_ok, "候选格都是能放的空格（" + std::to_string(cands.size()) + " 个）");
    bool has_aft = false, has_top = false, has_side = false, has_far = false;
    for (const Cell &c : cands) {
        has_aft = has_aft || c == Cell{ 0, 0, 1 };
        has_top = has_top || c == Cell{ 0, 1, -1 };
        has_side = has_side || c == Cell{ 1, 0, 0 };
        has_far = has_far || c == Cell{ 1, 0, -1 } || c == Cell{ 0, 0, 2 } || c == Cell{ 0, 1, 0 };
    }
    check(has_aft && has_top && has_side && !has_far, "小艇的候选格：后方、动力区顶上、舷门排两侧；不含隔空的与驾驶区顶上");

    // id 往返
    const std::vector<std::string> ids = layout_ids(cargo);
    check(ids.size() == 3 && ids[0] == "0,0,-1:power" && ids[1] == "0,0,0:bridge" && ids[2] == "0,0,1:storage", "布局 → id（按 y, z, x 排）");
    bool ok = false;
    check(layout_from_ids(ids, &ok) == cargo && ok, "id → 布局");
    check(layout_from_ids({ "0,0,-1:power", "cabin", "0,0,0:bridge" }, &ok).size() == 2 && !ok, "认不出的条目跳过并报告");
    Module m;
    check(!parse_module_id("1,2:power", m) && !parse_module_id("1,2,3:cabin", m) && parse_module_id("-1,2,3:power", m) && m.at.x == -1 && m.at.z == 3, "解析条目");
}

void test_inventory() {
    std::printf("inventory\n");
    Inventory pack;
    check(pack.empty() && pack.count("millet") == 0 && !pack.has("millet"), "空背包");
    pack.add("millet", 5);
    pack.add("bean", 3);
    pack.add("millet", 2);
    check(pack.size() == 2 && pack.count("millet") == 7 && pack.count("bean") == 3, "同种物品叠成一堆");
    check(pack.get_stacks()[0].item == "millet" && pack.get_stacks()[1].item == "bean", "按第一次放入的顺序排");
    check(!pack.remove("bean", 4) && pack.count("bean") == 3, "不够拿：不动");
    check(pack.remove("bean", 3) && pack.size() == 1 && pack.count("bean") == 0, "拿光了这一堆就没了");
    check(!pack.remove("bean", 1), "没有的东西拿不了");
    pack.add("bean", 1);
    check(pack.get_stacks()[1].item == "bean", "再放回来排在最后");
    pack.add("millet", 0);
    pack.add("", 3);
    check(pack.size() == 2 && pack.count("millet") == 7, "放 0 份、放空名字：不做事");
    check(pack.has("millet", 7) && !pack.has("millet", 8), "has 按份数比");
}

void test_crops() {
    std::printf("crops\n");
    check(catalog::CROP_COUNT >= 2 && catalog::ITEM_COUNT >= 2, "至少两种作物、两样物品");
    for (int k = 0; k < catalog::CROP_COUNT; k++) {
        const CropDef &c = catalog::CROPS[k];
        check(catalog::item(c.seed) && catalog::item(c.product), std::string("作物 ") + c.id + " 的种子与产物都在物品表里");
        check(c.yield > 1 && c.grow_factor > 0.0, std::string("作物 ") + c.id + " 种一收多、生长期为正");
        check(catalog::crop_of_seed(c.seed) == &c, std::string("由种子能查回作物 ") + c.id);
    }
    check(catalog::crop("millet") && catalog::crop("bean") && !catalog::crop("rice"), "按 id 查作物");
    check(catalog::is_seed("millet") && !catalog::is_seed("pebble"), "是不是种子");
    check(catalog::item_name("millet") == "粟" && catalog::item_name("pebble") == "pebble", "显示名；没有的原样返回");
}

void test_flight() {
    std::printf("flight_model\n");
    const FlightParams params;
    const double dt = 1.0 / 60.0;

    // 档位：头一档就是原样，档越高越快；越界夹到两头
    check(flight_gears::count() >= 3, "至少三档");
    check_near(apply_gear(params, 0).max_speed, params.max_speed, 1e-12, "头一档 = 船本身的极速");
    check(apply_gear(params, 1).max_speed > params.max_speed && apply_gear(params, 2).max_speed > apply_gear(params, 1).max_speed, "档越高极速越大");
    check(apply_gear(params, 2).max_climb > params.max_climb, "高档升降也快");
    check_near(apply_gear(params, 99).max_speed, apply_gear(params, flight_gears::count() - 1).max_speed, 1e-12, "越界夹到最高档");
    {
        FlightState geared;
        const FlightInput full{ 1.0, 0.0, 0.0 };
        for (int k = 0; k < 60 * 20; k++) {
            geared = flight_step(geared, full, apply_gear(params, flight_gears::count() - 1), dt);
        }
        const double v = std::hypot(geared.vx, geared.vz);
        check(v > 100.0, "最高档 20 秒内过 100 m/s（" + std::to_string(v) + "）：30 km 五分钟内飞到");
    }

    FlightState s;
    FlightInput full;
    full.throttle = 1.0;
    for (int k = 0; k < 60 * 30; k++) {
        s = flight_step(s, full, params, dt);
    }
    check_near(s.vz, -params.max_speed, 0.05, "全油门：沿船头（−z）到极速");
    check_near(s.vx, 0.0, 1e-6, "全油门：不侧移");

    for (int k = 0; k < 60 * 30; k++) {
        s = flight_step(s, FlightInput(), params, dt);
    }
    check(std::hypot(s.vx, s.vz) < 0.01 && std::abs(s.vy) < 0.01, "松手：停下来");

    FlightInput right;
    right.rudder = 1.0;
    FlightState t;
    for (int k = 0; k < 60 * 5; k++) {
        t = flight_step(t, right, params, dt);
    }
    check(t.yaw < -1.0, "右舵：yaw 减小");
    check_near(t.yaw_rate, -params.max_yaw_rate, 0.01, "右舵：角速度到极值");
    double fx, fz;
    flight_forward(-1.5707963267948966, fx, fz);
    check_near(fx, 1.0, 1e-9, "右转 90°：船头指向 +x");

    FlightInput up;
    up.lift = 1.0;
    FlightState u;
    for (int k = 0; k < 60 * 10; k++) {
        u = flight_step(u, up, params, dt);
    }
    check_near(u.vy, params.max_climb, 0.01, "升：到爬升极速");

    FlightState side;
    side.vx = 10.0; // 船头朝 −z，纯侧滑
    for (int k = 0; k < 60 * 5; k++) {
        side = flight_step(side, FlightInput(), params, dt);
    }
    check(std::abs(side.vx) < 0.01, "侧滑很快衰减");

    // 与帧率无关（直线段）
    FlightState a, b;
    for (int k = 0; k < 10; k++) {
        a = flight_step(a, full, params, 0.1);
    }
    for (int k = 0; k < 100; k++) {
        b = flight_step(b, full, params, 0.01);
    }
    check_near(a.vz, b.vz, 1e-9, "10 × 0.1 s 与 100 × 0.01 s 速度相同");

    FlightInput wild;
    wild.throttle = 50.0;
    FlightState w;
    for (int k = 0; k < 60 * 30; k++) {
        w = flight_step(w, wild, params, dt);
    }
    check(std::hypot(w.vx, w.vz) <= params.max_speed + 1e-6, "输入越界被夹住");
}

void test_settlement() {
    std::printf("settlement_model\n");
    constexpr double PI = 3.14159265358979323846;
    const BuildingDef &house = *buildings::by_id("house");

    // 建筑表
    check(buildings::by_id("house") && buildings::by_id("hall") && buildings::by_id("granary") && buildings::by_id("tower") && !buildings::by_id("palace"), "按 id 查建筑");
    check(std::string(house.name) == "民居" && buildings::index_of("tower") == 3 && buildings::index_of("palace") == -1, "显示名与序号");
    check(buildings::COUNT == 5 && &buildings::at(0) == &house, "表里五种（四种房屋 + 船台）");

    // 文本往返
    const Building b{ "house", 12.5, -30.25, 0.5 * PI };
    const std::string id = building_id(b);
    check(id == "house@12.50,-30.25,90.0", "building_id（" + id + "）");
    Building back;
    check(parse_building_id(id, back) && back.kind == "house" && std::abs(back.x - 12.5) < 1e-9 && std::abs(back.z + 30.25) < 1e-9 && std::abs(back.yaw - 0.5 * PI) < 1e-9, "parse 往返");
    check(parse_building_id("tower@0,0,-90", back) && std::abs(back.yaw - 1.5 * PI) < 1e-9, "负角度归到 [0, 360)");
    check(!parse_building_id("house@1,2", back) && !parse_building_id("palace@1,2,3", back) && !parse_building_id("house@1,2,3,4", back) && !parse_building_id("@1,2,3", back), "残缺 / 不认识的条目");
    bool ok = true;
    const SettlementLayout parsed = settlement_from_ids({ "# 注释", "", "  house@1,2,3  ", "bogus" }, &ok);
    check(parsed.size() == 1 && !ok, "跳过注释与空行，认不出的条目报 ok=false");
    check(settlement_ids(parsed).size() == 1 && settlement_ids(parsed)[0] == "house@1.00,2.00,3.0", "layout_ids");
    check(settlement_from_ids(settlement_ids(parsed)) == parsed, "往返相等");

    // 占地与压着
    const Building a{ "house", 0.0, 0.0, 0.0 };
    check(footprint_contains(house, a, 5.4, 3.2) && !footprint_contains(house, a, 5.6, 0.0) && !footprint_contains(house, a, 0.0, 3.3), "占地：面阔 11 进深 6.5");
    check(footprint_contains(house, a, 5.9, 0.0, 0.5), "占地可以外扩");
    const Building e{ "house", 0.0, 6.0, 0.5 * PI };
    check(footprint_contains(house, e, 3.2, 6.0) && !footprint_contains(house, e, 3.3, 6.0) && footprint_contains(house, e, 0.0, 11.4), "转 90° 后面阔沿 z");
    double cx[4], cz[4];
    footprint_corners(house, 0.0, 0.0, 0.5 * PI, 0.0, cx, cz);
    check_near(cx[0], 3.25, 1e-9, "转 90° 的左前角 x");
    check_near(cz[0], 5.5, 1e-9, "转 90° 的左前角 z");
    check(!footprints_overlap(a, Building{ "house", 12.2, 0.0, 0.0 }, 1.0) && footprints_overlap(a, Building{ "house", 11.8, 0.0, 0.0 }, 1.0), "并排两座：空 1 m 才不算压着");
    check(footprints_overlap(a, e, 1.0) && !footprints_overlap(a, Building{ "house", 0.0, 9.8, 0.5 * PI }, 1.0), "转 90° 的邻居按转后的占地算");
    check(!footprints_overlap(Building{ "house", 0.0, 0.0, 0.25 * PI }, Building{ "house", 13.0, 0.0, 0.25 * PI }, 1.0) && footprints_overlap(Building{ "house", 0.0, 0.0, 0.25 * PI }, Building{ "house", 9.0, 0.0, 0.25 * PI }, 1.0), "斜着的两座用分离轴判（都转 45° 后沿进深方向错开就分得开）");
    SettlementLayout pair;
    pair.add(a);
    pair.add(Building{ "house", 12.2, 0.0, 0.0 });
    check(settlement_find(pair, 12.2, 0.0) == 1 && settlement_find(pair, 0.0, 0.0) == 0 && settlement_find(pair, 6.0, 0.0) == -1, "点落在哪一座里");

    // 勘察
    IslandParams p = river_island_params(0.8);
    p.trail = true;
    p.yard = true;
    const IslandField f = generate_island_field(p);
    const SiteRules rules;
    const IslandGround island_map(f, p.dock_apron_m);
    const MapSiteGround island_site(island_map, rules);
    SettlementLayout layout;
    // 停机坪：岸缘点周围几米是掩膜的锯齿边，往岛内退 0.7 个坪的半径（14 m）
    const double apron_x = f.dock.x - 0.7 * p.dock_apron_m * f.dock.out_x, apron_z = f.dock.z - 0.7 * p.dock_apron_m * f.dock.out_z;
    check(survey_site(island_site, house, apron_x, apron_z, 0.0, layout, rules).reason == "停机坪上不能盖", "停机坪被拒");
    check(survey_site(island_site, house, 2.0 * f.half_extent(), 0.0, 0.0, layout, rules).reason.find("没有地面") != std::string::npos, "岛外被拒");
    const RiverPoint &rp = f.river[f.river.size() / 2];
    check(survey_site(island_site, house, rp.x, rp.z, 0.0, layout, rules).reason == "这是河，盖不了", "河里被拒");
    check(f.yard.present && survey_site(island_site, house, f.yard.x, f.yard.z, 0.0, layout, rules).reason == "这是船坞的坪，盖不得", "船坞坪被拒");
    // 路上：从路的中段（在坡上，两侧都有地）往两头找一处离停机坪、船坞都够远的路点
    int trail_checked = 0;
    const int mid = int(f.trail.size()) / 2;
    for (int step = 0; step < int(f.trail.size()) && trail_checked == 0; step++) {
        for (int k : { mid + step, mid - step }) {
            if (k < 0 || k >= int(f.trail.size()) || trail_checked > 0) {
                continue;
            }
            const TrailPoint &t = f.trail[size_t(k)];
            if (std::hypot(t.x - f.dock.x, t.z - f.dock.z) > p.dock_apron_m + rules.dock_margin_m + 8.0 && !f.yard.contains(t.x, t.z, rules.yard_margin_m + 8.0)) {
                const std::string why = survey_site(island_site, house, t.x, t.z, 0.0, layout, rules).reason;
                check(why == "压着路了", "路上被拒（" + why + "）");
                trail_checked++;
            }
        }
    }
    check(trail_checked == 1, "路上有一处可以测");
    // 扫全岛：得有能盖的地，也得有太陡的地
    int sites = 0, steep = 0;
    double site_x = 0.0, site_z = 0.0;
    for (int i = 0; i < f.n; i += 2) {
        for (int j = 0; j < f.n; j += 2) {
            const SiteReport r = survey_site(island_site, house, f.coord(j), f.coord(i), 0.0, layout, rules);
            if (r.ok()) {
                if (sites == 0) {
                    site_x = f.coord(j);
                    site_z = f.coord(i);
                }
                sites++;
            } else if (r.reason.rfind("坡太陡", 0) == 0) {
                steep++;
            }
        }
    }
    check(sites > 20, "岛上有不少能盖民居的地（" + std::to_string(sites) + "）");
    check(steep > 0, "岛缘有太陡的地（" + std::to_string(steep) + "）");
    const SiteReport site = survey_site(island_site, house, site_x, site_z, 0.0, layout, rules);
    check(site.ok() && site.ground_max - site.ground_min <= rules.max_rise_m, "选中的地高差在限内");
    check_near(site.floor_y, site.ground_max + house.plinth_m, 1e-9, "地坪 = 最高地面 + 台基");
    check_near(site.plinth_bottom, site.ground_min - rules.bury_m, 1e-9, "台基埋到最低地面之下");
    check(settlement_place(layout, island_site, "house", site_x, site_z, 0.0, rules).empty() && layout.size() == 1, "放一座");
    check(settlement_place(layout, island_site, "house", site_x, site_z, 0.0, rules) == "和民居挨得太近" && layout.size() == 1, "同一处再放被拒");
    check(survey_site(island_site, house, site_x, site_z, 0.0, layout, rules, 0).ok(), "忽略自己就能过");
    check(settlement_place(layout, island_site, "palace", site_x, site_z, 0.0, rules) == "没有这种建筑：palace", "不认识的建筑");
    check(settlement_place(layout, island_site, "house", apron_x, apron_z, 0.0, rules) == "停机坪上不能盖" && layout.size() == 1, "被拒时清单不动");
    check(settlement_find(layout, site_x, site_z) == 0, "放下的那座找得到");
    layout.erase(0);
    check(layout.empty(), "拆掉");
}

void test_villagers() {
    std::printf("villager_model\n");
    IslandParams p = river_island_params(0.8);
    p.trail = true;
    const IslandField f = generate_island_field(p);
    SiteRules site_rules;
    const IslandGround island_map(f, p.dock_apron_m);
    const MapSiteGround island_site(island_map, site_rules);

    // 地面高与网格一致：节点上正好是节点高
    double y = 0.0;
    int node_i = -1, node_j = -1;
    for (int i = f.n / 2; i < f.n && node_i < 0; i++) {
        for (int j = f.n / 2; j < f.n; j++) {
            if (f.quad_solid(i, j) && f.quad_solid(i - 1, j - 1)) {
                node_i = i;
                node_j = j;
                break;
            }
        }
    }
    check(node_i >= 0 && island_map.walk_height(f.coord(node_j), f.coord(node_i), y) && std::abs(y - f.height[f.index(node_i, node_j)]) < 1e-4, "节点上的地面高 = 节点高");
    check(!island_map.walk_height(10.0 * f.half_extent(), 0.0, y), "岛外没有地面");

    // 离河 18–30 m 找一块能盖民居的地，门朝河
    SettlementLayout layout;
    double hx = 0.0, hz = 0.0, hyaw = 0.0;
    bool found = false;
    for (int i = 0; i < f.n && !found; i += 2) {
        for (int j = 0; j < f.n && !found; j += 2) {
            if (!f.in_mask(i, j) || f.river_dist[f.index(i, j)] < 18.0 || f.river_dist[f.index(i, j)] > 30.0) {
                continue;
            }
            double best = 1e18, rx = 0.0, rz = 0.0;
            for (const RiverPoint &rp : f.river) {
                const double d = std::hypot(rp.x - f.coord(j), rp.z - f.coord(i));
                if (d < best) {
                    best = d;
                    rx = rp.x;
                    rz = rp.z;
                }
            }
            const double yaw = std::atan2(rx - f.coord(j), rz - f.coord(i));
            if (settlement_place(layout, island_site, "house", f.coord(j), f.coord(i), yaw, site_rules).empty()) {
                hx = f.coord(j);
                hz = f.coord(i);
                hyaw = yaw;
                found = true;
            }
        }
    }
    check(found, "河边找得到一块盖民居的地");
    if (!found) {
        return;
    }
    const BuildingDef &house = *buildings::by_id("house");

    // 导航格
    NavRules nav_rules;
    const NavGrid nav = island_nav(f, layout, nav_rules);
    check(nav.walkable_count() > 1000, "岛上大片地走得了（" + std::to_string(nav.walkable_count()) + " 格）");
    check(!nav.walkable_at(hx, hz), "房子里走不了");
    const NavPoint door = building_door(layout.at(0));
    check(!footprint_contains(house, layout.at(0), door.x, door.z, 0.6), "门前站位在台基外");
    NavPoint snapped;
    check(nav_nearest_walkable(nav, door.x, door.z, 6.0, true, snapped), "门前 6 m 内有干地");
    int wet = 0;
    for (uint8_t w : nav.walk) {
        wet += w == NavGrid::WET ? 1 : 0;
    }
    check(wet > 0, "河里的格子记作水里");
    const double apron_x = f.dock.x - f.dock.out_x * 14.0, apron_z = f.dock.z - f.dock.out_z * 14.0;
    check(nav.walkable_at(apron_x, apron_z), "停机坪走得了");
    // 崖壁：岛上总有一处相邻两格陡得走不过去
    bool cliff = false;
    for (int r = 0; r + 1 < nav.rows && !cliff; r++) {
        for (int c = 0; c < nav.cols && !cliff; c++) {
            if (nav.walkable(r, c) && nav.walkable(r + 1, c)) {
                const NavPoint a = nav.center(r, c), b = nav.center(r + 1, c);
                cliff = std::abs(nav.y[nav.index(r, c)] - nav.y[nav.index(r + 1, c)]) > nav_rules.max_grade * nav.cell_m &&
                        !nav_line_clear(nav, nav_rules, a.x, a.z, b.x, b.z);
            }
        }
    }
    check(cliff, "太陡的两格之间走不过去");

    auto path_ok = [&](const std::vector<NavPoint> &p_path, double p_x0, double p_z0, const char *p_what) {
        bool ok = !p_path.empty();
        double px = p_x0, pz = p_z0;
        NavPoint start;
        if (ok && nav_nearest_walkable(nav, p_x0, p_z0, 6.0, false, start)) {
            px = start.x;
            pz = start.z;
        }
        for (const NavPoint &q : p_path) {
            ok = ok && nav.walkable_at(q.x, q.z) && nav_line_clear(nav, nav_rules, px, pz, q.x, q.z);
            for (int k = 0; k <= 20 && ok; k++) {
                const double t = k / 20.0;
                ok = !footprint_contains(house, layout.at(0), px + (q.x - px) * t, pz + (q.z - pz) * t, 0.3);
            }
            px = q.x;
            pz = q.z;
        }
        check(ok, p_what);
    };
    const std::vector<NavPoint> to_door = nav_find_path(nav, nav_rules, apron_x, apron_z, snapped.x, snapped.z);
    path_ok(to_door, apron_x, apron_z, "停机坪到门前的路：每段都走得了、不穿房子");
    check(!to_door.empty() && std::abs(to_door.back().x - snapped.x) < 1e-9 && std::abs(to_door.back().z - snapped.z) < 1e-9, "路的末点就是门前");
    // 绕房：房前 → 房后
    double ux = 0, uz = 0, vx = 0, vz = 0;
    building_axes(hyaw, ux, uz, vx, vz);
    const double off = 0.5 * house.depth_m + 3.0;
    const double bx = hx - vx * off, bz = hz - vz * off, fx = hx + vx * off, fz = hz + vz * off;
    check(!nav_line_clear(nav, nav_rules, fx, fz, bx, bz), "直穿房子的线不通");
    if (nav.walkable_at(bx, bz) && nav.walkable_at(fx, fz)) {
        const std::vector<NavPoint> around = nav_find_path(nav, nav_rules, fx, fz, bx, bz);
        path_ok(around, fx, fz, "房前到房后的路绕开房子");
        double len = 0.0, qx = fx, qz = fz;
        for (const NavPoint &q : around) {
            len += std::hypot(q.x - qx, q.z - qz);
            qx = q.x;
            qz = q.z;
        }
        check(len > 2.0 * off + 1.0 && around.size() >= 2, "绕过去比直穿远、至少拐一次弯（" + std::to_string(len) + " m）");
    }
    const std::vector<NavPoint> into = nav_find_path(nav, nav_rules, apron_x, apron_z, hx, hz);
    check(!into.empty() && !footprint_contains(house, layout.at(0), into.back().x, into.back().z, 0.5), "要去房子正中：停在房子外面");
    check(nav_find_path(nav, nav_rules, apron_x, apron_z, 10.0 * f.half_extent(), 0.0).empty(), "岛外去不了");

    // 钟点
    DayRules day;
    check(read_clock(0.0, day).day == 0 && std::abs(read_clock(0.0, day).hour) < 1e-9, "子夜");
    check(std::abs(read_clock(day.day_seconds * 0.5, day).hour - 12.0) < 1e-9, "半天是正午");
    check(read_clock(day.day_seconds * 1.25, day).day == 1 && std::abs(read_clock(day.day_seconds * 1.25, day).hour - 6.0) < 1e-9, "第二天卯时");
    check(is_home_hour(3.0, day) && !is_home_hour(8.0, day) && is_home_hour(12.0, day) && !is_home_hour(14.0, day) && is_home_hour(19.0, day), "夜里与歇晌在家");
    check(std::string(shichen_name(23.5)) == "子时" && std::string(shichen_name(0.5)) == "子时" && std::string(shichen_name(9.5)) == "巳时" && std::string(shichen_name(12.0)) == "午时", "时辰名");
    const std::string text = clock_text(day.day_seconds * (1.0 + 9.5 / 24.0), day);
    check(text == "巳时 · 第 2 日 · 09:30", "钟点文字（" + text + "）");

    // 分派
    const SettlementLayout town({ { "house", 0, 0, 0 }, { "hall", 40, 0, 0 }, { "house", 20, 0, 0 }, { "tower", 80, 0, 0 }, { "granary", 60, 0, 0 } });
    const std::vector<Resident> folk = assign_residents(town, true, true, 2, 7);
    check(folk.size() == 6, "两户各两口 + 堂屋一口 + 守望一个（" + std::to_string(folk.size()) + "）");
    int farmers = 0, carriers = 0, watchmen = 0, idlers = 0;
    bool names_unique = true;
    for (size_t k = 0; k < folk.size(); k++) {
        farmers += folk[k].job == Job::FARMER ? 1 : 0;
        carriers += folk[k].job == Job::CARRIER ? 1 : 0;
        watchmen += folk[k].job == Job::WATCHMAN ? 1 : 0;
        idlers += folk[k].job == Job::IDLER ? 1 : 0;
        for (size_t m = 0; m < k; m++) {
            names_unique = names_unique && folk[k].name != folk[m].name;
        }
    }
    check(farmers == 2 && carriers == 2 && watchmen == 1 && idlers == 1, "职业：农 2 挑水 2 守望 1 闲 1");
    check(names_unique && !folk[0].name.empty(), "名字不重复");
    check(folk.back().job == Job::WATCHMAN && folk.back().home == 1 && folk.back().post == 3, "守望住堂屋、守望楼");
    check(assign_residents(town, true, true, 2, 7)[0].name == folk[0].name, "同一个种子名字一样");
    int dry_idlers = 0;
    for (const Resident &r : assign_residents(town, false, false, 2, 7)) {
        dry_idlers += r.job == Job::IDLER ? 1 : 0;
    }
    check(dry_idlers == 5, "没地没河的岛：除守望都闲着");
    check(assign_residents(SettlementLayout({ { "house", 0, 0, 0 }, { "tower", 30, 0, 0 } }), false, false, 2, 1).back().home == 0, "没有堂屋：守望住最近的民居");
    check(assign_residents(SettlementLayout({ { "tower", 0, 0, 0 } }), true, true, 2, 1).empty(), "没有能住的房子就没人");
    check(villagers::job_by_id("farmer") == &villagers::job(Job::FARMER) && !villagers::job_by_id("king"), "职业表按 id 查");
    check(std::string(villagers::task(TaskKind::TILL).id) == "till" && std::string(villagers::task(TaskKind::WAIT).name) == "歇着", "活计表");

    // 决策：河边这一户，加一座仓与一座望楼（不管站不站得住，决策只看清单）
    SettlementLayout village = layout;
    village.add({ "granary", hx + ux * 20.0, hz + uz * 20.0, hyaw });
    village.add({ "tower", hx - ux * 20.0, hz - uz * 20.0, hyaw });
    const NavGrid vnav = island_nav(f, village, nav_rules);
    FarmModel farm(&island_map, island_map.plot_lattice(), FarmRules());
    const PlotLattice plots = island_map.plot_lattice();
    std::vector<WaterLine> water;
    island_map.water_lines(-f.half_extent(), -f.half_extent(), f.half_extent(), f.half_extent(), water);
    std::vector<Claim> claims;
    Inventory store;
    store.add("millet", 3);
    WorkRules work;
    VillageView view;
    view.plots = &plots;
    view.water = &water;
    view.layout = &village;
    view.nav = &vnav;
    ModelFields fields(farm);
    view.fields = &fields;
    view.claims = &claims;
    view.store = &store;
    view.day = &day;
    view.work = &work;
    Rng rng(42);
    VillagerMind farmer;
    farmer.index = 0;
    farmer.who = { "阿禾", Job::FARMER, 0, -1 };
    farmer.x = door.x;
    farmer.z = door.z;

    view.hour = 20.0;
    Task t = decide_task(farmer, view, rng);
    check(t.kind == TaskKind::GO_HOME && t.building == 0, "收工了回家");
    view.hour = 12.0;
    check(decide_task(farmer, view, rng).kind == TaskKind::GO_HOME, "歇晌回家");
    view.hour = 8.0;
    t = decide_task(farmer, view, rng);
    check(t.kind == TaskKind::TILL && farm.available_action(t.cell_i, t.cell_j, false) == FarmAction::TILL, "农夫出门先开荒");
    const NavPoint pc = plot_center(plots, t.cell_i, t.cell_j);
    check(std::hypot(pc.x - snapped.x, pc.z - snapped.z) <= work.field_radius_m + 3.0 && vnav.dry_at(pc.x, pc.z), "开的地在家门口、站得住");
    check(std::abs(t.x - pc.x) < 1e-9 && t.seconds == work.till_s, "站在地里干");
    farm.till(t.cell_i, t.cell_j);
    claims.push_back({ t.cell_i, t.cell_j, 0 });
    const int ci = t.cell_i, cj = t.cell_j;
    t = decide_task(farmer, view, rng);
    check(t.kind == TaskKind::SOW && t.cell_i == ci && t.cell_j == cj && t.seed == "millet", "垦好了就播仓里的种子");
    store.remove("millet", 3);
    t = decide_task(farmer, view, rng);
    check(t.kind == TaskKind::TILL && !(t.cell_i == ci && t.cell_j == cj), "仓里没种子：先去开下一块");
    int pi = 0, pj = 0;
    claims.push_back({ t.cell_i, t.cell_j, 1 }); // 别人认领了
    check(pick_wild_plot(view, snapped.x, snapped.z, pi, pj) && !(pi == t.cell_i && pj == t.cell_j), "别人认领的地不抢");
    claims.pop_back();
    store.add("millet", 1);
    farm.sow(ci, cj, catalog::crop("millet"));
    farm.advance(1e6);
    t = decide_task(farmer, view, rng);
    check(t.kind == TaskKind::HARVEST && t.cell_i == ci && t.cell_j == cj, "熟了先收");
    farmer.carrying = "millet";
    farmer.carry_count = 4;
    t = decide_task(farmer, view, rng);
    check(t.kind == TaskKind::DELIVER && t.building == 1, "背着粮送进仓");
    farmer.carrying.clear();
    // 认领满了、都在长：锄草
    farm.harvest(ci, cj);
    farm.sow(ci, cj, catalog::crop("millet"));
    for (int k = 1; k < work.plots_per_farmer; k++) {
        claims.push_back({ ci, cj, 0 });
    }
    t = decide_task(farmer, view, rng);
    check(t.kind == TaskKind::TEND && t.cell_i == ci, "地都种上了：锄草");
    claims.clear();

    VillagerMind carrier = farmer;
    carrier.index = 1;
    carrier.who = { "石头", Job::CARRIER, 0, -1 };
    t = decide_task(carrier, view, rng);
    check(t.kind == TaskKind::FETCH_WATER && vnav.dry_at(t.x, t.z), "挑水的去河边，站在干地上");
    double near_river = 1e18;
    for (const RiverPoint &rp : f.river) {
        near_river = std::min(near_river, std::hypot(rp.x - t.x, rp.z - t.z));
    }
    check(near_river < 8.0, "打水的站位紧挨着河（" + std::to_string(near_river) + " m）");
    carrier.carrying = "water";
    t = decide_task(carrier, view, rng);
    check(t.kind == TaskKind::DELIVER && t.building == 0, "挑满了：没有堂屋就送回家");
    SettlementLayout with_hall = village;
    with_hall.add({ "hall", hx - vx * 30.0, hz - vz * 30.0, hyaw });
    view.layout = &with_hall;
    t = decide_task(carrier, view, rng);
    check(t.kind == TaskKind::DELIVER && t.building == 3, "挑满了：送到堂屋的水缸");
    view.layout = &village;

    VillagerMind watchman = farmer;
    watchman.index = 2;
    watchman.who = { "长庚", Job::WATCHMAN, 0, 2 };
    t = decide_task(watchman, view, rng);
    check(t.kind == TaskKind::WATCH && t.building == 2 && t.seconds == work.watch_s, "守望先站班");
    const Building &tower = village.at(2);
    check(std::hypot(t.face_x - tower.x, t.face_z - tower.z) > std::hypot(t.x - tower.x, t.z - tower.z), "站班背对望楼");
    int patrols = 0;
    for (int k = 0; k < work.patrol_stops; k++) {
        const Task s = decide_task(watchman, view, rng);
        patrols += s.kind == TaskKind::PATROL && s.building != 2 ? 1 : 0;
    }
    check(patrols == work.patrol_stops && decide_task(watchman, view, rng).kind == TaskKind::WATCH, "巡完一圈再回去站班");

    VillagerMind idler = farmer;
    idler.who = { "小满", Job::IDLER, 0, -1 };
    t = decide_task(idler, view, rng);
    check(t.kind == TaskKind::CHAT && t.building != 0 && t.seconds >= work.chat_min_s && t.seconds <= work.chat_max_s, "闲人去别家串门");
}


// 合成一个 terrain.bin：圆岛，半径 p_radius，岸缘 200 m、中间鼓到 260 m
std::vector<uint8_t> synthetic_terrain_bin(int p_rows, int p_cols, double p_res, double p_radius) {
    std::vector<uint8_t> out;
    auto put = [&out](const void *p, size_t n) { out.insert(out.end(), (const uint8_t *)p, (const uint8_t *)p + n); };
    const uint32_t version = 1, rows = uint32_t(p_rows), cols = uint32_t(p_cols);
    const double x0 = -(p_cols - 1) * 0.5 * p_res, z0 = -(p_rows - 1) * 0.5 * p_res;
    put("ZZWG", 4);
    put(&version, 4);
    put(&rows, 4);
    put(&cols, 4);
    put(&p_res, 8);
    put(&x0, 8);
    put(&z0, 8);
    std::vector<uint16_t> h;
    std::vector<int16_t> shore;
    std::vector<uint8_t> isl, cover, stream;
    for (int r = 0; r < p_rows; r++) {
        for (int c = 0; c < p_cols; c++) {
            const double d = std::hypot(x0 + c * p_res, z0 + r * p_res);
            const double inside = std::max(0.0, 1.0 - d / p_radius);
            h.push_back(uint16_t(std::lround((200.0 + 60.0 * inside * inside) * 100.0)));
            shore.push_back(int16_t(std::lround(p_radius - d)));
            isl.push_back(0);
            cover.push_back(d < p_radius ? (d < 0.5 * p_radius ? 7 : 4) : 0);
            stream.push_back(0);
        }
    }
    put(h.data(), h.size() * 2);
    put(shore.data(), shore.size() * 2);
    put(isl.data(), isl.size());
    put(cover.data(), cover.size());
    put(stream.data(), stream.size());
    return out;
}

WorldGrid synthetic_grid() {
    const std::vector<uint8_t> bin = synthetic_terrain_bin(41, 51, 100.0, 1500.0);
    WorldGrid g;
    std::string err;
    parse_world_grid(bin.data(), bin.size(), g, err);
    return g;
}

void test_world_grid() {
    std::printf("world_grid\n");
    const std::vector<uint8_t> bin = synthetic_terrain_bin(41, 51, 100.0, 1500.0);
    WorldGrid g;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err), "合成的 terrain.bin 能解析：" + err);
    check(g.rows == 41 && g.cols == 51 && g.res == 100.0, "行列与格距读对");
    check_near(g.x0, -2500.0, 1e-9, "格 (0,0) 格心 x");
    check_near(g.height_at(0.0, 0.0), 260.0, 0.01, "格心上双三次 = 格值");
    check_near(g.height_at(300.0, -400.0), g.height[g.index(20 - 4, 25 + 3)], 0.01, "另一个格心");
    check(g.shore_at(0, 0) > 1400.0 && g.shore_at(2400.0, 0) < 0.0, "岸距：岛心为正、岛外为负");
    double jump = 0.0;
    for (double x = -2000; x < 2000; x += 7.3) {
        jump = std::max(jump, std::abs(g.height_at(x, 123.0) - g.height_at(x + 0.05, 123.0)));
    }
    check(jump < 0.05, "高程连续：5 cm 内的变化 < 5 cm");
    check(g.landcover[g.nearest_cell(0, 0)] == 7 && g.landcover[g.nearest_cell(1100, 0)] == 4, "最近格的地表类");

    std::vector<uint8_t> broken = bin;
    broken[0] = 'X';
    check(!parse_world_grid(broken.data(), broken.size(), g, err), "开头不是 ZZWG 拒收");
    broken = bin;
    broken.pop_back();
    check(!parse_world_grid(broken.data(), broken.size(), g, err), "短一个字节拒收");
}

WorldIsland synthetic_island() {
    WorldIsland isl;
    isl.id = 0;
    isl.rim_m = 200.0;
    isl.cliff_m = 30.0;
    isl.keel_m = 120.0;
    isl.max_shore_m = 1500.0;
    return isl;
}

void test_world_terrain() {
    std::printf("world_terrain\n");
    const WorldGrid g = synthetic_grid();
    const WorldTerrain t(g, { synthetic_island() }, 7), t2(g, { synthetic_island() }, 7), t3(g, { synthetic_island() }, 8);

    bool same = true, other = false, thick = true;
    double jump = 0.0, detail_max = 0.0;
    for (int k = 0; k < 20000; k++) {
        const double a = k * 0.0219911, d = 200.0 + (k % 97) * 20.0;
        const double x = d * std::cos(a), z = d * std::sin(a);
        const TerrainSample s = t.sample(x, z), s2 = t2.sample(x, z), s3 = t3.sample(x, z);
        same = same && s.top == s2.top && s.land == s2.land && s.bottom == s2.bottom;
        other = other || s.top != s3.top;
        if (s.land) {
            thick = thick && s.bottom < s.top - 1.0;
            detail_max = std::max(detail_max, std::abs(s.top - g.height_at(x, z)));
        }
        jump = std::max(jump, std::abs(s.top - t.sample(x + 0.02, z).top));
    }
    // 沿射线找岸：岸线进退不超过噪声幅度
    double coast_lo = 1e9, coast_hi = 0.0;
    for (int k = 0; k < 360; k++) {
        const double a = k * 3.14159265 / 180.0;
        double r = 0.0;
        while (r < 2500.0 && t.sample(r * std::cos(a), r * std::sin(a)).land) {
            r += 1.0;
        }
        coast_lo = std::min(coast_lo, r);
        coast_hi = std::max(coast_hi, r);
    }
    check(same, "同种子同地形");
    check(other, "换种子细节不同");
    check(thick, "岛底总在顶面之下");
    check(jump < 0.1, "顶面连续：2 cm 内的变化 < 10 cm（得到 " + std::to_string(jump) + "）");
    check(detail_max > 0.3 && detail_max < 8.0, "细节噪声只补小起伏（得到 " + std::to_string(detail_max) + " m）");
    check(coast_lo > 1500.0 - WorldTerrain::COAST_AMP_M - 60.0 && coast_hi < 1500.0 + WorldTerrain::COAST_AMP_M + 60.0,
            "岸线在栅格岸线附近进退（" + std::to_string(coast_lo) + " … " + std::to_string(coast_hi) + "）");
    check(coast_hi - coast_lo > 8.0, "岸线不是正圆：噪声真的让它进退了");
    const TerrainSample rim = t.sample(1500.0 - WorldTerrain::COAST_AMP_M - 60.0, 0.0), core = t.sample(0.0, 0.0);
    check(core.bottom < rim.bottom - 60.0, "岛底中间垂得最深（倒锥）");
    double y = 0.0;
    check(t.surface(0, 0, y) && std::abs(y - 260.0) < 8.0 && !t.surface(2400.0, 0.0, y), "surface：岛心有地、岛外没有");
}

void test_terrain_tiles() {
    std::printf("terrain_tile\n");
    const WorldGrid g = synthetic_grid();
    const WorldTerrain t(g, { synthetic_island() }, 7);
    const TileLayout layout(g);
    check(layout.size(layout.get_max_level()) == TileLayout::LEAF_M, "叶块 128 m");
    check(layout.size(0) >= 5100.0, "根块盖住整个栅格");

    const std::vector<TileKey> tiles = layout.select(100.0, 50.0);
    const TileKey here = layout.leaf_at(100.0, 50.0);
    bool has_here = false, disjoint = true;
    for (size_t a = 0; a < tiles.size(); a++) {
        has_here = has_here || tiles[a] == here;
        for (size_t b = a + 1; b < tiles.size(); b++) {
            disjoint = disjoint && !layout.overlaps(tiles[a], tiles[b]);
        }
    }
    check(has_here, "焦点所在的叶块被选中");
    check(disjoint, "选出的块互不重叠");
    int uncovered = 0;
    for (int k = 0; k < 4096; k++) {
        const double x = -1600.0 + (k % 64) * 50.0, z = -1600.0 + (k / 64) * 50.0;
        if (!t.sample(x, z).land) {
            continue;
        }
        int covering = 0;
        for (const TileKey &key : tiles) {
            double cx, cz;
            layout.corner(key, cx, cz);
            const double s = layout.size(key.level);
            covering += x >= cx && x < cx + s && z >= cz && z < cz + s ? 1 : 0;
        }
        uncovered += covering == 1 ? 0 : 1;
    }
    check(uncovered == 0, "每一处有地的点恰好落在一个块里（漏 / 重 " + std::to_string(uncovered) + " 处）");
    int near_leaves = 0, far_coarse = 0;
    for (const TileKey &key : tiles) {
        double cx, cz;
        layout.corner(key, cx, cz);
        const double s = layout.size(key.level);
        const double d = std::hypot(cx + 0.5 * s - 100.0, cz + 0.5 * s - 50.0);
        near_leaves += d < 200.0 && key.level == layout.get_max_level() ? 1 : 0;
        far_coarse += d > 1200.0 && key.level < layout.get_max_level() ? 1 : 0;
    }
    check(near_leaves >= 4, "焦点附近全是叶块");
    check(far_coarse >= 1, "远处用粗块");

    const TileMesh inner = build_tile_mesh(t, layout, here);
    const size_t verts = inner.positions.size() / 3;
    bool indices_ok = inner.indices.size() % 3 == 0, local = true;
    for (int32_t k : inner.indices) {
        indices_ok = indices_ok && k >= 0 && size_t(k) < verts;
    }
    for (size_t k = 0; k < verts; k++) {
        local = local && inner.positions[k * 3] >= -0.01f && inner.positions[k * 3] <= 128.01f && inner.positions[k * 3 + 2] >= -0.01f && inner.positions[k * 3 + 2] <= 128.01f;
    }
    check(inner.land_quads == 64 * 64, "岛心的叶块满是陆地");
    check(indices_ok && local, "下标有效、顶点在块的局部坐标里");
    check(!inner.collision.empty() && inner.collision.size() % 9 == 0, "叶块带碰撞面");
    bool heights_match = true;
    for (size_t k = 0; k < verts && k < 400; k++) {
        if (inner.normals[k * 3 + 1] > 0.5f) {
            const TerrainSample s = t.sample(inner.origin_x + inner.positions[k * 3], inner.origin_z + inner.positions[k * 3 + 2]);
            heights_match = heights_match && std::abs(s.top - inner.positions[k * 3 + 1]) < 0.01;
        }
    }
    check(heights_match, "顶面顶点就是地形函数的值");

    // 岸边的块：有崖壁（水平法线的面）
    const TileMesh coast = build_tile_mesh(t, layout, layout.leaf_at(1500.0, 0.0));
    int walls = 0;
    for (size_t k = 0; k < coast.normals.size() / 3; k++) {
        walls += coast.normals[k * 3 + 1] == 0.0f ? 1 : 0;
    }
    check(coast.land_quads > 0 && coast.land_quads < 64 * 64, "岸边的块一半是地");
    check(walls > 0, "岸边的块有崖壁");
    const TileMesh coarse = build_tile_mesh(t, layout, { 1, 0, 0 });
    check(coarse.collision.empty(), "粗块不带碰撞");

    // 地皮色跨 可耕地 | 林地（r = 750 m）是渐变：相邻顶点（2 m）色差很小，走过这一块又变了一大截
    {
        const TileMesh edge = build_tile_mesh(t, layout, layout.leaf_at(760.0, 30.0));
        const double step = layout.step(layout.get_max_level());
        std::map<std::pair<int, int>, std::array<float, 3>> top;
        for (size_t k = 0; k < edge.positions.size() / 3; k++) {
            if (edge.normals[k * 3 + 1] > 0.5f) {
                const int j = int(std::lround(edge.positions[k * 3] / step)), i = int(std::lround(edge.positions[k * 3 + 2] / step));
                top[{ i, j }] = { edge.colors[k * 4], edge.colors[k * 4 + 1], edge.colors[k * 4 + 2] };
            }
        }
        float jump = 0.0f, lo = 1.0f, hi = 0.0f;
        for (const auto &[key, c] : top) {
            lo = std::min(lo, c[0]);
            hi = std::max(hi, c[0]);
            for (const std::pair<int, int> d : { std::pair<int, int>{ 0, 1 }, std::pair<int, int>{ 1, 0 } }) {
                const auto next = top.find({ key.first + d.first, key.second + d.second });
                if (next != top.end()) {
                    for (int ch = 0; ch < 3; ch++) {
                        jump = std::max(jump, std::abs(next->second[size_t(ch)] - c[size_t(ch)]));
                    }
                }
            }
        }
        std::printf("  可耕地 | 林地的叶块：%zu 个顶点，红通道 %.2f … %.2f，相邻顶点最多差 %.3f\n", top.size(), lo, hi, jump);
        check(top.size() > 4000 && hi - lo > 0.1 && jump < 0.1, "地皮色跨地表类渐变：整块变了一大截，相邻 2 m 的顶点差不过 0.1（一刀切是 0.3）");
    }
}

// 两类各占一半的合成栅格：x < 0 草坡（6）、x > 0 林地（4），交界线在 x = 0（两列格心 −50、+50 之间）
WorldGrid half_grid() {
    WorldGrid g;
    g.rows = 24;
    g.cols = 24;
    g.res = 100.0;
    g.x0 = -1150.0;
    g.z0 = -1150.0;
    const size_t n = size_t(g.rows) * size_t(g.cols);
    g.height.assign(n, 200.0f);
    g.shore.assign(n, 5000.0f);
    g.island.assign(n, 0);
    g.stream.assign(n, 0);
    g.landcover.resize(n);
    for (int r = 0; r < g.rows; r++) {
        for (int c = 0; c < g.cols; c++) {
            g.landcover[g.index(r, c)] = g.x0 + c * g.res < 0.0 ? 6 : 4;
        }
    }
    return g;
}

// 沿 z = p_z 横穿交界：林地份额一成、九成处的 x（没找到返回 NaN）
void cover_profile(const CoverBlend &p_blend, double p_z, double p_footprint, double &r_x10, double &r_x90) {
    r_x10 = r_x90 = std::numeric_limits<double>::quiet_NaN();
    for (double x = -300.0; x <= 300.0; x += 0.5) {
        const float w = p_blend.mix(x, p_z, p_footprint).w[4];
        if (std::isnan(r_x10) && w >= 0.1f) {
            r_x10 = x;
        }
        if (std::isnan(r_x90) && w >= 0.9f) {
            r_x90 = x;
        }
    }
}

void test_cover_blend() {
    std::printf("cover_blend\n");
    const WorldGrid g = half_grid();
    CoverRules plain;
    plain.warp_m = 0.0;
    plain.bend_m = 0.0;
    plain.breakup_m = 0.0;
    const CoverBlend straight(g, 7, plain);
    check(straight.valid() && !CoverBlend().valid(), "有栅格才有效");

    // 份额：非负、和为 1；深处全是本类；交界线上各半
    bool sums = true, ranged = true;
    for (double x = -900.0; x <= 900.0; x += 3.7) {
        const CoverMix m = straight.mix(x, 123.0);
        double sum = 0.0;
        for (float w : m.w) {
            sum += w;
            ranged = ranged && w >= 0.0f && w <= 1.0f;
        }
        sums = sums && std::abs(sum - 1.0) < 1e-5;
    }
    check(sums && ranged, "份额都在 [0, 1] 里、和为 1");
    check(straight.mix(-260.0, 0.0).w[6] == 1.0f && straight.mix(260.0, 0.0).w[4] == 1.0f, "离交界两格半以外全是本类（4×4 格都是一类）");
    check_near(straight.mix(0.0, 40.0).w[4], 0.5, 1e-4, "交界线上各半");
    check(straight.mix(0.0, 0.0).dominant() == 4 || straight.mix(0.0, 0.0).dominant() == 6, "主类是两类之一");
    check(straight.mix(-30.0, 0.0).dominant() == 6 && straight.mix(30.0, 0.0).dominant() == 4, "交界两侧的主类");
    check_near(straight.mix(-30.0, 0.0).w[6] + straight.mix(-30.0, 0.0).w[4], 1.0, 1e-5, "只有这两类");

    // 过渡带的宽度：近处按 transition_m（40 m）；远处（取样隔半格以上）换成 B 样条的平滑，一百多米
    double x10, x90;
    cover_profile(straight, 0.0, 0.0, x10, x90);
    const double narrow = x90 - x10;
    check(std::abs(narrow - 40.0) < 2.0 && std::abs(x10 + x90) < 1.0, "林地一成到九成 40 m、对着交界线对称（" + std::to_string(narrow) + " m）");
    cover_profile(straight, 0.0, 100.0, x10, x90);
    const double smooth_only = x90 - x10;
    check(smooth_only > 100.0, "取样隔一格：B 样条平滑，一百多米（" + std::to_string(smooth_only) + " m）");
    cover_profile(straight, 0.0, 2.0, x10, x90);
    check(x90 - x10 < narrow + 3.0, "叶块（2 m 一个顶点）几乎不放宽");
    cover_profile(straight, 0.0, 25.0, x10, x90);
    check(x90 - x10 > narrow + 5.0 && x90 - x10 < smooth_only, "中等间距放宽一些（" + std::to_string(x90 - x10) + " m）");

    // 只有一格的小片也在，面积与一格一样（一半处的交界线就是最近格的边）
    {
        WorldGrid lone = g;
        std::fill(lone.landcover.begin(), lone.landcover.end(), uint8_t(6));
        lone.landcover[lone.nearest_cell(50.0, 50.0)] = 5;
        const CoverBlend spot(lone, 7, plain);
        int inside = 0;
        for (double z = -60.0; z < 160.0; z += 1.0) {
            for (double x = -60.0; x < 160.0; x += 1.0) {
                inside += spot.mix(x + 0.5, z + 0.5).dominant() == 5 ? 1 : 0;
            }
        }
        check(spot.mix(50.0, 50.0).w[5] > 0.95f, "一格的小片：格心上几乎全是它");
        check(std::abs(inside - 10000) < 1500, "一格的小片：占了的地方约一格（四个角磨圆一点，" + std::to_string(inside) + " m²）");
    }

    // 默认规则：连续（半米内份额变不过 0.05），交界线扭过、碎过，沿着走进退几十米，同一个种子处处一样
    const CoverBlend ragged(g, 7);
    const CoverBlend other(g, 8);
    double jump = 0.0, lo = 1e9, hi = -1e9;
    bool same = true, differs = false;
    for (double z = -400.0; z <= 400.0; z += 10.0) {
        double prev = ragged.mix(-200.0, z).w[4], cross = std::numeric_limits<double>::quiet_NaN();
        for (double x = -199.75; x <= 200.0; x += 0.25) {
            const double w = ragged.mix(x, z).w[4];
            jump = std::max(jump, std::abs(w - prev));
            if (std::isnan(cross) && w >= 0.5) {
                cross = x;
            }
            prev = w;
        }
        if (!std::isnan(cross)) {
            lo = std::min(lo, cross);
            hi = std::max(hi, cross);
        }
        const CoverMix a = ragged.mix(7.0, z), b = CoverBlend(g, 7).mix(7.0, z), c = other.mix(7.0, z);
        same = same && std::equal(a.w, a.w + COVER_CLASSES, b.w);
        differs = differs || a.w[4] != c.w[4];
    }
    std::printf("  直交界扭碎后：一半处的 x 在 %.0f … %.0f m 之间，半米内份额最多变 %.3f\n", lo, hi, jump);
    check(jump < 0.05, "份额连续：半米内变不过 0.05（" + std::to_string(jump) + "）");
    check(hi - lo > 30.0 && lo > -150.0 && hi < 150.0, "交界线扭成犬牙：进退几十米，不出一格半");
    check(same && differs, "同一个种子处处一样，换种子就换样子");

    // 同一套平滑给别的逐格量用
    check_near(ragged.smooth(ragged.stencil(333.0, -71.0), [](size_t) { return 0.3; }), 0.3, 1e-6, "处处一样的量平滑后不变");
    const CoverStencil st = straight.stencil(0.0, 0.0);
    check_near(straight.smooth(st, [&g](size_t k) { return g.landcover[k] == 4 ? 1.0 : 0.0; }), 0.5, 1e-6, "交界线上林地格的平滑份额是一半");
    float labels[2];
    straight.blend(st, 0.0, 0.0, 0.0, 9, 2, [&g](size_t k) { return g.landcover[k] == 4 ? 1 : 0; }, labels);
    check_near(labels[1], 0.5, 1e-4, "别的类别也按同一套混（交界线上各半）");
    check_near(cover_ramp(0.5, 0.0, 1.0), 0.5, 1e-9, "渐入：一半处一半");
    check(cover_ramp(-1.0, 0.0, 1.0) == 0.0 && cover_ramp(2.0, 0.0, 1.0) == 1.0 && cover_ramp(0.9, 1.0, 0.0) < 0.1, "渐入夹在 [0, 1]、可以反着");
}

void test_map_image() {
    std::printf("map_image\n");
    const WorldGrid g = synthetic_grid();
    const MapImage img = build_map_image(g);
    check(img.width == 51 && img.height == 41 && img.rgba.size() == size_t(51 * 41 * 4), "一格一像素");
    check(img.rgba[g.index(20, 25) * 4 + 3] == 255 && img.rgba[g.index(0, 0) * 4 + 3] == 0, "陆地不透明、虚空透明");
    double u, v, x, z;
    world_to_map(g, 1234.0, -567.0, u, v);
    map_to_world(g, u, v, x, z);
    check_near(x, 1234.0, 1e-6, "地图 ↔ 世界往返 x");
    check_near(z, -567.0, 1e-6, "地图 ↔ 世界往返 z");
    world_to_map(g, g.x0, g.z0, u, v);
    check_near(u, 0.5, 1e-9, "格 (0,0) 的格心在像素 (0.5, 0.5)");
}

// 真正入库的 2051 资产（仓库里有才测）
void test_world_2051() {
    const std::string path = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051/terrain.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::printf("world 2051（跳过：没有 %s）\n", path.c_str());
        return;
    }
    std::printf("world 2051\n");
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err), "2051 的 terrain.bin 能解析：" + err);
    check(g.rows == 1012 && g.cols == 837 && g.res == 100.0, "1012 × 837 格、100 m");
    check(g.land_count() == 258847, "陆地格数与 skyisle-gen 一致（" + std::to_string(g.land_count()) + "）");
    // 邑治（村001，skyisle 的 km (10.35, 21.85)，主岛东北的岸边低地）
    const double sx = 10350.0, sz = -21850.0;
    check(g.shore[g.nearest_cell(sx, sz)] > 0 && std::abs(g.height_at(sx, sz) - 238.0) < 6.0, "邑治在陆地上、高约 238 m（" + std::to_string(g.height_at(sx, sz)) + "）");
    check(g.island[g.nearest_cell(sx, sz)] == 0, "邑治在主岛");
    WorldIsland main;
    main.rim_m = 236.1;
    main.cliff_m = 45.1;
    main.keel_m = 191.0;
    main.max_shore_m = 15000.0;
    const WorldTerrain t(g, { main }, 2051);
    double y = 0.0;
    check(t.surface(sx, sz, y), "邑治处连续地形有地");
    const TileLayout layout(g);
    const auto started = std::chrono::steady_clock::now();
    const std::vector<TileKey> tiles = layout.select(sx, sz);
    size_t tris = 0;
    for (const TileKey &key : tiles) {
        tris += build_tile_mesh(t, layout, key).indices.size() / 3;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::printf("  站在邑治：%zu 块、%zu 个三角面，建完 %.0f ms（%.1f ms/块）\n", tiles.size(), tris, ms, ms / double(tiles.size()));
    check(tiles.size() > 20 && tiles.size() < 600, "块数在几十到几百之间（" + std::to_string(tiles.size()) + "）");
}

// 2051 的地形 v3：生成器下切过的河道、地形区与游戏的厚岛体
void test_terrain_v3_2051() {
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        std::printf("terrain v3（跳过：没有 2051 资产）\n");
        return;
    }
    std::printf("terrain v3（2051）\n");
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err) && g.has_hydro() && g.has_channels() && g.zone.size() == g.height.size(), "terrain.bin v3 带河道与地形区：" + err);
    check(parse_world_sites(tsv, sites, err), "sites.tsv 能解析：" + err);
    // 河道格：有宽深、汇流到溪涧门槛；河床顺流不升（生成器保证单调，导出按汇流反推的流向要接得上）
    int chan = 0, chan_small = 0, uphill = 0, off_channel = 0;
    double widest = 0.0;
    for (int r = 0; r < g.rows; r++) {
        for (int c = 0; c < g.cols; c++) {
            const size_t k = g.index(r, c);
            if (g.river_width[k] <= 0.0f) {
                continue;
            }
            chan++;
            widest = std::max(widest, double(g.river_width[k]));
            chan_small += g.flow_acc[k] < 0.9f ? 1 : 0;
            const uint8_t code = g.flow[k];
            if ((code & WorldGrid::FLOW_TO_VOID) || (code & 0x0F) == 0) {
                continue;
            }
            const size_t down = g.index(r + WorldGrid::FLOW_OFFSETS[(code & 0x0F) - 1][0], c + WorldGrid::FLOW_OFFSETS[(code & 0x0F) - 1][1]);
            uphill += g.height[down] > g.height[k] + 0.05f ? 1 : 0;
            off_channel += g.river_width[down] <= 0.0f ? 1 : 0;
        }
    }
    std::printf("  河道格 %d，最宽 %.1f m，顺流上坡 %d 格、流出河道 %d 格\n", chan, widest, uphill, off_channel);
    check(chan > 10000 && widest > 50.0 && widest < 80.0, "河道格上万、常年河最宽六十多米（台面口径起 2051 有常年河）");
    check(chan_small == 0, "河道格的汇流都到了 1 km² 的溪涧门槛");
    check(uphill < chan / 1000, "河床顺流几乎处处不升（" + std::to_string(uphill) + " 格例外）");
    check(off_channel == 0, "河道格顺流总流到下一个河道格（流向按生成器的汇流反推）");

    // 地形区：山里比台地崎岖
    const WorldTerrain t(g, sites.terrain_islands(), 2051);
    double rough[8] = {}, count[8] = {};
    for (int r = 2; r < g.rows - 2; r += 3) {
        for (int c = 2; c < g.cols - 2; c += 3) {
            const size_t k = g.index(r, c);
            if (g.shore[k] < 300.0f || g.zone[k] >= 8) {
                continue;
            }
            const double x = g.x0 + c * g.res, z = g.z0 + r * g.res;
            rough[g.zone[k]] += std::abs(t.detail(x, z));
            count[g.zone[k]] += 1.0;
        }
    }
    auto mean_of = [&](int z) { return count[z] > 0 ? rough[z] / count[z] : 0.0; };
    std::printf("  细节起伏均值：高山 %.1f、山地 %.1f、丘陵 %.1f、台地平原 %.1f m\n", mean_of(1), mean_of(2), mean_of(3), mean_of(4));
    check(count[2] > 50 && count[4] > 50, "2051 有山地也有台地平原");
    check(mean_of(2) > 2.0 * mean_of(4), "山地的细节起伏是台地的两倍以上");

    // 厚岛体：主岛崖壁上百米、岛心垂到云带顶以下一千多米；所有岛底都在云海之上
    const WorldIsland &main = t.island_info(0);
    const IslandBody body = island_body(main);
    check(body.wall_m >= 100.0 && body.keel_m > 2500.0 && body.keel_m < 4500.0, "主岛：崖壁 ≥ 100 m、龙骨 2.5–4.5 km（" + std::to_string(body.wall_m) + " / " + std::to_string(body.keel_m) + "）");
    const IslandBody tiny = island_body(t.island_info(int(sites.islands.size()) - 1));
    check(tiny.keel_m < body.keel_m * 0.5 && tiny.wall_m >= 30.0, "小岛薄些，但崖壁也有 30 m 起");
    // 岛心（最深岸距处）
    size_t core = 0;
    for (size_t k = 0; k < g.shore.size(); k++) {
        if (g.island[k] == 0 && g.shore[k] > g.shore[core]) {
            core = k;
        }
    }
    const double cx = g.x0 + double(core % size_t(g.cols)) * g.res, cz = g.z0 + double(core / size_t(g.cols)) * g.res;
    const TerrainSample deep = t.sample(cx, cz);
    std::printf("  主岛：崖壁 %.0f m、龙骨 %.0f m；岛心顶 %.0f m、底 %.0f m（厚 %.0f m）；全群最低 %.0f m\n", body.wall_m, body.keel_m, deep.top, deep.bottom, deep.top - deep.bottom, t.lowest_bottom());
    check(deep.top - deep.bottom > 3000.0, "主岛岛心厚三千米以上");
    constexpr double CLOUD_SEA_Y = -3800.0; // game/world_2051.tscn 的 CloudSea
    check(t.lowest_bottom() > CLOUD_SEA_Y + 100.0, "所有岛底都在云海之上一百米以外（" + std::to_string(t.lowest_bottom()) + "）");
    // 崖脚：离岸线二十米处岛底还贴着崖壁下沿
    int near_edge = 0, near_ok = 0;
    for (int k = 0; k < 400; k++) {
        const size_t cell = size_t(k) * 997 % g.shore.size();
        if (g.island[cell] != 0 || g.shore[cell] < 50.0f || g.shore[cell] > 150.0f) {
            continue;
        }
        const double x = g.x0 + double(cell % size_t(g.cols)) * g.res, z = g.z0 + double(cell / size_t(g.cols)) * g.res;
        const TerrainSample s = t.sample(x, z);
        near_edge++;
        near_ok += s.top - s.bottom > body.wall_m && s.top - s.bottom < body.wall_m + 0.3 * body.keel_m ? 1 : 0;
    }
    check(near_edge > 0 && near_ok == near_edge, "近岸处厚度 = 崖壁再多一点（" + std::to_string(near_ok) + " / " + std::to_string(near_edge) + "）");

    // 河用生成器的河道：中线落在河道格上的点，平岸宽不窄于生成器的一半（源头渐宽）、水面不高于生成器的 河床 + 水深
    int on = 0, narrow = 0, above = 0;
    for (const RiverStem &stem : t.get_rivers().get_stems()) {
        for (size_t i = 1; i + 1 < stem.v.size(); i++) {
            const RiverVertex &v = stem.v[i];
            const size_t k = g.nearest_cell(v.x, v.z);
            if (g.river_width[k] <= 0.0f || v.s < 200.0) {
                continue;
            }
            on++;
            narrow += v.width < 0.7 * g.river_width[k] ? 1 : 0;
            above += v.surface > double(g.height[k]) + g.river_depth[k] + 1.5 ? 1 : 0;
        }
    }
    std::printf("  河中线落在河道格上的点 %d：比生成器窄三成以上 %d、水面高出生成器 1.5 m 以上 %d\n", on, narrow, above);
    check(on > 10000, "河网的中线大都走在生成器的河道上");
    check(narrow < on / 50, "平岸宽跟得上生成器的河宽");
    check(above < on / 20, "水面贴着生成器下切出的河床");
}

// ---------------------------------------------------------------- 码头几何与铺村（两边共用的纯算法）

// 圆岛：半径 200 m，东边泊位旁有一根细岬（宽 3 m，伸到 x = 225），地面起伏
class RoundDockGround : public DockGround {
public:
    bool land(double p_x, double p_z) const override {
        if (std::hypot(p_x, p_z) <= 200.0) {
            return true;
        }
        return p_x > 0.0 && p_x < 225.0 && std::abs(p_z - 20.0) < 1.5; // 岬
    }
    bool ground(double p_x, double p_z, double &r_y) const override {
        if (!land(p_x, p_z)) {
            return false;
        }
        r_y = 30.0 + 4.0 * std::sin(p_x * 0.07) + 3.0 * std::cos(p_z * 0.05);
        return true;
    }
};

void test_dock_site() {
    std::printf("dock_site\n");
    const RoundDockGround ground;
    DockRules rules;
    rules.step_m = 0.5;
    rules.back_m = 1.0;
    rules.lattice_m = 1.0;
    const DockPlan plan = plan_dock(ground, 0.0, 0.0, 1.0, 0.0, 300.0, rules);
    check(plan.ok, "找到岸缘点");
    check(plan.x > 195.0 && plan.x <= 200.0 && std::abs(plan.z) < 1e-9, "岸缘点落在圆岸上，不在细岬上（x = " + std::to_string(plan.x) + "）");
    check(plan.x + plan.pier_m >= 224.5 + 3.0 - 1e-6, "栈桥伸过泊位旁的细岬（最后一块陆地在 x = 224.5）再留 3 m（" + std::to_string(plan.pier_m) + " m）");
    double bx = 0.0, by = 0.0, bz = 0.0;
    dock_berth(plan, 0.15, 0.1, bx, by, bz);
    bool clear = true;
    for (double s = -rules.berth_half_length_m; s <= rules.berth_half_length_m; s += 0.5) {
        for (double d = 0.0; d <= 3.0; d += 0.5) { // 船舷往外 3 m 的一条
            clear = clear && !ground.land(bx + d, bz + s);
        }
    }
    check(clear, "泊位上船身所在的一条（沿岸 ±30 m）碰不到岸");
    check_near(dock_pad_weight(plan, plan.x, plan.z), 1.0, 1e-12, "岸缘点全平");
    check_near(dock_pad_weight(plan, plan.x - 0.5 * plan.apron_m, plan.z), 1.0, 1e-12, "半个半径处仍全平");
    check_near(dock_pad_weight(plan, plan.x - plan.apron_m, plan.z), 0.0, 1e-12, "一个半径外回到原地形");
    double sum = 0.0;
    int n = 0;
    for (int i = -20; i <= 20; i++) {
        for (int j = 150; j <= 230; j++) {
            double y = 0.0;
            if (std::hypot(j - plan.x, i - plan.z) <= 0.5 * rules.apron_m && ground.land(j, i) && ground.ground(j, i, y)) {
                sum += y;
                n++;
            }
        }
    }
    check_near(plan.y, sum / n, 1e-9, "坪高 = 半个半径内格点地面的平均");
    const DockPlan none = plan_dock(ground, 500.0, 500.0, 1.0, 0.0, 50.0, rules);
    check(!none.ok, "一路都是虚空：找不到岸缘");
}

// 测铺村用的地面：x 方向缓坡 5%；x ∈ [60, 100] 是陡坡（1:1）；(−60, 0) 半径 25 m 是码头的坪；z ∈ [40, 46] 是溪涧；|x|、|z| > 400 没有地
class TestSiteGround : public SiteGround {
public:
    bool site_height(double p_x, double p_z, double &r_y) const override {
        if (std::abs(p_x) > 400.0 || std::abs(p_z) > 400.0) {
            return false;
        }
        r_y = 0.05 * p_x + (std::clamp(p_x, 60.0, 100.0) - 60.0);
        return true;
    }
    SiteBlock site_block(double p_x, double p_z) const override {
        if (p_z >= 40.0 && p_z <= 46.0) {
            return { 0, "压着溪涧了" };
        }
        if (std::hypot(p_x + 60.0, p_z) <= 25.0) {
            return { 1, "码头的停机坪上不能盖" };
        }
        return SiteBlock();
    }
};

void test_village_plan() {
    std::printf("village_plan\n");
    const TestSiteGround ground;
    const SiteRules site;
    const VillageRules rules;
    VillageRequest request;
    request.households = 60;
    request.seat = true;
    request.seed = 7;
    const VillagePlan plan = plan_village(ground, request, site, rules);
    check(plan.wanted == 62 && plan.missing == 0 && plan.layout.size() == 62, "邑治 60 户：60 座民居 + 堂屋 + 仓，全摆下（" + std::to_string(plan.layout.size()) + "）");
    check(plan.layout.at(0).kind == "hall" && std::hypot(plan.layout.at(0).x, plan.layout.at(0).z) < 1e-9, "堂屋落在村心");
    check(plan.layout.at(1).kind == "granary", "仓排第二");
    int houses = 0, on_steep_band = 0;
    bool blocked = false, steep = false, overlap = false, stands = true;
    for (int k = 0; k < plan.layout.size(); k++) {
        const Building &b = plan.layout.at(k);
        const BuildingDef &def = *buildings::by_id(b.kind);
        houses += b.kind == "house";
        double cx[4], cz[4];
        footprint_corners(def, b.x, b.z, b.yaw, 0.0, cx, cz);
        double lo = 1e9, hi = -1e9;
        for (int c = 0; c < 4; c++) {
            blocked = blocked || ground.site_block(cx[c], cz[c]).rank >= 0;
            double y = 0.0;
            ground.site_height(cx[c], cz[c], y);
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
        blocked = blocked || ground.site_block(b.x, b.z).rank >= 0;
        steep = steep || hi - lo > site.max_rise_m;
        on_steep_band += b.x > 62.0 && b.x < 98.0;
        stands = stands && survey_site(ground, def, b.x, b.z, b.yaw, plan.layout, site, k).ok();
        for (int o = 0; o < k; o++) {
            overlap = overlap || footprints_overlap(b, plan.layout.at(o), site.gap_m);
        }
    }
    check(houses == 60, "一户一座民居");
    check(!blocked, "没有一座压着码头的坪或溪涧");
    check(!steep && on_steep_band == 0, "没有一座在陡坡上（占地四角高差都不超过 max_rise，陡坡带上一座没有）");
    check(!overlap, "房子两两不挨着");
    check(stands, "每一座按整份清单重新勘察都站得住");
    check(plan.layout == plan_village(ground, request, site, rules).layout, "同输入，清单一样");
    VillageRequest other = request;
    other.seed = 8;
    check(plan.layout != plan_village(ground, other, site, rules).layout, "换种子，摆法不同");
    // 缓坡上门朝下坡：x 向东升，门朝西
    int downhill = 0, sloped = 0;
    for (const Building &b : plan.layout.get_items()) {
        if (b.kind == "house" && b.x < 50.0) {
            sloped++;
            const double door_x = std::sin(b.yaw), door_z = std::cos(b.yaw);
            downhill += door_x < -0.99 || std::abs(door_z) > 0.99; // 转 90° 那一试门朝南北，也算顺着等高线
        }
    }
    check(sloped > 20 && downhill == sloped, "缓坡上的民居都顺着等高线摆（" + std::to_string(downhill) + " / " + std::to_string(sloped) + "）");
    // 散户：1 户就 1 座，没有堂屋
    VillageRequest hamlet;
    hamlet.x = 200.0;
    hamlet.z = -200.0;
    hamlet.households = 1;
    const VillagePlan one = plan_village(ground, hamlet, site, rules);
    check(one.layout.size() == 1 && one.layout.at(0).kind == "house", "散户 1 户：一座民居");
    // 地方不够：摆不下的如数报出
    VillageRules tight = rules;
    tight.max_radius_m = 30.0;
    request.households = 40;
    const VillagePlan cramped = plan_village(ground, request, site, tight);
    check(cramped.missing > 0 && cramped.layout.size() + cramped.missing == 42, "半径太小：摆不下的如数报出（" + std::to_string(cramped.missing) + "）");
}

void test_world_sites() {
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        std::printf("world_sites（跳过：没有 2051 资产）\n");
        return;
    }
    std::printf("world_sites（2051）\n");
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err), "terrain.bin 能解析");
    check(parse_world_sites(tsv, sites, err), "sites.tsv 能解析：" + err);
    check(sites.islands.size() == 32 && sites.docks.size() == 60 && sites.settlements.size() == 1202, "32 岛、60 码头、211 村（含专业聚落 51）+ 991 散户");
    const SiteSettlement *seat = sites.village_by_id(1);
    check(seat && seat->seat && seat->households == 407 && seat->name == "村001", "村001 是邑治、407 户（农 327 + 邑里的市户 80）");
    const SiteDock *main_dock = sites.dock_by_id(1);
    check(main_dock && main_dock->main && main_dock->island == 0, "主码头 #1 在主岛");
    WorldSites bad;
    check(!parse_world_sites("dock\t1\tx\t0\t0\t0\n", bad, err), "坏行报错");

    WorldTerrain t(g, sites.terrain_islands(), 2051);
    const WorldWorks works = lay_world_works(t, sites);
    const std::vector<DockPlan> &docks = works.docks;
    int ok = 0;
    for (const DockPlan &d : docks) {
        ok += d.ok;
    }
    check(ok == 60, "60 个码头都找到岸缘（" + std::to_string(ok) + "）");
    // 主码头 #1 与岛 #2 上的目的码头 #26：压平后泊位附近的地面等高（这两处没有河从坪上过；有河的坪让给河）
    for (int id : { 1, 26 }) {
        size_t k = 0;
        while (sites.docks[k].id != id) {
            k++;
        }
        const DockPlan &d = docks[k];
        const std::string tag = "码头 #" + std::to_string(id);
        check(std::hypot(d.x - sites.docks[k].x, d.z - sites.docks[k].z) < 150.0, tag + "：岸缘点离生成器的点不到 150 m");
        double worst = 0.0;
        int samples = 0;
        for (double along = -0.45 * d.apron_m; along <= 0.0; along += 2.0) {
            for (double side = -8.0; side <= 8.0; side += 2.0) {
                const double x = d.x + along * d.out_x - side * d.out_z, z = d.z + along * d.out_z + side * d.out_x;
                const TerrainSample s = t.sample(x, z);
                if (s.land && std::hypot(x - d.x, z - d.z) <= 0.5 * d.apron_m) {
                    worst = std::max(worst, std::abs(s.top - d.y));
                    samples++;
                }
            }
        }
        check(samples > 20 && worst < 1e-6, tag + "：栈桥根到岛内半个坪半径，地面与坪同高（最大差 " + std::to_string(worst) + " m）");
        double y = 0.0;
        check(t.surface(d.x - 12.0 * d.out_x, d.z - 12.0 * d.out_z, y) && std::abs(y - d.y) < 1e-6, tag + "：出生点（岸缘内 12 m）在坪上");
        const TerrainSample raw = t.sample_raw(d.x + (d.pier_m + 0.15) * d.out_x, d.z + (d.pier_m + 0.15) * d.out_z);
        check(!raw.land, tag + "：泊位在岸外");
        check(d.y > 100.0 && d.y < 400.0, tag + "：坪高在岸缘一带（" + std::to_string(d.y) + " m）");
    }
    // 目的码头 #26 旁的村126 与邑治（主码头旁）：不压码头的坪、不在陡坡上
    const WorldVillageRules rules;
    for (int id : { 126, 1 }) {
        const SiteSettlement *v = sites.village_by_id(id);
        const VillagePlan plan = plan_world_village(t, *v, 2051, rules);
        const std::string tag = v->name;
        check(plan.missing == 0 && plan.layout.size() == plan.wanted, tag + "：全摆下（" + std::to_string(plan.layout.size()) + " 座）");
        const WorldGround map(t, v->x, v->z);
        const MapSiteGround ground(map, rules.site);
        bool on_pad = false, steep = false;
        for (const Building &b : plan.layout.get_items()) {
            const BuildingDef &def = *buildings::by_id(b.kind);
            const SiteReport r = survey_site(ground, def, b.x, b.z, b.yaw, SettlementLayout(), rules.site);
            steep = steep || r.ground_max - r.ground_min > rules.site.max_rise_m;
            for (const DockPlan &d : docks) {
                on_pad = on_pad || std::hypot(v->x + b.x - d.x, v->z + b.z - d.z) < d.apron_m + 0.5 * std::hypot(def.width_m, def.depth_m);
            }
        }
        check(!on_pad, tag + "：没有一座压着码头的坪");
        check(!steep, tag + "：没有一座在陡坡上");
        check(plan.layout == plan_world_village(t, *v, 2051, rules).layout, tag + "：同输入同清单");
    }
}

// ---------------------------------------------------------------- 地图地面：三种地面，同一套规矩

// 一张地图上的几个探点：水里、停机坪上、船坞坪上、离水远的生地（不是熟地）；p_water_x/z 是水中线上的一点
struct GroundProbe {
    std::string name;
    const MapGround *ground = nullptr;
    double water_x = 0.0, water_z = 0.0;
    double pad_x = 0.0, pad_z = 0.0;
    double yard_x = 0.0, yard_z = 0.0;
    double dry_x = 0.0, dry_z = 0.0;
};

// 格子 (i, j) 按规矩该有的墒情：四角离水平均距离的线性衰减（熟地另算）
double expected_moisture(const MapGround &p_ground, const PlotLattice &p_l, int p_i, int p_j, const FarmRules &p_rules) {
    double dist = 0.0;
    for (int di = 0; di <= 1; di++) {
        for (int dj = 0; dj <= 1; dj++) {
            PlotNode node;
            p_ground.plot_node(p_l, p_i + di, p_j + dj, node);
            dist += 0.25 * node.water_m;
        }
    }
    const double near = dist <= p_rules.max_river_m ? std::clamp(1.0 - dist / p_rules.max_river_m, 0.0, 1.0) : 0.0;
    return std::max(near, p_ground.fertile(p_l.center_x(p_j), p_l.center_z(p_i)) ? p_rules.fertile_moisture : 0.0);
}

void check_probe(const GroundProbe &p) {
    const MapGround &g = *p.ground;
    const SiteRules site;
    const FarmRules farm;
    const PlotLattice l = g.plot_lattice();
    const std::string tag = p.name + "：";
    auto plot_at = [&](double x, double z) {
        int i = 0, j = 0;
        l.cell_of(x, z, i, j);
        return survey_plot(g, l, i, j, farm).land;
    };
    // 禁区：同一种处境给出同一条原因
    check(ground_block(g, p.water_x, p.water_z, site).rank == 0, tag + "水里不能盖（" + std::string(ground_block(g, p.water_x, p.water_z, site).reason ? ground_block(g, p.water_x, p.water_z, site).reason : "—") + "）");
    check(std::string(ground_block(g, p.pad_x, p.pad_z, site).reason ? ground_block(g, p.pad_x, p.pad_z, site).reason : "") == "停机坪上不能盖", tag + "停机坪上不能盖");
    check(std::string(ground_block(g, p.yard_x, p.yard_z, site).reason ? ground_block(g, p.yard_x, p.yard_z, site).reason : "") == "这是船坞的坪，盖不得", tag + "船坞坪上不能盖");
    check(ground_block(g, p.dry_x, p.dry_z, site).rank < 0, tag + "生地不是禁区");
    // 可耕：同一种处境同一个结论
    check(plot_at(p.water_x, p.water_z) == Arability::WATER, tag + "水里不能垦");
    check(plot_at(p.pad_x, p.pad_z) == Arability::DOCK, tag + "停机坪上不能垦");
    check(plot_at(p.yard_x, p.yard_z) == Arability::YARD, tag + "船坞坪上不能垦");
    check(plot_at(p.dry_x, p.dry_z) == Arability::TOO_DRY, tag + "离水远的生地引不到水");
    // 水边一圈：能垦的格子墒情都按同一条算式，且水边确有能垦的地
    int i0 = 0, j0 = 0;
    l.cell_of(p.water_x, p.water_z, i0, j0);
    int arable = 0, law_ok = 0;
    for (int i = i0 - 12; i <= i0 + 12; i++) {
        for (int j = j0 - 12; j <= j0 + 12; j++) {
            const PlotSurvey s = survey_plot(g, l, i, j, farm);
            if (s.land != Arability::ARABLE) {
                continue;
            }
            arable++;
            law_ok += std::abs(double(s.moisture) - expected_moisture(g, l, i, j, farm)) < 1e-6 ? 1 : 0;
        }
    }
    check(arable > 0 && law_ok == arable, tag + "水边有能垦的地，墒情都按离水距离算（" + std::to_string(law_ok) + " / " + std::to_string(arable) + "）");
    std::vector<WaterLine> lines;
    g.water_lines(p.water_x - 100.0, p.water_z - 100.0, p.water_x + 100.0, p.water_z + 100.0, lines);
    double nearest = 1e18;
    for (const WaterLine &line : lines) {
        for (const WaterPoint &w : line) {
            nearest = std::min(nearest, std::hypot(w.x - p.water_x, w.z - p.water_z));
        }
    }
    check(nearest < 15.0, tag + "挑水的水线就在水上（最近 " + std::to_string(nearest) + " m）");
}

void test_map_grounds() {
    std::printf("map_ground（甲岛、岛群、平板岛同一套规矩）\n");
    // 甲岛：河、码头、船坞坪
    IslandParams p = river_island_params(0.8);
    p.trail = true;
    p.yard = true;
    const IslandField f = generate_island_field(p);
    const IslandGround island(f, p.dock_apron_m);
    GroundProbe a;
    a.name = "甲岛";
    a.ground = &island;
    const RiverPoint &rp = f.river[f.river.size() / 2];
    a.water_x = rp.x;
    a.water_z = rp.z;
    a.pad_x = f.dock.x - 0.5 * p.dock_apron_m * f.dock.out_x;
    a.pad_z = f.dock.z - 0.5 * p.dock_apron_m * f.dock.out_z;
    a.yard_x = f.yard.x;
    a.yard_z = f.yard.z;
    for (int i = 0; i < f.n && a.dry_x == 0.0; i++) { // 离河 60 m 以上、缓坡、不在坪上的一格
        for (int j = 0; j < f.n; j++) {
            const double x = f.coord(j) + 1.25, z = f.coord(i) + 1.25;
            if (f.quad_solid(i, j) && f.river_dist[f.index(i, j)] > 60.0 && !island.on_dock(x, z, 10.0) && !island.on_yard(x, z, 10.0) &&
                    !island.on_trail(x, z, 10.0) && std::abs(f.height[f.index(i, j)] - f.height[f.index(i + 1, j + 1)]) < 0.1) {
                a.dry_x = x;
                a.dry_z = z;
                break;
            }
        }
    }
    check_probe(a);

    // 平板岛
    const FlatIsland flat = lay_flat_island(FlatIslandSpec());
    check(flat.dock.ok && flat.yard.present, "平板岛：码头与船坞坪都落得下");
    const FlatGround plate(flat);
    GroundProbe c;
    c.name = "平板岛";
    c.ground = &plate;
    c.water_x = flat.spec.stream_x0;
    c.water_z = 0.0;
    c.pad_x = flat.dock.x - 0.5 * flat.spec.apron_m * flat.dock.out_x;
    c.pad_z = flat.dock.z - 0.5 * flat.spec.apron_m * flat.dock.out_z;
    c.yard_x = flat.yard.x;
    c.yard_z = flat.yard.z;
    c.dry_x = 20.0;
    c.dry_z = -100.0;
    check_probe(c);

    // 岛群：主码头 #7 的坪与船坞坪、一处溪涧、离水远的生地
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        return;
    }
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    parse_world_grid(bin.data(), bin.size(), g, err);
    parse_world_sites(tsv, sites, err);
    WorldTerrain t(g, sites.terrain_islands(), 2051);
    const WorldWorks works = lay_world_works(t, sites);
    size_t main_k = 0;
    while (main_k < sites.docks.size() && !sites.docks[main_k].main) {
        main_k++;
    }
    const DockPlan &dock = works.docks[main_k];
    const YardPlan &yard = works.yards[main_k];
    check(yard.present, "岛群：主码头 #" + std::to_string(sites.docks[main_k].id) + " 旁凿得出船坞坪");
    int yard_flat = 0, yard_samples = 0;
    for (double u = -0.5 * yard.length; u <= 0.5 * yard.length; u += 5.0) {
        for (double v = -0.5 * yard.width; v <= 0.5 * yard.width; v += 4.0) {
            double x = 0.0, z = 0.0, y = 0.0;
            yard.from_local(u, v, x, z);
            yard_samples++;
            yard_flat += t.surface(x, z, y) && std::abs(y - yard.y) < 1e-9 ? 1 : 0;
        }
    }
    check(yard_samples > 50 && yard_flat == yard_samples, "岛群：船坞坪整块是地、全平、与停机坪同高（" + std::to_string(yard_flat) + " / " + std::to_string(yard_samples) + "）");
    // 村子从主码头附近铺起时的地面：原点放在码头
    const WorldGround world(t, dock.x, dock.z);
    for (double u = -0.5 * yard.length; u <= 0.5 * yard.length; u += 10.0) {
        double x = 0.0, z = 0.0;
        yard.from_local(u, 0.0, x, z);
        check(world.water_at(x - dock.x, z - dock.z) == WaterKind::NONE, "岛群：船坞坪不压溪涧与水面");
    }
    GroundProbe b;
    b.name = "岛群";
    b.ground = &world;
    b.pad_x = -0.5 * dock.apron_m * dock.out_x;
    b.pad_z = -0.5 * dock.apron_m * dock.out_z;
    b.yard_x = yard.x - dock.x;
    b.yard_z = yard.z - dock.z;
    // 离码头最近的一处溪涧（格心在陆地上、四周也是地、不是河道湖泊）
    double best = 1e18;
    for (int r = 1; r < g.rows - 1; r++) {
        for (int c = 1; c < g.cols - 1; c++) {
            const size_t k = g.index(r, c);
            const double x = g.x0 + c * g.res, z = g.z0 + r * g.res;
            const double d = std::hypot(x - dock.x, z - dock.z);
            if (!g.stream[k] || d >= best || !t.is_land(x, z) || !t.is_land(x + 60, z) || !t.is_land(x - 60, z) || !t.is_land(x, z + 60) || !t.is_land(x, z - 60)) {
                continue;
            }
            if (world.fertile(x - dock.x, z - dock.z) || world.water_at(x - dock.x, z - dock.z) != WaterKind::STREAM) {
                continue;
            }
            best = d;
            b.water_x = x - dock.x;
            b.water_z = z - dock.z;
        }
    }
    check(best < 1e17, "岛群：找得到一处溪涧");
    // 离水远的生地：码头往岛内 200 m 起找
    bool dry_found = false;
    for (double in = 200.0; in < 3000.0 && !dry_found; in += 25.0) {
        const double x = -in * dock.out_x, z = -in * dock.out_z;
        double y0 = 0.0, y1 = 0.0;
        if (world.water_distance(x, z) > 1e17 && !world.fertile(x, z) && world.site_height(x, z, y0) && world.site_height(x + 2.5, z + 2.5, y1) && std::abs(y1 - y0) < 0.3) {
            b.dry_x = x;
            b.dry_z = z;
            dry_found = true;
        }
    }
    check(dry_found, "岛群：找得到离水远的生地");
    check_probe(b);
    // 熟地：可耕地 / 梯田不靠水也能垦，墒情 0.6
    const FarmRules farm_rules;
    const PlotLattice wl = world.plot_lattice();
    bool fertile_ok = false;
    for (double in = 100.0; in < 20000.0 && !fertile_ok; in += 50.0) {
        const double x = -in * dock.out_x, z = -in * dock.out_z;
        if (!world.fertile(x, z) || world.water_distance(x, z) < 1e17) {
            continue;
        }
        int i = 0, j = 0;
        wl.cell_of(x, z, i, j);
        const PlotSurvey s = survey_plot(world, wl, i, j, farm_rules);
        if (s.land == Arability::ARABLE) {
            fertile_ok = std::abs(s.moisture - farm_rules.fertile_moisture) < 1e-6;
            check(fertile_ok, "岛群：熟地墒情 = fertile_moisture");
            FarmModel model(&world, wl, farm_rules);
            check(model.any_arable_near(x, z, 10.0) && model.available_action(i, j, false) == FarmAction::TILL, "岛群：农田模型按需勘察，熟地能开垦");
        }
    }
    check(fertile_ok, "岛群：离水远的熟地照样能垦");
    // 地块格与岛群坐标对齐：从码头看与从岛群原点看，同一块地是同一格
    const WorldGround origin(t, 0.0, 0.0);
    int i1 = 0, j1 = 0, i2 = 0, j2 = 0;
    wl.cell_of(b.water_x + 7.0, b.water_z + 3.0, i1, j1);
    origin.plot_lattice().cell_of(dock.x + b.water_x + 7.0, dock.z + b.water_z + 3.0, i2, j2);
    check(i1 == i2 && j1 == j2, "岛群：换个原点，同一块地还是同一格");
}

void test_layout_edits() {
    std::printf("清单的改动（加 / 拆）\n");
    const SettlementLayout base({ { "house", 10.123, 4.5, 0.3 }, { "hall", 0.0, 0.0, 0.0 }, { "house", -20.0, 7.25, 1.2 } });
    SettlementLayout now = base;
    now.erase(0);
    now.add({ "tower", 30.0, 30.0, 0.0 });
    now.add({ "granary", -5.5, 18.0, 1.5707963 });
    const LayoutEdits edits = diff_layouts(base, now);
    check(edits.removed.size() == 1 && edits.added.size() == 2, "改了什么：拆一座、加两座");
    check(settlement_ids(apply_layout_edits(base, edits)) == settlement_ids(now), "底 + 改动 = 现在");
    const std::vector<std::string> lines = layout_edit_lines(edits);
    check(lines.size() == 3 && lines[0][0] == '-', "拆写成 -kind@…，加不带符号");
    bool ok = false;
    const LayoutEdits back = parse_layout_edits(lines, &ok);
    check(ok && settlement_ids(apply_layout_edits(base, back)) == settlement_ids(now), "写出再读回，叠上去一样");
    check(diff_layouts(base, base).empty(), "没改就没有改动");
    int unmatched = 0;
    LayoutEdits stale;
    stale.removed.push_back("house@99.00,99.00,0.0");
    check(apply_layout_edits(base, stale, &unmatched).size() == 3 && unmatched == 1, "底里没有的那座拆不了，如数报出");
    // 旧的整份清单文件：底是空的，全是加
    const std::vector<std::string> old_file = { "# 聚落清单", "house@125.50,-66.00,103.9", "", "tower@104.00,-46.00,103.9" };
    const SettlementLayout from_old = apply_layout_edits(SettlementLayout(), parse_layout_edits(old_file, &ok));
    check(ok && from_old == settlement_from_ids(old_file), "旧清单文件照样读");
    check(layout_edit_lines(diff_layouts(SettlementLayout(), from_old)) == std::vector<std::string>({ "house@125.50,-66.00,103.9", "tower@104.00,-46.00,103.9" }), "空底上存回去，文件与旧格式一模一样");
    check(!ok || parse_layout_edits({ "-palace@1,2,3" }, &ok).empty(), "认不出的行跳过");
    check(!ok, "认不出时 r_ok 为假");
}

// ---------------------------------------------------------------- 河：河网、水情、凿河道、地面问水、水面网格（2051）

void test_world_rivers() {
    // v1 的栅格没有水文：河网是空的，地形照旧
    {
        const WorldGrid g = synthetic_grid();
        const WorldTerrain t(g, {}, 7);
        check(!g.has_hydro() && !t.has_rivers(), "v1 栅格没有水文、没有河网");
    }
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary), wx_in(dir + "/weather.tsv", std::ios::binary);
    if (!bin_in || !tsv_in || !wx_in) {
        std::printf("world_rivers（跳过：没有 2051 资产）\n");
        return;
    }
    std::printf("world_rivers（2051）\n");
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    const std::string weather((std::istreambuf_iterator<char>(wx_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err) && g.has_hydro(), "terrain.bin v2 带水文：" + err);
    parse_world_sites(tsv, sites, err);

    // 水情
    RiverRegime regime;
    check(RiverRegime::parse(weather, regime, err), "weather.tsv 能解析：" + err);
    check(regime.get_daily().size() == 336 && std::abs(regime.precip_mm() - 901.0) < 1.0, "一年 336 天、年降水 901 mm");
    double mean = 0.0, lo = 1e9, hi = 0.0;
    for (double r : regime.get_daily()) {
        mean += r / double(regime.get_daily().size());
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    check_near(mean, 1.0, 1e-9, "径流指数全年平均 = 1");
    check(lo < 0.3 && hi > 3.0, "有枯水也有涨水（" + std::to_string(lo) + " .. " + std::to_string(hi) + "）");
    check_near(regime.runoff(336.0 + 10.5), regime.get_daily()[10], 1e-9, "按年循环、日值在正午");
    check(std::abs(regime.runoff(335.99) - regime.runoff(0.01)) < 0.5 * (hi - lo), "跨年不跳（年末与年初之间线性过渡）");
    RiverRegime bad;
    check(!RiverRegime::parse("# year_days 3\n0\t1\n2\t1\n", bad, err), "日序断了报错");

    RiverRules rules;
    rules.precip_mm = regime.precip_mm();
    rules.year_days = regime.year_days();
    const WorldTerrain t(g, sites.terrain_islands(), 2051, rules);
    check(t.has_rivers(), "2051 有河网");
    const WorldRivers &rivers = t.get_rivers();
    int stems = 0, mouths = 0, rising = 0, drying_acc = 0, off_parent = 0;
    const RiverStem *trunk = nullptr;
    for (const RiverStem &stem : rivers.get_stems()) {
        if (stem.v.empty()) {
            continue;
        }
        stems++;
        mouths += stem.mouth;
        if (!trunk || stem.v.back().acc > trunk->v.back().acc) {
            trunk = &stem;
        }
        for (size_t i = 0; i + 1 < stem.v.size(); i++) {
            rising += stem.v[i + 1].surface > stem.v[i].surface + 1e-9;
            drying_acc += stem.v[i + 1].acc < stem.v[i].acc - 1e-6;
        }
        if (stem.parent >= 0) {
            const RiverStem &parent = rivers.get_stems()[size_t(stem.parent)];
            double best = 1e18;
            for (size_t i = 0; i + 1 < parent.v.size(); i++) {
                const double dx = parent.v[i + 1].x - parent.v[i].x, dz = parent.v[i + 1].z - parent.v[i].z;
                const double len2 = dx * dx + dz * dz;
                const double u = len2 > 0 ? std::clamp(((stem.v.back().x - parent.v[i].x) * dx + (stem.v.back().z - parent.v[i].z) * dz) / len2, 0.0, 1.0) : 0.0;
                best = std::min(best, std::hypot(stem.v.back().x - parent.v[i].x - u * dx, stem.v.back().z - parent.v[i].z - u * dz));
            }
            off_parent += best > 0.5;
        }
    }
    check(stems > 400 && mouths > 100, "河 " + std::to_string(stems) + " 条、出岸缘 " + std::to_string(mouths) + " 条");
    check(rivers.total_length() > 900e3, "河网总长 > 900 km（" + std::to_string(int(rivers.total_length() / 1000)) + " km）");
    check(rising == 0, "平岸水面顺流不升（" + std::to_string(rising) + " 处）");
    check(drying_acc == 0, "汇流面积顺流不减（" + std::to_string(drying_acc) + " 处）");
    check(off_parent == 0, "支流末点都落在干流中线上（" + std::to_string(off_parent) + " 条没有）");
    // 最大的河：下游宽过上游，宽窄在一条河里有变化
    const RiverVertex &src = trunk->v.front(), &end = trunk->v.back();
    check(end.width > 2.0 * src.width && end.depth > src.depth, "干流越往下越宽越深（" + std::to_string(src.width) + " → " + std::to_string(end.width) + " m）");
    check(end.dry < src.dry, "大河的渗漏门槛低于源头");
    check(trunk->mouth, "干流从岸缘跌下去");
    check(t.get_grid().shore_at(end.x, end.z) + WorldTerrain::COAST_AMP_M * t.coast_noise(end.x, end.z) < 1.0, "河口截在岸线上");

    // 凿河道：中线上的地面就是河床；年均水位时水面半宽内低于水位、外高于水位
    int bed_off = 0, dry_bank = 0, samples = 0;
    for (size_t i = trunk->v.size() / 4; i + 1 < trunk->v.size() * 3 / 4; i += 25) {
        const RiverVertex &v = trunk->v[i];
        RiverHit hits[WorldRivers::MAX_HITS];
        const int n = rivers.near(v.x, v.z, hits, WorldRivers::MAX_HITS);
        if (n != 1) {
            continue; // 汇流口附近两条河叠着，跳过
        }
        samples++;
        const TerrainSample s = t.sample_raw(v.x, v.z);
        bed_off += std::abs(s.top - v.bed()) > 0.05;
        const RiverStage mean_water = river_stage(v, 1.0, rules);
        double lx = -(trunk->v[i + 1].z - v.z), lz = trunk->v[i + 1].x - v.x;
        const double ll = std::hypot(lx, lz);
        lx /= ll, lz /= ll;
        // 按取样点自己最近的中线点定水（山里比降大，侧向一点最近的中线点可能在上游几米、水位高一截）
        auto wet_ok = [&](double p_offset, bool p_inside) {
            const double x = v.x + lx * p_offset, z = v.z + lz * p_offset;
            RiverHit h;
            if (!rivers.nearest(x, z, rivers.max_reach(), h) || h.stem != int(trunk - &rivers.get_stems()[0])) {
                return true; // 最近的是别的河（汇流口一带），不算
            }
            const RiverStage local = river_stage(h.at, 1.0, rules);
            const double top = t.sample_raw(x, z).top;
            const bool ok = p_inside ? (h.distance >= local.half_width || top < local.level) : (h.distance <= local.half_width || top > local.level);
            if (!ok) {
                std::printf("  水面半宽不对：(%.0f, %.0f) 离中线 %.1f m，水位 %.2f、半宽 %.1f m，地面 %.2f\n", x, z, h.distance, local.level, local.half_width, top);
            }
            return ok;
        };
        dry_bank += !(wet_ok(0.8 * mean_water.half_width, true) && wet_ok(mean_water.half_width + 1.0, false));
    }
    check(samples > 10, "干流上取样够（" + std::to_string(samples) + "）");
    check(bed_off == 0, "中线上的地面就是河床（" + std::to_string(bed_off) + " 处不是）");
    check(dry_bank == 0, "年均水位时水面半宽内是水、外是岸（" + std::to_string(dry_bank) + " 处不对）");

    // 水情落到一点上：小溪先干、大河旱季也有水、涨水漫滩
    check(!river_stage(src, 0.25, rules).wet && river_stage(end, 0.25, rules).wet, "径流 0.25（旱季）：源头的溪干了、干流还有水");
    check(river_stage(end, 0.15, rules).wet, "全年最枯时干流也有水");
    const RiverStage low = river_stage(end, 0.5, rules), high = river_stage(end, 3.0, rules), flood = river_stage(end, 6.5, rules);
    check(low.level < high.level && low.half_width < high.half_width, "水涨了：水位高、水面宽");
    check(flood.half_width > end.half() && flood.half_width <= end.core() + 1e-9, "大水漫上河漫滩、不出滩");

    // 地面问水：河槽是水、滩上不是水但不盖房；挑水线沿河
    const WorldGround ground(t, 0.0, 0.0);
    const size_t m = trunk->v.size() / 2;
    const RiverVertex &mid = trunk->v[m];
    check(ground.water_at(mid.x, mid.z) == WaterKind::STREAM, "中线上是水");
    double lx = -(trunk->v[m + 1].z - mid.z), lz = trunk->v[m + 1].x - mid.x;
    const double ll = std::hypot(lx, lz);
    lx /= ll, lz /= ll;
    const double on_bank = mid.half() + 0.5 * mid.floodplain;
    const double bx = mid.x + lx * on_bank, bz = mid.z + lz * on_bank;
    check(ground.water_at(bx, bz) == WaterKind::NONE && ground.on_floodplain(bx, bz), "河漫滩上不是水、但算滩");
    const SiteBlock block = ground_block(ground, bx, bz, SiteRules());
    check(block.reason && std::string(block.reason) == "这是河滩，涨水会淹，盖不得", "滩上不盖房，说得出为什么");
    const double far = mid.core() + rules.blend_m + 30.0;
    check(!ground.on_floodplain(mid.x + lx * far, mid.z + lz * far) && ground.water_at(mid.x + lx * far, mid.z + lz * far) == WaterKind::NONE, "滩外是干地");
    check(ground.water_distance(mid.x + lx * 5.0, mid.z + lz * 5.0) < 5.5, "离水距离量到河中线");
    check(ground.water_depth(mid.x, mid.z) > 0.1, "河心有水深（导航格按它蹚水）");
    std::vector<WaterLine> lines;
    ground.water_lines(mid.x - 100.0, mid.z - 100.0, mid.x + 100.0, mid.z + 100.0, lines);
    check(!lines.empty() && lines.front().size() >= 2, "框里有挑水的线");

    // 水面网格：块里有带子，顶点属性齐；河口有跌水帘
    const std::vector<RiverChunkKey> chunks = river_chunks(t);
    check(chunks.size() > 1000, "有河的块 " + std::to_string(chunks.size()));
    const RiverMeshRules mesh_rules;
    const RiverChunkKey key{ int(std::floor(mid.x / mesh_rules.chunk_m)), int(std::floor(mid.z / mesh_rules.chunk_m)) };
    const RiverMesh mesh = build_river_mesh(t, key);
    const size_t verts = mesh.positions.size() / 3;
    check(!mesh.empty() && mesh.indices.size() % 3 == 0, "干流所在的块有水面");
    check(mesh.normals.size() == verts * 3 && mesh.uvs.size() == verts * 2 && mesh.custom0.size() == verts * 4 && mesh.custom1.size() == verts * 4, "顶点属性一一对上");
    int bad_index = 0;
    for (int32_t ix : mesh.indices) {
        bad_index += ix < 0 || size_t(ix) >= verts;
    }
    check(bad_index == 0, "下标都在顶点里");
    const RiverChunkKey mouth_key{ int(std::floor(end.x / mesh_rules.chunk_m)), int(std::floor(end.z / mesh_rules.chunk_m)) };
    const RiverMesh mouth_mesh = build_river_mesh(t, mouth_key);
    bool fall = false;
    for (size_t k = 1; k < mouth_mesh.custom1.size(); k += 4) {
        fall = fall || mouth_mesh.custom1[k] > 0.5f;
    }
    check(fall, "河口块有跌水帘");
}

// ---------------------------------------------------------------- 金标：地图宿主重构前后逐位一致

struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void mix(uint64_t v) { h = (h ^ v) * 1099511628211ull; }
    void mix_d(double v) {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        mix(bits);
    }
    void mix_s(const std::string &s) {
        for (unsigned char c : s) {
            mix(c);
        }
        mix(0xFF);
    }
};

// main.tscn 的出发岛（IslandA）
IslandParams island_a_params() {
    IslandParams p;
    p.seed = 1165;
    p.area_m2 = 120000.0;
    p.age = 0.8;
    p.peak_m = 24.0;
    p.dock_bearing = -18.6 * (3.14159265358979323846 / 180.0); // 与 Math::deg_to_rad 同一算法
    p.river = true;
    p.trail = true;
    p.yard = true;
    return p;
}

SettlementLayout read_layout_file(const std::string &p_path) {
    std::ifstream in(p_path);
    std::vector<std::string> ids;
    for (std::string line; std::getline(in, line);) {
        ids.push_back(line);
    }
    return settlement_from_ids(ids);
}

void report_golden(const char *p_what, uint64_t p_hash, uint64_t p_expected) {
    std::printf("  %s = 0x%016llxull\n", p_what, (unsigned long long)p_hash);
    check(p_hash == p_expected, std::string("金标：") + p_what);
}

void test_goldens() {
    std::printf("金标（地图宿主重构前后逐位一致）\n");
    const IslandParams p = island_a_params();
    const IslandField f = generate_island_field(p);

    Fnv field;
    field.mix(field_hash(f));
    for (double v : { f.yard.x, f.yard.z, f.yard.ux, f.yard.uz, f.yard.vx, f.yard.vz, f.yard.y, f.yard.length, f.yard.width }) {
        field.mix_d(v);
    }
    field.mix(f.yard.present);
    report_golden("出发岛高度场与船坞坪", field.h, 0xd0e8efacdd581979ull);

    Fnv farm;
    const IslandFarm survey(f, p.dock_apron_m, FarmRules());
    for (int i = 0; i < f.n - 1; i++) {
        for (int j = 0; j < f.n - 1; j++) {
            const PlotSurvey plot = survey.model.survey(i, j);
            farm.mix(uint64_t(plot.land));
            if (plot.land == Arability::ARABLE) {
                farm.mix_d(double(plot.moisture));
            }
        }
    }
    report_golden("出发岛农田勘察", farm.h, 0x68431c2ace2c61b6ull);

    Fnv blocks, sites;
    const SiteRules site_rules;
    const IslandGround island_map(f, p.dock_apron_m);
    const MapSiteGround ground(island_map, site_rules);
    const BuildingDef &house = *buildings::by_id("house");
    const double ext = f.half_extent();
    for (double z = -ext; z <= ext; z += 1.3) {
        for (double x = -ext; x <= ext; x += 1.3) {
            const SiteBlock b = ground.site_block(x, z);
            blocks.mix(uint64_t(b.rank + 1));
            if (b.reason) {
                blocks.mix_s(b.reason);
            }
        }
    }
    for (double z = -ext; z <= ext; z += 4.1) {
        for (double x = -ext; x <= ext; x += 4.1) {
            const SiteReport r = survey_site(ground, house, x, z, 0.3, SettlementLayout(), site_rules);
            sites.mix_s(r.reason);
            if (r.ok()) {
                sites.mix_d(r.floor_y);
                sites.mix_d(r.plinth_bottom);
            }
        }
    }
    report_golden("出发岛禁区", blocks.h, 0x1c212dbdec9c0029ull);
    report_golden("出发岛选址勘察", sites.h, 0xd58287e5b2d1ed82ull);

    Fnv nav;
    const SettlementLayout village = read_layout_file(std::string(ZHOUZHU_SOURCE_DIR) + "/game/settlements/island_a.txt");
    const NavGrid grid = island_nav(f, village, NavRules());
    nav.mix(uint64_t(grid.cols));
    nav.mix(uint64_t(grid.rows));
    nav.mix_d(grid.x0);
    nav.mix_d(grid.z0);
    for (size_t k = 0; k < grid.walk.size(); k++) {
        nav.mix(grid.walk[k]);
        nav.mix_d(double(grid.y[k]));
    }
    report_golden("出发岛导航格", nav.h, 0x2a3e1b14d039d760ull);

    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        return;
    }
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites world_sites;
    std::string err;
    parse_world_grid(bin.data(), bin.size(), g, err);
    parse_world_sites(tsv, world_sites, err);
    WorldTerrain t(g, world_sites.terrain_islands(), 2051);
    lay_world_works(t, world_sites);
    // 离主码头（#1，邑治旁）远的几处：目的码头 #26 边的村126、两个普通村、一户散户
    Fnv villages;
    for (const SiteSettlement &s : world_sites.settlements) {
        if ((!s.hamlet && (s.id == 126 || s.id == 40 || s.id == 120)) || (s.hamlet && s.id == 175)) {
            const VillagePlan plan = plan_world_village(t, s, 2051, WorldVillageRules());
            for (const std::string &id : settlement_ids(plan.layout)) {
                villages.mix_s(id);
            }
            villages.mix(uint64_t(plan.missing));
        }
    }
    // 拓展「岛群的河」起地形凿了河道、河滩不许盖房，村落清单有意变了，此处重记（河按游戏尺度放宽后又记了一次；拓展「船台」村里多盖一座船台，又记了一次；
    // 拓展「按新版生成器优化地形」换了下切过河道的栅格、按地形区加减起伏，又记了一次；
    // 重构「按生成器的真实起伏重做 2051」换了地形与全部点位，换了一组村重记；
    // 拓展「按点 / 片 / 散生成资源」换了生成器新版的地表（村周开垦、采场格改裸岩；细节起伏按地表调幅）与码头，又记了一次）
    report_golden("岛群远离主码头的村", villages.h, 0x2617f4ca68eeea4cull);
}

// ---------------------------------------------------------------- 村落的账本与推进（远近同源）

// 平板岛上的一个村（与 flat_map.tscn 同一个：村心 (0, −70)、6 户、有堂屋与仓、西边 60 m 一条溪）：地面、农田、清单、地块格、水、导航格
struct SimBed {
    FlatIsland island;
    FlatGround ground;
    FarmModel farm;
    SettlementLayout layout;
    PlotLattice plots;
    std::vector<WaterLine> water;
    NavGrid nav;
    SimRules rules;
    DayRules day;
    ClaimRegistry taken;
    ModelFields near_fields;

    SimBed() :
            island(lay_flat_island(FlatIslandSpec())), ground(island), farm(&ground, ground.plot_lattice(), FarmRules()), near_fields(farm) {
        const SiteRules site;
        const MapSiteGround site_ground(ground, site);
        VillageRequest request;
        request.x = 0.0;
        request.z = -70.0;
        request.households = 6;
        request.seat = true;
        request.seed = 1;
        layout = plan_village(site_ground, request, site, VillageRules()).layout;
        plots = ground.plot_lattice();
        water = village_water(ground, layout, rules.work);
        nav = build_nav_grid(ground, -110.0, -150.0, int(170.0 / rules.nav.cell_m), int(150.0 / rules.nav.cell_m), layout, rules.nav);
    }
    VillageWorld world(bool p_near, FieldBook *p_fields) {
        VillageWorld w;
        w.layout = &layout;
        w.plots = &plots;
        w.water = &water;
        w.nav = p_near ? &nav : nullptr;
        w.ground = &ground;
        w.fields = p_fields;
        w.taken = &taken;
        w.day = &day;
        w.rules = &rules;
        return w;
    }
    void found(VillageState &r_state, bool p_near, double p_now) {
        Inventory store;
        store.add("millet", 8);
        store.add("bean", 4);
        const bool has_farm = village_has_farm(farm, 0.0, 0.0, layout, rules.work);
        HeldFields held(r_state.held, &farm);
        found_village(r_state, world(p_near, p_near ? static_cast<FieldBook *>(&near_fields) : &held), has_farm, !water.empty(), 2, 40, 77, store, p_now);
    }
    // 近处逐帧：农田先长（与 Farm 节点排在村民前面一致），再推进村子
    void run_near(VillageState &r_state, double p_until, double p_dt = 0.25) {
        VillageWorld w = world(true, &near_fields);
        while (r_state.clock < p_until - 1e-9) {
            const double to = std::min(p_until, r_state.clock + p_dt);
            farm.advance(game_hours(to - r_state.clock, day));
            advance_village(r_state, w, to);
        }
    }
    // 远处每半个游戏小时结一次
    void run_far(VillageState &r_state, double p_until) {
        HeldFields held(r_state.held, &farm);
        VillageWorld w = world(false, &held);
        const double step = game_seconds(0.5, day);
        while (r_state.clock < p_until - 1e-9) {
            advance_village(r_state, w, std::min(p_until, r_state.clock + step));
        }
    }
};

int grain_ledger(const VillageState &p_state, int p_start) {
    return p_start + p_state.tally.harvested - p_state.tally.sown - p_state.tally.eaten;
}

void test_village_sim() {
    std::printf("village_sim\n");
    const DayRules day;
    // 钟点：分界点
    DayMark mark = DayMark::WORK_START;
    check_near(next_day_mark(clock_at(0, 5.0, day), day, &mark), clock_at(0, 6.0, day), 1e-9, "五点之后的分界点是六点出工");
    check(mark == DayMark::WORK_START, "……是出工");
    check_near(next_day_mark(clock_at(0, 11.5, day), day, &mark), clock_at(0, 12.5, day), 1e-9, "正在歇晌那一秒之后是歇晌完");
    check(next_day_mark(clock_at(0, 20.0, day), day, &mark) == clock_at(1, 6.0, day) && mark == DayMark::WORK_START, "夜里之后是第二天出工");
    check(is_meal(DayMark::LUNCH_START) && is_meal(DayMark::WORK_END) && !is_meal(DayMark::WORK_START), "午饭、晚饭");
    check_near(game_hours(day.day_seconds, day), 24.0, 1e-9, "一天 24 个钟头");

    // 远近同源：同一个村、同一本初始账，一个逐帧近处跑、一个远处半小时一结，跑一整天（出工、歇晌、收工、两顿饭）
    const double t0 = clock_at(0, 5.5, day), t1 = clock_at(1, 5.5, day);
    SimBed near_bed, far_bed;
    VillageState near_state, far_state;
    near_bed.found(near_state, true, t0);
    far_bed.found(far_state, false, t0);
    int farmers = 0, carriers = 0;
    for (const PersonState &p : near_state.people) {
        farmers += p.mind.who.job == Job::FARMER ? 1 : 0;
        carriers += p.mind.who.job == Job::CARRIER ? 1 : 0;
    }
    check(near_state.people.size() == far_state.people.size() && farmers > 0 && carriers > 0,
            "平板岛的村：农夫 " + std::to_string(farmers) + "、挑水 " + std::to_string(carriers) + "，共 " + std::to_string(near_state.people.size()) + " 人");
    const int grain0 = village_grain(near_state);
    const auto started = std::chrono::steady_clock::now();
    near_bed.run_near(near_state, t1);
    const double near_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    const auto far_started = std::chrono::steady_clock::now();
    far_bed.run_far(far_state, t1);
    const double far_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - far_started).count();
    const VillageTally &a = near_state.tally, &b = far_state.tally;
    std::printf("  一整天：近处 收 %d 播 %d 吃 %d 饿 %d 挑水 %.1f 担 串门 %d 巡村 %d 找不到路 %d（%.0f ms）\n", a.harvested, a.sown, a.eaten, a.hungry, a.water_in,
            a.done[int(TaskKind::CHAT)], a.done[int(TaskKind::PATROL)], a.path_failures, near_ms);
    std::printf("          远处 收 %d 播 %d 吃 %d 饿 %d 挑水 %.1f 担 串门 %d 巡村 %d（%.1f ms）\n", b.harvested, b.sown, b.eaten, b.hungry, b.water_in,
            b.done[int(TaskKind::CHAT)], b.done[int(TaskKind::PATROL)], far_ms);
    std::printf("          仓里的粮 近 %d / 远 %d，水缸 近 %.1f / 远 %.1f（容量 %.0f），认领 近 %zu / 远 %zu\n", village_grain(near_state), village_grain(far_state),
            near_state.water, far_state.water, near_state.water_cap, near_state.claims.size(), far_state.claims.size());
    auto within = [](double p_a, double p_b, double p_rel, double p_abs) { return std::abs(p_a - p_b) <= std::max(p_abs, p_rel * std::max(std::abs(p_a), std::abs(p_b))); };
    check(a.harvested > 0 && within(a.harvested, b.harvested, 0.2, 4), "一天收的粮：远处结算与逐帧差不出两成");
    check(within(a.sown, b.sown, 0.2, 3), "一天播的种子：差不出两成");
    check(within(village_grain(near_state), village_grain(far_state), 0.2, 4), "一天下来村里的粮：差不出两成");
    check(within(a.water_in, b.water_in, 0.2, 3), "一天挑进缸里的水：差不出两成");
    check(a.eaten == b.eaten && a.hungry == b.hungry, "吃饭按人头，远近一样");
    check(within(a.done[int(TaskKind::FETCH_WATER)], b.done[int(TaskKind::FETCH_WATER)], 0.2, 3), "打水的趟数：差不出两成");
    check(far_ms * 5.0 < near_ms, "远处结算比逐帧省得多");
    check(village_grain(near_state) == grain_ledger(near_state, grain0) && village_grain(far_state) == grain_ledger(far_state, grain0), "粮对得上账：起初 + 收 − 播 − 吃");
    int indoors = 0;
    for (const PersonState &p : far_state.people) {
        indoors += p.phase == FolkPhase::INDOORS ? 1 : 0;
    }
    check(indoors == int(far_state.people.size()), "远处：天亮前全村在屋里");

    // 展开 → 收回 → 再展开：账守恒，地块原样
    SimBed bed;
    VillageState s;
    bed.found(s, true, clock_at(0, 5.9, day));
    bed.run_near(s, clock_at(0, 9.0, day));
    const int g0 = village_grain(s);
    const size_t plots_before = bed.farm.plot_cells().size() / 2;
    std::vector<std::pair<int, Plot>> before;
    for (const Claim &c : s.claims) {
        before.push_back({ c.owner, bed.farm.plot_at(c.i, c.j) });
    }
    check(plots_before > 0 && !s.claims.empty(), "上午三个钟头：农夫垦了 " + std::to_string(plots_before) + " 块地");
    collapse_village(s, &bed.farm);
    check(bed.farm.plot_cells().empty() && s.held.size() == plots_before && !s.deployed, "收回：认领的地连同长势记进账本，农田里没了");
    check(village_grain(s) == g0, "收回：粮一份不少");
    s.arable = survey_arable(bed.farm, 0.0, -70.0, 120.0);
    check(s.arable.any() && !s.arable.arable(s.arable.i0, s.arable.j0), "村子周围能垦的格：溪边有，角上没有");
    const std::string saved = save_village_state(s);
    VillageState loaded;
    std::string error;
    check(load_village_state(saved, loaded, error), "账本存成文本再读回（" + error + "）");
    check(save_village_state(loaded) == saved, "读回的账本再存一遍一字不差");
    {
        VillageWorld w = bed.world(true, &bed.near_fields);
        deploy_village(s, w, &bed.farm);
    }
    bool same = s.held.empty() && bed.farm.plot_cells().size() / 2 == plots_before;
    for (size_t k = 0; k < s.claims.size() && same; k++) {
        const Plot p = bed.farm.plot_at(s.claims[k].i, s.claims[k].j);
        same = p.state == before[k].second.state && p.growth == before[k].second.growth && p.crop == before[k].second.crop;
    }
    check(same, "马上再展开：地块原样放回农田（状态、长势、作物）");
    check(village_grain(s) == g0 && s.deployed, "马上再展开：粮一份不多一份不少");
    bool on_nav = true;
    for (const PersonState &p : s.people) {
        on_nav = on_nav && (p.phase == FolkPhase::INDOORS || bed.nav.walkable_at(p.mind.x, p.mind.z));
        on_nav = on_nav && (p.phase != FolkPhase::WALKING || !p.path.empty());
    }
    check(on_nav, "展开：在外面的都站在导航格上，走着的都有路");

    // 收回、远处过半天、再展开、再近处跑：账一直对得上
    collapse_village(s, &bed.farm);
    bed.run_far(s, clock_at(0, 15.0, day));
    const int held_plots = int(s.held.size());
    {
        VillageWorld w = bed.world(true, &bed.near_fields);
        deploy_village(s, w, &bed.farm);
    }
    check(int(bed.farm.plot_cells().size() / 2) == held_plots && held_plots >= int(plots_before), "远处半天后展开：地块都放回来了（" + std::to_string(held_plots) + " 块）");
    bed.run_near(s, clock_at(0, 16.0, day));
    check(village_grain(s) == grain_ledger(s, grain0), "近 → 远 → 近：粮对得上账（" + std::to_string(village_grain(s)) + "）");
    int out = 0, at_door = 0;
    for (const PersonState &p : s.people) {
        if (p.phase == FolkPhase::INDOORS) {
            continue;
        }
        out++;
        const NavPoint door = building_door(bed.layout.at(p.mind.who.home));
        at_door += std::hypot(p.mind.x - door.x, p.mind.z - door.z) < 3.0 ? 1 : 0;
    }
    check(out * 2 >= int(s.people.size()) && at_door * 3 <= out, "下午展开的村：多半人在外面，在自家门口的不到三成（在外 " + std::to_string(out) + "，门口 " + std::to_string(at_door) + "）");
    check(s.water > 0.0 && s.water <= s.water_cap, "水缸有水、不溢出");

    // 远处连着推进，拆成几段结算与一口气结算一样（结算的粒度不改规矩）
    SimBed chunk_bed, whole_bed;
    VillageState chunked, whole;
    chunk_bed.found(chunked, false, t0);
    whole_bed.found(whole, false, t0);
    chunk_bed.run_far(chunked, t1);
    {
        HeldFields held(whole.held, &whole_bed.farm);
        advance_village(whole, whole_bed.world(false, &held), t1);
    }
    check(save_village_state(chunked) == save_village_state(whole), "远处半小时一结与一口气结一整天：账本一字不差");
}

} // namespace

// 仓库里的房屋模板：game/buildings/<外壳>/<款式>.txt
std::vector<HouseTemplate> shipped_templates(std::vector<std::string> &r_files) {
    std::vector<HouseTemplate> out;
    for (int k = 0; k < buildings::COUNT; k++) {
        const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/buildings/" + buildings::at(k).id;
        for (const char *style : { "sanjian", "zhihu", "nonghu", "citang", "gongsuo", "cang", "wanglou" }) {
            std::ifstream in(dir + "/" + style + ".txt");
            if (!in) {
                continue;
            }
            std::vector<std::string> lines;
            for (std::string line; std::getline(in, line);) {
                lines.push_back(line);
            }
            HouseTemplate t;
            std::vector<std::string> errors;
            const bool ok = parse_template(lines, t, &errors);
            check(ok && errors.empty(), std::string("模板读得进来：") + buildings::at(k).id + "/" + style + (errors.empty() ? "" : "：" + errors[0]));
            if (ok) {
                out.push_back(t);
                r_files.push_back(dir + "/" + style + ".txt");
            }
        }
    }
    return out;
}

// 盒子里挡人的那些有没有一块压着这一点
bool solid_at(const BuildingParts &p_parts, double p_x, double p_y, double p_z) {
    for (const BoxPart &b : p_parts.boxes) {
        if (b.solid && part_contains(b, p_x, p_y, p_z)) {
            return true;
        }
    }
    return false;
}

void test_house_templates() {
    std::printf("房屋模板：文本、室内规矩、走得通、几何\n");
    const BuildingDef &house = *buildings::by_id("house");
    const BuildingDef &tower = *buildings::by_id("tower");

    // 清单里的款式
    Building b;
    check(parse_building_id("house/zhihu@1.50,-2.00,90.0", b) && b.kind == "house" && b.style == "zhihu", "清单行 kind/style@… 读得出款式");
    check(building_id(b) == "house/zhihu@1.50,-2.00,90.0", "款式写回清单行");
    check(parse_building_id("house@1,2,0", b) && b.style.empty(), "不带款式的旧行照读，款式空");
    check(!parse_building_id("house/@1,2,0", b) && !parse_building_id("barn/x@1,2,0", b), "空款式、不认识的外壳都不认");

    // 文本往返
    HouseTemplate t;
    t.id = "house/test";
    t.name = "试 验";
    t.openings.push_back(Opening{ false, WallSide::FRONT, 0.0, 1.3, 0 });
    t.openings.push_back(Opening{ true, WallSide::LEFT, -0.5, 1.2, 0 });
    t.partitions.push_back(Partition{ 1.8, -2.95, 1.8, 2.95, 0 });
    t.doorways.push_back(Doorway{ 1.8, 1.0, 0 });
    t.items.push_back(Furnishing{ "bed", 3.8, -1.9, 0, 0 });
    HouseTemplate back;
    std::vector<std::string> errors;
    check(parse_template(template_lines(t), back, &errors) && back == t && errors.empty(), "模板写成文本再读回来一模一样（名字里有空格也行）");
    check(back.shell() == "house" && back.style() == "test", "id 拆成外壳 / 款式");
    errors.clear();
    check(parse_template({ "id house/x", "item throne 0,0 0", "door up 0 1", "wall 1,2" }, back, &errors) && errors.size() == 3, "认不出的行记下来、跳过");
    check(!parse_template({ "name 无名" }, back) && !parse_template({ "id barn/x" }, back), "没 id、外壳不认识都读不成");
    HouseTemplate tw;
    check(parse_template({ "id tower/t", "ladder 1,1 2 @1" }, tw) && tw.ladders.size() == 1 && tw.ladders[0].level == 1 && tw.ladders[0].quarter == 2, "行尾 @层");
    check(tw.auto_pick, "缺省参与自动挑");
    t.auto_pick = false;
    check(parse_template(template_lines(t), back) && !back.auto_pick && back == t, "auto no 写出去、读回来");

    // 放东西的规矩
    HouseTemplate e = bare_template(house);
    check(interior_report(e, house).ok(), "只有一扇正门的空屋没毛病");
    check(interior_place_item(e, house, "bed", 3.8, -1.9, 0, 0).empty(), "床放在屋角");
    check(interior_place_item(e, house, "chest", 3.8, -1.9, 0, 0) == "压着床了", "压着床放不了");
    check(interior_place_item(e, house, "chest", 5.0, 0.0, 0, 0).find("伸到墙外") != std::string::npos, "伸出墙外放不了");
    check(interior_place_item(e, house, "table", 0.0, 2.4, 0, 0) == "挡着门了", "挡门放不了");
    check(interior_place_item(e, house, "table", 0.0, 0.0, 0, 1).find("只有 1 层") != std::string::npos, "民居没有二层");
    check(interior_place_opening(e, house, true, -3.6, 2.8, 0).empty(), "光标在正面墙边：开窗");
    check(e.openings.back().side == WallSide::FRONT && std::abs(e.openings.back().at + 3.6) < 1e-9, "窗开在正面 x = −3.6");
    check(interior_place_opening(e, house, true, -3.0, 2.8, 0).find("挨得太近") != std::string::npos, "紧挨着的窗开不了");
    check(interior_place_opening(e, house, false, 0.0, 0.0, 0).find("墙边") != std::string::npos, "屋当中开不了门");
    check(interior_place_opening(e, house, true, 5.1, 2.5, 0).find("墙角") != std::string::npos, "离墙角太近开不了窗");
    // 隔墙：两点取直、接到墙上
    check(interior_place_wall(e, house, -1.8, -2.8, -1.7, 2.8, 0).empty(), "隔墙");
    const Partition &p = e.partitions.back();
    check(p.x0 == p.x1 && std::abs(p.x0 + 1.8) < 1e-9 && std::abs(p.z0 + 2.95) < 1e-9 && std::abs(p.z1 - 2.95) < 1e-9, "取直成竖墙、两头接到前后墙上");
    check(interior_place_wall(e, house, 0.5, 0.0, 1.0, 0.1, 0).find("太短") != std::string::npos, "太短的隔墙不要");
    check(interior_place_wall(e, house, 3.8, -2.95, 3.8, 0.0, 0) == "穿过床了", "隔墙穿不过床");
    InteriorReport r = interior_report(e, house);
    check(!r.ok() && r.unreachable_m2[0] > 5.0, "西间被隔死：报走不到");
    check(interior_place_opening(e, house, true, -1.8, 1.0, 0) == "隔墙上不开窗", "隔墙上不开窗");
    check(interior_place_opening(e, house, false, -1.85, 1.0, 0).empty() && e.doorways.size() == 1, "隔墙上开门洞");
    check(interior_report(e, house).ok(), "开了门洞就都走得到");
    InteriorHit hit = interior_find(e, house, -1.8, 1.1, 0);
    check(hit.kind == InteriorHit::DOORWAY && interior_hit_name(e, hit) == "门洞", "点中门洞");
    hit = interior_find(e, house, -1.8, -2.0, 0);
    check(hit.kind == InteriorHit::PARTITION && interior_remove(e, hit) && e.partitions.empty() && e.doorways.empty(), "拆隔墙，墙上的门洞跟着拆");
    hit = interior_find(e, house, 0.0, 2.9, 0);
    check(hit.kind == InteriorHit::OPENING && interior_remove(e, hit), "点中正门、拆掉");
    r = interior_report(e, house);
    check(std::any_of(r.problems.begin(), r.problems.end(), [](const std::string &p_text) { return p_text.find("没有门") != std::string::npos; }), "没门：报进不去");

    // 梯子与多层
    HouseTemplate w = bare_template(tower);
    check(interior_place_ladder(w, tower, -1.5, -1.3, 0, 0).empty(), "望楼底层搭梯");
    check(interior_place_ladder(w, tower, 1.5, 1.3, 2, 2).find("顶层") != std::string::npos, "顶层没法再往上搭");
    check(interior_place_item(w, tower, "bed", -1.5, -0.6, 0, 1) == "挡着梯口了", "楼上梯口不能放东西");
    check(interior_place_item(w, tower, "chest", -1.5, -0.7, 0, 0) == "挡着梯脚了", "梯脚不能放东西");
    r = interior_report(w, tower);
    check(!r.ok() && r.unreachable_m2[1] < 0.1 && r.unreachable_m2[2] > 5.0, "二层顺梯子走得到，三层还上不去");
    check(interior_place_ladder(w, tower, 1.5, 1.3, 2, 1).empty() && interior_report(w, tower).ok(), "再搭一段就都走得到");

    // 几何
    const HouseTemplate s = bare_template(house);
    auto flat = [](double, double) { return 0.5; }; // 地面比地坪低半米
    const BuildingParts parts = building_parts(house, s, 1.1, flat);
    check(!solid_at(parts, 0.0, 1.2, 3.1), "正门那儿（墙厚里）没有挡人的");
    check(solid_at(parts, 2.0, 1.2, 3.1) && solid_at(parts, 5.4, 1.2, 0.0), "别处的墙挡人");
    check(solid_at(parts, 0.0, house.wall_m - 0.05, 0.0), "天花板挡人（相机停在屋里）");
    check(!solid_at(parts, 0.0, 1.0, 0.0), "屋里是空的");
    const double edge = 0.5 * house.depth_m + house_dims::PLINTH_MARGIN;
    check(parts.doors.size() == 1 && std::abs(parts.doors[0].out_y + 0.5) < 1e-6 && parts.doors[0].out_z > edge, "门外那点在坡道脚下的地面上");
    // 坡道：台基边上贴着地坪
    check(solid_at(parts, 0.0, -0.1, edge + 0.1) && !solid_at(parts, 0.0, 0.1, edge + 0.1), "坡道上沿贴着地坪");
    bool all_inside = true;
    for (const BoxPart &bp : parts.boxes) {
        all_inside = all_inside && std::abs(bp.x) <= 0.5 * house.width_m + 0.7 && bp.z >= -0.5 * house.depth_m - 0.7 && bp.z <= edge + 1.5;
    }
    check(all_inside, "盒子都在台基与坡道里");
    HouseTemplate tw2 = bare_template(tower);
    interior_place_ladder(tw2, tower, -1.5, -1.3, 0, 0);
    const BuildingParts tparts = building_parts(tower, tw2, 0.8, flat);
    check(tparts.ladders.size() == 1 && std::abs(tparts.ladders[0].top_y - 4.0) < 1e-6, "一段梯子，上到 4 m");
    check(!solid_at(tparts, -1.5, 3.95, -0.6) && solid_at(tparts, 1.5, 3.95, 1.5), "二层楼板在梯口处开了洞，别处是实的");
    check(solid_at(tparts, -1.5, 2.0, -1.3), "梯板挡人（人贴着它爬）");

    // 仓库里的模板都没毛病、四种外壳都有
    std::vector<std::string> files;
    const std::vector<HouseTemplate> shipped = shipped_templates(files);
    check(shipped.size() >= 7, "仓库里至少七套模板（得到 " + std::to_string(shipped.size()) + "）");
    bool shells[4] = { false, false, false, false };
    for (size_t k = 0; k < shipped.size(); k++) {
        const HouseTemplate &st = shipped[k];
        const BuildingDef *def = buildings::by_id(st.shell());
        shells[buildings::index_of(st.shell())] = true;
        const InteriorReport sr = interior_report(st, *def);
        check(sr.ok(), st.id + " 没毛病" + (sr.ok() ? std::string() : "：" + sr.problems[0]));
        check(files[k].find(st.id) != std::string::npos, st.id + " 放在对应的文件里");
        const BuildingParts sp = building_parts(*def, st, def->plinth_m + 0.6, flat);
        check(int(sp.ladders.size()) == def->levels - 1, st.id + " 每层之间都有梯子");
    }
    check(shells[0] && shells[1] && shells[2] && shells[3], "四种外壳都有模板");
}

// ---------------------------------------------------------------- 跑货：货位、港口村、委托、货箱账

void test_freight() {
    std::printf("freight\n");
    // 货位：储存区每节 4 个，数目 = 船况的货舱格数；舱体里的够不着
    check(hold_slots(default_layout()).empty(), "开局小艇（没有储存区）没有货位");
    ShipLayout cargo = default_layout();
    check(layout_place(cargo, { 0, 0, 1 }, ModuleKind::STORAGE).empty(), "舷门后面加一节储存区");
    std::vector<HoldSlot> slots = hold_slots(cargo);
    check(int(slots.size()) == layout_stats(cargo).hold && slots.size() == 4, "4 个货位 = 货舱 4 格");
    bool placed = true;
    for (const HoldSlot &s : slots) {
        placed = placed && s.open && std::abs(std::abs(s.x) - HOLD_SLOT_SIDE_M) < 1e-9 && std::abs(std::abs(s.z - 7.0) - HOLD_SLOT_ALONG_M) < 1e-9 && s.y == 0.0;
    }
    check(placed, "货位在那一节甲板上、两舷各前后两个");
    check(slots[0].x < 0 && slots[0].z < slots[1].z && slots[2].x > 0, "编号：左前、左后、右前、右后");
    ShipLayout stacked = cargo;
    check(layout_place(stacked, { 0, 1, 1 }, ModuleKind::STORAGE).empty(), "储存区上再摞一节");
    slots = hold_slots(stacked);
    int open = 0;
    for (const HoldSlot &s : slots) {
        open += s.open ? 1 : 0;
    }
    check(slots.size() == 8 && open == 4, "被盖住的那节货位够不着，顶上那节够得着");

    // 土产：确定、在表里、不是粮
    const char *sp = freight::specialty_of(2051, 152, false);
    check(std::string(sp) == freight::specialty_of(2051, 152, false), "同种子同村同土产");
    check(catalog::item(sp) && !catalog::crop_of_seed(sp) && catalog::item("coin"), "土产与铜钱在物品表里，不是种子");
    int kinds = 0;
    for (const char *id : { "cloth", "pottery", "lacquer" }) {
        bool seen = false;
        for (int v = 1; v <= 60; v++) {
            seen = seen || std::string(freight::specialty_of(2051, v, false)) == id;
        }
        kinds += seen ? 1 : 0;
    }
    check(kinds == 3, "三样土产都有村子出");
    VillageState vs;
    vs.store.add("millet", 3);
    vs.store.add("cloth", 20);
    check(village_grain(vs) == 3, "土产不算粮");

    // 委托与货箱账：三个港口，甲、乙同岛，丙在别的岛
    std::vector<FreightPort> ports(3);
    ports[0].island = 0, ports[0].x = 0.0, ports[0].name = "甲村";
    ports[1].island = 0, ports[1].x = 2000.0, ports[1].name = "乙村";
    ports[2].island = 1, ports[2].x = 9000.0, ports[2].name = "丙村";
    FreightRules rules;
    Inventory a_store, c_store, purse;
    a_store.add("cloth", 7);
    FreightOrder order;
    std::string why;
    check(offer_freight(ports, 0, a_store, "cloth", rules, order, why), "甲村开得出一单");
    check(order.to == 2 && order.crates == 1 && order.per_crate == 5, "开到别岛最近的丙村；7 份只够一箱");
    check(order.fee == 8 + 9 && std::abs(order.km - 9.0) < 1e-9, "运费 = 每箱 8 + 每 km 1");
    a_store.add("cloth", 8);
    check(offer_freight(ports, 0, a_store, "cloth", rules, order, why) && order.crates == 2 && order.fee == 34, "15 份开满两箱");
    FreightOrder none;
    check(!offer_freight(ports, 0, Inventory(), "cloth", rules, none, why) && why.find("不够一箱") != std::string::npos, "仓里没货开不出来");
    std::vector<FreightPort> lonely(ports.begin(), ports.begin() + 2);
    check(!offer_freight(lonely, 0, a_store, "cloth", rules, none, why) && why.find("别的岛") != std::string::npos, "别的岛上没有港口");

    CargoBook book;
    check(book.set_slot_count(4) && book.free_slots() == 4, "船上 4 个货位");
    check(!book.pack(2, order, c_store).empty(), "不在起运村接不了这单");
    check(book.pack(0, order, a_store).empty() && book.is_active() && book.is_holding() && a_store.count("cloth") == 10, "接单、打包一箱：村仓少 5 份");
    check(book.pack(0, order, a_store).empty() && !book.is_holding() && !book.is_active() && a_store.count("cloth") == 15, "抱着箱子再按一下：放回仓里，这单退了");
    check(book.pack(0, order, a_store).empty() && book.get_packed() == 1 && book.is_holding(), "重新接单打包");
    check(book.pack(2, order, c_store) == "手上已经抱着一箱了", "抱着箱子在别处打不了包");
    check(book.stow(9) == "这里不是货位" && book.stow(1).empty() && book.aboard() == 1, "放上 1 号货位");
    check(book.stow(1) == "手上没有货", "手上空了");
    check(book.lift(0) == "这个货位是空的", "空货位抱不起来");
    check(book.pack(0, order, a_store).empty() && book.stow(1) == "这个货位上已经有一箱了" && book.stow(2).empty(), "第二箱放 2 号");
    check(book.pack(0, order, a_store).find("都打包了") != std::string::npos && a_store.count("cloth") == 5, "两箱都打包了，不再多扣");
    check(!book.set_slot_count(2) && book.get_slots().size() == 4, "货位上有箱子时船不能改小");
    check(book.lift(1).empty() && book.is_holding(), "抱起 1 号");
    bool done = false;
    check(book.deliver(0, a_store, purse, done) == "这箱不是送这儿的", "送回起运村不算交货");
    check(book.deliver(2, c_store, purse, done).empty() && !done && c_store.count("cloth") == 5 && purse.count("coin") == 0, "交一箱：丙村仓多 5 份，没交齐不给钱");
    check(book.lift(2).empty() && book.deliver(2, c_store, purse, done).empty() && done, "交齐");
    check(purse.count("coin") == 34 && c_store.count("cloth") == 10 && !book.is_active() && book.aboard() == 0, "付运费 34 文，这单结了");
    check(a_store.count("cloth") + c_store.count("cloth") == 15, "货一份不多一份不少");

    // 村仓在船台上
    SettlementLayout layout;
    layout.add({ "house", 30.0, 0.0, 0.0, "" });
    layout.add({ "granary", 5.0, 4.0, 0.0, "" });
    check(freight_post_building(layout) == -1, "没有船台就没有村仓");
    layout.add({ "berth", 60.0, 0.0, 0.0, "" });
    check(freight_post_building(layout) == 2, "村仓在船台上");

    // 船台：非房屋；几何与停得下的船
    const BuildingDef &berth = *buildings::by_id("berth");
    check(!berth.house && buildings::by_id("house")->house && buildings::index_of("tower") == 3, "船台不是房屋，排在望楼后面");
    const BuildingParts bp = building_parts(berth, HouseTemplate(), 0.6, [](double, double) { return 0.6; });
    check(bp.doors.size() == 1 && bp.doors[0].out_z > 0.5 * berth.depth_m && bp.group_count == 2, "船台：前沿一道坡道，只有地面一组");
    check(solid_at(bp, 0.0, -0.1, 0.0) && !solid_at(bp, 0.0, 1.0, 0.0), "台面挡脚，台面上空着（跳板在前面）");
    const double top_z = land_berth::CONTACT_Z + 0.2, foot_z = land_berth::CONTACT_Z + land_berth::GANGWAY_RUN_M - 0.2;
    check(solid_at(bp, 0.0, land_berth::LIFT_M - 0.2, top_z) && !solid_at(bp, 0.0, land_berth::LIFT_M + 0.2, top_z), "跳板顶贴着接触线的甲板高");
    check(solid_at(bp, 0.0, 0.0, foot_z) && !solid_at(bp, 0.0, 0.3, foot_z), "跳板脚落在台面上");
    const double keel_z = land_berth::CONTACT_Z - 3.5, hull_bottom = land_berth::LIFT_M - land_berth::HULL_DROP_M;
    check(solid_at(bp, 3.5, hull_bottom - 0.1, keel_z) && !solid_at(bp, 3.5, hull_bottom + 0.1, keel_z), "坞墩托到船体底");
    check(!solid_at(bp, 0.0, hull_bottom - 0.1, keel_z) && !solid_at(bp, 7.0, hull_bottom - 0.1, keel_z), "格中心下面没有坞墩（浮石吊在那儿）");
    check(land_berth::ship_problem(cargo).empty(), "开局的船（储存区 + 动力区 + 驾驶区）停得下");
    ShipLayout wide = cargo;
    check(layout_place(wide, { 1, 0, 0 }, ModuleKind::STORAGE).empty() && land_berth::ship_problem(wide).find("太宽") != std::string::npos, "两列宽的船停不下");
    ShipLayout longer = default_layout();
    for (int z = 1; z <= 3; z++) {
        layout_place(longer, { 0, 0, z }, ModuleKind::STORAGE);
    }
    for (int z = -2; z >= -3; z--) {
        layout_place(longer, { 0, 0, z }, ModuleKind::STORAGE);
    }
    check(land_berth::ship_problem(longer).find("太长") != std::string::npos, "七节长的船停不下");

    // 2051：主码头 #1 附近的村031 的委托开到岛 #9 上的烧炭营43（专业聚落也是村，有船台）
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        std::printf("  （跳过 2051：没有资产）\n");
        return;
    }
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    parse_world_grid(bin.data(), bin.size(), g, err);
    parse_world_sites(tsv, sites, err);
    WorldTerrain t(g, sites.terrain_islands(), 2051);
    const WorldWorks works = lay_world_works(t, sites);
    const std::vector<FreightPort> world_ports = plan_freight_ports(sites);
    int from = -1;
    for (size_t k = 0; k < world_ports.size(); k++) {
        from = world_ports[k].site_id == 31 ? int(k) : from;
    }
    check(from >= 0 && world_ports.size() == 211, "211 个村都能托运（含专业聚落，散户不算）");
    const SiteSettlement *v31 = sites.village_by_id(31);
    const WorldGround land_ground(t, 0.0, 0.0);
    const FarmModel land(&land_ground, land_ground.plot_lattice(), FarmRules());
    WorldLedgerSpec spec;
    spec.site = *v31;
    spec.lattice = land.get_lattice();
    spec.farm_rules = land.get_rules();
    const WorldLedger ledger = open_world_ledger(t, spec, SimRules());
    const std::string item = freight::specialty_of(2051, 31, false);
    check(ledger.state->store.count(item) == 132 && ledger.state->store.count("millet") == 8, "村031 开账：仓里 " + catalog::item_name(item) + " 132 份（66 户 × 2），粟照旧 8");
    check(freight_post_building(ledger.layout) >= 0, "村031 有船台（村仓）");
    FreightOrder o;
    check(offer_freight(world_ports, from, ledger.state->store, item, rules, o, why), "村031 开得出委托：" + why);
    if (o.valid()) {
        const FreightPort &to = world_ports[size_t(o.to)];
        std::printf("  村031 → %s（岛 #%d，%.1f km）%d 箱，运费 %d\n", to.name.c_str(), to.island, o.km, o.crates, o.fee);
        check(to.site_id == 203 && to.island == 9, "开到别的岛上最近的烧炭营43");
        check(o.crates == 2 && o.km > 8.0 && o.km < 9.0, "两箱，八公里多");
        const SiteSettlement *v203 = sites.village_by_id(203);
        const VillagePlan p203 = plan_world_village(t, *v203, 2051, WorldVillageRules());
        check(freight_post_building(p203.layout) >= 0, "烧炭营43 也有船台");
    }
}

// 资源：种类表、存量账、自家的货箱、2051 的采场与地标规划
void test_gather() {
    std::printf("gather\n");
    // 种类表：散的有采场（产物在物品表里、有颜色、有工时与存量），点是地标，片没有采场
    int workings = 0, landmarks = 0, patches = 0;
    bool products_ok = true, others_ok = true;
    for (int k = 0; k < deposits::KIND_COUNT; k++) {
        const DepositKind &kind = deposits::KINDS[k];
        const std::string item = deposits::product_of(kind, "");
        float r = 0, g = 0, b = 0;
        if (deposits::has_working(kind)) {
            workings++;
            products_ok = products_ok && !item.empty() && catalog::item(item) && !catalog::crop_of_seed(item) && deposits::product_color(item, r, g, b) &&
                    kind.gather_s > 0.0 && kind.base_stock > 0 && kind.refill_hours > 0.0 && kind.look != DepositLook::NONE && std::string(kind.site).size() > 0;
        } else {
            landmarks += deposits::is_landmark(kind) ? 1 : 0;
            patches += kind.form == DepositForm::PATCH ? 1 : 0;
            others_ok = others_ok && item.empty();
        }
    }
    check(workings == 6 && products_ok, "六种散的有采场（石料、黏土、砂砾、砂金、金属矿、硫磺）：产物在物品表里、不是种子、有货箱色、有工时与存量");
    check(landmarks == 3 && patches == 4 && others_ok, "点（泉眼、温泉、洞穴）与片（林木、泥炭芦苇、浮石、鸟粪）没有采场");
    check(!deposits::kind("quarry") && deposits::kind("stone") && deposits::kind("stone")->form == DepositForm::FIELD, "石料的键是 stone（生成器改名：石料是赋存，采石场是采场）");
    const DepositKind &ore = *deposits::kind("ore"), &stone = *deposits::kind("stone"), &clay = *deposits::kind("clay");
    check(deposits::product_of(ore, "铜") == "copper_ore" && deposits::product_of(ore, "锡钨") == "tin_ore" && catalog::item_name("copper_ore") == "铜矿石", "矿坑按矿种出矿石");
    check(deposits::site_name(stone, "安山岩 / 凝灰岩") == "采石场（安山岩 / 凝灰岩）" && deposits::site_name(ore, "铜") == "铜矿坑" &&
                    deposits::site_name(clay, "溪边黏土") == "土坑（溪边黏土）" && deposits::site_name(*deposits::kind("cave"), "崖洞") == "洞穴（崖洞）",
            "采场与地标的名字");
    check(deposits::working_cap(stone, 0.0) < deposits::working_cap(stone, 0.5) && deposits::working_cap(stone, 0.5) < deposits::working_cap(stone, 1.0) &&
                    deposits::working_cap(stone, 0.5) == 40 && deposits::working_cap(stone, 1.0) == 60 && deposits::working_cap(stone, 5.0) == 60,
            "采场存量随那一格的品位：品位 0.5 时 40 份，满品位 60 份");

    // 散布物：每种都采得出东西，树挡人
    bool scatter_ok = true;
    for (int k = 0; k < deposits::SCATTER_COUNT; k++) {
        const ScatterKind &sk = deposits::SCATTER[k];
        const std::string item = deposits::scatter_product(ScatterLook(k), 0);
        float r = 0, g = 0, b = 0;
        scatter_ok = scatter_ok && int(sk.look) == k && catalog::item(item) && deposits::product_color(item, r, g, b) && sk.gather_s > 0.0 && sk.yield > 0 &&
                sk.refill_hours > 0.0 && sk.radius > 0.0;
    }
    check(scatter_ok, "十一种散布物：下标即样子，产物在物品表里、有颜色，有工时、份数、长回的钟头");
    check(deposits::scatter(ScatterLook::BROADLEAF).solid && deposits::scatter(ScatterLook::CONIFER).solid && !deposits::scatter(ScatterLook::SHRUB).solid &&
                    !deposits::scatter(ScatterLook::BOULDER).solid,
            "树干挡人挡船，灌丛、石块不挡");
    check(deposits::scatter_product(ScatterLook::SHRUB, 0) == "firewood" && catalog::item_name("firewood") == "柴", "灌丛砍出柴");
    check(deposits::scatter_product(ScatterLook::REEDS, deposits::SUB_REED) == "reed" && deposits::scatter_product(ScatterLook::REEDS, deposits::SUB_PEAT) == "peat" &&
                    deposits::scatter_name(ScatterLook::REEDS, deposits::SUB_PEAT) == "泥炭" && deposits::scatter_verb(ScatterLook::REEDS, deposits::SUB_PEAT) == "挖泥炭",
            "苇丛按子类：芦苇 / 泥炭");
    check(deposits::scatter_product(ScatterLook::ORE_ROCK, uint8_t(deposits::ore_index("铁"))) == "iron_ore" &&
                    deposits::scatter_name(ScatterLook::ORE_ROCK, uint8_t(deposits::ore_index("铜"))) == "铜矿石",
            "矿石按矿种");
    check(deposits::scatter_product(ScatterLook::CRUST, deposits::SUB_GUANO) == "guano" && deposits::scatter_product(ScatterLook::CRUST, deposits::SUB_SULFUR) == "sulfur",
            "结壳：硫磺 / 鸟粪");
    check(deposits::scatter_yield(ScatterLook::BROADLEAF, 1.0) == 4 && deposits::scatter_yield(ScatterLook::BROADLEAF, 1.3) == 7 &&
                    deposits::scatter_yield(ScatterLook::PLACER, 0.5) == 1,
            "一株几份按大小²（至少 1 份）");
    check(catalog::tool_speed("stone_axe", "firewood") == 2.0 && catalog::tool_speed("stone_axe", "timber") == 2.0, "石斧伐木、砍柴都快一倍");
    const RecipeDef *torch = recipes::find("torch_firewood");
    check(torch && std::string(torch->product) == "torch" && std::string(torch->in[0].item) == "firewood", "柴也能扎火把");

    // resources.tsv：三种行
    ResourceData data;
    std::string err;
    const std::string sample = "# x\n"
                               "deposit\t0\ttimber\t0\t-450\t550\t818.36\t上\t阔叶林\tpatch\n"
                               "deposit\t6\tspring\t0\t13950\t-350\t0.01\t上\t\tpoint\n"
                               "occurrence\t47\tstone\t0\t9050\t-11250\t1.2\t中\t安山岩 / 凝灰岩\t2.3\t41.0\n"
                               "working\t3\tstone\t0\t9050\t-11250\t47\t0.404\t安山岩 / 凝灰岩\t9,46,56\n"
                               "working\t4\tclay\t1\t0\t0\t12\t0.7\t溪边黏土\t-\n"
                               "future\t1\n";
    check(parse_resources(sample, data, err) && data.deposits.size() == 2 && data.occurrences.size() == 1 && data.workings.size() == 2,
            "resources.tsv 能读，不认得的行跳过：" + err);
    check(data.deposits[0].form == DepositForm::PATCH && data.deposits[1].form == DepositForm::POINT && data.deposits[0].subtype == "阔叶林" && data.deposits[1].subtype.empty(),
            "点与片分得清");
    check(data.occurrences[0].id == 47 && data.occurrences[0].grade == deposits::GRADE_MID && data.occurrences[0].length_km == 2.3 && data.occurrence_by_id(47),
            "赋存区的字段都对");
    check(data.workings[0].occurrence == 47 && data.workings[0].grade == 0.404 && data.workings[0].villages == std::vector<int>({ 9, 46, 56 }) &&
                    data.workings[1].villages.empty(),
            "采场挂赋存区、用它的村（- 是没有）");
    check(!parse_resources("deposit\t1\tstone\t0\t0\t0\t0\t中\t\tblob\n", data, err) && err.find("第 1 行") != std::string::npos, "形态不认得报错");
    check(!parse_resources("working\t1\tstone\t0\t0\t0\tx\t0.5\t\t-\n", data, err) && err.find("第 1 行") != std::string::npos, "坏行报错");

    // 存量账：满 → 采 → 按钟回满
    ResourceStock stock;
    check(stock_amount(stock, 40, 24.0, 100.0) == 40.0, "没动过的是满的");
    check(stock_take(stock, 40, 24.0, 100.0, 5) && std::abs(stock_amount(stock, 40, 24.0, 100.0) - 35.0) < 1e-9, "采 5 份剩 35");
    check(std::abs(stock_amount(stock, 40, 24.0, 102.4) - 39.0) < 1e-9, "一个钟头回 40 / 24 份");
    check(std::abs(stock_amount(stock, 40, 24.0, 106.0) - 40.0) < 1e-9, "6 个钟头早回满了，不超上限");
    ResourceStock empty;
    check(stock_take(empty, 6, 72.0, 0.0, 6) && !stock_take(empty, 6, 72.0, 0.0, 1) && stock_amount(empty, 6, 72.0, 0.0) == 0.0, "采空了就不给");
    check(std::abs(stock_hours_until(empty, 6, 72.0, 0.0, 1.0) - 12.0) < 1e-9 && stock_hours_until(empty, 6, 72.0, 12.0, 1.0) == 0.0, "砂金 72 个钟头回满：回一份要 12 个钟头");
    check(stock_amount(empty, 6, 72.0, -5.0) == 0.0, "钟不倒流");

    // 自家的货箱：背包 → 箱 → 货位 → 拆回背包；交货只认托运的箱子
    Inventory pack;
    pack.add("stone", 7);
    CargoBook book;
    book.set_slot_count(4);
    check(book.pack_goods("stone", 5, pack).empty() && book.is_holding_goods() && pack.count("stone") == 2, "背包 7 份石料打一箱：剩 2");
    check(book.pack_goods("stone", 5, pack) == "手上已经抱着一箱了", "抱着箱子打不了第二箱");
    check(book.stow(0).empty() && book.aboard() == 1 && book.aboard_consigned() == 0, "放上货位：船上一箱，不是托运的");
    check(book.pack_goods("stone", 5, pack).find("不够一箱（2 / 5 份）") != std::string::npos, "背包不够一箱说差多少");
    check(book.lift(0).empty() && book.is_holding_goods(), "从货位抱起来还是自家的");
    Inventory store, purse;
    bool done = false;
    check(book.deliver(0, store, purse, done) == "这箱不是送这儿的" && store.empty(), "自家的货不算交货");
    check(book.unpack_goods(pack).empty() && !book.is_holding() && pack.count("stone") == 7, "拆箱：7 份回到背包");
    check(book.unpack_goods(pack) == "手上没有货", "空手拆不了");

    // 2051：按 resources.tsv 落采场与地标
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary), res_in(dir + "/resources.tsv", std::ios::binary),
            wx_in(dir + "/weather.tsv", std::ios::binary);
    if (!bin_in || !tsv_in || !res_in || !wx_in) {
        std::printf("  （跳过 2051：没有资产）\n");
        return;
    }
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    const std::string res((std::istreambuf_iterator<char>(res_in)), std::istreambuf_iterator<char>());
    const std::string weather((std::istreambuf_iterator<char>(wx_in)), std::istreambuf_iterator<char>());
    WorldGrid grid;
    WorldSites sites;
    RiverRegime regime;
    parse_world_grid(bin.data(), bin.size(), grid, err);
    parse_world_sites(tsv, sites, err);
    RiverRegime::parse(weather, regime, err);
    RiverRules river_rules;
    river_rules.precip_mm = regime.precip_mm();
    river_rules.year_days = regime.year_days();
    const bool parsed = parse_resources(res, data, err);
    check(parsed && data.deposits.size() == 709 && data.occurrences.size() == 499 && data.workings.size() == 257,
            "2051：点与片 709、赋存区 499、采场 257（" + std::to_string(data.workings.size()) + "）");
    // 栅格的片号与清单对得上：片的代表格在自己的片里（点可以落在片里：林子里的泉眼、洞）
    bool patches_ok = grid.has_resources();
    int patch_count = 0, points_in_patch = 0;
    for (const ResourceDeposit &d : data.deposits) {
        const int at = grid.patch_at(d.x, d.z);
        if (d.form == DepositForm::PATCH) {
            patch_count++;
            patches_ok = patches_ok && at == d.id;
        } else {
            points_in_patch += at >= 0 ? 1 : 0;
        }
    }
    check(patches_ok && patch_count == 556, "2051 的片号：556 片的代表格都在自己的片里（另有 " + std::to_string(points_in_patch) + " 个点在林子这类片里）");
    // 赋存区的峰值格品位够门槛；采场那一格的品位与清单一致（u8 量化）
    bool fields_ok = true;
    for (const ResourceOccurrence &o : data.occurrences) {
        fields_ok = fields_ok && grid.field_cell(WorldGrid::field_of(o.kind.c_str()), grid.nearest_cell(o.x, o.z)) >= 0.15 - 1e-9;
    }
    for (const ResourceWorking &w : data.workings) {
        fields_ok = fields_ok && std::abs(grid.field_cell(WorldGrid::field_of(w.kind.c_str()), grid.nearest_cell(w.x, w.z)) - w.grade) < 0.01;
    }
    check(fields_ok, "赋存区的峰值格、采场那一格的品位与赋存场一致");
    WorldTerrain t(grid, sites.terrain_islands(), 2051, river_rules);
    lay_world_works(t, sites);
    const auto started = std::chrono::steady_clock::now();
    const ResourcePlan plan = plan_resource_sites(t, sites, data);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::printf("  采场 %zu 处落地（%zu 处够不着船）；并掉 %d、在村里 %d、落不了 %zu；地标 %zu 处（落不了 %zu）（%.0f ms）\n", plan.sites.size(), plan.unreachable.size(),
            plan.merged, plan.in_village, plan.failed.size(), plan.landmarks.size(), plan.landmarks_failed.size(), ms);
    check(int(plan.sites.size()) + plan.merged + plan.in_village + int(plan.failed.size()) == 257, "每一处采场都有下落");
    check(plan.sites.size() >= 220 && plan.failed.size() * 50 <= 257, "绝大多数采场落了地（落不了的不过 2%）");
    check(plan.underside == 14 && plan.landmarks.size() + plan.landmarks_failed.size() + 14 == 153 && plan.landmarks_failed.size() * 20 <= plan.landmarks.size(),
            "泉眼 40、洞穴 105、温泉 8：岛底的洞 14 处不摆，其余绝大多数摆得出样子");
    bool spots_ok = true, piles_ok = true, site_fields_ok = true;
    for (const ResourceSite &s : plan.sites) {
        const ResourceWorking &w = data.workings[size_t(s.working)];
        spots_ok = spots_ok && resource_spot_ok(t, sites, s.face_x, s.face_z, ResourceSiteRules()) && resource_spot_ok(t, sites, s.pile_x, s.pile_z, ResourceSiteRules());
        piles_ok = piles_ok && std::abs(std::hypot(s.pile_x - s.face_x, s.pile_z - s.face_z) - ResourceSiteRules().pile_m) < 1e-6 &&
                std::hypot(s.face_x - w.x, s.face_z - w.z) <= ResourceSiteRules().search_m + 1e-6;
        site_fields_ok = site_fields_ok && s.kind && !s.item.empty() && catalog::item(s.item) && s.cap > 0 && !s.name.empty() && s.id == w.id && s.villages == w.villages;
    }
    check(spots_ok, "每处的采掘面与堆场都站得住（有地、不是水、不在坪上村里、坡缓、离崖边远）");
    check(piles_ok, "堆场离采掘面 7 m，采掘面离采场格心不过 150 m");
    check(site_fields_ok, "每处都有产物、存量、名字，记着用它的村");
    const ResourcePlan again = plan_resource_sites(t, sites, data);
    bool same = again.sites.size() == plan.sites.size() && again.landmarks.size() == plan.landmarks.size();
    for (size_t k = 0; same && k < plan.sites.size(); k++) {
        same = again.sites[k].id == plan.sites[k].id && again.sites[k].face_x == plan.sites[k].face_x && again.sites[k].pile_z == plan.sites[k].pile_z &&
                again.sites[k].land_x == plan.sites[k].land_x;
    }
    check(same, "同一份资产铺两遍一样");
    // 够得着船：采掘面 60 m 内有开局的船野泊得下的地方（规划时量好、记下泊地，与体检同一份）
    size_t reached = 0;
    bool reach_consistent = true;
    for (const ResourceSite &s : plan.sites) {
        reached += s.reach_m >= 0.0 ? 1 : 0;
        const bool listed = std::find(plan.unreachable.begin(), plan.unreachable.end(), s.id) != plan.unreachable.end();
        reach_consistent = reach_consistent && listed == (s.reach_m < 0.0) && s.reach_m <= ResourceSiteRules().reach_m &&
                (s.reach_m < 0.0 || std::abs(std::hypot(s.land_x - s.face_x, s.land_z - s.face_z) - s.reach_m) < 1e-6);
    }
    check(reach_consistent, "够不着船的都记在 unreachable 里，够得着的泊地在 60 m 内那一圈上");
    check(reached * 100 >= plan.sites.size() * 96, "96% 以上的采掘面够得着船（" + std::to_string(reached) + " / " + std::to_string(plan.sites.size()) + "）");
    for (size_t k = 0; k < plan.sites.size() && k < 200; k += 20) {
        const ResourceSite &s = plan.sites[k];
        double x = 0.0, z = 0.0, yaw = 0.0;
        check(resource_reach(t, sites, s.face_x, s.face_z, ResourceSiteRules(), x, z, yaw) == s.reach_m && x == s.land_x && z == s.land_z,
                "采场 #" + std::to_string(s.id) + "：单独量够不够得着船、泊地在哪，与规划记的一样");
    }
    // 主码头 #1（邑治旁）一公里内有土坑（邑治用的），六公里内有采石场（生成器按村挑石头 5 km 内就近取，林茂的低地少露头）
    const SiteDock *main_dock = sites.dock_by_id(1);
    bool clay_near = false, stone_near = false;
    for (const ResourceSite &s : plan.sites) {
        const double d = main_dock ? std::hypot(s.face_x - main_dock->x, s.face_z - main_dock->z) : 1e9;
        clay_near = clay_near || (d < 1000.0 && s.item == "clay");
        stone_near = stone_near || (d < 6000.0 && s.item == "stone");
    }
    check(clay_near && stone_near, "主码头 #1 一公里内有土坑、六公里内有采石场");
}

// 散布：按块生成、让开、确定；存量账（采空倒下、长回来）；2051 的密度与泊地
void test_scatter() {
    std::printf("scatter\n");
    // 键：块 + 块内序号，负的块号也拆得回来
    const ScatterChunkKey neg{ -3, 17 };
    const uint64_t key = scatter_key(neg, 513);
    check(scatter_key_chunk(key) == neg && scatter_key_index(key) == 513 && ScatterChunkKey::unpack(neg.packed()) == neg, "键拆得回块号（含负数）与序号");

    // 合成的圆岛：内圈可耕地、外圈林地；东边一片石料赋存
    WorldGrid g = synthetic_grid();
    const size_t n = g.height.size();
    g.field.assign(n * WorldGrid::FIELD_COUNT, 0);
    g.patch.assign(n, 0);
    for (int r = 0; r < g.rows; r++) {
        for (int c = 0; c < g.cols; c++) {
            const double x = g.x0 + c * g.res, z = g.z0 + r * g.res;
            if (x > 700.0 && x < 1300.0 && std::abs(z) < 400.0) {
                g.field[size_t(WorldGrid::FIELD_STONE) * n + g.index(r, c)] = 230; // 石料品位 0.9
            }
        }
    }
    check(g.has_resources() && std::abs(g.field_at(WorldGrid::FIELD_STONE, 1000.0, 0.0) - 230.0 / 255.0) < 1e-9 && g.field_at(WorldGrid::FIELD_STONE, 0.0, 0.0) == 0.0,
            "赋存场：格心上是格值，别处 0");
    check(std::abs(g.field_at(WorldGrid::FIELD_STONE, 750.0, 0.0) - 0.5 * 230.0 / 255.0) < 1e-9, "格心之间双线性渐变"); // 700 那一格不在里面，800 起在
    check(WorldGrid::field_of("stone") == WorldGrid::FIELD_STONE && WorldGrid::field_of("timber") == -1 && std::string(WorldGrid::field_key(WorldGrid::FIELD_CLAY)) == "clay",
            "层与生成器的键对得上");
    WorldTerrain t(g, { synthetic_island() }, 7);
    WorldSites sites;
    SiteSettlement village;
    village.id = 1;
    village.x = -900.0;
    village.z = 300.0;
    village.households = 25; // 估的半径 55 m
    sites.settlements.push_back(village);
    const ResourceData data;
    const ResourcePlan plan;
    const ResourceScatter scatter(t, sites, data, plan, 2051);
    check(scatter.valid(), "有资源栅格就能生成");
    std::map<int, int> counts;
    int in_forest = 0, trees = 0, on_field = 0, in_village = 0, off_island = 0;
    bool same = true, indexed = true;
    size_t total = 0;
    for (int iz = -3; iz <= 2; iz++) { // 横穿岛心的一条：可耕地、林地、村子、石料都在里面
        for (int ix = -13; ix <= 12; ix++) {
            const ScatterChunkKey k{ ix, iz };
            const std::vector<ScatterItem> items = scatter.chunk(k);
            const std::vector<ScatterItem> again = scatter.chunk(k);
            same = same && items.size() == again.size();
            for (size_t m = 0; m < items.size(); m++) {
                const ScatterItem &it = items[m];
                same = same && it.x == again[m].x && it.z == again[m].z && it.look == again[m].look && it.scale == again[m].scale;
                indexed = indexed && it.index == m && scatter.chunk_at(it.x, it.z) == k;
                counts[it.look]++;
                const int lc = g.landcover[g.nearest_cell(it.x, it.z)];
                if (deposits::scatter(it.kind()).solid) {
                    trees++;
                    in_forest += lc == 4 ? 1 : 0;
                }
                on_field += std::hypot(it.x, it.z) < 600.0 ? 1 : 0; // 内圈 750 m 是可耕地：离边远一点（林缘扭过）的不该有东西
                in_village += std::hypot(it.x - village.x, it.z - village.z) < settlement_guess_radius(village) + 12.0 ? 1 : 0;
                double y = 0.0;
                off_island += t.surface(it.x, it.z, y) ? 0 : 1;
            }
            total += items.size();
        }
    }
    std::printf("  合成岛：%zu 株（树 %d，%d 棵在林地格）；阔叶 %d、针叶 %d、灌丛 %d、石块 %d\n", total, trees, in_forest, counts[int(ScatterLook::BROADLEAF)],
            counts[int(ScatterLook::CONIFER)], counts[int(ScatterLook::SHRUB)], counts[int(ScatterLook::BOULDER)]);
    check(same, "同一块生成两遍一模一样");
    check(indexed, "序号即生成的次序，每一株都在自己的块里（块边上的格点归左上角所在的块）");
    check(trees > 3000 && in_forest * 100 >= trees * 90, "树长在林地里（林缘扭过，九成以上落在林地格）");
    check(on_field == 0, "可耕地上不长树、灌丛、石块");
    check(in_village == 0, "村子（估的半径外加 12 m）里什么都不长");
    check(off_island == 0, "都长在陆地上");
    check(counts[int(ScatterLook::BOULDER)] > 50, "石料赋存的那片长出石块");
    // 地图的资源底图：每格画最稀的那一类，品位不够门槛不算
    {
        WorldGrid m = g;
        const size_t a = m.nearest_cell(1000.0, 0.0), b = m.nearest_cell(-1000.0, 0.0), c = m.nearest_cell(0.0, 0.0);
        m.field[size_t(WorldGrid::FIELD_ORE) * n + a] = 100; // 石料 0.9 那一格再叠一点矿（0.39）
        m.patch[b] = 1; // 林地里的一格划进片 0（林木）
        m.field[size_t(WorldGrid::FIELD_CLAY) * n + c] = 60; // 黏土 0.24：不够 0.3 的门槛
        const int timber = int(deposits::kind("timber") - deposits::KINDS), ore = int(deposits::kind("ore") - deposits::KINDS);
        const std::vector<int> patch_kinds = { timber };
        double grade = 0.0;
        check(dominant_resource(m, a, patch_kinds, grade) == ore && std::abs(grade - 100.0 / 255.0) < 1e-9, "矿与石料叠在一格：画稀的矿");
        check(dominant_resource(m, b, patch_kinds, grade) == timber && grade == 1.0, "片按片号：林木");
        check(dominant_resource(m, c, patch_kinds, grade) == -1, "品位不够门槛的不画");
        const MapImage img = build_resource_image(m, patch_kinds);
        const MapImage plain = build_map_image(m);
        check(img.width == plain.width && img.rgba.size() == plain.rgba.size() && img.rgba[a * 4 + 3] == 255 && img.rgba[a * 4] > img.rgba[a * 4 + 2],
                "资源底图与地表底图一样大、陆地不透明，矿那一格偏红");
    }
    // 让开采场与泊地：规划里放一处采场，泊地周围不长树
    ResourcePlan with_site;
    ResourceSite site;
    site.face_x = 1100.0, site.face_z = -600.0, site.pile_x = 1107.0, site.pile_z = -600.0;
    site.reach_m = 30.0, site.land_x = 1100.0, site.land_z = -570.0;
    with_site.sites.push_back(site);
    const ResourceScatter cleared(t, sites, data, with_site, 2051);
    std::vector<ScatterItem> solids;
    scatter.solids_near(1100.0, -570.0, 16.0, solids);
    const size_t before = solids.size();
    solids.clear();
    cleared.solids_near(1100.0, -570.0, 16.0, solids);
    check(before > 0 && solids.empty(), "泊地 17 m 内的树让开了（原来有 " + std::to_string(before) + " 棵）");

    // 存量账：一棵树 4 份，采空了倒，48 个钟头回满才立回来
    ScatterLedger ledger;
    const uint64_t tree = scatter_key(ScatterChunkKey{ 2, 3 }, 7);
    check(ledger.standing(tree, 4, 48.0, 10.0) && !ledger.find(tree) && !ledger.any_felled_in(ScatterChunkKey{ 2, 3 }), "没动过的立着、没记账");
    check(ledger.take(tree, 4, 48.0, 10.0, 1) && ledger.standing(tree, 4, 48.0, 10.0) && std::abs(stock_amount(*ledger.find(tree), 4, 48.0, 10.0) - 3.0) < 1e-9,
            "采一份：还立着，剩 3 份");
    check(ledger.take(tree, 4, 48.0, 10.0, 3) && !ledger.standing(tree, 4, 48.0, 10.0) && ledger.any_felled_in(ScatterChunkKey{ 2, 3 }) &&
                    !ledger.any_felled_in(ScatterChunkKey{ 3, 2 }),
            "采空了就倒（这一块里有倒着的）");
    check(!ledger.take(tree, 4, 48.0, 30.0, 1) && !ledger.standing(tree, 4, 48.0, 30.0), "倒着的没长满不给采（20 个钟头后长了一半多也不行）");
    check(ledger.standing(tree, 4, 48.0, 58.0) && ledger.take(tree, 4, 48.0, 58.0, 1), "48 个钟头后立回来、又能采");
    // 采场那样按份采到空的走法：stock_take + settle
    const uint64_t rock = scatter_key(ScatterChunkKey{ 0, 0 }, 1);
    ResourceStock *s = ledger.stock(rock);
    check(stock_take(*s, 2, 72.0, 1.0, 1) && !ledger.settle(rock, 2, 72.0, 1.0) && stock_take(*s, 2, 72.0, 1.0, 1) && ledger.settle(rock, 2, 72.0, 1.0) &&
                    !ledger.standing(rock, 2, 72.0, 1.0),
            "一份份采（GatherHand 的走法）：采到空才倒");

    // 2051：主码头附近的密度、采场的泊地没有树、生成一块的耗时
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/worlds/2051";
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary), res_in(dir + "/resources.tsv", std::ios::binary),
            wx_in(dir + "/weather.tsv", std::ios::binary);
    if (!bin_in || !tsv_in || !res_in || !wx_in) {
        std::printf("  （跳过 2051：没有资产）\n");
        return;
    }
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    const std::string res((std::istreambuf_iterator<char>(res_in)), std::istreambuf_iterator<char>());
    const std::string weather((std::istreambuf_iterator<char>(wx_in)), std::istreambuf_iterator<char>());
    WorldGrid grid;
    WorldSites sites51;
    ResourceData data51;
    RiverRegime regime;
    std::string err;
    parse_world_grid(bin.data(), bin.size(), grid, err);
    parse_world_sites(tsv, sites51, err);
    parse_resources(res, data51, err);
    RiverRegime::parse(weather, regime, err);
    RiverRules river_rules; // 同 World：年降水与一年几天取 weather.tsv
    river_rules.precip_mm = regime.precip_mm();
    river_rules.year_days = regime.year_days();
    WorldTerrain t51(grid, sites51.terrain_islands(), 2051, river_rules);
    lay_world_works(t51, sites51);
    const ResourcePlan plan51 = plan_resource_sites(t51, sites51, data51);
    const ResourceScatter s51(t51, sites51, data51, plan51, 2051);
    const SiteDock *dock = sites51.dock_by_id(1);
    std::map<int, int> near;
    size_t items = 0;
    const auto started = std::chrono::steady_clock::now();
    const ScatterChunkKey center = s51.chunk_at(dock->x, dock->z);
    int chunks = 0;
    for (int iz = center.iz - 4; iz <= center.iz + 4; iz++) {
        for (int ix = center.ix - 4; ix <= center.ix + 4; ix++) {
            for (const ScatterItem &it : s51.chunk(ScatterChunkKey{ ix, iz })) {
                near[it.look]++;
                items++;
            }
            chunks++;
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::printf("  2051 主码头周围 %d 块：%zu 株（树 %d、灌丛 %d、苇 %d、浮石 %d），每块 %.2f ms\n", chunks, items,
            near[int(ScatterLook::BROADLEAF)] + near[int(ScatterLook::CONIFER)], near[int(ScatterLook::SHRUB)], near[int(ScatterLook::REEDS)],
            near[int(ScatterLook::FLOATSTONE)], ms / chunks);
    check(near[int(ScatterLook::BROADLEAF)] + near[int(ScatterLook::CONIFER)] > 500 && near[int(ScatterLook::SHRUB)] > 500, "主码头周围一公里有树林、有灌丛");
    int blocked = 0;
    for (const ResourceSite &site51 : plan51.sites) {
        std::vector<ScatterItem> trunks;
        if (site51.reach_m >= 0.0) {
            s51.solids_near(site51.land_x, site51.land_z, 14.0, trunks);
        }
        blocked += trunks.empty() ? 0 : 1;
    }
    check(blocked == 0, "每处采场的泊地 14 m 内都没有树（规划说落得下船，游戏里就落得下）");
}

// 散布的过渡：草坡 | 林地 一条直交界（同 half_grid），按林地份额分五档量树、灌丛的密度与树的大小
void test_scatter_ecotone() {
    std::printf("scatter ecotone\n");
    WorldGrid g = half_grid();
    const size_t n = g.height.size();
    g.field.assign(n * WorldGrid::FIELD_COUNT, 0);
    g.patch.assign(n, 0);
    WorldIsland isl;
    isl.rim_m = 200.0;
    isl.max_shore_m = 5000.0;
    const WorldTerrain t(g, { isl }, 11);
    const ResourceScatter scatter(t, WorldSites(), ResourceData(), ResourcePlan(), 2051);
    constexpr int BINS = 5;
    const double edges[BINS + 1] = { 0.0, 0.05, 0.35, 0.65, 0.95, 1.01 };
    auto bin_of = [&](double w) {
        int b = 0;
        while (b + 1 < BINS && w >= edges[b + 1]) {
            b++;
        }
        return b;
    };
    double area[BINS] = {}, trees[BINS] = {}, shrubs[BINS] = {}, scale[BINS] = {};
    const double X0 = -384.0, X1 = 384.0, Z0 = -640.0, Z1 = 640.0; // 整块：x 3 块 × 2、z 5 块 × 2
    for (double z = Z0 + 1.0; z < Z1; z += 2.0) {
        for (double x = X0 + 1.0; x < X1; x += 2.0) {
            area[bin_of(t.cover(x, z).w[4])] += 4.0;
        }
    }
    for (int iz = int(Z0 / 128.0); iz < int(Z1 / 128.0); iz++) {
        for (int ix = int(X0 / 128.0); ix < int(X1 / 128.0); ix++) {
            for (const ScatterItem &it : scatter.chunk(ScatterChunkKey{ ix, iz })) {
                const int b = bin_of(t.cover(it.x, it.z).w[4]);
                if (it.kind() == ScatterLook::BROADLEAF || it.kind() == ScatterLook::CONIFER) {
                    trees[b]++;
                    scale[b] += it.scale;
                } else if (it.kind() == ScatterLook::SHRUB) {
                    shrubs[b]++;
                }
            }
        }
    }
    double tree_d[BINS], shrub_d[BINS];
    std::printf("  林地份额   面积 m²    树 / 公顷  灌丛 / 公顷  树的平均大小\n");
    for (int b = 0; b < BINS; b++) {
        tree_d[b] = area[b] > 0.0 ? trees[b] / area[b] * 10000.0 : 0.0;
        shrub_d[b] = area[b] > 0.0 ? shrubs[b] / area[b] * 10000.0 : 0.0;
        std::printf("  %.2f–%.2f  %8.0f  %9.1f  %10.1f  %8.2f\n", edges[b], std::min(1.0, edges[b + 1]), area[b], tree_d[b], shrub_d[b], trees[b] > 0 ? scale[b] / trees[b] : 0.0);
    }
    check(area[1] > 10000.0 && area[2] > 10000.0 && area[3] > 10000.0, "交界处有一条几十米宽的过渡带（1.3 km 长的交界，每档都有一公顷以上）");
    check(tree_d[0] < tree_d[1] && tree_d[1] < tree_d[2] && tree_d[2] < tree_d[3] && tree_d[3] < tree_d[4] * 1.1, "树的密度随林地份额一档档升上去，不是一刀切");
    check(tree_d[2] > 0.3 * tree_d[4] && tree_d[2] < 0.7 * tree_d[4], "过渡带中间树约是林子里的一半（" + std::to_string(tree_d[2] / tree_d[4]) + "）");
    check(tree_d[0] < 0.1 * tree_d[4], "草坡那边只零星几棵");
    check(shrub_d[2] > 2.0 * shrub_d[0] && shrub_d[2] > 2.0 * shrub_d[4], "林缘一圈灌丛带：过渡带中间的灌丛比两边都密一倍以上");
    check(scale[2] / trees[2] < 0.85 * scale[4] / trees[4], "林缘的树矮一截");
}

// 野泊：平地、斜坡、崖边、水边、太高
struct TestLandingGround : LandingGround {
    std::function<bool(double, double, double &)> h;
    std::function<bool(double, double)> w;
    bool height(double p_x, double p_z, double &r_y) const override { return h(p_x, p_z, r_y); }
    bool water(double p_x, double p_z) const override { return w ? w(p_x, p_z) : false; }
};

void test_wild_landing() {
    std::printf("wild landing\n");
    ShipLayout ship = default_layout();
    layout_place(ship, { 0, 0, -2 }, ModuleKind::STORAGE); // 岛群开局那条：储存区 + 动力区 + 驾驶区
    TestLandingGround flat;
    flat.h = [](double, double, double &r_y) {
        r_y = 100.0;
        return true;
    };
    const LandingRules rules;
    WildLanding w = plan_wild_landing(flat, ship, 0.0, 120.0, 0.0, 0.0);
    check(w.ok && std::abs(w.deck_y - 105.6) < 1e-9 && w.ground_hi == 100.0 && w.ground_lo == 100.0, "平地：甲板落在地面上 5.6 m：" + w.why);
    check(w.side == 1 && std::abs(w.door_x - starboard_edge_x(ship)) < 1e-9 && w.door_z == 0.0 && w.out_x == 1.0, "一样短挑右舷，舷梯从右舷舷门往外");
    check(std::abs(w.run - 9.5) < 1e-9 && std::abs(w.rise - 5.6) < 1e-9 && w.rise / w.run <= rules.max_slope && w.foot_y == 100.0, "舷梯 9.5 m 长、落差 5.6 m（不陡过 32°）");
    check(std::abs(w.foot_x - (starboard_edge_x(ship) + 9.5)) < 1e-9, "梯脚在舷门外 9.5 m");
    w = plan_wild_landing(flat, ship, 0.0, 120.0, 0.0, 0.0, rules, -50.0, 0.0);
    check(w.ok && w.side == -1 && w.out_x == -1.0, "想让舷梯朝左边就放左舷");
    w = plan_wild_landing(flat, ship, 10.0, 120.0, 20.0, 1.5707963267948966);
    check(w.ok && std::abs(w.door_x - 10.0) < 1e-9 && std::abs(w.door_z - (20.0 - starboard_edge_x(ship))) < 1e-9 && std::abs(w.out_z + 1.0) < 1e-9,
            "船头朝西（yaw 90°）：右舷朝北");
    w = plan_wild_landing(flat, ship, 0.0, 150.0, 0.0, 0.0);
    check(!w.ok && w.why.find("离地太高") != std::string::npos, "离地 44 m：先降下来");

    TestLandingGround cliff = flat;
    cliff.h = [](double p_x, double, double &r_y) {
        r_y = 100.0;
        return p_x < 8.0;
    };
    w = plan_wild_landing(cliff, ship, 0.0, 110.0, 0.0, 0.0);
    check(w.ok && w.side == -1, "右舷外是崖：舷梯放左舷");
    cliff.h = [](double p_x, double, double &r_y) {
        r_y = 100.0;
        return p_x < 1.0;
    };
    w = plan_wild_landing(cliff, ship, 0.0, 110.0, 0.0, 0.0);
    check(!w.ok && w.why.find("悬空") != std::string::npos, "船底下一半悬空：不行");

    TestLandingGround wet = flat;
    wet.w = [](double p_x, double) { return p_x > 6.0 || p_x < -6.0; };
    w = plan_wild_landing(wet, ship, 0.0, 110.0, 0.0, 0.0);
    check(!w.ok && w.why.find("水") != std::string::npos, "两舷外都是水：放不下舷梯");

    TestLandingGround slope = flat;
    slope.h = [](double, double p_z, double &r_y) {
        r_y = 100.0 + 0.3 * p_z;
        return true;
    };
    w = plan_wild_landing(slope, ship, 0.0, 110.0, 0.0, 0.0);
    check(!w.ok && w.why.find("太不平") != std::string::npos, "顺着船身 30% 的坡：船底下高低差太大");
    slope.h = [](double p_x, double, double &r_y) {
        r_y = 100.0 + 0.2 * p_x;
        return true;
    };
    w = plan_wild_landing(slope, ship, 0.0, 110.0, 0.0, 0.0);
    check(w.ok && w.side == 1 && w.run < 9.5, "横着 20% 的坡：高的那一舷舷梯短：" + w.why);
    check(std::abs(w.deck_y - (100.0 + 0.2 * 2.6 + rules.deck_lift_m)) < 1e-9, "甲板按船底下最高处定");

    TestLandingGround mound = flat;
    mound.h = [](double p_x, double, double &r_y) {
        r_y = std::abs(p_x) > 4.0 && std::abs(p_x) < 7.0 ? 104.0 : 100.0; // 两舷外各一道土埂
        return true;
    };
    w = plan_wild_landing(mound, ship, 0.0, 110.0, 0.0, 0.0);
    check(w.ok && w.foot_y == 104.0 && w.run < 3.6, "舷门外是土埂：舷梯短短一段搭到埂上");
    mound.h = [](double p_x, double, double &r_y) {
        r_y = std::abs(p_x) > 5.0 && std::abs(p_x) < 6.0 ? 105.4 : 100.0; // 埂顶快到甲板高：落不下脚，往外搭又顶着梯板
        return true;
    };
    w = plan_wild_landing(mound, ship, 0.0, 110.0, 0.0, 0.0);
    check(!w.ok && w.why.find("顶着梯板") != std::string::npos, "舷门外一道高埂顶着梯板：放不下（" + w.why + "）");
    mound.h = [](double p_x, double, double &r_y) {
        r_y = p_x > 5.0 && p_x < 6.0 ? 105.4 : 100.0; // 只有右舷外有
        return true;
    };
    w = plan_wild_landing(mound, ship, 0.0, 110.0, 0.0, 0.0);
    check(w.ok && w.side == -1, "只有右舷外有埂：放左舷");
}

void test_item_slots() {
    std::printf("item slots\n");
    // 物品表：叠数、颜色、工具
    check(catalog::stack_of("stone") == 64 && catalog::stack_of("stone_axe") == 1 && catalog::stack_of("coin") == 999 && catalog::stack_of("nothing") == 64, "一格叠几份");
    bool items_ok = true;
    for (int k = 0; k < catalog::ITEM_COUNT; k++) {
        const ItemDef &d = catalog::ITEMS[k];
        items_ok = items_ok && d.stack >= 1 && d.r >= 0.0f && d.r <= 1.0f && d.g >= 0.0f && d.g <= 1.0f && d.b >= 0.0f && d.b <= 1.0f;
        for (int j = 0; j < k; j++) {
            items_ok = items_ok && std::strcmp(catalog::ITEMS[j].id, d.id) != 0;
        }
    }
    check(items_ok, "物品表：叠数至少 1、颜色在 0–1、id 不重");
    float r = 0, g = 0, b = 0, r2 = 0, g2 = 0, b2 = 0;
    catalog::item_color("clay", r, g, b);
    check(deposits::product_color("clay", r2, g2, b2) && r == r2 && g == g2 && b == b2 && !deposits::product_color("plank", r2, g2, b2), "原料的颜色就是物品表里的；木板不是原料");
    check(catalog::tool_speed("stone_axe", "timber") == 2.0 && catalog::tool_speed("stone_axe", "stone") == 1.0 && catalog::tool_speed("stone_pick", "floatstone") == 2.0 &&
                    catalog::tool_speed("stone_pick", "tin_ore") == 2.0 && catalog::tool_speed("wood_shovel", "clay") == 2.0 && catalog::tool_speed("plank", "timber") == 1.0 &&
                    catalog::tool_speed("stone_pick", "stone_") == 1.0,
            "工具只对口的原料快（整词比）");

    // 放：先并旧叠，再占主区空格，最后才占快捷栏
    ItemSlots pack(36, 9);
    check(pack.size() == 36 && pack.empty() && pack.room_for("stone") == 36 * 64 && pack.room_for("stone_axe") == 36, "空背包 36 格");
    check(pack.put("millet", 5) == 0 && pack.at(9).item == "millet" && pack.at(0).empty(), "新东西进主区第一格，快捷栏空着");
    pack.set(0, "stone", 60);
    check(pack.put("stone", 10) == 0 && pack.at(0).count == 64 && pack.at(10).item == "stone" && pack.at(10).count == 6, "先把快捷栏上那叠添满，余下的占主区空格");
    check(pack.count("stone") == 70 && pack.used() == 3, "数得清");
    check(pack.put("stone_axe", 2) == 0 && pack.at(11).item == "stone_axe" && pack.at(12).item == "stone_axe" && pack.at(11).count == 1, "工具一格一把");
    check(pack.put("", 3) == 0 && pack.put("stone", 0) == 0 && pack.used() == 5, "空名字、0 份不做事");
    // 拿：全够才拿，从后往前
    check(!pack.take("stone", 71) && pack.count("stone") == 70, "不够就不动");
    check(pack.take("stone", 10) && pack.at(10).empty() && pack.at(0).count == 60, "从后往前拿：主区那叠先拿光、快捷栏的后动");
    check(pack.take_at(0, 100) == 60 && pack.at(0).empty() && pack.at(0).item.empty(), "按格拿，拿光了格子空出来");
    // 塞满：放不下的份数退回来
    ItemSlots small(2);
    check(small.put("clay", 100) == 0 && small.put("clay", 40) == 12 && small.count("clay") == 128 && small.room_for("clay") == 0, "两格黏土满了 128，多的 12 份退回");
    check(small.put("stone", 1) == 1, "满了别的也放不进");

    // MC 的点法
    ItemSlots bag(4);
    bag.set(0, "stone", 10);
    bag.set(1, "clay", 5);
    bag.set(2, "stone", 60);
    ItemStack cursor;
    slots::left_click(bag, 0, cursor);
    check(cursor.item == "stone" && cursor.count == 10 && bag.at(0).empty(), "左键空手：拿起整叠");
    slots::left_click(bag, 2, cursor);
    check(bag.at(2).count == 64 && cursor.count == 6, "左键同样物品：并到满为止，余下的还拖着");
    slots::left_click(bag, 1, cursor);
    check(bag.at(1).item == "stone" && bag.at(1).count == 6 && cursor.item == "clay" && cursor.count == 5, "左键不同物品：对调");
    slots::left_click(bag, 3, cursor);
    check(bag.at(3).item == "clay" && cursor.empty() && cursor.item.empty(), "左键空格：放下");
    slots::right_click(bag, 2, cursor);
    check(cursor.count == 32 && bag.at(2).count == 32, "右键空手：拿一半（多的那一半）");
    ItemStack odd;
    bag.set(0, "clay", 5);
    slots::right_click(bag, 0, odd);
    check(odd.count == 3 && bag.at(0).count == 2, "5 份拿一半是 3 份");
    slots::right_click(bag, 0, odd);
    check(odd.count == 2 && bag.at(0).count == 3, "右键拖着同样物品：放一份");
    slots::right_click(bag, 2, cursor);
    check(cursor.count == 31 && bag.at(2).count == 33, "右键往自己那叠放一份");
    bag.clear(1);
    slots::right_click(bag, 1, cursor);
    check(bag.at(1).item == "stone" && bag.at(1).count == 1 && cursor.count == 30, "右键空格：放一份");
    slots::right_click(bag, 0, cursor);
    check(bag.at(0).item == "stone" && bag.at(0).count == 30 && cursor.item == "clay" && cursor.count == 3, "右键不同物品：对调");
    ItemSlots full(1);
    full.set(0, "clay", 64);
    ItemStack one{ "clay", 1 };
    slots::right_click(full, 0, one);
    check(full.at(0).count == 64 && one.count == 1, "那叠满了：右键放不进");

    // Shift 点：整叠搬到另一边（先并、再空格）；同一个容器里在两段之间搬
    ItemSlots hold(3);
    hold.set(1, "stone", 50);
    ItemSlots from(2);
    from.set(0, "stone", 30);
    check(slots::move_stack(from, 0, hold) == 30 && hold.at(1).count == 64 && hold.at(0).item == "stone" && hold.at(0).count == 16 && from.at(0).empty(),
            "搬 30 份：先把 50 那叠添满，余下 16 份占第一个空格");
    ItemSlots tight(1);
    tight.set(0, "stone", 60);
    from.set(1, "stone", 10);
    check(slots::move_stack(from, 1, tight) == 4 && from.at(1).count == 6, "那边只放得下 4 份：余下 6 份留在原格");
    ItemSlots p2(36, 9);
    p2.set(3, "timber", 12);
    check(slots::move_stack(p2, 3, p2, 9, 36) == 12 && p2.at(9).item == "timber" && p2.at(3).empty(), "快捷栏 → 主区");
    check(slots::move_stack(p2, 9, p2, 0, 9) == 12 && p2.at(0).item == "timber" && p2.at(9).empty(), "主区 → 快捷栏");
    p2.set(9, "timber", 5);
    check(slots::move_stack(p2, 9, p2, 9, 36) == 5 && p2.at(9).empty() && p2.at(10).count == 5, "同一段里搬：搬到别的空格，不搬回自己那格");
    // 数字键对调、拖着的放回去
    ItemSlots other(2);
    other.set(0, "clay", 7);
    slots::swap(other, 0, p2, 1);
    check(other.at(0).empty() && p2.at(1).item == "clay" && p2.at(1).count == 7, "两个容器之间对调");
    ItemStack back{ "plank", 70 };
    ItemSlots one_slot(1);
    ItemSlots spill(2);
    check(slots::return_cursor(back, one_slot, &spill) == 0 && one_slot.count("plank") == 64 && spill.count("plank") == 6 && back.empty(), "拖着的放回去：先背包、再另一边");

    // 改格数：变少时先并叠往前挪，挪不下就不改
    ItemSlots bay(24);
    bay.set(0, "stone", 10);
    bay.set(20, "stone", 5);
    bay.set(23, "clay", 3);
    check(bay.resize(12) && bay.size() == 12 && bay.count("stone") == 15 && bay.count("clay") == 3 && bay.used() == 2, "24 → 12 格：尾巴上的并到前面");
    ItemSlots crowded(3);
    crowded.set(0, "stone", 1);
    crowded.set(1, "clay", 1);
    crowded.set(2, "timber", 1);
    check(!crowded.resize(2) && crowded.size() == 3, "三样东西挤不进两格：不改");
    check(crowded.resize(5) && crowded.size() == 5 && crowded.at(2).item == "timber", "变多：原样");

    // 跑货认格子背包：拆箱放不下就不拆，运费放不下就不交
    ItemSlots tiny(1);
    tiny.set(0, "stone", 62);
    CargoBook book;
    book.set_slot_count(4);
    check(book.pack_goods("stone", 5, tiny).empty() && tiny.count("stone") == 57, "格子背包打一箱");
    tiny.set(0, "clay", 64);
    check(book.unpack_goods(tiny).find("放不下") != std::string::npos && book.is_holding_goods(), "背包满了：箱子拆不开，还抱着");
    tiny.clear(0);
    check(book.unpack_goods(tiny).empty() && tiny.count("stone") == 5, "腾出地方就拆了");
}

void test_recipes() {
    std::printf("recipes\n");
    bool table_ok = true;
    for (int k = 0; k < recipes::RECIPE_COUNT; k++) {
        const RecipeDef &r = recipes::RECIPES[k];
        table_ok = table_ok && catalog::item(r.product) && r.yield >= 1 && recipes::ingredient_count(r) >= 1 && recipes::find(r.id) == &r;
        for (int i = 0; i < recipes::ingredient_count(r); i++) {
            table_ok = table_ok && catalog::item(r.in[i].item) && r.in[i].count >= 1;
        }
    }
    check(table_ok, "配方表：产物与料都在物品表里、份数至少 1、id 找得到");
    const RecipeDef *plank = recipes::find("plank");
    const RecipeDef *bench = recipes::find("workbench");
    const RecipeDef *chest = recipes::find("chest");
    const RecipeDef *pottery = recipes::find("pottery");
    check(plank && bench && chest && pottery && !recipes::find("nothing"), "找配方");
    check(std::string(recipes::station_name(Station::KILN)) == "窑", "设施名");

    ItemSlots pack(36, 9);
    pack.put("timber", 3);
    check(recipes::times(*plank, pack) == 3 && recipes::times(*bench, pack) == 0, "料够合几回");
    check(recipes::craft(*plank, 0, pack).empty() && pack.count("timber") == 2 && pack.count("plank") == 4, "随身：1 份木料劈 4 块板");
    check(recipes::craft(*bench, 0, pack).empty() && pack.count("plank") == 0 && pack.count("workbench") == 1, "4 块板钉一张工作台");
    check(recipes::craft(*bench, 0, pack) == "木板不够（0 / 4）", "料不够说差多少：" + recipes::craft(*bench, 0, pack));
    pack.put("plank", 8);
    check(recipes::problem(*chest, 0, pack) == "要在工作台旁边做" && pack.count("plank") == 8, "木箱要工作台：身边没有就不做");
    check(recipes::craft(*chest, station_bit(Station::WORKBENCH), pack).empty() && pack.count("chest") == 1 && pack.count("plank") == 0, "工作台旁：8 块板一只木箱");
    check(recipes::station_ok(*pottery, station_bit(Station::KILN)) && !recipes::station_ok(*pottery, station_bit(Station::WORKBENCH)), "陶罐只认窑");

    // 放不下就不合：一格背包里 64 块板，木料劈了还有地方？
    ItemSlots one(1);
    one.set(0, "timber", 1);
    check(recipes::craft(*plank, 0, one).empty() && one.count("plank") == 4 && one.count("timber") == 0, "料用光了腾出的格子放产物");
    ItemSlots two(2);
    two.set(0, "timber", 2);
    two.set(1, "plank", 62);
    check(recipes::craft(*plank, 0, two) == "背包放不下木板" && two.count("timber") == 2, "产物放不下：不做，料不扣");
    // 拖着的那叠要有地方放回去
    ItemSlots three(2);
    three.set(0, "timber", 2); // 合完还剩 1 份木料占着这格
    const ItemStack dragged{ "clay", 10 }; // 从第 2 格拿起来的
    check(recipes::problem(*plank, 0, three, dragged).find("拖着的") != std::string::npos, "合完没地方放回拖着的黏土：不合");
    ItemSlots side(1);
    check(recipes::problem(*plank, 0, three, dragged, &side).empty(), "另一边（船舱）放得下就行");
}

void test_placeables() {
    std::printf("placeables\n");
    bool table_ok = true;
    for (int k = 0; k < props::PROP_COUNT; k++) {
        const PropDef &d = props::PROPS[k];
        table_ok = table_ok && catalog::item(d.item) && d.width > 0.0 && d.depth > 0.0 && d.height > 0.0;
        bool craftable = false;
        for (int i = 0; i < recipes::RECIPE_COUNT; i++) {
            craftable = craftable || std::string(recipes::RECIPES[i].product) == d.item;
        }
        table_ok = table_ok && craftable;
    }
    check(table_ok, "摆放物都在物品表里、有尺寸、合得出来");
    check(props::placeable("chest") && props::find("chest")->slots == 18 && props::find("workbench")->station == int(Station::WORKBENCH) && !props::placeable("plank"), "木箱 18 格、工作台是设施、木板不能摆");
    // 朝向：人朝北（yaw 0）看，物件正面朝人（朝南）= quarter 0；转一下 90°
    check(facing_quarter(0.0, 0) == 0 && facing_quarter(1.6, 0) == 1 && facing_quarter(-1.6, 0) == 3 && facing_quarter(3.1, 0) == 2 && facing_quarter(0.0, 1) == 1 &&
                    facing_quarter(0.0, -1) == 3,
            "朝向取整到 90°、转几下");
    double hx = 0, hz = 0;
    prop_half_extent(*props::find("fence"), 1, hx, hz);
    check(std::abs(hx - 0.15) < 1e-9 && std::abs(hz - 1.0) < 1e-9, "篱笆转 90° 占地换过来");

    struct FnGround : LandingGround {
        std::function<bool(double, double, double &)> h;
        std::function<bool(double, double)> w;
        bool height(double p_x, double p_z, double &r_y) const override { return h(p_x, p_z, r_y); }
        bool water(double p_x, double p_z) const override { return w ? w(p_x, p_z) : false; }
    };
    FnGround flat;
    flat.h = [](double, double, double &r_y) {
        r_y = 100.0;
        return true;
    };
    const PlaceRules rules;
    Placements placed;
    const PropDef &bench = *props::find("workbench");
    // 人在 (0.1, 0.2) 朝北（−z）看：工作台深 0.8，中心在北边 1 + 0.4 m 处（z = −1.2），按 0.5 m 对齐 → z = −1.0
    PlaceCheck c = plan_placement(flat, bench, 0.1, 0.2, 0.0, 0, placed, rules);
    check(c.ok && std::abs(c.x - 0.0) < 1e-9 && std::abs(c.z + 1.0) < 1e-9 && c.y == 100.0 && c.quarter == 0, "平地：摆在身前、对齐到 0.5 m");
    const int bench_id = placed.add("workbench", c);
    check(placed.size() == 1 && placed.find(bench_id) && placed.get_revision() == 1, "记下一件");
    PlaceCheck again = plan_placement(flat, bench, 0.1, 0.2, 0.0, 0, placed, rules);
    check(!again.ok && again.why == "和工作台挤在一起了", "同一处再摆：挤在一起（" + again.why + "）");
    // 往东挪 1.5 m（工作台宽 1.4）：贴着摆可以
    check(plan_placement(flat, bench, 1.5, 0.2, 0.0, 0, placed, rules).ok, "挨着摆，中间留 0.1 m：行");
    // 看着它：身前扇面、够得着
    check(placed.facing(0.0, 100.0, 0.0, 0.0, rules) == bench_id, "朝北看得见身前的工作台");
    check(placed.facing(0.0, 100.0, 0.0, 3.14159, rules) == -1, "背对着看不见");
    check(placed.facing(0.0, 100.0, 3.0, 0.0, rules) == -1, "离得远够不着（离边 3.6 m）");
    check(placed.stations_near(0.0, 100.0, 0.0, rules.station_range_m) == station_bit(Station::WORKBENCH) && placed.stations_near(0.0, 100.0, 9.0, 3.0) == 0,
            "身边 3 m 内有工作台；远了没有");
    // 地面：崖外、水、不平
    FnGround cliff;
    cliff.h = [](double, double p_z, double &r_y) {
        r_y = 100.0;
        return p_z > -1.6;
    };
    PlaceCheck edge = plan_placement(cliff, bench, 0.0, 0.0, 0.0, 0, Placements(), rules);
    check(!edge.ok && edge.why == "这儿没有地", "占地一角出了崖边：不行");
    FnGround wet = flat;
    wet.w = [](double, double p_z) { return p_z < -1.2; };
    check(plan_placement(wet, bench, 0.0, 0.0, 0.0, 0, Placements(), rules).why == "水里摆不了", "水里不行");
    FnGround slope;
    slope.h = [](double p_x, double, double &r_y) {
        r_y = 100.0 + 0.5 * p_x; // 东高西低：工作台宽 1.4 → 高低差 0.7
        return true;
    };
    PlaceCheck steep = plan_placement(slope, bench, 0.0, 0.0, 0.0, 0, Placements(), rules);
    check(!steep.ok && steep.why == "地不平（高低差 0.7 m）", "坡上：高低差 0.7 m 不行（" + steep.why + "）");
    PlaceCheck turned = plan_placement(slope, bench, 0.0, 0.0, 0.0, 1, Placements(), rules);
    check(turned.ok && turned.quarter == 1 && std::abs(turned.y - turned.low - 0.4) < 1e-9, "转 90° 顺着坡摆：高低差 0.4 m 行，底座垫到低处");
    // 拆：木箱里有东西不让拆
    const PropDef &chest = *props::find("chest");
    PlaceCheck spot = plan_placement(flat, chest, 5.0, 5.0, 0.0, 0, placed, rules);
    const int chest_id = placed.add("chest", spot);
    check(placed.find(chest_id)->contents.size() == 18 && pickup_problem(*placed.find(chest_id)).empty(), "空木箱 18 格，能拆");
    placed.find(chest_id)->contents.put("stone", 3);
    check(pickup_problem(*placed.find(chest_id)) == "木箱里还有东西，先搬空", "木箱里有东西：不让拆");
    check(placed.remove(bench_id) && !placed.find(bench_id) && placed.size() == 1 && placed.get_revision() == 3 && !placed.remove(bench_id), "拆掉工作台");
}

void test_ship_bay() {
    std::printf("ship bay\n");
    check(layout_stats(default_layout()).bay == 0, "开局小艇没有储存区：没有船舱");
    ShipLayout l = default_layout();
    check(layout_place(l, Cell{ 0, 0, 1 }, ModuleKind::STORAGE).empty() && layout_stats(l).bay == 12 && layout_stats(l).hold == 4, "加一节储存区：船舱 12 格、货位 4 个");
    check(layout_contains(l, 0.0, 0.2, 0.0) && layout_contains(l, 3.0, 1.0, 7.0) && !layout_contains(l, 4.0, 0.0, 0.0) && !layout_contains(l, 0.0, 0.0, 11.0), "人在不在船上：按列的格子");
    check(!layout_contains(l, 0.0, 3.0, 0.0) && !layout_contains(l, 0.0, -2.0, 0.0), "高过甲板 2.5 m、低过船底：不算");
}

int main() {
    test_rng();
    test_noise();
    test_island("新岛 · 火山锥", 0.1, IslandAge::YOUNG);
    test_island("中年 · 山脊", 0.5, IslandAge::MID);
    test_island("老岛 · 台地", 0.8, IslandAge::OLD);
    test_river("老岛 · 台地", 0.8);
    test_river("中年 · 山脊", 0.5);
    test_river("新岛 · 火山锥", 0.1);
    test_trail("老岛 · 台地", 0.8);
    test_trail("中年 · 山脊", 0.5);
    test_trail("新岛 · 火山锥", 0.1);
    test_yard("老岛 · 台地", 0.8);
    test_yard("中年 · 山脊", 0.5);
    test_yard("新岛 · 火山锥", 0.1);
    test_ship_layout();
    test_inventory();
    test_crops();
    test_farm();
    test_flight();
    test_settlement();
    test_villagers();
    test_village_sim();
    test_world_grid();
    test_world_terrain();
    test_cover_blend();
    test_terrain_tiles();
    test_map_image();
    test_world_2051();
    test_terrain_v3_2051();
    test_dock_site();
    test_village_plan();
    test_world_sites();
    test_world_rivers();
    test_map_grounds();
    test_layout_edits();
    test_house_templates();
    test_freight();
    test_gather();
    test_scatter();
    test_scatter_ecotone();
    test_wild_landing();
    test_item_slots();
    test_recipes();
    test_placeables();
    test_ship_bay();
    test_goldens();
    std::printf("%d 项检查，%d 项失败\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
