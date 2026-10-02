// 纯算法文件（不含 Godot 头的那几个）的单元测试。不引测试框架：一个可执行文件，失败即非零退出。
//
//   cmake --build build --config Debug --target zhouzhu_core_tests
//   ctest --test-dir build -C Debug --output-on-failure

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall SetErrorMode(unsigned long);
#endif

#include "build_pieces.h"
#include "ship_cabins.h"
#include "cover_blend.h"
#include "earthworks.h"
#include "ground_works.h"
#include "piece_mesh.h"
#include "structure_styles.h"
#include "dev_manual.h"
#include "god_works.h"
#include "terrain_edits.h"
#include "group_memory.h"
#include "crops.h"
#include "deposit_kinds.h"
#include "day_clock.h"
#include "field_book.h"
#include "dock_site.h"
#include "far_group_mesh.h"
#include "farm_model.h"
#include "flat_ground.h"
#include "flight_model.h"
#include "freight.h"
#include "building_mesh.h"
#include "building_parts.h"
#include "house_template.h"
#include "interior_rules.h"
#include "inventory.h"
#include "item_slots.h"
#include "island_field.h"
#include "island_ground.h"
#include "job_pool.h"
#include "land_berth.h"
#include "local_season.h"
#include "map_ground.h"
#include "map_image.h"
#include "placeables.h"
#include "recipes.h"
#include "settlement_model.h"
#include "refit_pick.h"
#include "ship_layout.h"
#include "stream_meter.h"
#include "template_set.h"
#include "terrain_tile.h"
#include "value_noise.h"
#include "village_plan.h"
#include "village_prep.h"
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
#include "weather.h"
#include "weather_noise.h"
#include "wild_landing.h"
#include "world_terrain.h"
#include "almanac.h"
#include "planet_data.h"
#include "planet_frame.h"
#include "planet_sky.h"
#include "planet_surface.h"
#include "planet_weather.h"
#include "inflate.h"
#include "cloud_band.h"
#include "sea_state.h"
#include "gen_config.h"
#include "gen_core.h"
#include "gen_export.h"
#include "py_format.h"
#include "scipy_edt.h"
#include "skyisle/island/generate.hpp"
#include "skyisle/island/coast.hpp"
#include "skyisle/planet/view.hpp"
#include "story_start.h"

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

// 仓库里带细版的两群：开局那群与它最近的邻群（开局配置 story_start.h；PLAN-LAND P0 起是 #6329、#6327，以前是 #2051、#2050）。
// 下面「开局那群」的金标（岛数、码头号、村号、户数……）都是这一群的，换开局或重导要重记。
constexpr int START = story_start::SPAWN_NODE;
constexpr int NEIGHBOR = story_start::NEIGHBOR_NODE;
std::string group_dir(int p_node) {
    return std::string(ZHOUZHU_SOURCE_DIR) + "/" + story_start::GROUPS_REL + "/" + std::to_string(p_node);
}
std::string start_dir() {
    return group_dir(START);
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
    check(small.node_count() == 2 && small.cell_count() == 2 && small.at({ 0, 0, -1 }) && small.at({ 0, 0, -1 })->kind == ModuleKind::SAIL && small.at(GATE)->kind == ModuleKind::BRIDGE,
            "默认小艇：帆在舷门格前面，驾驶区在舷门格");
    check(layout_problem(small).empty(), "默认小艇合法");
    check(modules::by_id("bridge") && modules::by_id("storage") && modules::by_id("sail") && modules::by_id("long_sail") && !modules::by_id("cabin"), "按 id 查节点");
    check(modules::by_id("power") == &modules::def(ModuleKind::SAIL), "旧名 power 认作帆");
    check(std::string(modules::def(ModuleKind::STORAGE).name) == "储存区", "节点显示名");

    // 节点表：占格、说法
    check(modules::shape_name(modules::def(ModuleKind::SAIL)) == "单节" && modules::shape_name(modules::def(ModuleKind::LONG_SAIL)) == "前后双节" &&
                    modules::shape_name(modules::def(ModuleKind::TALL_HOLD)) == "上下双节" && modules::shape_name(modules::def(ModuleKind::WIDE_DECK)) == "左右双节",
            "形状：单节 / 前后双节 / 上下双节 / 左右双节");
    check(modules::rule_tags(modules::def(ModuleKind::BRIDGE)) == std::vector<std::string>({ "只能在最上面", "只能有一个", "拆不得" }), "驾驶区的规矩：只能在最上面、只能有一个、拆不得");
    check(modules::rule_tags(modules::def(ModuleKind::SAIL)) == std::vector<std::string>({ "只能在最上面" }) &&
                    modules::rule_tags(modules::def(ModuleKind::FLOATSTONE)) == std::vector<std::string>({ "只能在最下面一层" }) &&
                    modules::rule_tags(modules::def(ModuleKind::STORAGE)).empty(),
            "帆只能在最上面、浮石舱只能在最下面一层、储存区没有规矩");
    check(modules::function_text(modules::def(ModuleKind::DECK)) == "没有功能：能摆东西 · 盖住了能进" &&
                    modules::function_text(modules::def(ModuleKind::WIDE_DECK)) == "没有功能：能摆东西 · 盖住了能进",
            "空节、宽空节没有功能、能摆东西、盖住了能进");
    check(modules::function_text(modules::def(ModuleKind::STORAGE)) == "船舱 12 格 · 货位 4 · 盖住了能进" && modules::function_text(modules::def(ModuleKind::SAIL)) == "推力 1.0" &&
                    modules::function_text(modules::def(ModuleKind::BRIDGE)) == "舵在这儿" && modules::function_text(modules::def(ModuleKind::TALL_HOLD)) == "船舱 30 格 · 货位 8 · 下层能进" &&
                    modules::function_text(modules::def(ModuleKind::FLOATSTONE)) == "推力 0.7",
            "功能说法");
    check(modules::def(ModuleKind::STORAGE).enterable && modules::def(ModuleKind::TALL_HOLD).enterable && modules::def(ModuleKind::DECK).enterable &&
                    modules::def(ModuleKind::WIDE_DECK).enterable && !modules::def(ModuleKind::FLOATSTONE).enterable,
            "货舱类与空节盖住了能进，浮石舱封死");
    check(modules::def(ModuleKind::DECK).furnish && modules::def(ModuleKind::WIDE_DECK).furnish && !modules::def(ModuleKind::STORAGE).furnish &&
                    !modules::def(ModuleKind::BRIDGE).furnish && !modules::def(ModuleKind::SAIL).furnish,
            "只有空节、宽空节能摆东西");
    check(footprint({ 0, 0, 2 }, ModuleKind::TALL_HOLD) == std::vector<Cell>({ { 0, 0, 2 }, { 0, 1, 2 } }) &&
                    footprint({ 0, 1, -1 }, ModuleKind::LONG_SAIL) == std::vector<Cell>({ { 0, 1, -1 }, { 0, 1, 0 } }) &&
                    footprint({ -1, 0, 1 }, ModuleKind::WIDE_DECK) == std::vector<Cell>({ { -1, 0, 1 }, { 0, 0, 1 } }),
            "复合节占的格子：上下 / 前后 / 左右各两节，锚点是最小的那一格");

    // 船况
    const ShipStats s0 = layout_stats(small);
    check(s0.sections == 2 && s0.can_fly() && s0.hold == 0 && s0.bay == 0, "小艇能飞、没货舱");
    check_near(s0.max_speed, 32.0, 0.05, "小艇航速 = 切片一的 32 m/s");
    check_near(s0.max_climb, 10.0, 0.05, "小艇升速 = 切片一的 10 m/s");
    check_near(s0.max_yaw_rate, 0.75, 1e-9, "小艇转向率 = 切片一的 0.75");
    ShipLayout cargo = small;
    check(layout_place(cargo, { 0, 0, 1 }, ModuleKind::STORAGE).empty() && cargo.cell_count() == 3, "舷门格后面加一节储存区");
    const ShipStats s1 = layout_stats(cargo);
    check(s1.hold == 4 && s1.bay == 12 && s1.max_speed < s0.max_speed && s1.max_climb < s0.max_climb && s1.max_yaw_rate < s0.max_yaw_rate, "多一节货：有货舱，但更慢、更钝");
    ShipLayout strong = cargo;
    check(layout_place(strong, { 0, 0, -2 }, ModuleKind::SAIL).empty() && layout_stats(strong).max_speed > s1.max_speed, "再加一面帆：更快");
    ShipLayout dead({ { { 0, 0, 0 }, ModuleKind::BRIDGE }, { { 0, 0, 1 }, ModuleKind::STORAGE } });
    check(layout_problem(dead).empty() && !layout_stats(dead).can_fly() && layout_stats(dead).max_speed == 0.0, "没有帆也没有浮石舱：合法，但飞不起来");
    ShipLayout bare = small;
    check(layout_place(bare, { 0, 0, 1 }, ModuleKind::DECK).empty() && layout_stats(bare).hold == 0 && layout_stats(bare).bay == 0 && layout_stats(bare).thrust == 1.0,
            "空节：没有货位、船舱、推力，只有分量");

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
    check(layout_place(tall, { 0, 1, 0 }, ModuleKind::STORAGE) == "驾驶区只能在最上面：顶上不能再搭", "驾驶区顶上不能搭");
    check(layout_place(tall, { 0, 1, -1 }, ModuleKind::STORAGE) == "帆只能在最上面：顶上不能再搭", "帆顶上不能搭");
    check(layout_place(tall, { 0, 2, 1 }, ModuleKind::SAIL).empty() && layout_remove(tall, { 0, 2, 1 }).empty(), "帆搭在第三层：在最上面就行");
    check(layout_place(tall, { -1, 1, 1 }, ModuleKind::STORAGE) == "悬空了：下面没有节撑着", "悬空被拒");
    check(layout_place(tall, { -1, 0, 1 }, ModuleKind::STORAGE) == "舷门那一排得是最宽的一排，不然靠不上码头", "先加宽舷门排");
    check(layout_place(tall, { -1, 0, 0 }, ModuleKind::STORAGE).empty(), "舷门排左舷加一节");
    check_near(port_edge_x(tall), -10.5, 1e-9, "左舷边到了 −10.5");
    check_near(starboard_edge_x(tall), 3.5, 1e-9, "右舷边还是 3.5");
    check(layout_place(tall, { -1, 0, 1 }, ModuleKind::SAIL).empty(), "现在左舷后面也能加");
    check(layout_place(tall, { -1, 1, 0 }, ModuleKind::STORAGE) == "舷门两头的格子上面不能搭：跳板要接甲板", "舷门排两头不能往上搭");
    check(layout_remove(tall, { 0, 0, 1 }) == "上面还搭着节，先拆上面的", "拆支撑被拒");
    check(layout_remove(tall, { 0, 1, 1 }).empty() && tall.levels() == 1, "先拆上面的");
    check(layout_remove(tall, GATE) == "驾驶区拆不得", "驾驶区拆不得");
    check(layout_remove(tall, { 1, 1, 1 }) == "这一格没有节", "空格拆不了");
    check(layout_place(tall, { 1, 0, 1 }, ModuleKind::STORAGE) == "舷门那一排得是最宽的一排，不然靠不上码头", "右舷伸出去也不行");
    check(layout_place(tall, { 0, 0, 3 }, ModuleKind::STORAGE) == "有的节没和船身连上", "隔着空格不相连");
    check(layout_place(tall, { 0, 0, 4 }, ModuleKind::STORAGE) == "超出了船坞能造的范围", "越界");
    check(layout_place(tall, GATE, ModuleKind::STORAGE) == "这里已经有节了", "占着的格放不了");
    check(layout_place(tall, { 0, 0, 2 }, ModuleKind::BRIDGE) == "驾驶区只能有一个", "驾驶区只能有一个");
    check(layout_problem(ShipLayout({ { { 0, 0, 0 }, ModuleKind::STORAGE } })) == "没有驾驶区，没处掌舵", "少不了驾驶区");

    // 浮石舱：只能在最下面一层，能被压着
    ShipLayout stone = cargo;
    check(layout_place(stone, { 0, 0, 2 }, ModuleKind::FLOATSTONE).empty() && layout_place(stone, { 0, 1, 2 }, ModuleKind::STORAGE).empty(), "浮石舱在第 0 层，上面还能搭");
    check(layout_place(stone, { 0, 1, 1 }, ModuleKind::FLOATSTONE) == "浮石舱只能在最下面一层", "浮石舱搭不上第二层");
    check(layout_stats(stone).thrust > layout_stats(cargo).thrust, "浮石舱出推力");

    // 复合节：前后双节、上下双节、左右双节
    ShipLayout combo = cargo;
    check(layout_place(combo, { 0, 0, 2 }, ModuleKind::TALL_HOLD).empty() && combo.cell_count() == 5 && combo.node_count() == 4 && combo.column_height(0, 2) == 2,
            "双层货舱一放就是上下两节");
    check(combo.at({ 0, 1, 2 }) == combo.at({ 0, 0, 2 }) && combo.at({ 0, 1, 2 })->kind == ModuleKind::TALL_HOLD, "两节属于同一个节点");
    const ShipStats sc = layout_stats(combo);
    check(sc.bay == 42 && sc.hold == 12 && sc.sections == 5, "双层货舱：船舱 30 格、货位 8（加储存区一共 42 格、12 个）");
    const std::vector<HoldSlot> combo_slots = hold_slots(combo);
    check(combo_slots.size() == 12 && combo_slots[4].cell == Cell{ 0, 0, 2 } && combo_slots[4].open && combo_slots[8].cell == Cell{ 0, 1, 2 } && combo_slots[8].open,
            "双层货舱下层是能进的舱，货位也够得着");
    check(layout_place(combo, { 0, 1, -1 }, ModuleKind::LONG_SAIL) == "悬空了：下面没有节撑着" || layout_place(combo, { 0, 1, -1 }, ModuleKind::LONG_SAIL) == "帆只能在最上面：顶上不能再搭",
            "双桅帆搭在帆与驾驶区顶上：不行");
    check(layout_place(combo, { 0, 0, -3 }, ModuleKind::LONG_SAIL).empty() && combo.cell_count() == 7 && combo.min_z() == -3, "双桅帆放在帆前面，占两排");
    check(bow_z(combo) < bow_z(cargo) - 13.9, "船头往前伸了两排");
    check(layout_place(combo, { 0, 1, -2 }, ModuleKind::STORAGE) == "双桅帆只能在最上面：顶上不能再搭", "双桅帆的后一节上面也不能搭");
    check(layout_place(combo, { -1, 0, 0 }, ModuleKind::WIDE_DECK) == "这里已经有节了", "宽空节压着驾驶区：已经有节了");
    check(layout_remove(combo, { 0, 0, -2 }).empty() && combo.cell_count() == 5 && !combo.has({ 0, 0, -3 }), "拆双桅帆：点哪一节都拆掉整个");
    check(layout_remove(combo, { 0, 1, 2 }).empty() && !combo.has({ 0, 0, 2 }), "拆双层货舱：点上层也整个拆掉");
    ShipLayout wide = small;
    check(layout_place(wide, { -1, 0, 1 }, ModuleKind::WIDE_DECK) == "舷门那一排得是最宽的一排，不然靠不上码头", "宽空节放在舷门排后面：比舷门排宽");
    ShipLayout gate_wide({ { { 0, 0, -1 }, ModuleKind::SAIL }, { { 0, 0, 0 }, ModuleKind::BRIDGE }, { { -1, 0, 0 }, ModuleKind::DECK }, { { 1, 0, 0 }, ModuleKind::DECK } });
    check(layout_problem(gate_wide).empty() && layout_place(gate_wide, { -1, 0, 1 }, ModuleKind::WIDE_DECK).empty() && port_edge_x(gate_wide) < -10.0, "舷门排三列宽后，宽空节放得下");
    check(layout_place(gate_wide, { -1, 0, -1 }, ModuleKind::TALL_HOLD).empty() && layout_place(gate_wide, { 1, 0, -1 }, ModuleKind::TALL_HOLD).empty() &&
                    layout_problem(gate_wide).empty(),
            "双层货舱挨着舷门排也行（不在舷门排的两头）");
    ShipLayout end_tall = default_layout();
    check(layout_place(end_tall, { -1, 0, 0 }, ModuleKind::TALL_HOLD) == "舷门两头的格子上面不能搭：跳板要接甲板", "双层货舱放在舷门排头上：跳板接不上");

    // 上限
    ShipLayout full = small;
    for (int z = 1; z <= 3; z++) {
        for (int x = -1; x <= 1; x++) {
            if (full.cell_count() < MAX_CELLS) {
                if (x != 0 && !full.has({ x, 0, 0 })) {
                    layout_place(full, { x, 0, 0 }, ModuleKind::STORAGE);
                }
                layout_place(full, { x, 0, z }, ModuleKind::STORAGE);
            }
        }
    }
    check(full.cell_count() == MAX_CELLS && layout_problem(full).empty(), "堆到上限 " + std::to_string(MAX_CELLS) + " 节");
    check(layout_place(full, { 0, 1, 1 }, ModuleKind::STORAGE) == "最多 12 节" && full.cell_count() == MAX_CELLS, "第 13 节加不上");
    ShipLayout near_full = full;
    check(layout_remove(near_full, { 0, 0, 3 }).empty() && layout_place(near_full, { 0, 1, 1 }, ModuleKind::TALL_HOLD) != "" &&
                    layout_place(near_full, { 0, 1, 2 }, ModuleKind::STORAGE).empty(),
            "差一节到上限：双节的放不下，单节的放得下");

    // 落点
    auto has_spot = [](const std::vector<Cell> &p_spots, const Cell &p_cell) { return std::find(p_spots.begin(), p_spots.end(), p_cell) != p_spots.end(); };
    const std::vector<Cell> spots = layout_placements(small, ModuleKind::STORAGE);
    bool spots_ok = !spots.empty();
    for (const Cell &c : spots) {
        ShipLayout trial = small;
        spots_ok = spots_ok && !small.has(c) && layout_place(trial, c, ModuleKind::STORAGE).empty();
    }
    check(spots_ok, "落点都是能放的（储存区 " + std::to_string(spots.size()) + " 个）");
    check(has_spot(spots, { 0, 0, 1 }) && has_spot(spots, { 1, 0, 0 }) && has_spot(spots, { 0, 0, -2 }) && !has_spot(spots, { 0, 1, -1 }) && !has_spot(spots, { 0, 1, 0 }) &&
                    !has_spot(spots, { 1, 0, -1 }) && !has_spot(spots, { 0, 0, 2 }),
            "储存区的落点：后方、前方、舷门排两侧；帆与驾驶区顶上不行，隔空的不行");
    const std::vector<Cell> sail_spots = layout_placements(cargo, ModuleKind::SAIL);
    check(has_spot(sail_spots, { 0, 1, 1 }) && has_spot(sail_spots, { 0, 0, 2 }), "帆能放在储存区顶上（那就在最上面了）");
    const std::vector<Cell> stone_spots = layout_placements(cargo, ModuleKind::FLOATSTONE);
    bool stone_ok = !stone_spots.empty();
    for (const Cell &c : stone_spots) {
        stone_ok = stone_ok && c.y == 0;
    }
    check(stone_ok, "浮石舱的落点都在第 0 层");
    const std::vector<Cell> long_spots = layout_placements(cargo, ModuleKind::LONG_SAIL);
    check(has_spot(long_spots, { 0, 0, 2 }) && has_spot(long_spots, { 0, 0, -3 }) && !has_spot(long_spots, { 0, 0, -2 }) && !has_spot(long_spots, { 0, 1, 0 }),
            "双桅帆的落点：船尾接两节、船头接两节；压着帆、搭在驾驶区顶上的不行");
    const std::vector<Cell> tall_spots = layout_placements(cargo, ModuleKind::TALL_HOLD);
    check(has_spot(tall_spots, { 0, 0, 2 }) && !has_spot(tall_spots, { 1, 0, 0 }), "双层货舱的落点：船尾能放；舷门排头上不能");
    check(layout_placements(small, ModuleKind::BRIDGE).empty(), "已经有驾驶区：没有落点");

    // id 往返
    const std::vector<std::string> ids = layout_ids(cargo);
    check(ids.size() == 3 && ids[0] == "0,0,-1:sail" && ids[1] == "0,0,0:bridge" && ids[2] == "0,0,1:storage", "布局 → id（按 y, z, x 排）");
    bool ok = false;
    check(layout_from_ids(ids, &ok) == cargo && ok, "id → 布局");
    check(layout_from_ids({ "0,0,-1:power", "0,0,0:bridge" }, &ok) == small && ok, "旧布局的 power 读成帆");
    check(layout_from_ids({ "0,0,-1:sail", "cabin", "0,0,0:bridge" }, &ok).node_count() == 2 && !ok, "认不出的条目跳过并报告");
    Module m;
    check(!parse_module_id("1,2:sail", m) && !parse_module_id("1,2,3:cabin", m) && parse_module_id("-1,2,3:tall_hold", m) && m.at.x == -1 && m.at.z == 3 &&
                    m.kind == ModuleKind::TALL_HOLD,
            "解析条目");
    const ShipLayout back = layout_from_ids(layout_ids(gate_wide), &ok);
    check(ok && back == gate_wide && back.cell_count() == gate_wide.cell_count(), "带复合节的布局 id 往返");
}

void test_ship_cabins() {
    std::printf("ship_cabins\n");
    // 舷门那一排三节宽；后一排：宽空节（左、中）+ 浮石舱（右），上面再搭一排：空节、空节、储存区
    ShipLayout ship = default_layout();
    bool built = layout_place(ship, { -1, 0, 0 }, ModuleKind::DECK).empty() && layout_place(ship, { 1, 0, 0 }, ModuleKind::DECK).empty() &&
            layout_place(ship, { -1, 0, 1 }, ModuleKind::WIDE_DECK).empty() && layout_place(ship, { 1, 0, 1 }, ModuleKind::FLOATSTONE).empty() &&
            layout_place(ship, { -1, 1, 1 }, ModuleKind::DECK).empty() && layout_place(ship, { 0, 1, 1 }, ModuleKind::DECK).empty() &&
            layout_place(ship, { 1, 1, 1 }, ModuleKind::STORAGE).empty();
    check(built && layout_problem(ship).empty() && ship.cell_count() == 10, "搭出带舱的船：10 节两层");

    // 每一节是什么地方
    check(cell_space(ship, { -1, 0, 1 }) == CellSpace::ROOM && cell_space(ship, { 0, 0, 1 }) == CellSpace::ROOM, "被盖住的宽空节是一间能进的舱");
    check(cell_space(ship, { 1, 0, 1 }) == CellSpace::SOLID, "被盖住的浮石舱封死");
    check(cell_space(ship, { 0, 0, 0 }) == CellSpace::DECK && cell_space(ship, { -1, 1, 1 }) == CellSpace::DECK && cell_space(ship, { 0, 0, 3 }) == CellSpace::NONE,
            "顶上是开放甲板，没有的节是 NONE");

    // 舱的四面
    check(cell_side(ship, { -1, 0, 1 }, 1, 0) == SideKind::OPEN, "宽空节的两节之间不砌墙");
    check(cell_side(ship, { -1, 0, 1 }, -1, 0) == SideKind::WINDOW && cell_side(ship, { -1, 0, 1 }, 0, 1) == SideKind::WINDOW, "朝船外是带窗的墙");
    check(cell_side(ship, { -1, 0, 1 }, 0, -1) == SideKind::DOOR && cell_side(ship, { 0, 0, 1 }, 0, -1) == SideKind::DOOR, "朝甲板开门洞");
    check(cell_side(ship, { 0, 0, 1 }, 1, 0) == SideKind::WALL, "挨着封死的浮石舱是实墙");
    check(cell_side(ship, { -1, 0, 0 }, 0, 1) == SideKind::DOOR && cell_side(ship, { -1, 0, 0 }, 1, 0) == SideKind::OPEN && cell_side(ship, { -1, 0, 0 }, -1, 0) == SideKind::RAIL,
            "甲板：朝舱是门、朝甲板通着、朝船外是舷栏");
    check(cell_side(ship, { 1, 0, 0 }, 0, 1) == SideKind::WALL, "甲板挨着封死的舱：墙");
    ShipLayout two_rooms = ship;
    check(layout_remove(two_rooms, { 1, 1, 1 }).empty() && layout_place(two_rooms, { 1, 1, 1 }, ModuleKind::DECK).empty(), "浮石舱上换一节空节");
    check(cell_side(two_rooms, { 0, 0, 1 }, 1, 0) == SideKind::WALL, "浮石舱照样封死");

    // 人在不在舱里
    check(in_cabin(ship, -7.0, 0.0, 7.0) && in_cabin(ship, -3.0, 0.05, 6.0), "宽空节的舱里算舱里");
    check(!in_cabin(ship, -7.0, 3.8, 7.0) && !in_cabin(ship, 0.0, 0.0, 0.0) && !in_cabin(ship, 7.0, 0.0, 7.0), "舱顶上、甲板上、封死的舱不算");
    check(ship_level_at(0.0) == 0 && ship_level_at(3.8) == 1 && ship_level_at(3.75) == 1 && ship_level_at(-0.5) == 0 && ship_level_at(7.6) == 2 && ship_level_at(40.0) == 2,
            "脚底高 → 第几层");

    // 船上的地面
    double y = -1.0;
    const ShipFloor floor0(ship, 0), floor1(ship, 1);
    check(floor0.height(-7.0, 7.0, y) && y == 0.0 && floor1.height(-7.0, 7.0, y) && std::abs(y - CELL_HEIGHT_M) < 1e-9, "舱里、舱顶的甲板都有地");
    check(!floor0.height(7.0, 7.0, y) && !floor0.height(0.0, -20.0, y), "封死的舱、船外没有地");

    // 摆东西：工作台 1.4 × 0.8
    const PropDef &bench = *props::find("workbench");
    check(ship_prop_problem(ship, bench, -7.0, 0.0, 0, 0).empty(), "舷门排左边的空节上能摆");
    check(ship_prop_problem(ship, bench, -9.5, 0.0, 0, 0) == "挡着舷门了", "舷门口不让摆");
    check(ship_prop_problem(ship, bench, -10.0, -2.5, 0, 0) == "贴着舷栏了", "压着舷栏不让摆");
    check(ship_prop_problem(ship, bench, -7.0, 3.0, 0, 0) == "挡着舱门了", "甲板上的舱门口不让摆");
    check(ship_prop_problem(ship, bench, -4.5, 2.5, 0, 0) == "挡着梯子了", "梯脚不让摆");
    check(ship_prop_problem(ship, bench, -7.0, 7.0, 0, 0).empty(), "舱里能摆");
    check(ship_prop_problem(ship, bench, -3.5, 7.0, 0, 0).empty(), "宽空节的舱里跨两节也行（中间不隔墙）");
    check(ship_prop_problem(ship, bench, 2.8, 7.0, 0, 0) == "贴着舱壁了", "贴着舱壁不让摆");
    check(ship_prop_problem(ship, bench, -7.0, 4.2, 0, 0) == "挡着舱门了", "舱里的门口不让摆");
    check(ship_prop_problem(ship, bench, 0.0, -1.0, 0, 0) == "驾驶区上摆不了东西：摆在空节上", "驾驶区上不让摆");
    check(ship_prop_problem(ship, bench, 0.0, -20.0, 0, 0) == "船外头摆不了", "船外头");
    check(ship_prop_problem(ship, bench, 7.0, 7.0, 0, 0) == "封死的舱里摆不了", "封死的舱");
    check(ship_prop_problem(ship, bench, -7.0, 7.0, 0, 1).empty(), "第二层的空节上能摆");
    check(ship_prop_problem(ship, bench, 7.0, 7.0, 0, 1) == "储存区上摆不了东西：摆在空节上", "储存区上不让摆（留给货位）");
    check(ship_prop_problem(ship, bench, 3.4, 7.0, 0, 1) == "占到储存区上了", "伸到储存区上不行");
    check(ship_prop_problem(ship, bench, -4.8, 4.5, 0, 1) == "挡着梯子了", "梯顶不让摆");
    check(ship_prop_problem(ship, bench, -7.0, 7.0, 1, 0).empty() && ship_prop_problem(ship, bench, -9.8, 7.0, 1, 0).empty() &&
                    ship_prop_problem(ship, bench, -9.8, 7.0, 0, 0) == "贴着舱壁了",
            "转 90° 占地跟着转");

    // 改船：摆着的东西放不下就说
    Placements aboard;
    PlaceCheck at;
    at.ok = true;
    at.x = -3.5, at.y = 0.0, at.z = 7.0;
    aboard.add("workbench", at);
    check(ship_props_problem(ship, aboard).empty(), "工作台跨在宽空节的舱里：放得下");
    ShipLayout opened = ship;
    check(layout_remove(opened, { -1, 1, 1 }).empty() && cell_space(opened, { -1, 0, 1 }) == CellSpace::DECK && cell_side(opened, { -1, 0, 1 }, 1, 0) == SideKind::DOOR,
            "拆掉左上那节空节：宽空节一半露天、一半还是舱，中间砌墙开门");
    check(ship_props_problem(opened, aboard) == "工作台放不下了（贴着舱壁了）", "工作台会被新砌的墙压住：不改");
    Placements side;
    at.x = -7.0;
    side.add("chest", at);
    check(ship_props_problem(opened, side).empty(), "木箱在露天那一半的中间：放得下");
    ShipLayout bare = ship;
    check(layout_remove(bare, { -1, 1, 1 }).empty() && layout_remove(bare, { 0, 1, 1 }).empty() && layout_remove(bare, { 1, 1, 1 }).empty() &&
                    layout_remove(bare, { -1, 0, 1 }).empty(),
            "拆掉上面一排和宽空节");
    check(ship_props_problem(bare, side) == "木箱放不下了（脚下的节没了）", "木箱脚下的节拆了：放不下");
}

void test_refit_pick() {
    std::printf("refit_pick\n");
    ShipLayout ship = default_layout();
    layout_place(ship, { 0, 0, 1 }, ModuleKind::STORAGE);
    // 从正上方往下看某一点
    auto down_at = [](double p_x, double p_z) {
        PickRay r;
        r.ox = p_x;
        r.oy = 60.0;
        r.oz = p_z;
        r.dx = 0.0;
        r.dy = -1.0;
        r.dz = 0.0;
        return r;
    };
    auto from_side = [](double p_x0, double p_y, double p_z) {
        PickRay r;
        r.ox = p_x0;
        r.oy = p_y;
        r.oz = p_z;
        r.dx = -1.0;
        r.dy = 0.0;
        r.dz = 0.0;
        return r;
    };
    double t = 0.0;
    check(ray_box(down_at(0.0, 0.0), cell_box({ 0, 0, 0 }), t) && std::abs(t - (60.0 - CELL_HEIGHT_M)) < 1e-9, "射线从上往下打进格子顶面");
    check(!ray_box(down_at(4.0, 0.0), cell_box({ 0, 0, 0 }), t), "格子外打不着");
    check(!ray_box(from_side(30.0, -2.0, 7.0), cell_box({ 0, 0, 1 }), t) && ray_box(from_side(30.0, -2.0, 7.0), cell_box({ 0, 0, 1 }, true), t),
            "甲板下的船体：连船体的盒子才打得着");

    // 没拖东西：选中
    const RefitHover node = pick_node(ship, down_at(0.0, 7.0));
    check(node.type == RefitHover::NODE && node.anchor == Cell{ 0, 0, 1 }, "从上面点储存区：选中它");
    const RefitHover hull = pick_node(ship, from_side(30.0, -2.0, 7.0));
    check(hull.type == RefitHover::NODE && hull.anchor == Cell{ 0, 0, 1 }, "从旁边点船体：也是那一节");
    check(pick_node(ship, down_at(7.0, 7.0)).type == RefitHover::NONE, "点空处：什么也没选中");
    ShipLayout tall = ship;
    layout_place(tall, { 0, 0, 2 }, ModuleKind::TALL_HOLD);
    check(pick_node(tall, down_at(0.0, 14.0)).anchor == Cell{ 0, 0, 2 }, "点双层货舱的上层：选中的是它的锚点");

    // 拖着储存区：亮格与落点
    const std::vector<Cell> spots = layout_placements(ship, ModuleKind::STORAGE);
    check(glow_cells(spots, ModuleKind::STORAGE).size() == spots.size(), "单节的亮格就是落点");
    const RefitHover top = pick_spot(ship, ModuleKind::STORAGE, spots, down_at(0.0, 7.0));
    check(top.type == RefitHover::SPOT && top.legal && top.anchor == Cell{ 0, 1, 1 }, "拖着储存区指储存区顶上：落在第二层");
    const RefitHover aft = pick_spot(ship, ModuleKind::STORAGE, spots, down_at(0.0, 14.0));
    check(aft.type == RefitHover::SPOT && aft.legal && aft.anchor == Cell{ 0, 0, 2 }, "指船尾后面一格：落在那儿");
    const RefitHover on_sail = pick_spot(ship, ModuleKind::STORAGE, spots, down_at(0.0, -7.0));
    check(on_sail.type == RefitHover::SPOT && !on_sail.legal && on_sail.anchor == Cell{ 0, 1, -1 } && on_sail.reason == "帆只能在最上面：顶上不能再搭",
            "指帆顶上：不亮，红色虚影说帆只能在最上面");
    const RefitHover beside = pick_spot(ship, ModuleKind::STORAGE, spots, down_at(7.0, -7.0));
    check(beside.type == RefitHover::SPOT && !beside.legal && beside.reason == "舷门那一排得是最宽的一排，不然靠不上码头", "指帆旁边：舷门排不够宽");
    check(pick_spot(ship, ModuleKind::STORAGE, spots, down_at(20.0, 20.0)).type == RefitHover::NONE, "指远处：什么也不指");
    const RefitHover side = pick_spot(ship, ModuleKind::STORAGE, spots, from_side(30.0, 1.0, 7.0));
    check(side.type == RefitHover::SPOT && !side.legal && side.anchor == Cell{ 1, 0, 1 }, "水平看过去先打到右舷那一格（贴着船的空格）");
    check(pick_spot(ship, ModuleKind::STORAGE, spots, from_side(3.0, 1.0, 7.0)).type == RefitHover::NONE, "起点过了那一格：先打到船身，后面的格子被挡着");

    // 拖着双桅帆（前后双节）：亮格按命中点挑哪一头
    const std::vector<Cell> long_spots = layout_placements(ship, ModuleKind::LONG_SAIL);
    check(glow_cells(long_spots, ModuleKind::LONG_SAIL).size() > long_spots.size(), "复合节的亮格比落点多（每个落点亮两节）");
    const RefitHover near_half = pick_spot(ship, ModuleKind::LONG_SAIL, long_spots, down_at(0.0, 12.0));
    const RefitHover far_half = pick_spot(ship, ModuleKind::LONG_SAIL, long_spots, down_at(0.0, 20.0));
    check(near_half.legal && near_half.anchor == Cell{ 0, 0, 2 } && far_half.legal && far_half.anchor == Cell{ 0, 0, 2 }, "船尾后面两格：同一个落点的前一节、后一节");
    const RefitHover above = pick_spot(ship, ModuleKind::LONG_SAIL, long_spots, down_at(0.0, 5.0));
    check(above.type == RefitHover::SPOT && !above.legal && above.anchor == Cell{ 0, 1, 0 } && above.reason == "驾驶区只能在最上面：顶上不能再搭",
            "储存区顶上靠前的一半：双桅帆会压到驾驶区顶上，红色虚影说驾驶区只能在最上面");
    const std::vector<Cell> long_glow = glow_cells(long_spots, ModuleKind::LONG_SAIL);
    check(std::find(long_glow.begin(), long_glow.end(), Cell{ 0, 1, 1 }) == long_glow.end(), "储存区顶上对双桅帆不亮（往后伸会悬空，往前伸压着驾驶区）");
    // 拖着双层货舱（上下双节）
    const std::vector<Cell> tall_spots = layout_placements(ship, ModuleKind::TALL_HOLD);
    const RefitHover tall_hover = pick_spot(ship, ModuleKind::TALL_HOLD, tall_spots, down_at(0.0, 14.0));
    check(tall_hover.legal && tall_hover.anchor == Cell{ 0, 0, 2 }, "从上面指船尾后面：双层货舱落在第 0 层、占上下两节");
    const CellBox fb = footprint_box({ 0, 0, 2 }, ModuleKind::TALL_HOLD);
    check_near(fb.y1 - fb.y0, 2.0 * CELL_HEIGHT_M, 1e-9, "双层货舱的外包盒两层高");
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

// 真正入库的开局那群的资产（仓库里有才测）
void test_world_start() {
    const std::string path = start_dir() + "/terrain.bin";
    std::ifstream in(path, std::ios::binary), tsv_in(start_dir() + "/sites.tsv", std::ios::binary);
    if (!in || !tsv_in) {
        std::printf("world %d（跳过：没有 %s）\n", START, path.c_str());
        return;
    }
    std::printf("world %d（开局那群）\n", START);
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err), "开局那群的 terrain.bin 能解析：" + err);
    check(parse_world_sites(tsv, sites, err), "sites.tsv 能解析：" + err);
    check(g.rows == 1312 && g.cols == 1243 && g.res == 100.0, "1312 × 1243 格、100 m");
    check(g.land_count() == 513994, "陆地格数与生成器一致（" + std::to_string(g.land_count()) + "）");
    // 邑治（村001，主岛东岸的海岸平原，主码头 #1 往西南 1.2 km）
    const SiteSettlement *seat = sites.village_by_id(19);
    check(seat && seat->seat, "村019 是邑治");
    const double sx = seat ? seat->x : 0.0, sz = seat ? seat->z : 0.0;
    check(g.shore[g.nearest_cell(sx, sz)] > 0 && std::abs(g.height_at(sx, sz) - 572.0) < 8.0, "邑治在陆地上、高约 572 m（" + std::to_string(g.height_at(sx, sz)) + "）");
    check(g.island[g.nearest_cell(sx, sz)] == 0, "邑治在主岛");
    const WorldTerrain t(g, { sites.terrain_islands().front() }, START);
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

// 开局那群的地形 v3：生成器下切过的河道、地形区与游戏的厚岛体
void test_terrain_v3_start() {
    const std::string dir = start_dir();
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        std::printf("terrain v3（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("terrain v3（开局那群）\n");
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err) && g.has_hydro() && g.has_channels() && g.zone.size() == g.height.size(), "terrain.bin v3 带河道与地形区：" + err);
    check(parse_world_sites(tsv, sites, err), "sites.tsv 能解析：" + err);
    // 河道格：有宽深、汇流到溪涧门槛；河床顺流不升（生成器保证单调，导出按汇流反推的流向要接得上）；
    // 流进湖（地表类 11，湖格的高是湖面）的那一格另算：湖面比入湖的河床高，那一格河道到头（开局那群 #6329 有湖，以前的 2051 没有）
    constexpr uint8_t LAKE = 11;
    int chan = 0, chan_small = 0, uphill = 0, off_channel = 0, into_lake = 0;
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
            if (g.landcover[down] == LAKE) {
                into_lake++;
                continue;
            }
            uphill += g.height[down] > g.height[k] + 0.05f ? 1 : 0;
            off_channel += g.river_width[down] <= 0.0f ? 1 : 0;
        }
    }
    std::printf("  河道格 %d，最宽 %.1f m，顺流上坡 %d 格、流出河道 %d 格；流进湖 %d 格\n", chan, widest, uphill, off_channel, into_lake);
    check(chan > 10000 && widest > 12.0 && widest < 14.0, "新版真实河道格上万，年均最宽约 13 m");
    check(chan_small > 0 && g.authoritative_rivers, "局地降水与地下水允许 1 km² 以下的细沟，由生成器河网给出");
    check(uphill < chan / 1000, "河床顺流几乎处处不升（" + std::to_string(uphill) + " 格例外）");
    check(off_channel == 0, "河道格顺流总流到下一个河道格或湖（流向按生成器的汇流反推）");
    check(into_lake == 0, "开局新版没有湖，入湖格数（" + std::to_string(into_lake) + " 格）");

    // 地形区：山里比台地崎岖
    const WorldTerrain t(g, sites.terrain_islands(), START);
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
    check(count[2] > 50 && count[4] > 50, "开局那群有山地也有台地平原");
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
    // 浮高计划 F3：所有岛底（连岩棱）至少高出云带顶 1000 m；开局那群的岛本来就在这之上，龙骨一座也不收
    check(t.lowest_bottom() >= VerticalDatum::ISLAND_BOTTOM_MIN_Y - 1e-6,
            "所有岛底都在云带顶之上一千米以外（" + std::to_string(t.lowest_bottom()) + "，云带顶 " + std::to_string(VerticalDatum::CLOUD_TOP_Y) + "）");
    int clamped = 0;
    for (const WorldIsland &isl : sites.terrain_islands()) {
        clamped += island_body(isl).keel_m < 25.0 * std::sqrt(std::max(1.0, isl.max_shore_m)) - 1e-9 ? 1 : 0;
    }
    check(clamped == 0, "开局那群的岛龙骨一座也不收短（" + std::to_string(clamped) + "）");
    // 崖脚：离岸线五十到一百五十米处岛底还贴着崖壁下沿（在整张栅格上散着抽 4000 格）
    int near_edge = 0, near_ok = 0;
    for (int k = 0; k < 4000; k++) {
        const size_t cell = size_t(k) * 2654435761ull % g.shore.size();
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
    int on = 0, narrow = 0, source_points = 0, wrong_bed = 0;
    for (const RiverStem &stem : t.get_rivers().get_stems()) {
        for (size_t i = 1; i + 1 < stem.v.size(); i++) {
            const RiverVertex &v = stem.v[i];
            const size_t k = g.nearest_cell(v.x, v.z);
            if (g.river_width[k] <= 0.0f || v.s < 200.0) {
                continue;
            }
            on++;
            narrow += v.width < 0.7 * g.river_width[k] ? 1 : 0;
            // Resampled points interpolate two source cells; a nearest-cell bank
            // height is not their bed, especially on steep mountain channels.
            const double cx=g.x0+double(k%size_t(g.cols))*g.res;
            const double cz=g.z0+double(k/size_t(g.cols))*g.res;
            if (std::hypot(v.x-cx,v.z-cz)<0.01 && std::isfinite(g.bed_height[k])) {
                source_points++;
                wrong_bed+=std::abs(v.bed()-double(g.bed_height[k]))>0.15;
            }
        }
    }
    std::printf("  河道格上的中线点 %d：过窄 %d；源格中心 %d 个，河床不符 %d 个\n", on, narrow, source_points, wrong_bed);
    check(on > 10000, "河网的中线大都走在生成器的河道上");
    check(narrow < on / 50, "平岸宽跟得上生成器的河宽");
    check(source_points>1000 && wrong_bed==0, "源格中心的河床等于生成器的 bed_height");
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
    const std::string dir = start_dir();
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        std::printf("world_sites（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("world_sites（开局那群）\n");
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err), "terrain.bin 能解析");
    check(parse_world_sites(tsv, sites, err), "sites.tsv 能解析：" + err);
    check(sites.islands.size() == 38 && sites.docks.size() == 42 && sites.settlements.size() == 1922, "38 岛、42 码头、215 常住村（含驿站）+ 1707 散户");
    const SiteSettlement *seat = sites.village_by_id(19);
    check(seat && seat->seat && seat->households == 99 && seat->name == "村019", "村019 是邑治、99 户（农户与邑里的市户）");
    const SiteDock *main_dock = sites.dock_by_id(1);
    check(main_dock && main_dock->main && main_dock->island == 0, "主码头 #1 在主岛");
    WorldSites bad;
    check(!parse_world_sites("dock\t1\tx\t0\t0\t0\n", bad, err), "坏行报错");

    WorldTerrain t(g, sites.terrain_islands(), START);
    const WorldWorks works = lay_world_works(t, sites);
    const std::vector<DockPlan> &docks = works.docks;
    int ok = 0;
    for (const DockPlan &d : docks) {
        ok += d.ok;
    }
    check(ok == int(sites.docks.size()), "所有码头都找到岸缘（" + std::to_string(ok) + "）");
    // 主码头 #1 与岛 #13 上的目的码头 #28（场景里配的 destination_dock，离主码头 23 km）：压平后泊位附近的地面等高（这两处没有河从坪上过；有河的坪让给河）
    for (int id : { 1, 28 }) {
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
                    const auto raw=t.sample_raw(x,z);
                    double expected=raw.top+dock_pad_weight(d,x,z)*(1.0-double(raw.bank))*(d.y-raw.top);
                    for (const auto &yard:t.get_yards()) {
                        if (yard.outside_distance(x,z)<=WorldTerrain::YARD_REACH_M)
                            expected=yard_carve(yard,world_yard_rules(),x,z,expected);
                    }
                    worst = std::max(worst, std::abs(s.top - expected));
                    samples++;
                }
            }
        }
        check(samples > 20 && worst < 1e-6, tag + "：坪、河岸与船坞填挖混合正确（最大差 " + std::to_string(worst) + " m）");
        double y = 0.0;
        const double spawn_x=d.x-12*d.out_x, spawn_z=d.z-12*d.out_z;
        const auto spawn_raw=t.sample_raw(spawn_x,spawn_z);
        const double spawn_top=spawn_raw.top+dock_pad_weight(d,spawn_x,spawn_z)*(1.0-double(spawn_raw.bank))*(d.y-spawn_raw.top);
        check(t.surface(spawn_x,spawn_z,y) && std::abs(y-spawn_top)<1e-6,tag+"：出生点按坪与河岸的混合高度落地");
        const TerrainSample raw = t.sample_raw(d.x + (d.pier_m + 0.15) * d.out_x, d.z + (d.pier_m + 0.15) * d.out_z);
        check(!raw.land, tag + "：泊位在岸外");
        const double rim = sites.islands[size_t(sites.docks[k].island)].rim_m; // 浮高以后各岛的岸缘高低不同（主岛 611 m、岛 #13 464 m）
        check(d.y > rim - 30.0 && d.y < rim + 150.0, tag + "：坪高在岸缘一带（" + std::to_string(d.y) + " m，岸缘 " + std::to_string(rim) + " m）");
    }
    // 目的码头 #28 旁的村112（17 户，580 m）与邑治（主码头旁）：不压码头的坪、不在陡坡上
    const WorldVillageRules rules;
    for (int id : { 112, 1 }) {
        const SiteSettlement *v = sites.village_by_id(id);
        const VillagePlan plan = plan_world_village(t, *v, START, rules);
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
        check(plan.layout == plan_world_village(t, *v, START, rules).layout, tag + "：同输入同清单");
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
    const std::string dir = start_dir();
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
    WorldTerrain t(g, sites.terrain_islands(), START);
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
    double best = 1e18;
    for (const auto &stem:t.get_rivers().get_stems()) for (const auto &v:stem.v) {
        const double d=std::hypot(v.x-dock.x,v.z-dock.z);
        if (d>=best || !river_stage(v,1.0,t.get_rivers().get_rules()).wet || !t.is_land(v.x+60,v.z) || !t.is_land(v.x-60,v.z) || !t.is_land(v.x,v.z+60) || !t.is_land(v.x,v.z-60)) continue;
        best=d; b.water_x=v.x-dock.x; b.water_z=v.z-dock.z;
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

// ---------------------------------------------------------------- 河：河网、水情、凿河道、地面问水、水面网格（开局那群）

void test_world_rivers() {
    // v1 的栅格没有水文：河网是空的，地形照旧
    {
        const WorldGrid g = synthetic_grid();
        const WorldTerrain t(g, {}, 7);
        check(!g.has_hydro() && !t.has_rivers(), "v1 栅格没有水文、没有河网");
    }
    const std::string dir = start_dir();
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary), wx_in(dir + "/weather.tsv", std::ios::binary);
    if (!bin_in || !tsv_in || !wx_in) {
        std::printf("world_rivers（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("world_rivers（开局那群）\n");
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
    check(regime.get_daily().size() == 336 && std::abs(regime.precip_mm() - 510.0) < 1.0, "一年 336 天、年降水 510 mm");
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
    const WorldTerrain t(g, sites.terrain_islands(), START, rules);
    check(t.has_rivers(), "开局那群有河网");
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
    check(g.authoritative_rivers && !g.river_sources.empty(), "新版岸上高程来自生成器，不把它当作平水位单调化");
    int source_points=0, source_mismatch=0;
    for (size_t si=0;si<g.river_sources.size();si++) {
        const auto &source=g.river_sources[si];
        const auto &stem=rivers.get_stems()[si];
        size_t cursor=0;
        for (size_t pi=0;pi<source.points.size();pi++) {
            const auto &point=source.points[pi];
            if (cursor>=stem.v.size()) { source_mismatch++; break; }
            const auto &vertex=stem.v[cursor];
            source_points++;
            source_mismatch+=std::hypot(vertex.x-(g.x0+point[1]*g.res),vertex.z-(g.z0+point[0]*g.res))>1e-5
                    || std::abs(vertex.bed()-point[11])>1e-5 || std::abs(vertex.width-point[6])>1e-5
                    || std::abs(vertex.mean_flow-point[4])>1e-5 || std::abs(vertex.bankfull_flow-point[5])>1e-5;
            source_mismatch+=std::abs(vertex.source_surface-point[10])>1e-5 || vertex.level!=int(point[2]) || vertex.bed_class!=int(point[14]);
            const auto stage=river_stage(vertex,1.0,rules);
            source_mismatch+=std::abs(stage.depth-point[9])>0.02 || std::abs(stage.half_width*2-point[8])>0.04;
            if (pi+1<source.points.size()) {
                const auto &next=source.points[pi+1];
                cursor+=size_t(std::max(1,int(std::ceil(std::hypot(next[1]-point[1],next[0]-point[0])*g.res/rules.step_m))));
            }
        }
    }
    check(source_points>40000 && source_mismatch==0,"生成器每个河点的中线、河床、真实宽深与流量均保留（"+std::to_string(source_mismatch)+" 处差异）");
    for (const auto &basin:g.basin_runoff) {
        double average=0; for (float index:basin) average+=index/basin.size();
        check(basin.size()==336 && std::abs(average-1)<0.002,"流域径流指数一年 336 天，年均为 1");
        check_near(river_runoff_index(basin,10.5),basin[10],1e-8,"流域径流正午对齐日值");
        check_near(river_runoff_index(basin,346.5),basin[10],1e-8,"流域径流跨年循环");
    }
    check(drying_acc == 0, "汇流面积顺流不减（" + std::to_string(drying_acc) + " 处）");
    check(off_parent == 0, "支流末点都落在干流中线上（" + std::to_string(off_parent) + " 条没有）");
    // 最大的河：下游宽过上游，宽窄在一条河里有变化
    const RiverVertex &src = trunk->v.front(), &end = trunk->v.back();
    check(end.width > 2.0 * src.width && end.depth > src.depth, "干流越往下越宽越深（" + std::to_string(src.width) + " → " + std::to_string(end.width) + " m）");
    check(end.dry==0 && src.dry==0, "新版径流已经包含地下水，不再扣游戏的渗漏门槛");
    check(trunk->mouth, "干流从岸缘跌下去");
    check(std::abs(t.get_grid().shore_at(end.x, end.z)) < 1.0, "河口截在岸线上");

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
    check(river_stage(src, 0.25, rules).wet && river_stage(end, 0.25, rules).wet, "源数据仍给出正流量，浅溪与干流都保留水面");
    check(river_stage(end, 0.15, rules).wet, "全年最枯时干流也有水");
    const RiverStage low = river_stage(end, 0.5, rules), high = river_stage(end, 3.0, rules), flood = river_stage(end, 20.0, rules);
    check(low.level < high.level && low.half_width < high.half_width, "水涨了：水位高、水面宽");
    check(flood.half_width >= end.half() && flood.half_width <= end.core() + 1e-9, "大水漫上河漫滩、不出滩");

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
    check(mesh.normals.size() == verts * 3 && mesh.uvs.size() == verts * 2 && mesh.custom0.size() == verts * 4 && mesh.custom1.size() == verts * 4
            && mesh.custom2.size() == verts * 4 && mesh.custom3.size() == verts * 4, "顶点属性一一对上，含流量、悬沙、粒径、比降与左右滩宽");
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
    check(!fall, "只读 terrain.bin 而未读 falls 时不编造河口瀑布高度");
}

void test_river_contract() {
    std::printf("river_contract\n");
    RiverRules rules;
    RiverVertex p;
    p.surface=102; p.source_surface=102; p.depth=2; p.width=10;
    p.mean_flow=2; p.bankfull_flow=10; p.floodplain_left=2; p.floodplain_right=20; p.floodplain=20;
    const auto bank=river_stage(p,5,rules), low=river_stage(p,0.2,rules), mean=river_stage(p,1,rules);
    check_near(bank.level,102,1e-9,"平岸水面与源数据闭合时逐米吻合");
    check_near(bank.speed,0.5,1e-9,"10 m³/s 通过 10×2 m 断面，流速 0.5 m/s");
    check_near(low.discharge,0.4,1e-9,"枯水流量来自年均乘当日指数");
    check(low.speed<mean.speed && mean.speed<bank.speed,"真实断面下流速随当天流量变化");
    check_near(mean.speed*mean.depth*(mean.half_left+mean.half_right),2,1e-9,"流量与宽、深、流速守恒");
    const auto flood=river_stage(p,20,rules);
    check_near(flood.discharge,40,1e-9,"源数据流量不被旧 flood_max 偷截成 30");
    check_near(flood.half_left,7,1e-9,"左滩只有 2 m，不使用右滩 20 m 的上限");
    check(flood.half_right>flood.half_left,"漫滩左右保持不对称");
    check_near(river_profile(p,12,rules),102.08,1e-9,"左滩剖面在源宽度处截止");
    check_near(river_profile(p,-12,rules),102.28,1e-9,"右滩按右侧宽度计算");
    const std::vector<float> daily{0.2f,1.0f,5.0f,0.4f};
    check_near(river_runoff_index(daily,1.0),0.6,1e-7,"相邻日平滑过渡");
    check_near(river_runoff_index(daily,0.0),0.3,1e-7,"年末至年初也平滑过渡");
    auto dry_source=p; dry_source.authoritative=true; dry_source.mean_flow=0; dry_source.bankfull_flow=0; dry_source.acc=100;
    const auto zero=river_stage(dry_source,5,rules);
    check(zero.discharge==0 && zero.depth==0 && zero.speed==0 && !zero.wet,"生成器零流量不回退成按面积猜流量的旧河");
    auto brook=p; brook.width=0.62; brook.depth=0.11; brook.surface=100.11;
    brook.mean_flow=0.004; brook.bankfull_flow=0.0536;
    const auto shallow=river_stage(brook,0.23,rules);
    check(shallow.depth>0 && shallow.depth<0.03 && shallow.wet,
            "源数据仍有流量的厘米级溪水不能被旧 3 cm 断流阈值隐藏");

    WorldGrid g; g.rows=5; g.cols=5; g.res=100; g.authoritative_rivers=true;
    g.height.assign(25,102); g.shore.assign(25,500); g.island.assign(25,0);
    for(int row=0;row<5;row++) for(int col=0;col<5;col++) g.shore[size_t(row*5+col)]=float(250-col*100);
    g.landcover.assign(25,6); g.stream.assign(25,1); g.basin_runoff={daily};
    WorldGrid::RiverSource r; r.exit=1; r.basin=0;
    r.points={{{1,1,1,4,2,10,10,2,6.58f,1.05f,102,100,.03f,64,3,0,120,2,20,1}},
              {{1,2,1,4,2,10,10,2,6.58f,1.05f,101,99,.03f,64,3,0,120,2,20,1}}};
    r.outlet_drop=44.7; g.river_sources={r};
    WorldTerrain terrain(g,{WorldIsland()},42,rules);
    const auto mesh=build_river_mesh(terrain,{0,0});
    bool has_fall=false, attrs_ok=!mesh.empty(), has_side_normal=false;
    for(size_t i=0;i<mesh.custom1.size();i+=4) {
        const double drop=mesh.custom1[i+1];
        has_fall|=std::abs(drop-44.7)<1e-4;
        attrs_ok &= drop<1e-6 || std::abs(drop-44.7)<1e-4;
        attrs_ok &= mesh.custom2[i]==2 && mesh.custom2[i+1]==120 && mesh.custom2[i+2]==64;
        attrs_ok &= mesh.custom3[i]==2 && mesh.custom3[i+1]==20;
        if(drop>0) has_side_normal |= std::abs(mesh.normals[(i/4)*3+1])<1e-6;
    }
    check(has_fall && attrs_ok,"GPU 收到原始流量/SSC/粒径/左右滩宽以及 44.7 m 的源瀑布，非固定 150 m");
    check(has_side_normal,"瀑布水帘法线朝侧面");
    check(mesh.min_y<60,"瀑布真实落差包含在包围盒里");

    // A one-metre channel between the old 2 m grid rows must remain visible at low stage.
    // Check the rendered/collision triangles, not just the continuous height function.
    g.height.assign(25,100.25f);
    r.exit=0; r.outlet_drop=0;
    for(auto &pt:r.points) { pt[0]=1.003f; pt[4]=0.2f; pt[5]=1; pt[6]=1; pt[7]=0.25f; pt[10]=100.25f; pt[11]=100; }
    g.river_sources={r};
    WorldTerrain narrow(g,{WorldIsland()},42,rules);
    TileLayout layout(g);
    const double px=150.25,pz=double(r.points[0][0])*g.res;
    const auto tile=build_tile_mesh(narrow,layout,layout.leaf_at(px,pz));
    double rendered=-1e9;
    for(size_t i=0;i+8<tile.collision.size();i+=9) {
        const auto *a=&tile.collision[i], *b=&tile.collision[i+3], *c=&tile.collision[i+6];
        const double ax=a[0]+tile.origin_x,az=a[2]+tile.origin_z;
        const double bx=b[0]+tile.origin_x,bz=b[2]+tile.origin_z;
        const double cx=c[0]+tile.origin_x,cz=c[2]+tile.origin_z;
        const double det=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz);
        if(std::abs(det)<1e-9) continue;
        const double u=((bz-cz)*(px-cx)+(cx-bx)*(pz-cz))/det;
        const double v=((cz-az)*(px-cx)+(ax-cx)*(pz-cz))/det;
        if(u>=-1e-6 && v>=-1e-6 && u+v<=1+1e-6) rendered=std::max(rendered,u*a[1]+v*b[1]+(1-u-v)*c[1]);
    }
    RiverHit nh; narrow.get_rivers().nearest(px,pz,10,nh);
    const auto dry_stage=river_stage(nh.at,0.5,rules);
    check(rendered>99.9 && rendered<dry_stage.level,"枯水细溪在实际地面/碰撞网格上连续露出，不靠抬高水面");

    // Opening brook: 0.62 m bankfull width, 2--7 cm seasonal depth, diagonal
    // and offset from grid vertices. A 0.5 m mesh can roof over this entire flow.
    g.height.assign(25,100.11f); g.shore.assign(25,500);
    for(size_t k=0;k<r.points.size();k++) {
        auto &pt=r.points[k]; pt[0]=float(1+k)+0.003f; pt[1]=float(1+k);
        pt[4]=0.004f; pt[5]=0.0536f; pt[6]=0.62f; pt[7]=0.11f; pt[10]=100.11f; pt[11]=100;
    }
    g.river_sources={r};
    WorldTerrain opening(g,{WorldIsland()},42,rules);
    const auto opening_tile=build_tile_mesh(opening,layout,layout.leaf_at(150,150));
    int submerged=0;
    for(int sample=0;sample<16;sample++) {
        const double x=145.07+sample*0.17, z=x+double(r.points[0][0]-1)*g.res;
        double top=-1e9;
        for(size_t i=0;i+8<opening_tile.collision.size();i+=9) {
            const auto *a=&opening_tile.collision[i], *b=&opening_tile.collision[i+3], *c=&opening_tile.collision[i+6];
            const double ax=a[0]+opening_tile.origin_x,az=a[2]+opening_tile.origin_z;
            const double bx=b[0]+opening_tile.origin_x,bz=b[2]+opening_tile.origin_z;
            const double cx=c[0]+opening_tile.origin_x,cz=c[2]+opening_tile.origin_z;
            const double det=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz);
            if(std::abs(det)<1e-9) continue;
            const double u=((bz-cz)*(x-cx)+(cx-bx)*(z-cz))/det;
            const double v=((cz-az)*(x-cx)+(ax-cx)*(z-cz))/det;
            if(u>=-1e-6 && v>=-1e-6 && u+v<=1+1e-6) top=std::max(top,u*a[1]+v*b[1]+(1-u-v)*c[1]);
        }
        RiverHit hit; opening.get_rivers().nearest(x,z,1,hit);
        submerged+=top>99.9 && top<river_stage(hit.at,0.23,rules).level;
    }
    check(submerged==16,"开局宽 0.62 m 的斜溪在枯月实际网格中连续有水（"+std::to_string(submerged)+"/16）");
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

    const std::string dir = start_dir();
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
    WorldTerrain t(g, world_sites.terrain_islands(), START);
    lay_world_works(t, world_sites);
    // 离主码头（#1，邑治旁）远的几处：目的码头 #28（岛 #13）边的村112、主岛西边的村008 与村011、#28 边的散户1073
    Fnv villages;
    for (const SiteSettlement &s : world_sites.settlements) {
        if ((!s.hamlet && (s.id == 112 || s.id == 8 || s.id == 11)) || (s.hamlet && s.id == 1073)) {
            const VillagePlan plan = plan_world_village(t, s, START, WorldVillageRules());
            for (const std::string &id : settlement_ids(plan.layout)) {
                villages.mix_s(id);
            }
            villages.mix(uint64_t(plan.missing));
        }
    }
    // 拓展「岛群的河」起地形凿了河道、河滩不许盖房，村落清单有意变了，此处重记（河按游戏尺度放宽后又记了一次；拓展「船台」村里多盖一座船台，又记了一次；
    // 拓展「按新版生成器优化地形」换了下切过河道的栅格、按地形区加减起伏，又记了一次；
    // 重构「按生成器的真实起伏重做 2051」换了地形与全部点位，换了一组村重记；
    // 拓展「按点 / 片 / 散生成资源」换了生成器新版的地表（村周开垦、采场格改裸岩；细节起伏按地表调幅）与码头，又记了一次；
    // 浮高计划 Z2 按生成器的浮高重导（岛 #1、#2 整座上下平移，村号跟着户数重排、户数也变了：村026 55 → 80 户），同一处地方的村重记；
    // 地貌与聚落 P0 开局从 #2051 换到 #6329，换成这一群里的四处重记）
    report_golden("岛群远离主码头的村", villages.h, 0x930bbc25eb823b2aull);
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
    // 时令（有气候的地图）：给了就按它的作息、歇工、冷天不播；近处农田按 wx 逐格问气温（历日 = 钟的日 + wx_offset）
    const SeasonView *season = nullptr;
    const WeatherModel *wx = nullptr;
    double wx_offset = 0.0;

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
        w.season = season;
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
            if (wx) {
                const FieldClimate climate(wx, 0.5 * (r_state.clock + to) / day.day_seconds + wx_offset);
                farm.advance(game_hours(to - r_state.clock, day), &climate);
            } else {
                farm.advance(game_hours(to - r_state.clock, day));
            }
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

    // 展开时先不找路（流式加载：展开那一帧不给几十个人一起找路）：走着的接着估着走，之后 repath_walker 一个个找
    collapse_village(s, &bed.farm);
    bed.run_far(s, clock_at(0, 16.5, day));
    {
        VillageWorld w = bed.world(true, &bed.near_fields);
        deploy_village(s, w, &bed.farm, false);
        int estimated = 0, repathed = 0;
        for (int k = 0; k < int(s.people.size()); k++) {
            if (s.people[size_t(k)].phase == FolkPhase::WALKING && s.people[size_t(k)].estimated) {
                estimated++;
                repathed += repath_walker(s, w, k) ? 1 : 0;
            }
        }
        bool pathed = true;
        for (const PersonState &p : s.people) {
            pathed = pathed && !(p.phase == FolkPhase::WALKING && p.estimated) && (p.phase != FolkPhase::WALKING || !p.path.empty());
        }
        check(estimated > 0 && repathed == estimated && pathed && s.deployed, "展开先不找路：走着的 " + std::to_string(estimated) + " 人接着估着走，一个个找路之后都有路");
        check(!repath_walker(s, w, 0) || s.people[0].phase == FolkPhase::WALKING, "不是估着走的不再找");
    }

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
    check(slots.size() == 8 && open == 8, "被盖住的那节是能进的货舱，货位也够得着");

    // 土产：确定、在表里、不是粮
    const char *sp = freight::specialty_of(START, 152, false);
    check(std::string(sp) == freight::specialty_of(START, 152, false), "同种子同村同土产");
    check(catalog::item(sp) && !catalog::crop_of_seed(sp) && catalog::item("coin"), "土产与铜钱在物品表里，不是种子");
    int kinds = 0;
    for (const char *id : { "cloth", "pottery", "lacquer" }) {
        bool seen = false;
        for (int v = 1; v <= 60; v++) {
            seen = seen || std::string(freight::specialty_of(START, v, false)) == id;
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
    check(land_berth::ship_problem(cargo).empty(), "开局的船（储存区 + 帆 + 驾驶区）停得下");
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

    // 开局那群：主码头 #1 附近的一个村（FROM_ID）的委托开到别的岛上最近的村（专业聚落也是村，有船台）
    const std::string dir = start_dir();
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        std::printf("  （跳过：没有开局那群的资产）\n");
        return;
    }
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    parse_world_grid(bin.data(), bin.size(), g, err);
    parse_world_sites(tsv, sites, err);
    WorldTerrain t(g, sites.terrain_islands(), START);
    const WorldWorks works = lay_world_works(t, sites);
    const std::vector<FreightPort> world_ports = plan_freight_ports(sites);
    constexpr int FROM_ID = 3; // 村003：主码头 #1 往南 2.5 km、271 户
    int from = -1;
    for (size_t k = 0; k < world_ports.size(); k++) {
        from = world_ports[k].site_id == FROM_ID ? int(k) : from;
    }
    check(from >= 0 && world_ports.size() == size_t(std::count_if(sites.settlements.begin(),sites.settlements.end(),[](const SiteSettlement &s) { return !s.hamlet; })), "所有常住村都能托运，散户不算");
    const SiteSettlement *v29 = sites.village_by_id(FROM_ID);
    if (from < 0 || !v29) return;
    const WorldGround land_ground(t, 0.0, 0.0);
    const FarmModel land(&land_ground, land_ground.plot_lattice(), FarmRules());
    WorldLedgerSpec spec;
    spec.site = *v29;
    spec.lattice = land.get_lattice();
    spec.farm_rules = land.get_rules();
    const WorldLedger ledger = open_world_ledger(t, spec, SimRules());
    const std::string item = freight::specialty_of(START, FROM_ID, false);
    std::printf("  村003：%d 户，仓里 %s %d 份、粟 %d\n", v29->households, catalog::item_name(item).c_str(), ledger.state->store.count(item), ledger.state->store.count("millet"));
    check(ledger.state->store.count(item) == 2 * v29->households && ledger.state->store.count("millet") == 8,
            "村003 开账：仓里 " + catalog::item_name(item) + " " + std::to_string(2 * v29->households) + " 份（户数 × 2），粟照旧 8");
    check(freight_post_building(ledger.layout) >= 0, "村003 有船台（村仓）");
    FreightOrder o;
    check(offer_freight(world_ports, from, ledger.state->store, item, rules, o, why), "村003 开得出委托：" + why);
    if (o.valid()) {
        const FreightPort &to = world_ports[size_t(o.to)];
        std::printf("  村003 → %s（村号 %d，岛 #%d，%.1f km）%d 箱，运费 %d\n", to.name.c_str(), to.site_id, to.island, o.km, o.crates, o.fee);
        bool nearest=to.island!=world_ports[size_t(from)].island;
        for (const auto &port:world_ports) {
            if (port.island==world_ports[size_t(from)].island) continue;
            nearest=nearest && std::hypot(port.x-world_ports[size_t(from)].x,port.z-world_ports[size_t(from)].z)/1000.0>=o.km-1e-6;
        }
        check(nearest && o.crates==2, "两箱运到别岛最近的常住村");
        const SiteSettlement *destination = sites.village_by_id(to.site_id);
        check(destination!=nullptr, "委托目的地是有效村庄");
        if (destination) {
            const VillagePlan plan = plan_world_village(t, *destination, START, WorldVillageRules());
            check(freight_post_building(plan.layout) >= 0, "目的村有船台");
        }
    }
}

// 资源：种类表、存量账、自家的货箱、开局那群的采场与地标规划
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
    check(workings == 7 && products_ok, "七种散的有采场（石料、黏土、砂砾、砂金、金属矿、硫磺、盐）：产物在物品表里、不是种子、有货箱色、有工时与存量");
    check(landmarks == 5 && patches == 4 && others_ok, "点（泉眼、温泉、洞穴、盐泉、化石）与片（林木、泥炭芦苇、浮石、鸟粪）没有采场");
    check(!deposits::kind("quarry") && deposits::kind("stone") && deposits::kind("stone")->form == DepositForm::FIELD, "石料的键是 stone（生成器改名：石料是赋存，采石场是采场）");
    const DepositKind &ore = *deposits::kind("ore"), &stone = *deposits::kind("stone"), &clay = *deposits::kind("clay");
    check(deposits::product_of(ore, "铜") == "copper_ore" && deposits::product_of(ore, "锡") == "tin_ore" && catalog::item_name("copper_ore") == "铜矿石", "矿坑按矿种出矿石");
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

    // 开局那群：按 resources.tsv 落采场与地标
    const std::string dir = start_dir();
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary), res_in(dir + "/resources.tsv", std::ios::binary),
            wx_in(dir + "/weather.tsv", std::ios::binary);
    if (!bin_in || !tsv_in || !res_in || !wx_in) {
        std::printf("  （跳过：没有开局那群的资产）\n");
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
    std::printf("  点与片 %zu、赋存区 %zu、采场 %zu\n", data.deposits.size(), data.occurrences.size(), data.workings.size());
    check(parsed && data.deposits.size() == 869 && data.occurrences.size() == 821 && data.workings.size() == 417,
            "开局那群：点与片 869（片 514 + 点 355）、赋存区 821、采场 417（" + std::to_string(data.workings.size()) + "）");
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
    check(patches_ok && patch_count == 514, "片号：514 片的代表格都在自己的片里（另有 " + std::to_string(points_in_patch) + " 个点在林子这类片里）");
    // 赋存区的峰值格品位够门槛；采场那一格的品位与清单一致（u8 量化）
    bool fields_ok = true;
    for (const ResourceOccurrence &o : data.occurrences) {
        fields_ok = fields_ok && grid.field_cell(WorldGrid::field_of(o.kind.c_str()), grid.nearest_cell(o.x, o.z)) >= 0.15 - 1e-9;
    }
    for (const ResourceWorking &w : data.workings) {
        fields_ok = fields_ok && std::abs(grid.field_cell(WorldGrid::field_of(w.kind.c_str()), grid.nearest_cell(w.x, w.z)) - w.grade) < 0.01;
    }
    check(fields_ok, "赋存区的峰值格、采场那一格的品位与赋存场一致");
    WorldTerrain t(grid, sites.terrain_islands(), START, river_rules);
    lay_world_works(t, sites);
    const auto started = std::chrono::steady_clock::now();
    const ResourcePlan plan = plan_resource_sites(t, sites, data);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::printf("  采场 %zu 处落地（%zu 处够不着船）；并掉 %d、在村里 %d、落不了 %zu；地标 %zu 处（落不了 %zu）（%.0f ms）\n", plan.sites.size(), plan.unreachable.size(),
            plan.merged, plan.in_village, plan.failed.size(), plan.landmarks.size(), plan.landmarks_failed.size(), ms);
    const int working_count = int(data.workings.size());
    check(int(plan.sites.size()) + plan.merged + plan.in_village + int(plan.failed.size()) == working_count, "每一处采场都有下落");
    check(plan.sites.size() >= 320 && int(plan.failed.size()) * 50 <= working_count, "绝大多数采场落了地（落不了的不过 2%）");
    std::printf("  地标：摆了 %zu、落不了 %zu、岛底的洞 %d\n", plan.landmarks.size(), plan.landmarks_failed.size(), plan.underside);
    check(plan.underside == 26 && plan.landmarks.size() + plan.landmarks_failed.size() + 26 == 355 && plan.landmarks_failed.size() * 20 <= plan.landmarks.size(),
            "点 355 处（泉眼、洞穴、温泉、盐泉、化石）：岛底的洞 26 处不摆，其余绝大多数摆得出样子");
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
    // 主码头 #1（邑治旁）一带的土坑、采石场：开局那群的主码头在东岸的海岸平原上，近处是熟地，土坑在 5.3 km 外、采石场在 10.2 km 外的山脚
    // （以前的 #2051 一公里内有土坑、六公里内有采石场）
    const SiteDock *main_dock = sites.dock_by_id(1);
    double clay_d = 1e9, stone_d = 1e9;
    for (const ResourceSite &s : plan.sites) {
        const double d = main_dock ? std::hypot(s.face_x - main_dock->x, s.face_z - main_dock->z) : 1e9;
        clay_d = s.item == "clay" ? std::min(clay_d, d) : clay_d;
        stone_d = s.item == "stone" ? std::min(stone_d, d) : stone_d;
    }
    std::printf("  主码头 #1 最近的土坑 %.0f m、采石场 %.0f m\n", clay_d, stone_d);
    check(clay_d < 6000.0 && stone_d < 11000.0, "主码头 #1 六公里内有土坑、十一公里内有采石场");
}

// 散布：按块生成、让开、确定；存量账（采空倒下、长回来）；开局那群的密度与泊地
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
    const ResourceScatter scatter(t, sites, data, plan, START); // 合成岛；种子随便取，取开局那群的号
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
    // 远处的林子：同一条上，一团团树冠盖住的地面与近处的树差不多；也长在林地里、不上田；确定性
    {
        const double lo_x = -13 * 128.0, hi_x = 13 * 128.0, lo_z = -3 * 128.0, hi_z = 3 * 128.0;
        double near_canopy = 0.0, far_canopy = 0.0;
        for (int iz = -3; iz <= 2; iz++) {
            for (int ix = -13; ix <= 12; ix++) {
                for (const ScatterItem &it : scatter.chunk(ScatterChunkKey{ ix, iz })) {
                    if (it.kind() == ScatterLook::BROADLEAF || it.kind() == ScatterLook::CONIFER) {
                        near_canopy += it.scale * it.scale;
                    }
                }
            }
        }
        int clumps = 0, clumps_in_forest = 0, clumps_on_field = 0;
        bool far_same = true;
        for (int iz = -1; iz <= 0; iz++) {
            for (int ix = -4; ix <= 3; ix++) {
                const std::vector<ScatterItem> far = scatter.far_trees(ix, iz, 512.0, 19.5);
                const std::vector<ScatterItem> again = scatter.far_trees(ix, iz, 512.0, 19.5);
                far_same = far_same && far.size() == again.size() && (far.empty() || (far.back().x == again.back().x && far.back().scale == again.back().scale));
                for (const ScatterItem &it : far) {
                    if (it.x < lo_x || it.x >= hi_x || it.z < lo_z || it.z >= hi_z) {
                        continue;
                    }
                    clumps++;
                    far_canopy += it.scale * it.scale;
                    clumps_in_forest += g.landcover[g.nearest_cell(it.x, it.z)] == 4 ? 1 : 0;
                    clumps_on_field += std::hypot(it.x, it.z) < 600.0 ? 1 : 0;
                }
            }
        }
        std::printf("  远处的林子：%d 团（近处 %d 棵），树冠盖住的地面 远 / 近 = %.2f\n", clumps, trees, far_canopy / std::max(1.0, near_canopy));
        check(far_same && clumps > 100 && clumps * 5 < trees, "远处的林子：确定性，团数比近处的树少得多");
        check(far_canopy > 0.7 * near_canopy && far_canopy < 1.3 * near_canopy, "一团团树冠盖住的地面与近处的树差不出三成");
        check(clumps_in_forest * 100 >= clumps * 85 && clumps_on_field == 0, "远处的林子也在林地里、不上田");
    }
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
    const ResourceScatter cleared(t, sites, data, with_site, START);
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

    // 开局那群：主码头附近的密度、采场的泊地没有树、生成一块的耗时
    const std::string dir = start_dir();
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary), res_in(dir + "/resources.tsv", std::ios::binary),
            wx_in(dir + "/weather.tsv", std::ios::binary);
    if (!bin_in || !tsv_in || !res_in || !wx_in) {
        std::printf("  （跳过：没有开局那群的资产）\n");
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
    WorldTerrain t51(grid, sites51.terrain_islands(), START, river_rules);
    lay_world_works(t51, sites51);
    const ResourcePlan plan51 = plan_resource_sites(t51, sites51, data51);
    const ResourceScatter s51(t51, sites51, data51, plan51, START);
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
    std::printf("  开局那群主码头周围 %d 块：%zu 株（树 %d、灌丛 %d、苇 %d、浮石 %d），每块 %.2f ms\n", chunks, items,
            near[int(ScatterLook::BROADLEAF)] + near[int(ScatterLook::CONIFER)], near[int(ScatterLook::SHRUB)], near[int(ScatterLook::REEDS)],
            near[int(ScatterLook::FLOATSTONE)], ms / chunks);
    // 开局那群的主码头在海岸平原上（3 km 内可耕地一半、草坡两成）：树四百来棵、灌丛一千多丛（以前的 #2051 码头边有林子，树五百棵以上）
    check(near[int(ScatterLook::BROADLEAF)] + near[int(ScatterLook::CONIFER)] > 300 && near[int(ScatterLook::SHRUB)] > 1000, "主码头周围一公里有树、有灌丛");
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
    const ResourceScatter scatter(t, WorldSites(), ResourceData(), ResourcePlan(), START);
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
    layout_place(ship, { 0, 0, -2 }, ModuleKind::STORAGE); // 岛群开局那条：储存区 + 帆 + 驾驶区
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

namespace {

struct TestResult : JobResult {
    int value = 0;
    explicit TestResult(int p_value) :
            value(p_value) {}
};

int result_value(const std::unique_ptr<JobResult> &p_result) {
    const TestResult *r = dynamic_cast<const TestResult *>(p_result.get());
    return r ? r->value : -1;
}

} // namespace

void test_job_pool() {
    std::printf("job pool\n");
    const auto key = [](uint64_t id) { return JobPool::key(JobKind::TEST, id); };
    {
        // 一条线程、先交后开：按优先级做（一样的先来先做）；同键再交换成新的优先级与做法
        JobPool pool;
        std::mutex m;
        std::vector<int> order;
        const auto work = [&](int v) {
            return [&, v]() {
                std::lock_guard<std::mutex> lock(m);
                order.push_back(v);
                return std::unique_ptr<JobResult>(new TestResult(v));
            };
        };
        check(pool.submit(key(1), 30.0, work(1)) && pool.submit(key(2), 10.0, work(2)) && pool.submit(key(3), 20.0, work(3)), "交活：排上了");
        check(pool.submit(key(4), 20.0, work(4)), "一样的优先级：排在后面");
        check(pool.submit(key(1), 5.0, work(11)), "同键再交：换成新的");
        check(pool.queued_count(JobKind::TEST) == 4 && pool.state(key(1)) == JobPool::State::QUEUED, "排着 4 件（同键只一件）");
        check(pool.cancel(key(3)) && !pool.cancel(key(3)) && pool.state(key(3)) == JobPool::State::NONE, "撤掉一件");
        pool.start(1);
        std::vector<JobPool::Done> done;
        for (int spin = 0; spin < 2000 && done.size() < 3; spin++) {
            pool.collect(JobKind::TEST, done);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(done.size() == 3, "做完了三件，都取回来了");
        check(order == std::vector<int>({ 11, 2, 4 }), "按优先级做：5 → 10 → 20（被撤的没做、换掉的按新做法做）");
        check(done.size() == 3 && done[0].key == key(1) && result_value(done[0].result) == 11, "结果按做完的先后、带着键");
        check(pool.state(key(1)) == JobPool::State::NONE && pool.get_ran() == 3, "取走了就没有了");
    }
    {
        // 没开线程：run_now 在本线程当场做；重排优先级、按种类撤
        JobPool pool;
        pool.submit(key(7), 1.0, []() { return std::unique_ptr<JobResult>(new TestResult(7)); });
        pool.submit(key(8), 2.0, []() { return std::unique_ptr<JobResult>(new TestResult(8)); });
        pool.submit(key(9), 3.0, []() { return std::unique_ptr<JobResult>(new TestResult(9)); });
        check(result_value(pool.run_now(key(8))) == 8 && pool.state(key(8)) == JobPool::State::NONE, "急用：排着的当场做，不进「做完了」");
        check(pool.run_now(key(99)) == nullptr, "急用一件没交过的：空");
        pool.reprioritize(JobKind::TEST, [&](uint64_t k) { return k == key(7) ? -1.0 : 0.5; });
        check(pool.state(key(7)) == JobPool::State::NONE && pool.state(key(9)) == JobPool::State::QUEUED, "重排：返回负的就撤掉");
        pool.cancel_kind(JobKind::TEST);
        check(pool.queued_count(JobKind::TEST) == 0, "按种类全撤");
    }
    {
        // 几条线程一起做一百件，一件不少；正在做的 run_now 等它做完
        JobPool pool;
        pool.start(3);
        std::atomic<int> sum{ 0 };
        for (int k = 0; k < 100; k++) {
            pool.submit(key(uint64_t(k)), double(k), [&sum, k]() {
                sum += k;
                return std::unique_ptr<JobResult>(new TestResult(k));
            });
        }
        pool.submit(key(500), 1000.0, []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return std::unique_ptr<JobResult>(new TestResult(500));
        });
        for (int spin = 0; spin < 2000 && pool.state(key(500)) == JobPool::State::QUEUED; spin++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(result_value(pool.run_now(key(500))) == 500, "急用正在做的：等它做完、拿到结果");
        std::vector<JobPool::Done> done;
        for (int spin = 0; spin < 5000 && done.size() < 100; spin++) {
            pool.collect(JobKind::TEST, done);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(done.size() == 100 && sum == 4950, "三条线程做完一百件，一件不少");
        pool.submit(key(600), 1.0, []() { return std::unique_ptr<JobResult>(new TestResult(600)); });
        pool.stop();
        check(pool.thread_count() == 0 && pool.state(key(600)) == JobPool::State::NONE, "停：排着的撤掉、线程收了");
    }
    {
        // 优先级是 NaN（玩家掉出世界时按距离给的就是）：排到最后，次序不乱——同键再交、撤、重排都照常，一件不多一件不少
        const double nan = std::numeric_limits<double>::quiet_NaN();
        JobPool pool;
        std::mutex m;
        std::vector<int> order;
        const auto work = [&](int v) {
            return [&, v]() {
                std::lock_guard<std::mutex> lock(m);
                order.push_back(v);
                return std::unique_ptr<JobResult>(new TestResult(v));
            };
        };
        for (int k = 0; k < 8; k++) {
            pool.submit(key(uint64_t(k)), (k % 2) ? nan : double(k), work(k));
        }
        pool.submit(key(1), nan, work(101));
        pool.submit(key(2), nan, work(102));
        check(pool.cancel(key(3)) && pool.cancel(key(5)) && pool.state(key(3)) == JobPool::State::NONE, "NaN 的撤得掉");
        pool.reprioritize(JobKind::TEST, [&](uint64_t k) { return k == key(4) ? nan : (k == key(7) ? -1.0 : 1.0); });
        check(pool.queued_count(JobKind::TEST) == 5 && pool.state(key(7)) == JobPool::State::NONE, "重排成 NaN 的留着、负的撤掉：排着 5 件");
        pool.start(1);
        std::vector<JobPool::Done> done;
        for (int spin = 0; spin < 2000 && done.size() < 5; spin++) {
            pool.collect(JobKind::TEST, done);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(done.size() == 5 && pool.queued_count(JobKind::TEST) == 0, "做完 5 件，一件不多一件不少");
        check(order == std::vector<int>({ 0, 6, 101, 102, 4 }), "同优先级先来先做、NaN 排到最后、同键再交按新做法");
    }
    {
        // 行星计划 P3：通道——几个岛群共用一个池，同一种活同一个号分开排、分开取、分开撤；拆一个群只等它自己的
        const auto lane_key = [](uint64_t id, int lane) { return JobPool::key(JobKind::TEST, id, lane); };
        check(JobPool::lane_of(lane_key(5, 3)) == 3 && JobPool::id_of(lane_key(5, 3)) == 5 && JobPool::kind_of(lane_key(5, 3)) == JobKind::TEST &&
                        lane_key(5, 3) != lane_key(5, 4),
                "键：种类、通道、号拆得回来，同号不同通道是两个键");
        JobPool pool;
        pool.submit(lane_key(1, 1), 1.0, []() { return std::unique_ptr<JobResult>(new TestResult(11)); });
        pool.submit(lane_key(1, 2), 1.0, []() { return std::unique_ptr<JobResult>(new TestResult(21)); });
        pool.submit(lane_key(2, 2), 1.0, []() { return std::unique_ptr<JobResult>(new TestResult(22)); });
        check(pool.queued_count(JobKind::TEST, 1) == 1 && pool.queued_count(JobKind::TEST, 2) == 2 && pool.queued_count(JobKind::TEST) == 0,
                "按通道数：1 号一件、2 号两件、0 号没有");
        pool.reprioritize(JobKind::TEST, [](uint64_t) { return -1.0; }, 1);
        check(pool.queued_count(JobKind::TEST, 1) == 0 && pool.queued_count(JobKind::TEST, 2) == 2, "重排只动自己的通道");
        pool.start(2);
        std::vector<JobPool::Done> done1, done2;
        for (int spin = 0; spin < 5000 && done2.size() < 2; spin++) {
            pool.collect(JobKind::TEST, done1, 1);
            pool.collect(JobKind::TEST, done2, 2);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(done1.empty() && done2.size() == 2, "按通道取：2 号的两件在 2 号");
        std::atomic<bool> finished{ false };
        pool.submit(lane_key(7, 3), 1.0, [&finished]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            finished = true;
            return std::unique_ptr<JobResult>(new TestResult(7));
        });
        for (int k = 0; k < 20; k++) {
            pool.submit(lane_key(uint64_t(100 + k), 3), 5.0 + k, []() { return std::unique_ptr<JobResult>(new TestResult(0)); });
        }
        pool.submit(lane_key(9, 4), 100.0, []() { return std::unique_ptr<JobResult>(new TestResult(9)); });
        for (int spin = 0; spin < 2000 && pool.state(lane_key(7, 3)) == JobPool::State::QUEUED; spin++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        pool.drain_lane(3);
        check(finished && pool.queued_in_lane(3) == 0 && pool.state(lane_key(7, 3)) == JobPool::State::NONE, "拆 3 号：等正在做的做完、排着的撤、做完的丢");
        std::vector<JobPool::Done> done4;
        for (int spin = 0; spin < 2000 && done4.empty(); spin++) {
            pool.collect(JobKind::TEST, done4, 4);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(done4.size() == 1, "拆 3 号不碰 4 号");
    }
    {
        // 键的位数（计划 P3 第 9 条）：地面块（层 4 位、格号各 24 位）、散布块（24 + 24）、水面块（26 + 26）都在 52 位的号里，拆得回来
        const TileKey far{ 12, (1 << 23) - 1, -(1 << 23) };
        const uint64_t tk = JobPool::key(JobKind::TILE, far.packed(), 15);
        check(JobPool::id_of(tk) == far.packed() && JobPool::lane_of(tk) == 15 && JobPool::kind_of(tk) == JobKind::TILE, "地面块的键：层 12、格号 ±2²³ 放得下");
        const ScatterChunkKey sc{ -(1 << 23), (1 << 23) - 1 };
        check(ScatterChunkKey::unpack(JobPool::id_of(JobPool::key(JobKind::SCATTER, sc.packed(), 9))) == sc, "散布块的键：±2²³ 拆得回来");
        // 岛群最大 38,000 km²（400 m 格）约 200 km 见方：叶块 128 m 的格号不到 1,600，远在 2²³ 以内
        check(200000.0 / TileLayout::LEAF_M < double(1 << 23) && 200000.0 / RiverMeshRules().chunk_m < double(1 << 25), "最大的岛群也溢不出");
    }
}

// 模拟分级（行星计划 P3）：离开了一阵的账从哪一刻接着结
void test_resume_clock() {
    const double day = 2400.0;
    check(resume_clock(1000.0, 1500.0, day) == 1000.0, "离开半个钟头：从存下的那一刻接着结");
    check(resume_clock(1000.0, 1000.0 + day, day) == 1000.0, "离开正好一天：整天都补");
    check(resume_clock(1000.0, 1000.0 + 26.0 / 24.0 * day, day) == 1000.0 + 2.0 / 24.0 * day, "离开 26 个钟头：只补最后 24 个钟头（跳过两个钟头）");
    check(resume_clock(5000.0, 4000.0, day) == 5000.0, "账比钟还新（不该有）：不往回挪");
}

// 每个三角面的几何法线（Godot：正面顺时针，n = (v0 − v2) × (v0 − v1)）与记下的顶点法线同向
bool winding_ok(const MeshPart &p_part) {
    for (size_t t = 0; t + 2 < p_part.indices.size(); t += 3) {
        const auto P = [&](int i, int c) { return double(p_part.positions[size_t(p_part.indices[t + size_t(i)]) * 3 + size_t(c)]); };
        const double ax = P(0, 0) - P(2, 0), ay = P(0, 1) - P(2, 1), az = P(0, 2) - P(2, 2);
        const double bx = P(0, 0) - P(1, 0), by = P(0, 1) - P(1, 1), bz = P(0, 2) - P(1, 2);
        const double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        const size_t v = size_t(p_part.indices[t]) * 3;
        if (nx * p_part.normals[v] + ny * p_part.normals[v + 1] + nz * p_part.normals[v + 2] <= 0.0) {
            return false;
        }
    }
    return true;
}

void test_building_mesh() {
    std::printf("building mesh\n");
    const FlatIsland flat = lay_flat_island(FlatIslandSpec());
    const FlatGround plate(flat);
    const SiteRules rules;
    const MapSiteGround site(plate, rules);
    const BuildingDef &house = *buildings::by_id("house");
    Building b;
    b.kind = "house";
    b.x = 20.0;
    b.z = -100.0;
    b.yaw = 0.6;
    const BuildingFrame frame = building_frame(site, house, b, rules);
    check(frame.grounded && std::abs(frame.floor_y - house.plinth_m) < 1e-9 && std::abs(frame.bottom + rules.bury_m) < 1e-9, "平地上的民居：地坪 = 台基高、台基埋 bury_m");
    Building off = b;
    off.x = 400.0;
    check(!building_frame(site, house, off, rules).grounded, "岛外的：没有地面");

    const HouseTemplate tmpl = bare_template(house);
    const BuildingMesh m = build_building_mesh(plate, frame, b, tmpl, 3);
    const BuildingParts parts = building_parts(house, tmpl, frame.floor_y - frame.bottom, [&house](double, double) { return house.plinth_m; });
    int visual = 0, solid = 0;
    for (const BoxPart &part : parts.boxes) {
        visual += part.visual;
        solid += part.solid;
    }
    check(m.index == 3 && m.group_count == parts.group_count && int(m.groups.size()) == parts.group_count, "组数与 building_parts 同");
    check(m.triangle_count() == 12 * visual + 12 + 8, "三角面：每个画的盒子 12 个 + 屋脊 12 + 悬山 8（" + std::to_string(m.triangle_count()) + "）");
    check(m.collision.size() == size_t(solid) * 12 * 9, "碰撞：每个挡人的盒子 12 个三角面");
    check(m.lamps.size() == size_t(house.levels) && m.lamps[0].group == 1, "每层一盏灯，在那层的组里");
    check(!m.groups[size_t(m.roof_group())][size_t(PartMaterial::ROOF)].empty(), "瓦面在顶那一组");
    check(m.template_id == tmpl.id && m.doors.size() == parts.doors.size() && m.ladders.size() == parts.ladders.size(), "模板 id、门、梯子照 building_parts");
    bool wound = true;
    for (const auto &group : m.groups) {
        for (const MeshPart &part : group) {
            wound = wound && winding_ok(part) && part.normals.size() == part.positions.size();
        }
    }
    check(wound, "每个三角面都朝外（正面顺时针，与法线同向）");
    const BuildingDef &berth = *buildings::by_id("berth");
    Building bb = b;
    bb.kind = "berth";
    const BuildingMesh bm = build_building_mesh(plate, building_frame(site, berth, bb, rules), bb, tmpl, 0);
    check(bm.template_id == "berth" && bm.lamps.empty() && bm.groups[size_t(bm.roof_group())][size_t(PartMaterial::ROOF)].empty(), "船台：不挑模板、没有灯、没有顶");

    // 简模：台基 + 墙 + 悬山，顶点色，朝外，都在占地（算上出檐）里
    MeshPart proxy;
    append_building_proxy(proxy, house, b, frame, 10.0, -90.0);
    bool inside = true;
    double top = -1e9;
    for (size_t k = 0; k < proxy.positions.size(); k += 3) {
        inside = inside && footprint_contains(house, b, proxy.positions[k] + 10.0, proxy.positions[k + 2] - 90.0, house_dims::ROOF_OVERHANG + 0.01);
        top = std::max(top, double(proxy.positions[k + 1]));
    }
    check(!proxy.empty() && proxy.colors.size() == proxy.positions.size() / 3 * 4 && winding_ok(proxy), "简模：有顶点色、每面朝外");
    check(inside && top < frame.floor_y + house.wall_m + house.roof_m, "简模在占地（算上出檐）里、不高过屋脊");
    check(proxy.indices.size() / 3 < 40, "简模一座不到 40 个三角面（" + std::to_string(proxy.indices.size() / 3) + "）");

    // 格子：每座只进一格，格的外接框盖住它
    SettlementLayout layout;
    for (int k = 0; k < 12; k++) {
        Building h = b;
        h.x = -100.0 + 19.0 * k;
        h.z = -40.0 + 7.0 * (k % 3);
        layout.add(h);
    }
    const std::vector<BuildingFrame> frames = layout_frames(plate, layout, rules);
    const std::vector<VillageCell> cells = village_cells(layout, frames, 64.0);
    std::vector<int> seen(12, 0);
    bool covered = true;
    for (const VillageCell &c : cells) {
        for (int k : c.buildings) {
            seen[size_t(k)]++;
            covered = covered && cell_distance(c, layout.at(k).x, layout.at(k).z) == 0.0 && std::floor(layout.at(k).x / 64.0) == c.ix;
        }
    }
    check(cells.size() >= 3 && std::all_of(seen.begin(), seen.end(), [](int n) { return n == 1; }) && covered, "按 64 m 分格：每座只进一格、格的外接框盖住它");
    check_near(cell_distance(cells[0], cells[0].max_x + 30.0, cells[0].min_z - 40.0), 50.0, 1e-9, "离格的距离按外接框量");

    // 模板快照：挑法与库同一套（写了款式用它，否则按位置哈希、同输入同结果）
    TemplateSet set;
    HouseTemplate a = tmpl, c = tmpl;
    a.id = "house/aaa";
    c.id = "house/ccc";
    set.templates[a.id] = a;
    set.templates[c.id] = c;
    Building styled = b;
    styled.style = "ccc";
    check(set.resolve_id(styled) == "house/ccc", "写了款式：用那一套");
    const std::string picked = set.resolve_id(b);
    check((picked == "house/aaa" || picked == "house/ccc") && set.resolve_id(b) == picked, "没写款式：在能自动挑的里按位置挑，同一座每次一样");
    Building tower = b;
    tower.kind = "tower";
    check(set.resolve_id(tower).empty() && set.resolve(tower).id == bare_template(*buildings::by_id("tower")).id, "库里没有这种外壳：空屋");
}

void test_village_prep_start() {
    const std::string dir = start_dir();
    std::ifstream bin_in(dir + "/terrain.bin", std::ios::binary), tsv_in(dir + "/sites.tsv", std::ios::binary);
    if (!bin_in || !tsv_in) {
        std::printf("village prep（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("village prep（开局那群）\n");
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(bin_in)), std::istreambuf_iterator<char>());
    const std::string tsv((std::istreambuf_iterator<char>(tsv_in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    WorldSites sites;
    std::string err;
    check(parse_world_grid(bin.data(), bin.size(), g, err) && parse_world_sites(tsv, sites, err), "读得了开局那群：" + err);
    WorldTerrain terrain(g, sites.terrain_islands(), START);
    lay_world_works(terrain, sites);
    const SiteSettlement *seat = sites.village_by_id(19);
    check(seat != nullptr, "有村001");
    if (!seat) {
        return;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const VillagePrep prep = prepare_world_village(terrain, *seat, START, {});
    const auto t1 = std::chrono::steady_clock::now();
    check(prep.layout == plan_world_village(terrain, *seat, START, WorldVillageRules()).layout && prep.layout == prep.plan.layout, "没有改动：清单就是铺村算法铺的");
    check(prep.frames.size() == size_t(prep.layout.size()), "每座一个地坪");
    size_t in_cells = 0;
    int grounded = 0;
    for (const VillageCell &c : prep.cells) {
        in_cells += c.buildings.size();
    }
    for (const BuildingFrame &f : prep.frames) {
        grounded += f.grounded;
    }
    check(in_cells == size_t(prep.layout.size()) && grounded == prep.layout.size(), "每座都进了格子、都站在地上（" + std::to_string(prep.layout.size()) + " 座、" + std::to_string(prep.cells.size()) + " 格）");
    const std::string removed = "-" + building_id(prep.layout.at(5));
    const VillagePrep edited = prepare_world_village(terrain, *seat, START, { "# 注释", removed });
    check(edited.layout.size() == prep.layout.size() - 1 && edited.unmatched == 0 && edited.frames.size() == size_t(edited.layout.size()), "叠上改动：拆一座");
    // 一格细模的花费（后台线程上的活）
    const WorldGround ground(terrain, seat->x, seat->z);
    const VillageCell &cell = prep.cells[prep.cells.size() / 2];
    const auto t2 = std::chrono::steady_clock::now();
    int tris = 0;
    for (int k : cell.buildings) {
        const Building &bld = prep.layout.at(k);
        const BuildingDef *def = buildings::by_id(bld.kind);
        tris += build_building_mesh(ground, prep.frames[size_t(k)], bld, def && def->house ? bare_template(*def) : HouseTemplate(), k).triangle_count();
    }
    const auto t3 = std::chrono::steady_clock::now();
    const double prep_ms = std::chrono::duration<double, std::milli>(t1 - t0).count(), cell_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    std::printf("  邑治备料 %.1f ms（%d 座、%zu 格）；一格 %zu 座的细模 %.1f ms、%d 个三角面\n", prep_ms, prep.layout.size(), prep.cells.size(), cell.buildings.size(), cell_ms, tris);
    // 导航格：窗口在房子外扩一圈，挑水线与 village_water 同
    double x0 = 0.0, z0 = 0.0;
    int cols = 0, rows = 0;
    const SimRules sim;
    village_nav_window(prep.layout, sim.nav, 30.0, x0, z0, cols, rows);
    const VillageNav nav = prepare_village_nav(ground, prep.layout, sim, x0, z0, cols, rows);
    check(nav.nav.cols >= cols && nav.nav.rows >= rows && nav.water.size() == village_water(ground, prep.layout, sim.work).size(), "导航格盖住村子（" + std::to_string(nav.nav.cols) + " × " + std::to_string(nav.nav.rows) + "），挑水线同 village_water");
}

void test_stream_meter() {
    std::printf("stream meter\n");
    StreamMeter m;
    m.add(10, StreamChannel::TILES, 1500);
    m.add(10, StreamChannel::TILES, 500);
    m.add(10, StreamChannel::VILLAGES, 3000);
    m.add(11, StreamChannel::TILES, 700);
    const StreamMeter::Frame *f = m.frame(10);
    check(f && f->usec[size_t(StreamChannel::TILES)] == 2000 && f->usec[size_t(StreamChannel::VILLAGES)] == 3000 && f->total() == 5000, "一帧里同一项累加、各项分开");
    check(m.get_totals(StreamChannel::TILES).usec == 2700 && m.get_totals(StreamChannel::TILES).max_frame_usec == 2000 && m.get_totals(StreamChannel::TILES).frames == 2,
            "累计、单帧最多、有开销的帧数");
    check(m.frame(12) == nullptr, "没记的帧：空");
    m.add(10 + StreamMeter::RING, StreamChannel::SCATTER, 100);
    check(m.frame(10) == nullptr && m.frame(10 + StreamMeter::RING) != nullptr, "只记最近 RING 帧：旧的被新的挤掉");
    check(std::string(stream_channel_key(StreamChannel::FOLK)) == "folk", "频道名");
    m.reset();
    check(m.frame(11) == nullptr && m.get_totals(StreamChannel::TILES).usec == 0, "清零");
}

// ---------------------------------------------------------------- 时令与天气

bool load_start_climate(Climate &r_climate) {
    std::ifstream in(start_dir() + "/weather.tsv", std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string err;
    return parse_climate(text, r_climate, err);
}

void test_almanac() {
    std::printf("almanac\n");
    const CalendarRules cal; // 336 天、4 季、每季 3 个月、每月 28 天，北半球夏至第 126 天，交角 34°，北纬 30°
    CalendarDate d = calendar_date(0.0, cal);
    check(d.year == 0 && d.day_of_year == 0 && d.season == 0 && d.month == 0 && d.day_of_month == 0 && d.hour == 0.0, "第 0 天子夜：零年、头一季、一月初一");
    check(month_day_text(d) == "一月初一", "一月初一：" + month_day_text(d));
    d = calendar_date(336.0 + 84.0 + 28.0 + 13.5, cal);
    check(d.year == 1 && d.day_of_year == 125 && d.season == 1 && d.month == 4 && d.day_of_month == 13 && std::abs(d.hour - 12.0) < 1e-9, "第二年、第二季、五月十四、正午");
    check(month_day_text(d) == "五月十四", "五月十四：" + month_day_text(d));
    d = calendar_date(-1.25, cal);
    check(d.year == -1 && d.day_of_year == 334 && std::abs(d.hour - 18.0) < 1e-9, "往前一天多（−1.25）：上一年的倒数第二天 18 点");
    CalendarDate t;
    t.month = 11;
    t.day_of_month = 19;
    const std::string a = month_day_text(t);
    t.day_of_month = 20;
    const std::string b = month_day_text(t);
    t.month = 9;
    t.day_of_month = 9;
    const std::string c = month_day_text(t);
    t.day_of_month = 10;
    check(a == "十二月二十" && b == "十二月廿一" && c == "十月初十" && month_day_text(t) == "十月十一", "日子的叫法：" + a + " " + b + " " + c + " " + month_day_text(t));

    check_near(sun_declination_deg(126.0, cal), 34.0, 1e-9, "北半球夏至：直射北纬 34°");
    check_near(sun_declination_deg(126.0 + 168.0, cal), -34.0, 1e-9, "半年后：直射南纬 34°");
    check_near(sun_declination_deg(42.0, cal), 0.0, 1e-9, "两者之间的分日：直射赤道");
    CalendarRules eq = cal;
    eq.latitude_deg = 0.0;
    SunState s = sun_at(42.5, eq);
    check_near(s.altitude_deg, 90.0 - std::abs(sun_declination_deg(42.5, cal)), 1e-6, "赤道分日正午：太阳在天顶（差赤纬那一点）");
    check_near(s.sunrise_hour, 6.0, 0.1, "赤道分日：六点上下日出");
    check_near(s.sunset_hour, 18.0, 0.1, "……十八点上下日落");
    s = sun_at(42.0 + 8.0 / 24.0, eq);
    check(s.dir_x > 0.5 && s.dir_y > 0.0, "上午八点太阳在东边");
    s = sun_at(42.0 + 16.0 / 24.0, eq);
    check(s.dir_x < -0.5 && s.dir_y > 0.0, "下午四点在西边");
    check_near(std::sqrt(s.dir_x * s.dir_x + s.dir_y * s.dir_y + s.dir_z * s.dir_z), 1.0, 1e-9, "方向是单位向量");
    CalendarRules south = cal;
    south.latitude_deg = -31.65;
    const SunState winter = sun_at(126.5, south), summer = sun_at(294.5, south);
    check(winter.day_length_h < 10.0 && summer.day_length_h > 14.0,
            "南纬 31.65°：北半球夏至那天是冬天，昼 " + std::to_string(winter.day_length_h) + " h；半年后昼 " + std::to_string(summer.day_length_h) + " h");
    check(winter.dir_z < 0.0 && winter.altitude_deg > 20.0 && winter.altitude_deg < 30.0, "南半球冬天正午：太阳在北边、只有二十几度高");
    CalendarRules polar = cal;
    polar.latitude_deg = 80.0;
    check(sun_at(126.5, polar).day_length_h == 24.0 && sun_at(294.5, polar).day_length_h == 0.0, "北纬 80°：夏至极昼、冬至极夜");
    check(moon_at(0.0, cal).illuminated < 1e-9 && std::abs(moon_at(14.0, cal).illuminated - 1.0) < 1e-9, "初一朔、十五望");
    check(moon_at(14.0, eq).altitude_deg > 50.0, "望月午夜高挂");
    double start = 0.0, end = 0.0;
    daylight_work_hours(126.0, south, DaylightWork(), start, end);
    check(start > 7.5 && start <= 8.0 && end < 16.5, "冬天：日出后快八点才出工、四点多收工（" + std::to_string(start) + " – " + std::to_string(end) + "）");
    daylight_work_hours(294.0, south, DaylightWork(), start, end);
    check(start == 5.0 && end > 19.0, "夏天：五点出工（夹住）、七点多收工（" + std::to_string(start) + " – " + std::to_string(end) + "）");
}

void test_weather_noise() {
    std::printf("weather noise\n");
    // 与 game/shaders/weather.gdshaderinc 同一个式子：拿 numpy 按同一式子算的值对照
    check_near(wx_fbm(0.3, 0.7), 0.27625539464700555, 1e-9, "云团噪声 (0.3, 0.7)");
    check_near(wx_fbm(123.45, -67.8), 0.5225782686046223, 1e-9, "云团噪声 (123.45, −67.8)");
    check_near(wx_fbm(-500.25, 999.5), 0.6180736762068376, 1e-9, "云团噪声 (−500.25, 999.5)");
    check_near(wx_fbm(1023.9, 1024.1), 0.21326473863396878, 1e-9, "云团噪声 (1023.9, 1024.1)");
    bool periodic = true, in_range = true;
    for (int k = 0; k < 200; k++) {
        const double x = -800.0 + k * 7.31, y = 300.0 - k * 3.17;
        const double v = wx_fbm(x, y);
        in_range = in_range && v >= 0.0 && v <= 1.0;
        periodic = periodic && std::abs(v - wx_fbm(x + WX_PERIOD, y - 2.0 * WX_PERIOD)) < 1e-9;
    }
    check(in_range, "fbm 在 [0, 1]");
    check(periodic, "按 1024 格循环（着色器里漂移取模不失精度）");
    for (double cov : { 0.1, 0.25, 0.5, 0.75, 0.9 }) {
        const double thr = wx_threshold(cov);
        int above = 0, n = 0;
        for (int i = 0; i < 300; i++) {
            for (int j = 0; j < 300; j++) {
                above += wx_fbm(i * 3.31 + 0.13, j * 3.29 + 0.29) >= thr ? 1 : 0;
                n++;
            }
        }
        check_near(double(above) / n, cov, 0.03, "覆盖率 " + std::to_string(cov) + " 的门槛：重新取样落在里面的份额");
    }
    check(wx_threshold(0.0) > 1.0 && wx_threshold(1.0) < 0.0, "覆盖率 0：谁都不在里面；1：处处都在");
}

void test_climate() {
    std::printf("climate\n");
    Climate old;
    std::string err;
    check(parse_climate("# year_days 3  precip_mm 12\n0\t0.4\t22.6\t小雨\n1\t0.0\t25.2\t晴\n2\t11.6\t20.0\t大雨\n", old, err), "旧版四列能读：" + err);
    check(old.days.size() == 3 && old.days[0].kind == WeatherKind::RAIN && old.days[2].kind == WeatherKind::HEAVY_RAIN && old.days[1].wind_ms == 2.0 &&
                    old.calendar.year_days == 3 && old.precip_mm == 12.0,
            "旧版：类型对、风取默认、一年按行数");
    Climate bad;
    check(!parse_climate("0\t1\t2\t冰雹\n", bad, err), "不认识的天气报错");
    check(!parse_climate("# year_days 5\n0\t0\t20\t晴\n", bad, err), "天数对不上报错");
    Climate c;
    if (!load_start_climate(c)) {
        std::printf("  （跳过：没有开局那群的资产）\n");
        return;
    }
    check(c.days.size() == 336 && std::abs(c.calendar.latitude_deg - 34.2321) < 1e-3 && c.calendar.tilt_deg == 34.0 && c.ref_m == 755.5 &&
                    std::abs(c.rim_m - 528.3) < 1e-9 && c.season_names.size() == 4 && c.season_names[0] == "冷季" && c.calendar.days_per_month == 28,
            "开局那群：336 天、北纬 34.23°、交角 34°、台面 755.5 m、岸缘 528.3 m、四季名（冷季起头）");
    check(c.days[0].kind == WeatherKind::CLEAR && c.days[0].wind_from_deg == 258.0 && std::abs(c.days[0].wind_ms - 3.7) < 1e-9 && c.days[0].sailable,
            "第 0 天：晴、风从 258° 来 3.7 m/s、能出航");
    int no_sail = 0;
    for (const ClimateDay &day : c.days) {
        no_sail += day.sailable ? 0 : 1;
    }
    check(no_sail == 61, "禁航 61 天（生成器的数）");
    check(wind_name(1.0, 0.0) == "西风" && wind_name(0.0, 1.0) == "北风" && wind_name(-1.0, -1.0) == "东南风" && wind_name(0.1, 0.1) == "无风", "风向的叫法：风从哪来");
}

void test_weather_model() {
    Climate c;
    if (!load_start_climate(c)) {
        std::printf("weather model（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("weather model（开局那群）\n");
    const WeatherModel wx(c, 0.0, 2250.0);
    const double ref = c.ref_m;
    // 开局那群（#6329）的逐日表里挑的三天：第 159 天晴、第 160 天大雨（34 mm，风从 122° 来）、第 161 天晴
    constexpr int CLEAR_DAY = 161, RAIN_DAY = 160;
    // 晴天正午哪儿都不下；大雨天白天雨区七成上下，整片平均对得上那天的雨量
    double clear_max = 0.0, rain_sum = 0.0;
    int wet = 0, n = 0;
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 20; j++) {
            const double x = -40000.0 + i * 4000.0 + 700.0, z = -40000.0 + j * 4000.0 + 300.0;
            clear_max = std::max(clear_max, wx.sample(CLEAR_DAY + 0.5, x, z, ref).precip_mm_h);
            for (double hour : { 8.0, 11.0, 14.0, 17.0 }) {
                const WeatherSample s = wx.sample(RAIN_DAY + hour / 24.0, x, z, ref);
                rain_sum += s.precip_mm_h;
                wet += s.precip_mm_h > 0.05 ? 1 : 0;
                n++;
            }
        }
    }
    check(c.days[CLEAR_DAY].kind == WeatherKind::CLEAR && c.days[RAIN_DAY].kind == WeatherKind::HEAVY_RAIN && c.days[RAIN_DAY - 1].kind == WeatherKind::CLEAR,
            "挑的三天：晴、大雨、晴");
    check(clear_max == 0.0, "晴天（第 161 天）正午：哪儿都不下");
    const double day_mm = rain_sum / n * 24.0;
    std::printf("  大雨天：雨区 %.0f%%，整片平均折成一天 %.1f mm（数据 %.1f mm）\n", 100.0 * wet / n, day_mm, c.days[RAIN_DAY].precip_mm);
    check_near(double(wet) / n, 0.72, 0.15, "大雨天：雨区约七成");
    check_near(day_mm / c.days[RAIN_DAY].precip_mm, 1.0, 0.4, "大雨天：整片平均对得上那天的雨量");

    // 时间上连续：跨过子夜的锋面（晴转大雨），一分钟一分钟地看
    const double px = 12000.0, pz = -8000.0;
    WeatherSample prev = wx.sample(RAIN_DAY - 0.25, px, pz, ref);
    double dt_max = 0.0, dc_max = 0.0, dp_max = 0.0, front_max = 0.0;
    for (int m = 1; m <= 12 * 60; m++) {
        const WeatherSample s = wx.sample(RAIN_DAY - 0.25 + m / 1440.0, px, pz, ref);
        dt_max = std::max(dt_max, std::abs(s.temp_c - prev.temp_c));
        dc_max = std::max(dc_max, std::abs(s.cloud - prev.cloud));
        dp_max = std::max(dp_max, std::abs(s.precip - prev.precip));
        front_max = std::max(front_max, s.front);
        prev = s;
    }
    check(dt_max < 0.2 && dc_max < 0.08 && dp_max < 0.1 && front_max > 0.9,
            "换天没有一刀切：每游戏分钟 气温 ≤ " + std::to_string(dt_max) + " °C、云 ≤ " + std::to_string(dc_max) + "、雨 ≤ " + std::to_string(dp_max) + "，中途经过锋面");
    // 空间上连续：同一刻相距 30 m
    double ds_max = 0.0, dsp_max = 0.0;
    for (int k = 0; k < 400; k++) {
        const double x = -30000.0 + k * 150.0, z = 5000.0 - k * 70.0;
        const WeatherSample a = wx.sample(RAIN_DAY + 0.4, x, z, ref), b = wx.sample(RAIN_DAY + 0.4, x + 30.0, z, ref);
        ds_max = std::max({ ds_max, std::abs(a.precip - b.precip), std::abs(a.cloud - b.cloud) });
        dsp_max = std::max(dsp_max, std::abs(a.precip - b.precip));
    }
    std::printf("  相距 30 m：雨、云最多差 %.3f（雨 %.3f）\n", ds_max, dsp_max);
    // 门槛 0.12：原来按 #2051 第 2 天定的 0.1；#6329 第 160 天的大雨云量高，云边的坡陡一点（0.104），雨只差 0.05
    check(ds_max < 0.12, "相距 30 m 的两点：雨与云差不出 0.12（雨区边是软的）");
    // 锋面顺风扫过：大雨从上风先到
    double tx = 0.0, tz = 0.0;
    {
        const double to = (c.days[RAIN_DAY].wind_from_deg + 180.0) * 3.14159265358979 / 180.0;
        tx = std::sin(to);
        tz = -std::cos(to);
    }
    auto arrival = [&](double p_x, double p_z) {
        for (int m = 0; m < 24 * 60; m++) {
            const double t = RAIN_DAY - 0.5 + m / 1440.0;
            if (wx.sample(t, p_x, p_z, ref).cloud_cover > 0.5 * (0.12 + 0.95)) {
                return t;
            }
        }
        return 99.0;
    };
    const double up = arrival(-20000.0 * tx, -20000.0 * tz), down = arrival(20000.0 * tx, 20000.0 * tz);
    check_near((down - up) * 24.0, 40.0 / wx.get_rules().front_kmh, 0.15, "锋面顺风扫过：下风 40 km 晚到（钟头）");

    // 海拔与季节：积雪、冰、物候。开局那群在北半球北纬 34°，历年从冷季起头：第 0–83 天冷季（冬末到早春）、84–167 暖季、168–251 热季、
    // 252–335 凉季（秋到初冬）；一月、十二月台面日均 0 °C 上下，冬天台面也积雪（#2051 在南半球、冬天只有山上有雪，这几条原来按它写）
    constexpr int HOT_DAY = 180; // 热季（七月）
    double min_line = 1e9, min_day = 0.0, summer_line = 1e9;
    for (int dd = 0; dd < 336; dd += 2) {
        const double line = wx.snow_line(dd + 0.5);
        if (line < min_line) {
            min_line = line;
            min_day = dd + 0.5;
        }
        if (dd >= 168 && dd < 224) {
            summer_line = std::min(summer_line, line);
        }
    }
    std::printf("  雪线最低 %.0f m（第 %.0f 天）；热季头两个月最低 %.0f；冰线（河）第 %.0f 天 %.0f m\n", min_line, min_day, summer_line, min_day, wx.ice_line(min_day, 20.0));
    check(min_line < ref, "冬天雪线下到台面以下（冬天台面也积雪）");
    check(min_day < 84.0 || min_day >= 280.0, "雪线最低落在冬天（冷季或凉季末）");
    check(summer_line > 1e8, "热季头两个月（第 168–223 天）：哪儿都没雪");
    double deep_day = 0.0, deep_snow = 0.0, mid_day = 0.0;
    for (int dd = 0; dd < 336; dd++) {
        const double sn = wx.elevation(dd + 0.5, 2000.0).snow_mm;
        if (sn > deep_snow) {
            deep_snow = sn;
            deep_day = dd + 0.5;
        }
        const double line = wx.snow_line(dd + 0.5);
        if (mid_day == 0.0 && line > 1300.0 && line < 1700.0 && dd >= 84) {
            mid_day = dd + 0.5;
        }
    }
    std::printf("  两千米积雪最厚 %.0f mm（第 %.1f 天），那天台面 %.1f mm；雪线在 1300–1700 m 的头一天（暖季起）第 %.1f 天\n", deep_snow, deep_day,
            wx.elevation(deep_day, ref).snow_mm, mid_day);
    check(deep_snow > 100.0 && deep_day < 100.0 && wx.elevation(deep_day, 2000.0).snow_mm > 10.0 * wx.elevation(deep_day, ref).snow_mm,
            "冷季末两千米积雪最厚（" + std::to_string(deep_snow) + " mm，第 " + std::to_string(deep_day) + " 天）、台面薄得多");
    check(wx.ice_line(min_day, 20.0) < 2050.0 && wx.ice_line(HOT_DAY + 0.5, 8.0) > 1e8, "冬天高处的河冻、热季哪儿都不冻");
    const ElevationState lush = wx.elevation(HOT_DAY + 0.5, ref), bare = wx.elevation(min_day, 1800.0);
    std::printf("  热季台面 绿 %.2f 叶 %.2f；冬天一千八百米 绿 %.2f 叶 %.2f\n", lush.green, lush.leaf, bare.green, bare.leaf);
    check(lush.green > 0.6 && lush.leaf > 0.95 && bare.leaf < 0.3 && bare.green < lush.green, "热季台面满绿、冬天高处落叶");
    double autumn_max = 0.0, spring_max = 0.0;
    for (int dd = 0; dd < 336; dd++) {
        autumn_max = std::max(autumn_max, wx.elevation(dd + 0.5, 800.0).autumn * (dd >= 224 ? 1.0 : 0.0));
        spring_max = std::max(spring_max, wx.elevation(dd + 0.5, 800.0).spring * (dd >= 40 && dd < 168 ? 1.0 : 0.0));
    }
    std::printf("  八百米：秋色最多 %.2f、春芽最多 %.2f\n", autumn_max, spring_max);
    check(autumn_max > 0.4 && spring_max > 0.3, "八百米处：入冬前有秋色、开春有春芽");
    check(wx.mean_temperature(HOT_DAY + 0.5, ref + 1000.0) < wx.mean_temperature(HOT_DAY + 0.5, ref) - 5.9, "直减率：高一千米冷六度");

    // 开发者覆盖
    WeatherModel storm = wx;
    storm.edit_override() = { true, WeatherKind::STORM, 0.0, 1.0, 0.0 };
    WeatherSample s = storm.sample(CLEAR_DAY + 0.5, 0.0, 0.0, ref);
    check(s.storm == 1.0 && s.wind_ms >= 13.99 && !s.sailable && s.day_kind == WeatherKind::STORM && s.kind == WeatherKind::STORM, "覆盖成风暴：风暴、大风、禁航（雨区外也叫风暴）");
    storm.edit_override().blend = 0.5;
    s = storm.sample(CLEAR_DAY + 0.5, 0.0, 0.0, ref);
    check_near(s.storm, 0.5, 1e-9, "渐入一半：风暴一半");
    WeatherModel cold = wx;
    cold.edit_override() = { true, WeatherKind::COUNT, -8.0, 1.0, 0.0 };
    check(mid_day > 0.0 && cold.snow_line(mid_day) < wx.snow_line(mid_day) - 800.0, "气温偏移冷 8 °C：雪线低了八百米以上（第 " + std::to_string(mid_day) + " 天）");
    check_near(cold.mean_temperature(CLEAR_DAY + 0.5, ref), wx.mean_temperature(CLEAR_DAY + 0.5, ref) - 8.0, 1e-9, "气温偏移：日均温照偏");
    WeatherModel snowy = wx;
    snowy.edit_override() = { true, WeatherKind::HEAVY_SNOW, 0.0, 1.0, 30.0 };
    s = snowy.sample(HOT_DAY + 4.0 / 24.0, 0.0, 0.0, ref);
    check(s.snow_frac > 0.9 && s.temp_c < 0.0 && s.snow_mm >= 29.0, "覆盖成大雪（热季也行）：台面零下、下的是雪、新雪留得住");
}

// 行星计划 P3：几群的天气——按权混、云团的公共平面、地方时的作息
void test_group_weather() {
    Climate c;
    if (!load_start_climate(c)) {
        std::printf("group weather（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("group weather（开局那群）\n");
    const WeatherModel own(c, 0.0, 2250.0);
    // 一天的大势：按权加起来（群与群之间的混法）一份就是原样，两份各半取中间
    const WeatherState a = weather_day_state(c.days[2]), b = weather_day_state(c.days[3]);
    WeatherState one;
    one.clear();
    one.add(a, 1.0);
    check(one.cloud == a.cloud && one.temp == a.temp && one.wind_x == a.wind_x && one.dominant() == a.dominant(), "按权加：一份就是原样");
    WeatherState half;
    half.clear();
    half.add(a, 0.5);
    half.add(b, 0.5);
    check_near(half.temp, 0.5 * (a.temp + b.temp), 1e-12, "两份各半：气温取中间");
    check_near(half.wind_x, 0.5 * (a.wind_x + b.wind_x), 1e-12, "风按分量混");
    // 地方时：西边 3.4 分钟（邻群 #6327 在开局那群西边 0.86°，比钟点晚）——出工、午歇、收工的钟点都往后挪同样的量
    const DayRules day;
    const LocalSeason here(&own, day, 126.0, 0.0, 0.0, c.ref_m), west(&own, day, 126.0, 0.0, 0.0, c.ref_m, SeasonRules(), -0.0573);
    const DayMarks h = day_marks(3, day, &here), w = day_marks(3, day, &west);
    check_near(w.work_start - h.work_start, 0.0573, 1e-9, "西边的地方时晚：出工的钟点往后挪");
    check_near(w.lunch_start - h.lunch_start, 0.0573, 1e-9, "午歇同样");
    check_near(w.work_end - h.work_end, 0.0573, 1e-9, "收工同样");
}

void test_local_season() {
    Climate c;
    if (!load_start_climate(c)) {
        std::printf("local season（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("local season（开局那群）\n");
    const WeatherModel wx(c, 0.0, 2250.0);
    const DayRules day;
    // 冬（第 294 天：北半球的冬至）与夏（第 126 天：夏至）：出工收工随日出日落（开局那群在北纬 34°）
    const LocalSeason winter(&wx, day, 294.0, 0.0, 0.0, c.ref_m), summer(&wx, day, 126.0, 0.0, 0.0, c.ref_m);
    const DayMarks w = day_marks(0, day, &winter), s = day_marks(0, day, &summer);
    check(w.work_start > 7.5 && w.work_end < 16.5 && s.work_start == 5.0 && s.work_end > 19.0, "作息：冬天快八点出工、四点多收工，夏天五点到七点多");
    DayMark mark = DayMark::LUNCH_END;
    check_near(next_day_mark(clock_at(0, 5.0, day), day, &mark, &winter), clock_at(0, w.work_start, day), 1e-6, "冬天五点之后的分界点是出工（日出后）");
    check(mark == DayMark::WORK_START, "……是出工");
    check(is_home_time(clock_at(0, 7.0, day), day, &winter) && !is_home_time(clock_at(0, 7.0, day), day, &summer), "早上七点：冬天还在家、夏天已经出工");
    check(!is_home_time(clock_at(0, 7.0, day), day, nullptr) && is_home_time(clock_at(0, 5.5, day), day, nullptr), "没有时令：照固定钟点");
    // 庄稼
    const LocalSeason deep(&wx, day, 14.0, 0.0, 0.0, c.ref_m), high(&wx, day, 126.0, 0.0, 0.0, 1900.0);
    const double noon = clock_at(0, 12.0, day);
    check(summer.growth_factor(noon) > 0.9 && summer.sowing(noon), "夏天台面：庄稼照常长、照常播");
    check(deep.growth_factor(noon) < 0.35 && !deep.sowing(noon), "深冬（第 14 天，一月）台面：庄稼几乎不长、不播（" + std::to_string(deep.growth_factor(noon)) + "）");
    check(high.growth_factor(noon) < summer.growth_factor(noon), "同一天，一千九百米比台面长得慢");
    check(crop_season_factor(20.0, 10.0, SeasonRules()) == 0.0 && crop_season_factor(20.0, 0.0, SeasonRules()) == 1.0 && crop_season_factor(4.0, 0.0, SeasonRules()) == 0.0,
            "积雪盖着不长；暖和照常；5 °C 以下不长");
    // 歇工：第 10 天改成风暴，前后两天晴
    Climate c2 = c;
    c2.days[9].kind = WeatherKind::CLEAR;
    c2.days[10].kind = WeatherKind::STORM;
    c2.days[11].kind = WeatherKind::CLEAR;
    const WeatherModel wx2(c2, 0.0, 2250.0);
    const LocalSeason here(&wx2, day, 0.0, 0.0, 0.0, c.ref_m);
    check(!here.sheltering(clock_at(9, 12.0, day)) && here.sheltering(clock_at(10, 12.0, day)) && !here.sheltering(clock_at(11, 12.0, day)), "风暴那天正午歇工，前后一天不歇");
    const double change = here.next_shelter_change(clock_at(9, 12.0, day), clock_at(11, 12.0, day));
    const double step = game_seconds(0.25, day);
    check(change > clock_at(9, 18.0, day) && change < clock_at(10, 6.0, day), "风暴在子夜前后来");
    check(std::abs(change / step - std::round(change / step)) < 1e-6, "变在一格（15 分钟）的格边上");
    check(here.sheltering(change + 1e-3) && !here.sheltering(change - 1e-3), "变的那一刻前后");
    const double back = here.next_shelter_change(change, clock_at(12, 12.0, day));
    check(back > clock_at(10, 18.0, day) && back < clock_at(11, 6.0, day) && !here.sheltering(back + 1e-3), "第二天子夜前后风停");
    check(std::isinf(here.next_shelter_change(clock_at(9, 1.0, day), clock_at(9, 12.0, day))), "这段里不变：+∞");
}

int count_outdoors(const VillageState &p_state) {
    int n = 0;
    for (const PersonState &p : p_state.people) {
        n += p.phase != FolkPhase::INDOORS ? 1 : 0;
    }
    return n;
}

void test_village_seasons() {
    Climate c;
    if (!load_start_climate(c)) {
        std::printf("village seasons（跳过：没有开局那群的资产）\n");
        return;
    }
    std::printf("village seasons（开局那群的气候、平板岛的村）\n");
    for (ClimateDay &d : c.days) {
        d.kind = WeatherKind::CLEAR; // 天气由覆盖给，免得真实的雨搅进来
    }
    WeatherModel wx(c, -100.0, 2250.0);
    const DayRules day;
    constexpr double WARM = 180.0; // 暖和的一天：热季（北半球七月；原来按南半球的 #2051 取第 20 天）
    const double h = c.ref_m;
    // 作息：冬天（第 294 天，北半球的冬至）日出晚，七点半还没人出门
    {
        SimBed bed;
        const LocalSeason season(&wx, day, 294.0, 0.0, -70.0, h);
        bed.season = &season;
        bed.wx = &wx;
        bed.wx_offset = 294.0;
        VillageState s;
        bed.found(s, true, clock_at(0, 5.5, day));
        bed.run_near(s, clock_at(0, 7.5, day));
        const int early = count_outdoors(s);
        bed.run_near(s, clock_at(0, 9.0, day));
        check(early == 0 && count_outdoors(s) > 0, "冬天：七点半还没人出门、九点有人在外面（" + std::to_string(count_outdoors(s)) + " 人）");
    }
    // 风暴歇工：近处逐帧、远处半小时一结都一样——下起来一个钟头里全村进门，停了又出门
    for (bool near : { true, false }) {
        SimBed bed;
        VillageState s;
        {
            const LocalSeason season(&wx, day, WARM, 0.0, -70.0, h);
            bed.season = &season;
            bed.found(s, near, clock_at(0, 5.5, day));
            if (near) {
                bed.run_near(s, clock_at(0, 9.0, day));
            } else {
                bed.run_far(s, clock_at(0, 9.0, day));
            }
        }
        const int before = count_outdoors(s);
        wx.edit_override() = { true, WeatherKind::STORM, 0.0, 1.0, 0.0 };
        {
            const LocalSeason season(&wx, day, WARM, 0.0, -70.0, h); // 时令记着最近问过的格：天气换了就换一份（游戏里每帧、每次结算都现造）
            bed.season = &season;
            // 开发者覆盖不在格边上变：推进一秒，外面的人就都改成往家走
            {
                HeldFields held(s.held, &bed.farm);
                const VillageWorld w = bed.world(near, near ? static_cast<FieldBook *>(&bed.near_fields) : &held);
                advance_village(s, w, s.clock + 1.0);
            }
            int busy = 0;
            for (const PersonState &p : s.people) {
                busy += p.phase != FolkPhase::INDOORS && p.task.kind != TaskKind::GO_HOME ? 1 : 0;
            }
            check(busy == 0, std::string(near ? "近处" : "远处") + "：天气一变（覆盖），外面的人当场都往家走（还有 " + std::to_string(busy) + " 人在干活）");
            if (near) {
                bed.run_near(s, clock_at(0, 10.0, day));
            } else {
                bed.run_far(s, clock_at(0, 10.0, day));
            }
        }
        const int storm = count_outdoors(s);
        wx.edit_override() = WeatherOverride();
        {
            const LocalSeason season(&wx, day, WARM, 0.0, -70.0, h);
            bed.season = &season;
            if (near) {
                bed.run_near(s, clock_at(0, 11.0, day));
            } else {
                bed.run_far(s, clock_at(0, 11.0, day));
            }
        }
        check(before > 0 && storm == 0 && count_outdoors(s) > 0, std::string(near ? "近处" : "远处") + "：风暴里一个钟头全村进门（" + std::to_string(before) + " → " +
                        std::to_string(storm) + "），停了又出门（" + std::to_string(count_outdoors(s)) + "）");
    }
    // 冷天不播不开荒、地里的不长
    {
        SimBed warm_bed;
        const LocalSeason warm(&wx, day, WARM, 0.0, -70.0, h);
        warm_bed.season = &warm;
        warm_bed.wx = &wx;
        warm_bed.wx_offset = WARM;
        VillageState ws;
        warm_bed.found(ws, true, clock_at(0, 5.5, day));
        warm_bed.run_near(ws, clock_at(0, 11.0, day));
        std::vector<std::pair<uint64_t, double>> growing;
        const std::vector<int> cells = warm_bed.farm.plot_cells();
        for (size_t k = 0; k + 1 < cells.size(); k += 2) {
            const Plot p = warm_bed.farm.plot_at(cells[k], cells[k + 1]);
            if (p.state == PlotState::GROWING) {
                growing.push_back({ (uint64_t(uint32_t(cells[k])) << 32) | uint32_t(cells[k + 1]), p.growth });
            }
        }
        wx.edit_override() = { true, WeatherKind::COUNT, -25.0, 1.0, 0.0 };
        const LocalSeason cold(&wx, day, WARM, 0.0, -70.0, h);
        warm_bed.season = &cold;
        warm_bed.run_near(ws, clock_at(0, 13.0, day)); // 冷下来之前领了的活照样干完
        growing.clear();
        const std::vector<int> cells_cold = warm_bed.farm.plot_cells();
        for (size_t k = 0; k + 1 < cells_cold.size(); k += 2) {
            const Plot p = warm_bed.farm.plot_at(cells_cold[k], cells_cold[k + 1]);
            if (p.state == PlotState::GROWING) {
                growing.push_back({ (uint64_t(uint32_t(cells_cold[k])) << 32) | uint32_t(cells_cold[k + 1]), p.growth });
            }
        }
        const int sown = ws.tally.sown;
        warm_bed.run_near(ws, clock_at(0, 15.0, day));
        bool frozen = true;
        for (const auto &g : growing) {
            const Plot p = warm_bed.farm.plot_at(int(int32_t(uint32_t(g.first >> 32))), int(int32_t(uint32_t(g.first & 0xFFFFFFFFu))));
            frozen = frozen && p.growth == g.second;
        }
        check(sown > 0 && !growing.empty() && ws.tally.sown == sown && frozen,
                "暖天播了 " + std::to_string(sown) + " 格；冷下来（−25 °C）两个钟头不再播、地里 " + std::to_string(growing.size()) + " 格一点不长");
        SimBed cold_bed;
        cold_bed.season = &cold;
        cold_bed.wx = &wx;
        cold_bed.wx_offset = WARM;
        VillageState cs;
        cold_bed.found(cs, true, clock_at(0, 5.5, day));
        cold_bed.run_near(cs, clock_at(0, 11.0, day));
        check(cs.tally.sown == 0 && cold_bed.farm.plot_cells().empty() && count_outdoors(cs) > 0, "一开始就冷：农夫不开荒不播种，照样出门（挑水、串门）");
        wx.edit_override() = WeatherOverride();
    }
    // 远近同源（有时令）：一年里挑六天各跑一整天，加起来比（单独一天随机差得多一点，与没有时令时一样）
    {
        VillageTally a, b;
        for (double off : { 178.0, 188.0, 198.0, 208.0, 82.0, 132.0 }) { // 原来按南半球的 #2051 挑的六天挪半年
            const LocalSeason season(&wx, day, off, 0.0, -70.0, h);
            SimBed near_bed, far_bed;
            near_bed.season = far_bed.season = &season;
            near_bed.wx = &wx;
            near_bed.wx_offset = off;
            VillageState ns, fs;
            near_bed.found(ns, true, clock_at(0, 4.5, day));
            far_bed.found(fs, false, clock_at(0, 4.5, day));
            near_bed.run_near(ns, clock_at(1, 4.5, day));
            far_bed.run_far(fs, clock_at(1, 4.5, day));
            a.harvested += ns.tally.harvested;
            a.sown += ns.tally.sown;
            a.eaten += ns.tally.eaten;
            a.water_in += ns.tally.water_in;
            b.harvested += fs.tally.harvested;
            b.sown += fs.tally.sown;
            b.eaten += fs.tally.eaten;
            b.water_in += fs.tally.water_in;
        }
        std::printf("  有时令的六天：近处 收 %d 播 %d 吃 %d 挑水 %.1f；远处 收 %d 播 %d 吃 %d 挑水 %.1f\n", a.harvested, a.sown, a.eaten, a.water_in, b.harvested, b.sown, b.eaten,
                b.water_in);
        auto within = [](double p_a, double p_b, double p_rel, double p_abs) { return std::abs(p_a - p_b) <= std::max(p_abs, p_rel * std::max(std::abs(p_a), std::abs(p_b))); };
        check(a.harvested > 0 && within(a.harvested, b.harvested, 0.2, 4) && within(a.sown, b.sown, 0.2, 3) && within(a.water_in, b.water_in, 0.2, 3) && a.eaten == b.eaten,
                "有时令：远处结算与逐帧差不出两成、吃饭一样");
    }
}

// 行星与岛群的坐标（planet_frame）：AEQD 往返、大圆距离、下弯量、δ 的展开式、相机局部的弯曲（双精度 / 与着色器同式的单精度）、换锚
void test_planet_frame() {
    std::printf("[planet_frame]\n");
    const PlanetShape shape;
    const double R = shape.radius();
    check_near(R, 6377800.0, 1e-6, "R = 海面 6371 km + 6.8 km（D13，浮高计划 F4）");
    // 经纬度 ↔ 单位向量（同 sphere.py：x 指 (0,0)、y 指 (0,90)、z 指北极）
    {
        const PlanetVec u = planet_unit(0.0, 90.0);
        check(std::abs(u.y - 1.0) < 1e-12 && std::abs(planet_unit(90.0, 0.0).z - 1.0) < 1e-12, "planet_unit 的轴同生成器");
        double la = 0.0, lo = 0.0;
        planet_latlon(planet_unit(-31.65, 152.15), la, lo);
        check_near(la, -31.65, 1e-9, "经纬度往返：纬度");
        check_near(lo, 152.15, 1e-9, "经纬度往返：经度");
    }
    // 开局那群 #6329（北纬 34.23°、东经 27.77°）→ 邻群 #6327（北纬 34.51°、东经 26.91°）：大圆 84 km（生成器 s03 算的数）
    const double d_ab = great_circle_m(34.2321, 27.7650, 34.51, 26.91, 6371000.0);
    check(std::abs(d_ab - 84400.0) < 1500.0, "#6329 与 #6327 相距约 84 km（得到 " + std::to_string(d_ab) + "）");
    const GroupFrame a(34.2321, 27.7650, shape), b(34.51, 26.91, shape);
    // AEQD：往返、离群心的距离与方位不变
    {
        double worst = 0.0;
        for (int k = 0; k < 64; k++) {
            const double ang = k * 0.37, dist = 1000.0 + 2300.0 * k;
            const PlanetVec p(dist * std::cos(ang), 350.0 + 20.0 * k, dist * std::sin(ang));
            const PlanetVec q = a.to_flat(a.to_planet(p));
            worst = std::max(worst, (q - p).length());
        }
        std::printf("  AEQD 往返最大误差 %.6f mm\n", worst * 1000.0);
        check(worst < 1e-3, "AEQD 往返 150 km 内误差 ≤ 1 mm（得到 " + std::to_string(worst * 1000.0) + " mm）");
        const PlanetVec p(60000.0, 0.0, -80000.0); // 东 60 km、北 80 km
        double la = 0.0, lo = 0.0;
        a.flat_latlon(p, la, lo);
        check_near(great_circle_m(34.2321, 27.7650, la, lo, R), 100000.0, 1e-3, "AEQD：离群心的大圆距离 = 平面距离");
        check_near(bearing_deg(34.2321, 27.7650, la, lo), std::atan2(60.0, 80.0) * 180.0 / 3.14159265358979323846, 1e-6, "AEQD：方位不变");
        const PlanetVec back = a.latlon_flat(la, lo);
        check((back - p).length() < 1e-3, "经纬度 → 平面：回到原点");
    }
    // 下弯：100 km 外降 R(1 − cos(s/R)) ≈ 784 m
    {
        const PlanetVec t = a.tangent(PlanetVec(100000.0, 0.0, 0.0));
        check_near(t.y, -R * (1.0 - std::cos(100000.0 / R)), 1e-6, "切平面下弯 = R(1 − cos θ)");
        check_near(t.y, -784.1, 1.0, "100 km 处下弯约 784 m");
        double worst100 = 0.0, worst600 = 0.0;
        for (int k = 0; k < 40; k++) {
            const double ang = k * 0.61;
            for (double dist : { 100000.0, 600000.0 }) {
                const PlanetVec p(dist * std::cos(ang), 2500.0 - 150.0 * k, dist * std::sin(ang));
                const double e = (a.delta(p) - a.delta_series(p)).length();
                (dist < 2e5 ? worst100 : worst600) = std::max(dist < 2e5 ? worst100 : worst600, e);
            }
        }
        std::printf("  δ 展开式与精确式：100 km 内 %.4f mm、600 km 内 %.3f mm\n", worst100 * 1000.0, worst600 * 1000.0);
        check(worst100 < 1e-3 && worst600 < 1e-2, "δ 的展开式：100 km 内 ≤ 1 mm、600 km 内 ≤ 1 cm（得到 " + std::to_string(worst100 * 1000.0) + " / " + std::to_string(worst600 * 1000.0) + " mm）");
    }
    // 相机局部的弯曲：近处 visual(p) ≈ p；远处是真球面的相对位置；单精度版（着色器同式）够准
    {
        double near_worst = 0.0, f_near = 0.0, f_far = 0.0, dist_err = 0.0;
        for (int k = 0; k < 48; k++) {
            const double ang = k * 0.53;
            const PlanetVec cam(90000.0 * std::cos(ang) * (k % 3) / 2.0, 40.0 + 180.0 * (k % 5), 90000.0 * std::sin(ang) * (k % 4) / 3.0);
            const CurveView view = CurveView::at(a, cam);
            const CurveViewF vf = CurveViewF::from(view);
            for (int j = 0; j < 12; j++) {
                const double t = j * 0.9 + k;
                const double reach = j < 6 ? 8.0 : (j < 10 ? 60000.0 : 600000.0);
                const PlanetVec p = cam + PlanetVec(reach * std::cos(t), (j % 3 - 1) * 30.0, reach * std::sin(t));
                const PlanetVec v = view.visual(a, p);
                if (reach < 10.0) {
                    near_worst = std::max(near_worst, (v - p).length());
                }
                dist_err = std::max(dist_err, std::abs((v - cam).length() - (a.tangent(p) - a.tangent(cam)).length()));
                const float pf[3] = { float(p.x), float(p.y), float(p.z) };
                float vo[3];
                vf.visual(pf, vo);
                const double e = (PlanetVec(vo[0], vo[1], vo[2]) - v).length();
                (reach < 1e5 ? f_near : f_far) = std::max(reach < 1e5 ? f_near : f_far, e);
            }
        }
        std::printf("  相机 8 m 内 visual 与平面差 %.3f mm；单精度弯曲（着色器同式）±100 km 内 %.2f cm、600 km 处 %.3f m\n", near_worst * 1000.0, f_near * 100.0, f_far);
        check(near_worst < 2e-3, "相机 8 m 内 visual(p) 与平面位置差 ≤ 2 mm（得到 " + std::to_string(near_worst * 1000.0) + " mm）");
        check(dist_err < 1e-4, "visual 保持真球面上与相机的距离");
        check(f_near < 0.02, "单精度弯曲（着色器同式）：锚群 ±100 km 内 ≤ 2 cm（得到 " + std::to_string(f_near * 100.0) + " cm）");
        check(f_far < 1.0, "单精度弯曲：600 km 处 ≤ 1 m（得到 " + std::to_string(f_far) + " m）");
        const CurveView at0 = CurveView::at(a, PlanetVec(0.0, 100.0, 0.0));
        check_near(at0.visual(a, PlanetVec(100000.0, 100.0, 0.0)).y - 100.0, -784.1, 1.5, "相机在群心：100 km 外的地面在画面上低 784 m");
    }
    // 法线：平行移动的倾斜（着色器 planet_tilt）对得上该点的局部系；相机近处的法线不变；单精度版同式；画面 → 平面的逆
    {
        double tilt_err = 0.0, near_n = 0.0, f_err = 0.0, inv_err = 0.0, far_up = 0.0;
        for (int k = 0; k < 40; k++) {
            const double ang = k * 0.77, dist = 3000.0 * k;
            const PlanetVec p(dist * std::cos(ang), 100.0 + 40.0 * k, dist * std::sin(ang));
            const PlanetVec d = PlanetVec(std::sin(k * 1.3), 1.0 + 0.5 * std::cos(k * 0.4), std::cos(k * 2.1)).normalized();
            tilt_err = std::max(tilt_err, (planet_tilt(p, d, R) - a.local_basis(p).apply(d)).length());
            const CurveView view = CurveView::at(a, p);
            const PlanetVec q = p + PlanetVec(40.0 * std::cos(ang), -3.0, 40.0 * std::sin(ang));
            near_n = std::max(near_n, (view.visual_dir(q, d) - d).length());
            const CurveViewF vf = CurveViewF::from(view);
            const PlanetVec far = p + PlanetVec(150000.0 * std::cos(k * 0.3), 0.0, 150000.0 * std::sin(k * 0.3));
            const float pf[3] = { float(far.x), float(far.y), float(far.z) }, df[3] = { float(d.x), float(d.y), float(d.z) };
            float nf[3];
            vf.visual_normal(pf, df, nf);
            f_err = std::max(f_err, (PlanetVec(nf[0], nf[1], nf[2]) - view.visual_dir(far, d)).length());
            inv_err = std::max(inv_err, (view.flat_of(a, view.visual(a, far)) - far).length());
            // 150 km 外的地面朝上，在画面上往外倒 150/R 弧度（真球面）
            const PlanetVec up_far = view.visual_dir(far, PlanetVec(0.0, 1.0, 0.0));
            far_up = std::max(far_up, std::abs(std::acos(std::clamp(up_far.y, -1.0, 1.0)) - (far - p).length() / R));
        }
        std::printf("  法线的倾斜与局部系差 %.2e；相机 40 m 内法线变 %.2e；单精度同式 %.2e；画面 → 平面的逆 %.3f mm；150 km 外的朝上倒角差 %.2e rad\n", tilt_err, near_n, f_err, inv_err * 1000.0, far_up);
        check(tilt_err < 2e-4, "planet_tilt 与该点的局部系（AEQD 方位向形变）差 < 2e-4");
        check(near_n < 2e-4, "相机近处法线在画面上几乎不变（< 2e-4）");
        check(f_err < 1e-5, "单精度的法线弯曲与双精度差 < 1e-5");
        check(inv_err < 5e-3, "画面坐标 → 平面坐标（逆）150 km 外差 < 5 mm");
        check(far_up < 1e-3, "远处地面的朝上在画面上倒向外（按真球面）");
    }
    // 远处往相机压：10 km 内不动、往外单调、压到 26 km 以内、方向不变；单精度同式
    {
        bool mono = true;
        double prev = 0.0, f_err = 0.0;
        for (double d = 0.0; d < 2.0e6; d += 997.0) {
            const double s = CurveSqueeze::distance(d);
            mono = mono && s >= prev && s < CurveSqueeze::LIMIT_M;
            prev = s;
            const float cam[3] = { 1234.0f, 250.0f, -8000.0f };
            const float v[3] = { cam[0] + float(d * 0.6), cam[1] - float(d * 0.0), cam[2] + float(d * 0.8) };
            float o[3];
            planet_squeeze_f(v, cam, o);
            const double got = std::sqrt(double(o[0] - cam[0]) * (o[0] - cam[0]) + double(o[1] - cam[1]) * (o[1] - cam[1]) + double(o[2] - cam[2]) * (o[2] - cam[2]));
            f_err = std::max(f_err, std::abs(got - s) / std::max(1.0, s));
        }
        check(CurveSqueeze::distance(8000.0) == 8000.0 && CurveSqueeze::distance(CurveSqueeze::KEEP_M) == CurveSqueeze::KEEP_M, "离相机 10 km 以内不压");
        check(mono, "往外压得单调、不过 26 km（深度的先后不变）");
        check(std::abs(CurveSqueeze::distance(205000.0) - 24790.0) < 200.0, "205 km 外压到 25 km 上下");
        check(CurveSqueeze::distance(600000.0) - CurveSqueeze::distance(590000.0) > 0.1, "600 km 处相隔 10 km 压过之后还差十几厘米以上（单精度分得开）");
        check(f_err < 1e-5, "单精度的压法与双精度同式（相对差 " + std::to_string(f_err) + "）");
    }
    // 换锚：同一个真球面点换锚前后，相对相机的画面位置只差平面轴的转动 Q
    {
        const PlanetVec cam_a = a.latlon_flat(-32.1, 151.9) + PlanetVec(0.0, 2400.0, 0.0); // 两群之间的云海上空
        const CurveView va = CurveView::at(a, cam_a);
        const PlanetVec cam_b = rebase_point(a, b, cam_a);
        const CurveView vb = CurveView::at(b, cam_b);
        const PlanetBasis q = rebase_rotation(a, b, cam_a);
        double worst = 0.0;
        for (int k = 0; k < 24; k++) {
            const double t = k * 0.7, reach = k < 12 ? 300.0 : 90000.0;
            const PlanetVec pa = cam_a + PlanetVec(reach * std::cos(t), -800.0 + 60.0 * k, reach * std::sin(t));
            const PlanetVec pb = rebase_point(a, b, pa);
            const PlanetVec off_a = va.visual(a, pa) - cam_a, off_b = vb.visual(b, pb) - cam_b;
            worst = std::max(worst, (off_b - q.apply(off_a)).length());
        }
        std::printf("  换锚前后画面（相对相机）差 %.3f mm\n", worst * 1000.0);
        check(worst < 0.02, "换锚：画面（相对相机）只差平面轴的转动，≤ 2 cm（得到 " + std::to_string(worst * 1000.0) + " mm）");
        const PlanetVec yaw = q.apply(PlanetVec(0.0, 1.0, 0.0));
        check(std::abs(yaw.y - 1.0) < 1e-6, "换锚的转动只绕竖直轴（上还是上）");
        check((rebase_point(b, a, cam_b) - cam_a).length() < 1e-3, "换锚往返回到原处");
    }
    // 平面坐标规范化（god 高空绕行星转查出来的）：换到锚群自己 = 同一个真球面点在对跖点（πR）以内的写法；没过对跖点的不变；
    // 刚过对跖点 δ（游戏里每帧都查，δ 不过一帧走的几公里）换成离群心 πR − δ、方位反过来：转动近乎不转（朝向、视线不跳），离群心的方向走一步在真空间里一样；
    // 整整一圈（数学上；游戏里到不了）又回到群心附近。对跖点附近平面在真球面上横着压扁（sinθ/θ）、过了对跖点还是镜像的，横着走一步没有对应的转动，只比离群心的方向
    {
        const double half = 3.14159265358979323846 * a.radius;
        const PlanetVec near_p(3.0e6, 5.0e5, -4.0e6);
        check((rebase_point(a, a, near_p) - near_p).length() < 1e-3, "规范化：对跖点以内的点不变（5,000 km 处差 < 1 mm）");
        double pos_err = 0.0, radial_err = 0.0, yaw_err = 0.0, len_err = 0.0;
        const auto angle = [](const PlanetVec &p_u, const PlanetVec &p_v) {
            return std::atan2(p_u.cross(p_v).length(), p_u.dot(p_v));
        };
        for (int k = 0; k < 8; k++) {
            const double az = 0.3 + k * 0.785, e = std::sin(az), n = std::cos(az);
            for (double s : { half + 1000.0, half + 5000.0, 2.0 * half + 30000.0 }) {
                const PlanetVec p(e * s, 5.0e5, -n * s);
                const PlanetVec q = rebase_point(a, a, p);
                const double sq = std::hypot(q.x, q.z);
                const double want = std::abs(std::remainder(s, 2.0 * half));
                len_err = std::max(len_err, std::abs(sq - want));
                pos_err = std::max(pos_err, (a.to_planet(q) - a.to_planet(p)).length());
                check(sq <= half + 1.0 && std::abs(q.y - p.y) < 1e-3, "规范化：换过去在对跖点以内、高度不变");
                if (s > 2.0 * half) {
                    continue;
                }
                const PlanetBasis r = rebase_rotation(a, a, p);
                yaw_err = std::max(yaw_err, std::abs(std::atan2(-r.cols[0].z, r.cols[0].x)));
                const PlanetVec out(e, 0.0, -n);
                const auto step = [&](const PlanetVec &p_from, const PlanetVec &p_dir) { return a.to_planet(p_from + p_dir) - a.to_planet(p_from); };
                radial_err = std::max(radial_err, angle(step(p, out), step(q, r.apply(out))));
            }
        }
        std::printf("  规范化：离群心差 %.3f m、真球面位置差 %.3f mm；刚过对跖点换过去转了 %.4f°、离群心走一步的方向差 %.4f°\n", len_err, pos_err * 1000.0,
                yaw_err * 57.29578, radial_err * 57.29578);
        check(len_err < 1.0, "规范化：离群心 = 走过的大圆距离对一圈取余（差 < 1 m）");
        check(pos_err < 0.01, "规范化：真球面上是同一点（差 < 1 cm）");
        check(yaw_err < 0.5 / 57.29578, "规范化：刚过对跖点换过去，朝向几乎不转（< 0.5°，视线不跳）");
        check(radial_err < 1e-3, "规范化：离群心的方向走一步在真空间里一样（差 < 0.06°）");
    }
}

// 离开的岛群的账存成文本（行星计划 P4：进世界存档目录，退出再进还在）：村的账（多行、带制表符）、农田的地、采场存量、散布物的采空账，读回逐位一样
void test_group_memory() {
    std::printf("[group_memory]\n");
    GroupMemory m;
    m.ledgers[0] = "clock\t123.5\nfolk 3\n\n末行没有换行";
    m.ledgers[17] = "x\n";
    m.ledgers[250] = "";
    m.ledgers_at = 86400.125;
    m.has_world = true;
    m.plots_at = 90000.0 / 7.0;
    Plot tilled;
    tilled.state = PlotState::TILLED;
    Plot growing;
    growing.state = PlotState::GROWING;
    growing.growth = 1.0 / 3.0;
    growing.crop = &catalog::CROPS[0];
    m.plots.emplace_back((uint64_t(uint32_t(-5)) << 32) | 7u, tilled);
    m.plots.emplace_back(uint64_t(42) << 32, growing);
    m.stocks.resize(3);
    m.stocks[1].amount = 2.75;
    m.stocks[1].hour = 1234.5;
    ResourceStock felled;
    felled.amount = 0.0;
    felled.hour = 99.0 + 1e-9;
    m.scatter.put(0xABCDEF0123456789ull, felled, true);
    m.scatter.put(5, ResourceStock{ 3.0, 7.0 }, false);
    const std::string text = save_group_memory(m);
    GroupMemory r;
    std::string err;
    check(load_group_memory(text, r, err), "读得回来：" + err);
    check(r.ledgers == m.ledgers && r.ledgers_at == m.ledgers_at, "各村的账（多行、带制表符、空的）与存下时的钟逐字一样");
    bool plots_same = r.plots.size() == m.plots.size() && r.plots_at == m.plots_at;
    for (size_t k = 0; plots_same && k < m.plots.size(); k++) {
        plots_same = r.plots[k].first == m.plots[k].first && r.plots[k].second.state == m.plots[k].second.state &&
                r.plots[k].second.growth == m.plots[k].second.growth && r.plots[k].second.crop == m.plots[k].second.crop;
    }
    check(plots_same, "农田的地（键、状态、长势、作物）逐位一样");
    check(r.stocks.size() == 3 && r.stocks[1].amount == 2.75 && r.stocks[1].hour == 1234.5 && r.stocks[0].amount == -1.0, "采场的存量逐位一样");
    const ResourceStock *f = r.scatter.find(0xABCDEF0123456789ull);
    check(r.scatter.size() == 2 && f && f->hour == felled.hour && f->amount == 0.0, "散布物的采空账逐位一样");
    check(r.has_world && !r.empty(), "近景那一份在");
    GroupMemory only_ledgers;
    only_ledgers.ledgers[3] = "a";
    GroupMemory back;
    check(load_group_memory(save_group_memory(only_ledgers), back, err) && !back.has_world && back.ledgers.size() == 1, "只有村的账（细版 → 近景）也读得回来");
    check(GroupMemory().empty() && !load_group_memory("bogus\t1\n", back, err), "空的账是空的；认不出的行报错");
}

// 按需生成的粗版（行星计划 P4）：行星包先没有粗版（新建世界的样子），再一群一群 add_lods 加进来（lods/<节点>.bin，同格式、一条）；
// 加进来的与 lods.bin 里那一条解出来一样，同一格距再加一次换成新的；后台照样在读（几条线程一直 lod()）时加也不出错
void test_planet_add_lods() {
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42";
    std::printf("[planet_add_lods]\n");
    PlanetData ref;
    std::string err;
    if (!ref.load_files(dir, err)) {
        check(false, "行星包读得出：" + err);
        return;
    }
    std::ifstream pin(dir + "/planet.bin", std::ios::binary);
    const std::vector<uint8_t> planet_bytes((std::istreambuf_iterator<char>(pin)), std::istreambuf_iterator<char>());
    std::ifstream lin(dir + "/lods.bin", std::ios::binary);
    const std::vector<uint8_t> lods((std::istreambuf_iterator<char>(lin)), std::istreambuf_iterator<char>());
    // 从 lods.bin 里抠出一群一份的条目，拼成只有一条的 lods.bin
    auto single = [&](int p_node, float p_res) {
        uint32_t n = 0;
        std::memcpy(&n, lods.data() + 8, 4);
        for (uint32_t k = 0; k < n; k++) {
            const uint8_t *e = lods.data() + 12 + size_t(k) * 36;
            uint32_t node = 0;
            float res = 0.0f;
            std::memcpy(&node, e, 4);
            std::memcpy(&res, e + 4, 4);
            if (int(node) != p_node || res != p_res) {
                continue;
            }
            uint64_t off = 0;
            uint32_t bytes = 0;
            std::memcpy(&off, e + 24, 8);
            std::memcpy(&bytes, e + 32, 4);
            std::vector<uint8_t> out(lods.begin(), lods.begin() + 8);
            const uint32_t one = 1;
            out.insert(out.end(), reinterpret_cast<const uint8_t *>(&one), reinterpret_cast<const uint8_t *>(&one) + 4);
            out.insert(out.end(), e, e + 36);
            const uint64_t new_off = 12 + 36;
            std::memcpy(out.data() + 12 + 24, &new_off, 8);
            out.insert(out.end(), lods.begin() + std::ptrdiff_t(off), lods.begin() + std::ptrdiff_t(off + bytes));
            return out;
        }
        return std::vector<uint8_t>();
    };
    PlanetData pd;
    check(pd.parse_planet(planet_bytes.data(), planet_bytes.size(), err), "行星包读得出");
    const uint8_t empty[12] = { 'Z', 'Z', 'L', 'D', 1, 0, 0, 0, 0, 0, 0, 0 };
    check(pd.parse_lods(empty, sizeof(empty), err), "0 条的 lods.bin 读得出（新建世界时还没有粗版）");
    check(!pd.has_lod(NEIGHBOR) && pd.lod(NEIGHBOR).empty() && pd.lod_count() == 0, "没有粗版：has_lod 为假、lod() 空");
    const uint64_t gen0 = pd.lod_generation();
    const std::vector<uint8_t> b2050 = single(NEIGHBOR, 2000.0f); // 邻群的 2 km 粗版
    std::vector<int> added;
    check(!b2050.empty() && pd.add_lods(b2050.data(), b2050.size(), err, &added) && added.size() == 1 && added[0] == NEIGHBOR, "加进邻群的 2 km 粗版：" + err);
    check(pd.has_lod(NEIGHBOR) && pd.lod_generation() == gen0 + 1 && pd.lod_count() == 1, "加了以后有了、代数加一");
    const PlanetLod a = pd.lod(NEIGHBOR), b = ref.lod(NEIGHBOR, 2000.0);
    check(a.rows == b.rows && a.cols == b.cols && a.height == b.height && a.land == b.land && a.landcover == b.landcover && a.island == b.island &&
                    a.islands.size() == b.islands.size() && a.res_m == 2000.0,
            "加进来的粗版与 lods.bin 里那一条解出来一样");
    // 同一格距再加一次：换掉，不重复
    check(pd.add_lods(b2050.data(), b2050.size(), err) && pd.lod_resolutions(NEIGHBOR).size() == 1, "同一格距再加一次换成新的（仍是一份）");
    // 群号不在行星包里：不收
    std::vector<uint8_t> bad = b2050;
    const uint32_t bogus = 999999;
    std::memcpy(bad.data() + 12, &bogus, 4);
    check(!pd.add_lods(bad.data(), bad.size(), err), "群号不在行星包里的粗版不收");
    // 几条线程一直在读，主线程接着加别的群
    std::atomic<bool> stop{ false };
    std::atomic<int> reads{ 0 }, wrong{ 0 };
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; t++) {
        readers.emplace_back([&]() {
            while (!stop.load()) {
                const PlanetLod l = pd.lod(NEIGHBOR);
                if (l.rows != b.rows || l.land != b.land) {
                    wrong++;
                }
                reads++;
            }
        });
    }
    const int more[] = { START, 6331, 6328, 6609, 6330, 1303, 518, 3000 }; // 开局那群、它周围几群与远处几群
    int ok = 0;
    for (int node : more) {
        const std::vector<uint8_t> bytes = single(node, 2000.0f);
        ok += !bytes.empty() && pd.add_lods(bytes.data(), bytes.size(), err) ? 1 : 0;
    }
    while (reads.load() < 200) {
        std::this_thread::yield();
    }
    stop = true;
    for (std::thread &t : readers) {
        t.join();
    }
    check(ok == 8 && wrong.load() == 0, "边读边加：8 群都加上、读的 " + std::to_string(reads.load()) + " 回都对");
    check(pd.lod(6328).land == ref.lod(6328, 2000.0).land && pd.nodes_within(pd.node(START).lat, pd.node(START).lon, 300000.0).size() >= 5, "后加的 #6328 也对");
}

// 行星包（planet_data）：game/planet/seed42 的 planet.bin / lods.bin 读得出、数对得上生成器、开局那群的粗版与仓库里的细版是同一个群
void test_planet_data() {
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42";
    std::printf("[planet_data]\n");
    PlanetData pd;
    std::string err;
    const bool loaded = pd.load_files(dir, err); // 先读：实参的求值次序不定，拼提示时 err 可能还是空的
    check(loaded, "行星包读得出：" + err);
    if (pd.node_count() == 0) {
        return;
    }
    check(pd.seed == 42 && pd.node_count() == 8000, "seed 42、8000 个岛群");
    check_near(pd.radius_km, 6371.0, 1e-6, "行星半径");
    check_near(pd.tilt_deg, 34.0, 1e-6, "黄赤交角");
    check(pd.calendar.year_days == 336 && pd.calendar.seasons == 4 && pd.calendar.months_per_season == 3 && pd.calendar.days_per_month == 28,
            "历法 336 天、4 季 × 3 月 × 28 天");
    check_near(pd.calendar.solstice_n, 126.0, 1e-6, "北半球夏至第 126 天（与 weather.tsv 同）");
    double land = 0.0;
    for (int k = 0; k < pd.node_count(); k++) {
        land += pd.node(k).area_km2;
    }
    check(std::abs(land - 2.5e7) < 2.5e4, "全行星陆地 2500 万 km²（得到 " + std::to_string(land) + "）");
    const PlanetNode &n51 = pd.node(START);
    check(std::abs(n51.lat - 34.23) < 0.01 && std::abs(n51.lon - 27.77) < 0.01, "开局那群在北纬 34.23°、东经 27.77°");
    check(std::abs(n51.area_km2 - 5141.0) < 2.0 && n51.cls == 1 && n51.pop > 0.0, "开局那群：陆地 5141 km²、中疏、有人口");
    check(pd.nearest_node(n51.lat, n51.lon) == START, "离开局那群的群心最近的是它自己");
    check(pd.nearest_node(n51.lat + 0.22, n51.lon - 0.72) == NEIGHBOR, "往西北 70 km 最近的是邻群 #6327");
    check_near(pd.node_distance_m(START, NEIGHBOR), 84400.0, 1500.0, "开局那群与邻群相距 84 km");
    const std::vector<int> near90 = pd.nodes_within(n51.lat, n51.lon, 90000.0);
    check(near90.size() == 2 && near90[0] == START && near90[1] == NEIGHBOR, "90 km 内只有开局那群与邻群（#6331 在 113 km）");
    check(pd.nodes_within(n51.lat, n51.lon, 500000.0).size() == 51, "500 km 内 51 群（含自己）");
    check(pd.nodes_within(-89.0, 0.0, 800000.0).size() > 0 && pd.nearest_node(0.0, 0.0) >= 0, "极点、赤道也找得到群");
    // 每群都有粗版
    int missing = 0;
    for (int k = 0; k < pd.node_count(); k++) {
        missing += pd.lod_resolutions(k).empty() ? 1 : 0;
    }
    check(missing == 0, "每个岛群都有粗版（没有的 " + std::to_string(missing) + "）");
    const std::vector<double> r51 = pd.lod_resolutions(START);
    check(r51.size() == 2 && r51.front() == 2000.0 && r51.back() == 500.0, "开局那群有 2 km 与 500 m 两份粗版（开局周围 320 km 那一圈有 500 m 的）");
    int rings = 0;
    for (int k = 0; k < pd.node_count(); k++) {
        rings += pd.lod_resolutions(k).size() == 2 ? 1 : 0;
    }
    check(rings == 25 && pd.lod_resolutions(6332).size() == 2 && pd.lod_resolutions(6607).size() == 1, "500 m 的一圈：开局 320 km 内 25 群（#6332 在 268 km 有、#6607 在 322 km 没有）");
    // 开局那群的粗版：陆地面积、岛数与细版对得上
    const PlanetLod fine = pd.lod(START, 600.0), coarse = pd.lod(START);
    check(fine.res_m == 500.0 && coarse.res_m == 2000.0, "lod() 按格距挑：≤ 600 m 给 500 m，不限给最粗的");
    check(fine.islands.size() == 38 && std::abs(fine.land_km2() - 5141.0) < 0.03 * 5141.0, "500 m 粗版：38 岛、陆地差不到 3%（" + std::to_string(fine.land_km2()) + " km²）");
    check(std::abs(coarse.land_km2() - 5141.0) < 0.05 * 5141.0, "2 km 粗版：陆地差不到 5%");
    // 与仓库里开局那群的细版（terrain.bin）是同一个群：粗版里的实地格，细版在那儿也是陆地
    std::ifstream in(start_dir() + "/terrain.bin", std::ios::binary);
    const std::vector<uint8_t> bin((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    WorldGrid g;
    if (parse_world_grid(bin.data(), bin.size(), g, err)) {
        int solid = 0, agree = 0;
        double peak_err = 0.0;
        for (int r = 0; r < fine.rows; r++) {
            for (int c = 0; c < fine.cols; c++) {
                if (fine.land[fine.index(r, c)] < 250) {
                    continue;
                }
                double x = 0.0, z = 0.0;
                fine.cell_center(r, c, x, z);
                solid++;
                agree += g.shore_at(x, z) > 0.0 ? 1 : 0;
                peak_err = std::max(peak_err, g.height_at(x, z) - fine.height[fine.index(r, c)]);
            }
        }
        check(solid > 8000 && agree >= solid * 0.99, "粗版的实地格在细版里 99% 以上也是陆地（" + std::to_string(agree) + " / " + std::to_string(solid) + "）");
        check(peak_err < 20.0, "粗版的高是块内最高点：细版格心（双三次插值会冲过一点）不高出它 20 m（多出 " + std::to_string(peak_err) + " m）");
    }
    // 1° 场
    const PlanetGrid *u = pd.grid("s04.wind_local.u");
    const PlanetGrid *edges = pd.grid("s04.band_local.edges");
    const PlanetGrid *precip = pd.grid("s04.climate_grid.precip");
    check(u && u->rows == 180 && u->cols == 360 && std::abs(u->lat0 + 89.5) < 1e-6, "局地风 1° 网格、第 0 行在南");
    check(edges && edges->rows == 8 && edges->cols == 360, "带界 8 × 360");
    check(precip && std::isfinite(precip->sample(n51.lat, n51.lon)) && precip->sample(n51.lat, n51.lon) > 0.0, "气候场在开局那群取得到降水");
    check(u && std::abs(u->sample(10.0, 179.9) - u->sample(10.0, -180.1)) < 1e-4, "经度绕回：180° 两边一样");
    check(pd.column("s03.plate") && pd.grid("s03.plates.plate_id"), "第三层要的板块也在包里（P6 用）");
}

// 远处岛群的低模（far_group_mesh）：#6611 群（开局那群东北 170 km）的 500 m / 2 km 粗版建出顶面、崖壁、岛底；朝向对、按真球面弯过、岛体同游戏里的式子
void test_far_group_mesh() {
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42";
    std::printf("[far_group_mesh]\n");
    PlanetData pd;
    std::string err;
    if (!pd.load_files(dir, err)) {
        check(false, "行星包读得出：" + err);
        return;
    }
    const double R = PlanetShape().radius();
    constexpr int FAR = 6611; // 开局周围 500 m 那一圈里主岛大的一群（原来用 2051 旁边的 2049）
    for (double res : { 600.0, 1e9 }) {
        const PlanetLod lod = pd.lod(FAR, res);
        const auto t0 = std::chrono::steady_clock::now();
        const FarGroupMesh m = build_far_group_mesh(lod, R);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        int land = 0;
        for (uint8_t v : lod.land) {
            land += v >= 128 ? 1 : 0;
        }
        std::printf("  #6611 %.0f m：%d 实地格、%zu 顶点、%zu 三角面，%.1f ms；包围盒 y %.0f…%.0f，最高 %.0f m\n", lod.res_m, m.solid_cells, m.vertex_count(), m.indices.size() / 3, ms,
                m.aabb_min[1], m.aabb_max[1], m.peak_y);
        check(!m.empty() && m.solid_cells == land, "每个实地格都进了网格（" + std::to_string(m.solid_cells) + " / " + std::to_string(land) + "）");
        check(m.positions.size() == m.normals.size() && m.uvs.size() / 2 == m.vertex_count() && m.season.size() / 4 == m.vertex_count() && m.colors.size() / 4 == m.vertex_count(),
                "顶点的各样数组一样长");
        check(std::abs(m.peak_y - pd.node(FAR).peak_m) < 0.25 * pd.node(FAR).peak_m, "最高的角接近主峰（" + std::to_string(m.peak_y) + " m）");
        // 朝向：三角面的面法线（Godot：(a − c) × (a − b)）与顶点法线同向
        int agree = 0, tris = int(m.indices.size() / 3), down = 0, up = 0;
        for (int t = 0; t < tris; t++) {
            const int ia = m.indices[size_t(t) * 3], ib = m.indices[size_t(t) * 3 + 1], ic = m.indices[size_t(t) * 3 + 2];
            auto P = [&](int i) { return PlanetVec(m.positions[size_t(i) * 3], m.positions[size_t(i) * 3 + 1], m.positions[size_t(i) * 3 + 2]); };
            const PlanetVec fn = (P(ia) - P(ic)).cross(P(ia) - P(ib));
            const PlanetVec vn(m.normals[size_t(ia) * 3], m.normals[size_t(ia) * 3 + 1], m.normals[size_t(ia) * 3 + 2]);
            agree += fn.dot(vn) > 0.0 ? 1 : 0;
            up += vn.y > 0.5 ? 1 : 0;
            down += vn.y < -0.5 ? 1 : 0;
        }
        check(agree >= tris * 0.99, "三角面朝外（面法线与顶点法线同向 " + std::to_string(agree) + " / " + std::to_string(tris) + "）");
        check(up > 0 && down > 0 && std::abs(up - down) < tris / 5, "顶面朝上、岛底朝下，片数相当");
        // 岛体：岛底垂得很深（#6611 主岛 3482 km²、最深岸距二十来公里：龙骨 25·√R ≈ 3.5–4 km），但不低于云带顶之上 1000 m（浮高计划 F3）
        double deep = 0.0;
        for (size_t v = 0; v < m.vertex_count(); v++) {
            deep = std::min(deep, double(m.uvs[v * 2]));
        }
        check(deep < -2500.0 && deep >= VerticalDatum::ISLAND_BOTTOM_MIN_Y - 1e-3, "岛底倒锥最深 " + std::to_string(deep) + " m（−2500 以下、岛底最低 −4000 以上）");
        // 按真球面弯过：离群心最远的顶点比它的海拔低 s²/2R
        double worst = 0.0;
        for (size_t v = 0; v < m.vertex_count(); v++) {
            const double y = m.uvs[v * 2];
            const double tx = m.positions[v * 3], ty = m.positions[v * 3 + 1], tz = m.positions[v * 3 + 2];
            const double s = std::sqrt(tx * tx + tz * tz);
            worst = std::max(worst, std::abs((ty - y) + s * s / (2.0 * R)) - 0.02 * s * s / (2.0 * R));
        }
        check(worst < 5.0, "顶点按真球面下弯（离群心 s 处低 s²/2R，差 " + std::to_string(worst) + " m）");
    }
}

// 行星计划 P2 · 大气（planet_sky）：透过率表的参数化往返、查表与直接积分对得上、地平线以下给 0、多次散射表有限且随太阳变亮；
// 行星系里的日月与 almanac 在当地算的方向一致
void test_planet_sky() {
    std::printf("[planet_sky]\n");
    const AtmosphereModel atmo;
    double worst_rt = 0.0;
    for (int k = 0; k < 200; k++) {
        const double r = atmo.bottom_km + 0.1 + (atmo.top_km - atmo.bottom_km - 0.2) * (k % 20) / 19.0;
        const double horizon = -std::sqrt(std::max(0.0, 1.0 - (atmo.bottom_km / r) * (atmo.bottom_km / r)));
        const double mu = horizon + 1e-4 + (1.0 - horizon - 1e-4) * (k / 20) / 9.0;
        double u = 0.0, v = 0.0, r2 = 0.0, mu2 = 0.0;
        atmo_transmittance_uv(atmo, r, mu, u, v);
        atmo_transmittance_r_mu(atmo, u, v, r2, mu2);
        worst_rt = std::max({ worst_rt, std::abs(r2 - r), std::abs(mu2 - mu) });
    }
    check(worst_rt < 1e-6, "透过率表的 (r, mu) ↔ (u, v) 往返差 < 1e-6（得到 " + std::to_string(worst_rt) + "）");
    const auto t0 = std::chrono::steady_clock::now();
    const SkyLut trans = atmo_transmittance_lut(atmo);
    const auto t1 = std::chrono::steady_clock::now();
    const SkyLut ms = atmo_multiscatter_lut(atmo, trans);
    const auto t2 = std::chrono::steady_clock::now();
    std::printf("  透过率表 %d×%d %.1f ms、多次散射表 %d×%d %.1f ms\n", trans.width, trans.height, std::chrono::duration<double, std::milli>(t1 - t0).count(), ms.width,
            ms.height, std::chrono::duration<double, std::milli>(t2 - t1).count());
    double lut[3], direct[3];
    atmo_transmittance(atmo, trans, atmo.bottom_km, 1.0, lut);
    atmo_transmittance_direct(atmo, atmo.bottom_km, 1.0, 200, direct);
    std::printf("  海面朝天顶的透过率 %.4f %.4f %.4f\n", lut[0], lut[1], lut[2]);
    check(std::abs(lut[0] - direct[0]) < 3e-3 && std::abs(lut[2] - direct[2]) < 3e-3, "查表与直接积分对得上（天顶）");
    check(lut[0] > 0.9 && lut[0] < 0.97 && lut[2] > 0.7 && lut[2] < 0.8 && lut[2] < lut[1] && lut[1] < lut[0], "天顶透过率：红 0.9–0.97、蓝 0.7–0.8，蓝最少（Rayleigh）");
    double worst_lut = 0.0;
    for (int k = 0; k < 40; k++) {
        const double r = atmo.bottom_km + 6.8 + k * 2.3; // 从游戏 y = 0（海面之上 6.8 km，D13）往上
        const double mu = 1.0 - k * 0.024;
        atmo_transmittance(atmo, trans, r, mu, lut);
        atmo_transmittance_direct(atmo, r, mu, 200, direct);
        worst_lut = std::max(worst_lut, std::abs(lut[1] - direct[1]));
    }
    check(worst_lut < 0.01, "高处各方向查表与直接积分差 < 0.01（得到 " + std::to_string(worst_lut) + "）");
    atmo_transmittance(atmo, trans, atmo.bottom_km + 6.8, -0.2, lut);
    check(lut[0] == 0.0 && lut[1] == 0.0, "地平线以下（打到海面）透过率 0");
    atmo_transmittance(atmo, trans, atmo.bottom_km + 400.0, 0.2, lut);
    check(lut[0] > 0.99, "400 km 高往上几乎全透");
    bool finite = true;
    for (float f : ms.rgb) {
        finite = finite && std::isfinite(f) && f >= 0.0f;
    }
    double noon[3], dusk[3], night[3], high[3];
    ms.sample(1.0, 0.068, noon);
    ms.sample(0.5, 0.068, dusk);
    ms.sample(0.2, 0.068, night);
    ms.sample(1.0, 0.9, high);
    std::printf("  多次散射（6.8 km）：正午 %.5f %.5f %.5f、日落 %.5f、夜 %.6f、90 km 正午 %.6f\n", noon[0], noon[1], noon[2], dusk[2], night[2], high[2]);
    check(finite, "多次散射表处处有限、不为负");
    check(noon[2] > dusk[2] && dusk[2] > night[2] && noon[2] > noon[0], "多次散射：太阳越高越亮，蓝多于红");
    // Ψ_ms 是照到那一点的多次散射光（大半是下面的云带与低层大气反上来的），90 km 处比近地面暗；乘上那里的散射系数就几乎没有了
    check(high[2] > 0.0 && high[2] < noon[2], "90 km 处照到的多次散射比近地面暗");

    // 行星系里的日月：换到当地的东、上、南，与 almanac 在那一点算的方向一致
    CalendarRules rules;
    const double clock_lon = 152.15;
    double worst_sun = 0.0, worst_moon = 0.0;
    for (int k = 0; k < 30; k++) {
        const double day = 3.0 + k * 11.37;
        const double lat = -80.0 + k * 5.3, lon = -170.0 + k * 11.0;
        double s[3], m[3];
        planet_sun_dir(day, clock_lon, rules, s);
        planet_moon_dir(day, clock_lon, rules, m);
        const GroupFrame f(lat, lon);
        const PlanetVec sv(s[0], s[1], s[2]), mv(m[0], m[1], m[2]);
        CalendarRules here = rules;
        here.latitude_deg = lat;
        const double local_day = day + (lon - clock_lon) / 360.0;
        const SunState st = sun_at(local_day, here);
        const double e = sv.dot(f.east), u = sv.dot(f.up), so = -sv.dot(f.north);
        worst_sun = std::max(worst_sun, std::abs(e - st.dir_x) + std::abs(u - st.dir_y) + std::abs(so - st.dir_z));
        CalendarRules at = rules;
        at.latitude_deg = -31.65;
        const GroupFrame g(-31.65, clock_lon);
        const MoonState mo = moon_at(day, at);
        worst_moon = std::max(worst_moon, std::abs(mv.dot(g.east) - mo.dir_x) + std::abs(mv.dot(g.up) - mo.dir_y) + std::abs(-mv.dot(g.north) - mo.dir_z));
    }
    // almanac 在当地按「当地历日」算赤纬（经度差最多半天，赤纬差不到 0.6°）；行星系按钟的历日：三个分量的绝对差加起来 < 0.03
    check(worst_sun < 0.03, "行星系的太阳换到各地，与 almanac 当地算的差 < 0.03（得到 " + std::to_string(worst_sun) + "）");
    check(worst_moon < 1e-6, "参考经度上行星系的月亮与 almanac 一致（得到 " + std::to_string(worst_moon) + "）");
}

// 行星计划 P2 · 远处往相机压随相机的高放大：30 km 以下同 P1；两万公里高时看得见的都不压；单精度同式
void test_planet_squeeze_height() {
    std::printf("[planet_squeeze_height]\n");
    double keep = 0.0, limit = 0.0;
    CurveSqueeze::at_height(2.0, keep, limit);
    check(keep == CurveSqueeze::KEEP_M && limit == CurveSqueeze::LIMIT_M, "地面上同 P1（10 km / 26 km）");
    CurveSqueeze::at_height(30000.0, keep, limit);
    check(keep == CurveSqueeze::KEEP_M, "30 km 以下不变");
    CurveSqueeze::at_height(2.0e7, keep, limit);
    check(keep > 2.6e7 && std::abs(limit / keep - 2.6) < 1e-9, "两万公里高：不压的一段盖过整颗星（到星的另一边约 2.6 万公里）");
    // 两万公里高往下看，远处岛群 100 m 的起伏压过之后还分得开（相机的世界坐标约 2e7，单精度的格约 2 m）
    const double d0 = 2.0e7, d1 = d0 + 100.0;
    check(CurveSqueeze::distance(d1, keep, limit) - CurveSqueeze::distance(d0, keep, limit) > 50.0, "两万公里处相隔 100 m 压过之后还差 50 m 以上");
    check(CurveSqueeze::distance(d1) - CurveSqueeze::distance(d0) < 1e-3, "照 P1 的压法同一处只差不到 1 mm（所以要放大）");
    double f_err = 0.0;
    CurveSqueeze::at_height(500000.0, keep, limit);
    for (double d = 0.0; d < 3.0e6; d += 7919.0) {
        const float cam[3] = { 10.0f, 500000.0f, -20.0f };
        const float v[3] = { cam[0] + float(d * 0.6), cam[1] - float(d * 0.8), cam[2] };
        float o[3];
        planet_squeeze_f(v, cam, float(keep), float(limit), o);
        const double got = std::sqrt(double(o[0] - cam[0]) * (o[0] - cam[0]) + double(o[1] - cam[1]) * (o[1] - cam[1]) + double(o[2] - cam[2]) * (o[2] - cam[2]));
        const double s = CurveSqueeze::distance(d, keep, limit);
        f_err = std::max(f_err, std::abs(got - s) / std::max(1.0, s));
    }
    check(f_err < 1e-5, "单精度同式（500 km 高，相对差 " + std::to_string(f_err) + "）");
}

// 行星计划 P2 · 相机在两万公里高时单精度够不够：锚群的网格（着色器同式的 visual）与天空着色器按视线求交的行星边缘，
// 按单精度算出来的方向与双精度差多少（换成像素：1080 高、60° 视角一像素约 1e-3 rad）
float ps_hit_f(float r0, float dy, float rs) {
    // 逐式同 planet_sky.gdshaderinc 的 ps_hit
    const float b = r0 * dy;
    const float c = (r0 - rs) * (r0 + rs);
    const float disc = b * b - c;
    if (disc < 0.0f) {
        return -1.0f;
    }
    const float sq = std::sqrt(disc);
    if (c > 0.0f) {
        return b >= 0.0f ? -1.0f : c / (sq - b);
    }
    return b >= 0.0f ? sq + b : -c / (sq - b);
}

void test_planet_float_orbit() {
    std::printf("[planet_float_orbit]\n");
    const GroupFrame anchor(-31.65, 152.15);
    double worst_mesh = 0.0;
    for (double lateral : { 0.0, 3.0e5, 5.8e5 }) {
        for (double h : { 2.0e5, 2.0e6, 2.0e7 }) {
            const PlanetVec cam(lateral * 0.6, h, -lateral * 0.8);
            const CurveView view = CurveView::at(anchor, cam);
            const CurveViewF vf = CurveViewF::from(view);
            for (int k = 0; k < 40; k++) {
                const double a = k * 0.9, s = (k % 10) * 11000.0;
                const PlanetVec p(s * std::cos(a), -3000.0 + (k % 7) * 800.0, s * std::sin(a));
                const PlanetVec v = view.visual(anchor, p);
                const float pf[3] = { float(p.x), float(p.y), float(p.z) };
                float o[3];
                vf.visual(pf, o);
                const PlanetVec d(double(o[0]) - v.x, double(o[1]) - v.y, double(o[2]) - v.z);
                worst_mesh = std::max(worst_mesh, d.length() / (v - cam).length());
            }
        }
    }
    // 行星的边：相机在两万公里高，扫过边缘的视线在哪一条上开始打到海面（单精度 vs 双精度）
    const double R = PLANET_DEFAULT_R, sea = R + VerticalDatum::SEA_Y;
    double worst_limb = 0.0;
    for (double h : { 2.0e5, 2.0e6, 2.0e7 }) {
        const double r0 = R + h;
        const double edge = -std::sqrt(1.0 - (sea / r0) * (sea / r0)); // 擦边那条的 dy
        auto first_hit = [&](bool p_float) {
            double lo = edge - 0.01, hi = edge + 0.01; // lo 打得到、hi 打不到
            for (int it = 0; it < 60; it++) {
                const double mid = 0.5 * (lo + hi);
                const bool hit = p_float ? ps_hit_f(float(r0), float(mid), float(sea)) > 0.0f : [&] {
                    const double b = r0 * mid, c = (r0 - sea) * (r0 + sea);
                    return b * b - c >= 0.0 && b < 0.0;
                }();
                (hit ? lo : hi) = mid;
            }
            return 0.5 * (lo + hi);
        };
        worst_limb = std::max(worst_limb, std::abs(std::acos(std::clamp(first_hit(true), -1.0, 1.0)) - std::acos(std::clamp(first_hit(false), -1.0, 1.0))));
    }
    std::printf("  锚群网格的方向差最多 %.2e rad、行星边缘 %.2e rad（一像素约 1e-3 rad）\n", worst_mesh, worst_limb);
    check(worst_mesh < 2e-6, "两万公里高、离锚群 580 km 以内：锚群网格的画面方向单精度只差 < 2e-6 rad（千分之一像素）");
    check(worst_limb < 5e-5, "行星的边缘单精度求交只差 < 5e-5 rad（二十分之一像素）");
}

// 行星计划 P2 · 行星贴图（planet_surface）：立方体面映射往返、像素立体角加起来是 4π、全行星的陆地烘得对、开局那群群心那一格是陆地、替身
void test_planet_surface() {
    std::printf("[planet_surface]\n");
    double worst = 0.0;
    int faces_hit[6] = { 0, 0, 0, 0, 0, 0 };
    for (int k = 0; k < 2000; k++) {
        const double z = std::cos(k * 2.399963) * 0.999, lon = k * 0.7123;
        const double d[3] = { std::sqrt(1.0 - z * z) * std::cos(lon), std::sqrt(1.0 - z * z) * std::sin(lon), z };
        double u = 0.0, v = 0.0, back[3];
        const int f = planet_cube::face_of(d, u, v);
        faces_hit[f]++;
        planet_cube::dir_of(f, u, v, back);
        worst = std::max(worst, std::abs(back[0] - d[0]) + std::abs(back[1] - d[1]) + std::abs(back[2] - d[2]));
        worst = u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0 ? 1.0 : worst;
    }
    check(worst < 1e-12, "立方体面映射往返（得到 " + std::to_string(worst) + "）");
    check(*std::min_element(faces_hit, faces_hit + 6) > 200, "六个面都用得上");
    double omega = 0.0, lo = 1e9, hi = 0.0;
    for (int j = 0; j < 128; j++) {
        for (int i = 0; i < 128; i++) {
            const double w = planet_cube::texel_solid_angle(128, i, j);
            omega += w;
            lo = std::min(lo, w);
            hi = std::max(hi, w);
        }
    }
    check(std::abs(omega * 6.0 - 4.0 * 3.14159265358979) < 1e-3, "像素立体角六面加起来是 4π（得到 " + std::to_string(omega * 6.0) + "）");
    check(hi / lo < 1.5, "atan 拉匀之后像素面积最大最小差不到一半（得到 " + std::to_string(hi / lo) + "）");

    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42";
    PlanetData pd;
    std::string err;
    if (!pd.load_files(dir, err)) {
        check(false, "行星包读得出：" + err);
        return;
    }
    SurfaceBakeRules rules;
    rules.size = 512;
    double land = 0.0, ms_total = 0.0, ms_max = 0.0;
    int groups = 0, lit = 0;
    SurfaceFace home;
    int home_face = -1;
    double hu = 0.0, hv = 0.0;
    {
        const PlanetVec h = planet_unit(pd.node(START).lat, pd.node(START).lon);
        const double d[3] = { h.x, h.y, h.z };
        home_face = planet_cube::face_of(d, hu, hv);
    }
    for (int f = 0; f < 6; f++) {
        SurfaceFace face;
        SurfaceBakeStats st;
        bake_surface_face(pd, f, rules, face, &st);
        land += st.land_km2;
        groups += st.groups;
        lit += int(st.lit_texels);
        ms_total += st.ms;
        ms_max = std::max(ms_max, st.ms);
        check(st.stand_ins == 0, "seed42 每群都有粗版，不用替身（面 " + std::to_string(f) + "）");
        if (f == home_face) {
            home = std::move(face);
        }
    }
    std::printf("  512² × 6：陆地 %.3g km²、%d 群次、有灯 %d 像素；六面共 %.0f ms（最慢一面 %.0f ms）\n", land, groups, lit, ms_total, ms_max);
    check(std::abs(land - 2.5e7) < 0.08 * 2.5e7, "烘进去的陆地 ≈ 2500 万 km²（得到 " + std::to_string(land) + "）");
    check(groups >= 8000, "8000 群都烘进去了（跨面的算两次）");
    check(lit > 1000, "夜里有灯的像素上千");
    if (!home.color.empty()) {
        const int i = std::min(rules.size - 1, int(hu * rules.size)), j = std::min(rules.size - 1, int(hv * rules.size));
        int best = 0;
        for (int dj = -2; dj <= 2; dj++) {
            for (int di = -2; di <= 2; di++) {
                const int ii = std::clamp(i + di, 0, rules.size - 1), jj = std::clamp(j + dj, 0, rules.size - 1);
                best = std::max(best, int(home.color[(size_t(jj) * size_t(rules.size) + size_t(ii)) * 4 + 3]));
            }
        }
        check(best > 128, "开局那群群心附近的像素是陆地（份额 " + std::to_string(best) + " / 255）");
    }
    // 替身：面积按行星包
    const PlanetLod s = surface_stand_in(pd, 6611, 2000.0);
    check(!s.empty() && std::abs(s.land_km2() - pd.node(6611).area_km2) < 0.3 * pd.node(6611).area_km2,
            "替身的陆地与行星包差不到三成（" + std::to_string(s.land_km2()) + " / " + std::to_string(pd.node(6611).area_km2) + " km²）");
    // 游戏里的面宽：开局那群那一面 2048²，烘一面、压成 BC3 各要多久（游戏里六面在后台一面一面做）
    {
        SurfaceBakeRules full;
        SurfaceFace face;
        SurfaceBakeStats st;
        bake_surface_face(pd, home_face, full, face, &st);
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<uint8_t> blocks(size_t(full.size / 4) * size_t(full.size / 4) * 16);
        bc3_encode_image(face.color.data(), full.size, full.size, blocks.data());
        const double bc_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  2048² 一面（开局那群那面，%d 群）：烘 %.0f ms、压 BC3 %.0f ms\n", st.groups, st.ms, bc_ms);
        check(st.land_texels > 1000, "2048² 那一面烘得出陆地");
    }
    // BC3：压过再解开，平滑的渐变与贴图里的海岸都差得不多
    {
        const int W = 64;
        std::vector<uint8_t> img(size_t(W) * W * 4), blocks(size_t(W / 4) * (W / 4) * 16), back(size_t(W) * W * 4);
        for (int y = 0; y < W; y++) {
            for (int x = 0; x < W; x++) {
                uint8_t *p = &img[(size_t(y) * W + size_t(x)) * 4];
                const bool land = (x - 30) * (x - 30) + (y - 34) * (y - 34) < 300;
                const double a = land ? 1.0 : 0.0;
                p[0] = uint8_t(std::lround(a * (90 + x)));
                p[1] = uint8_t(std::lround(a * (120 + y)));
                p[2] = uint8_t(std::lround(a * 60));
                p[3] = uint8_t(std::lround(a * 255));
            }
        }
        bc3_encode_image(img.data(), W, W, blocks.data());
        double err = 0.0, aerr = 0.0;
        for (int by = 0; by < W / 4; by++) {
            for (int bx = 0; bx < W / 4; bx++) {
                uint8_t px[64];
                bc3_decode_block(&blocks[(size_t(by) * (W / 4) + size_t(bx)) * 16], px);
                for (int y = 0; y < 4; y++) {
                    for (int x = 0; x < 4; x++) {
                        const uint8_t *s = &img[(size_t(by * 4 + y) * W + size_t(bx * 4 + x)) * 4];
                        for (int c = 0; c < 3; c++) {
                            err += std::abs(int(s[c]) - int(px[(y * 4 + x) * 4 + c]));
                        }
                        aerr += std::abs(int(s[3]) - int(px[(y * 4 + x) * 4 + 3]));
                    }
                }
            }
        }
        err /= W * W * 3.0;
        aerr /= W * W;
        std::printf("  BC3 往返：颜色平均差 %.2f、份额平均差 %.2f（满 255）\n", err, aerr);
        check(err < 6.0 && aerr < 2.0, "BC3 压过再解开，颜色平均差 < 6、份额 < 2");
    }
    SurfaceBakeRules forced = rules;
    forced.force_stand_ins = true;
    SurfaceFace face;
    SurfaceBakeStats st;
    bake_surface_face(pd, home_face, forced, face, &st);
    check(st.stand_ins == st.groups && st.groups > 500 && st.land_km2 > 1e6, "一律画替身也烘得出来（" + std::to_string(st.groups) + " 群、" + std::to_string(st.land_km2) + " km²）");
}

// ---------------------------------------------------------------- 行星计划 P5：逐日天气包与行星天气场

// zlib / DEFLATE 的解压：Python zlib 压的三段（固定码表、不压的块、长一点的固定码表），坏了的流报错
void test_inflate() {
    std::printf("[inflate]\n");
    const std::vector<uint8_t> fixed = { 120, 218, 203, 72, 205, 201, 201, 87, 200, 64, 39, 117, 20, 10, 114, 18, 243, 82, 75, 20, 202, 83, 19, 75, 50, 82, 139, 20, 1, 48, 36, 14,
        178 };
    const std::vector<uint8_t> stored = { 120, 1, 1, 18, 0, 237, 255, 115, 116, 111, 114, 101, 100, 32, 98, 108, 111, 99, 107, 32, 49, 50, 51, 52, 53, 63, 161, 5, 220 };
    std::vector<uint8_t> out;
    std::string err;
    check(zlib_inflate(fixed.data(), fixed.size(), out, 40, err) && std::string(out.begin(), out.end()) == "hello hello hello hello, planet weather!",
            "固定码表的块解得开、Adler-32 对（" + err + "）");
    check(zlib_inflate(stored.data(), stored.size(), out, 18, err) && std::string(out.begin(), out.end()) == "stored block 12345", "不压的块解得开（" + err + "）");
    std::vector<uint8_t> bad = fixed;
    bad[12] ^= 0x5A;
    check(!zlib_inflate(bad.data(), bad.size(), out, 40, err), "坏了的流报错（" + err + "）");
    check(!zlib_inflate(fixed.data(), fixed.size() - 3, out, 40, err), "截断的流报错");
    check(!zlib_inflate(fixed.data(), fixed.size(), out, 41, err), "解开的字节数与包头记的不一样报错");
}

namespace {

struct WeatherFixture {
    std::shared_ptr<PlanetData> planet;
    WeatherPack pack;
    double parse_ms = 0.0;
};

bool load_weather_fixture(WeatherFixture &r_fx) {
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42";
    r_fx.planet = std::make_shared<PlanetData>();
    std::string err;
    if (!r_fx.planet->load_files(dir, err)) {
        check(false, "行星包读得出：" + err);
        return false;
    }
    std::ifstream in(dir + "/weather.bin", std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = r_fx.pack.parse(bytes.data(), bytes.size(), err);
    r_fx.parse_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(ok, "weather.bin 读得出（" + err + "）");
    return ok;
}

// 地图上两点之间的一条大圆：t 从 0 到 1
void great_circle_at(double p_lat0, double p_lon0, double p_lat1, double p_lon1, double p_t, double &r_lat, double &r_lon) {
    const PlanetVec a = planet_unit(p_lat0, p_lon0), b = planet_unit(p_lat1, p_lon1);
    const double om = std::acos(std::clamp(a.dot(b), -1.0, 1.0));
    const double sa = std::sin((1.0 - p_t) * om) / std::sin(om), sb = std::sin(p_t * om) / std::sin(om);
    planet_latlon(a * sa + b * sb, r_lat, r_lon);
}

} // namespace

// 逐日天气包：八千群、与细版 weather.tsv 逐位相同；单群的包（按需生成的 .wx）也读得出
void test_weather_pack() {
    std::printf("[weather_pack]\n");
    WeatherFixture fx;
    if (!load_weather_fixture(fx)) {
        return;
    }
    const WeatherPack &pk = fx.pack;
    std::printf("  %zu 群 × %d 天，解开 %.0f ms；季名 %zu 套；④ 的降水折成年降水 %.0f–%.0f mm（p^%.2f）\n", pk.entries.size(), pk.year_days, fx.parse_ms, pk.season_sets.size(),
            pk.precip_min, pk.precip_max, pk.precip_exp);
    check(pk.entries.size() == 8000 && pk.year_days == 336, "八千群、一年 336 天");
    int e51 = -1, e50 = -1;
    for (int k = 0; k < int(pk.entries.size()); k++) {
        e51 = pk.entries[size_t(k)].node == START ? k : e51;
        e50 = pk.entries[size_t(k)].node == NEIGHBOR ? k : e50;
    }
    check(e51 >= 0 && e50 >= 0, "开局那群、邻群都在包里");
    // 有细版的群：与 weather.tsv 逐位相同（粗细两版的天气类型逐日相同的比例报出来）
    for (int node : { START, NEIGHBOR }) {
        std::ifstream in(group_dir(node) + "/weather.tsv", std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Climate fine;
        std::string err;
        if (!parse_climate(text, fine, err)) {
            check(false, "细版 weather.tsv 读得出");
            continue;
        }
        const int entry = node == START ? e51 : e50;
        const Climate coarse = pk.climate(entry, fx.planet->node(node).lat, fx.planet->node(node).lon, fx.planet->tilt_deg);
        int same_kind = 0, same_all = 0;
        for (size_t d = 0; d < fine.days.size() && d < coarse.days.size(); d++) {
            const ClimateDay &a = fine.days[d], &b = coarse.days[d];
            same_kind += a.kind == b.kind ? 1 : 0;
            same_all += (a.kind == b.kind && a.precip_mm == b.precip_mm && a.temp_c == b.temp_c && a.wind_from_deg == b.wind_from_deg && a.wind_ms == b.wind_ms &&
                                a.sailable == b.sailable && a.storm == b.storm)
                    ? 1
                    : 0;
        }
        std::printf("  %d：类型逐日相同 %d / %zu，整行逐位相同 %d；台面 %.0f / %.0f m、岸缘 %.1f / %.1f m\n", node, same_kind, fine.days.size(), same_all, fine.ref_m, coarse.ref_m,
                fine.rim_m, coarse.rim_m);
        check(same_all == int(fine.days.size()) && fine.days.size() == 336, std::to_string(node) + "：粗版的逐日天气与细版 weather.tsv 逐位相同");
        check(std::abs(fine.ref_m - coarse.ref_m) < 0.5 && std::abs(fine.rim_m - coarse.rim_m) < 0.06 && std::abs(fine.fog_rise_m - coarse.fog_rise_m) < 0.06 &&
                        fine.season_names == coarse.season_names,
                std::to_string(node) + "：台面、岸缘、云海漫多高、季名一样");
    }
    // 全行星一年的构成（生成器报的：晴 47.9%、多云 20.6%……）
    double share[9] = {};
    for (const WeatherPackDay &d : pk.days) {
        share[d.kind_sail & 15] += 1.0 / double(pk.days.size());
    }
    std::printf("  全行星一年：晴 %.1f%%、多云 %.1f%%、小雨 %.1f%%、大雨 %.1f%%、云海漫顶 %.1f%%、风暴 %.1f%%、小雪 %.1f%%、大雪 %.1f%%、暴风雪 %.1f%%\n", 100 * share[0],
            100 * share[1], 100 * share[2], 100 * share[3], 100 * share[4], 100 * share[5], 100 * share[6], 100 * share[7], 100 * share[8]);
    check(pk.precip_min==0 && pk.precip_max==4000 && pk.precip_exp==1 && share[0]>0.4 && share[0]<0.6 && share[5]>0.03 && share[5]<0.1, "新版年雨采用 0–4000 mm 线性换算，全球天气构成在合理范围");
}

// 行星天气场
void test_planet_weather() {
    std::printf("[planet_weather]\n");
    WeatherFixture fx;
    if (!load_weather_fixture(fx)) {
        return;
    }
    const PlanetData &pd = *fx.planet;
    Climate c51;
    if (!load_start_climate(c51)) {
        std::printf("  （跳过：没有开局那群的资产）\n");
        return;
    }
    const auto t_build = std::chrono::steady_clock::now();
    auto pw = std::make_shared<PlanetWeather>(fx.planet);
    const int added = pw->add_pack(fx.pack);
    const double build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_build).count();
    std::printf("  建场 %.0f ms（%d 群有逐日表）；开局那群管 %.1f km、邻群管 %.1f km\n", build_ms, added, pw->reach_km(START), pw->reach_km(NEIGHBOR));
    check(added == 8000 && pw->table_count() == 8000, "八千群都有逐日表");
    check(pw->reach_km(START) > 50.0 && pw->reach_km(START) < 100.0 && pw->reach_km(NEIGHBOR) > 40.0 && pw->reach_km(NEIGHBOR) < 90.0, "一群管多远：按陆地往各方位铺多远（开局那群 79 km、邻群 67 km）");

    const PlanetNode &n51 = pd.node(START), &n50 = pd.node(NEIGHBOR);
    PlanetShape shape;
    shape.sea_radius_m = pd.radius_km * 1000.0;
    const GroupFrame f51(n51.lat, n51.lon, shape);
    // 1. 群自己的陆地上：挂上场与不挂一模一样（逐位）
    const WeatherModel alone(c51, 0.0, 2250.0);
    WeatherModel attached(c51, 0.0, 2250.0);
    pw->set_group_climate(START, c51);
    attached.attach_planet(pw, START, f51);
    int same = 0, total = 0;
    for (double day : { 3.3, 17.9, 40.01, 128.52, 250.77 }) {
        for (const auto &xz : std::vector<std::pair<double, double>>{ { 0.0, 0.0 }, { 20000.0, 5000.0 }, { -15000.0, 18000.0 }, { 8000.0, -25000.0 } }) {
            const WeatherSample a = alone.sample(day, xz.first, xz.second, 400.0), b = attached.sample(day, xz.first, xz.second, 400.0);
            total++;
            same += (a.cloud == b.cloud && a.precip == b.precip && a.temp_c == b.temp_c && a.wind_x == b.wind_x && a.wind_z == b.wind_z && a.kind == b.kind &&
                            a.front == b.front && a.fog_top == b.fog_top)
                    ? 1
                    : 0;
        }
    }
    check(same == total, "开局那群自己的陆地上：挂上行星天气场与不挂逐位一样（" + std::to_string(same) + " / " + std::to_string(total) + "）");
    // 开局那群的村落：几处在过渡带里（场说「别的群、背景也有份」）——那几处的天气与没挂场时不一样
    {
        std::ifstream sin(start_dir() + "/sites.tsv", std::ios::binary);
        const std::string tsv((std::istreambuf_iterator<char>(sin)), std::istreambuf_iterator<char>());
        WorldSites home_sites;
        std::string err;
        parse_world_sites(tsv, home_sites, err);
        const SiteDock *main_dock = home_sites.dock_by_id(1);
        int villages = 0, mixed = 0, home_mixed = 0;
        double worst = 1.0;
        for (const SiteSettlement &v : home_sites.settlements) {
            const double x = v.x, z = v.z;
            villages++;
            const PlanetGroupSample g = pw->sample_group(START, f51, c51.ref_m, 3.5, x, z, false);
            // 邑治与主码头一带（主码头 3 km 内）：开局那一刻人就在这儿
            const bool home = v.seat || (main_dock && std::hypot(x - main_dock->x, z - main_dock->z) < 3000.0);
            home_mixed += home && !g.own ? 1 : 0;
            if (!g.own) {
                mixed++;
                double lat = 0.0, lon = 0.0;
                f51.flat_latlon(PlanetVec(x, 0.0, z), lat, lon);
                const PlanetRegional r = pw->sample(3.5, lat, lon, 0.0);
                for (int k = 0; k < r.n; k++) {
                    if (r.nodes[k] == START) {
                        worst = std::min(worst, r.weights[k]);
                    }
                }
            }
        }
        std::printf("  开局那群的 %d 处村落：%d 处在过渡带里（别的群或背景也有份，它的权最少 %.2f）；主码头 3 km 内在过渡带里的 %d 处\n", villages, mixed, worst, home_mixed);
        // 开局那群（5141 km²）离邻群 #6327（3990 km²）只有 84 km，两群各管的范围（79 / 67 km）叠了一截：主岛西半边的村子天气里有邻群的份
        // （2051 那样孤零零的群九成以上的村落在它自己管的地方，这里是五成多）。开局那群的权处处过半，主码头、邑治一带是它自己的天气。
        check(villages > 1000 && mixed < villages * 6 / 10 && worst > 0.5 && home_mixed == 0,
                "开局那群的村落：过渡带里的也是它的权过半，主码头与邑治一带全是它自己的天气（与没挂场时逐位一样）");
    }
    // 2. 开局那群 → 邻群一路：每公里变得不多（大势连着、锋面连着），两头是各自的天气
    const double day0 = 9.37;
    double worst_cover = 0.0, worst_temp = 0.0, worst_wind = 0.0, worst_cov = 0.0;
    PlanetRegional prev;
    for (int k = 0; k <= 84; k++) {
        double lat = 0.0, lon = 0.0;
        great_circle_at(n51.lat, n51.lon, n50.lat, n50.lon, k / 84.0, lat, lon);
        const PlanetRegional r = pw->sample(day0, lat, lon, 0.0);
        if (k > 0) {
            worst_cover = std::max(worst_cover, std::abs(r.state.cloud - prev.state.cloud));
            worst_cov = std::max(worst_cov, std::abs(r.state.coverage - prev.state.coverage));
            worst_temp = std::max(worst_temp, std::abs(r.state.temp - prev.state.temp));
            worst_wind = std::max(worst_wind, std::hypot(r.state.wind_x - prev.state.wind_x, r.state.wind_z - prev.state.wind_z));
        }
        prev = r;
    }
    std::printf("  开局那群 → 邻群每公里最多变：云量 %.3f、雨区 %.3f、气温 %.2f °C、风 %.2f m/s\n", worst_cover, worst_cov, worst_temp, worst_wind);
    check(worst_cover < 0.08 && worst_cov < 0.08 && worst_temp < 0.5 && worst_wind < 1.0, "两群之间一路过去没有瞬变（每公里云量、雨区 < 0.08，气温 < 0.5 °C，风 < 1 m/s）");
    const PlanetRegional at51 = pw->sample(day0, n51.lat, n51.lon, 0.0), at50 = pw->sample(day0, n50.lat, n50.lon, 0.0);
    check(at51.n == 1 && at51.nodes[0] == START && at51.bg_weight == 0.0 && at50.n == 1 && at50.nodes[0] == NEIGHBOR, "两个群心各是各的（别的群、背景的权都丢了）");
    // 3. 时间上：两群中间，三天里每游戏分钟变得不多（锋面扫过也是渐变）
    double mlat = 0.0, mlon = 0.0;
    great_circle_at(n51.lat, n51.lon, n50.lat, n50.lon, 0.5, mlat, mlon);
    double worst_t = 0.0;
    PlanetRegional last = pw->sample(20.0, mlat, mlon, 0.0);
    for (int m = 1; m <= 3 * 24 * 60; m++) {
        const PlanetRegional r = pw->sample(20.0 + m / 1440.0, mlat, mlon, 0.0);
        worst_t = std::max({ worst_t, std::abs(r.state.cloud - last.state.cloud), std::abs(r.state.coverage - last.state.coverage) });
        last = r;
    }
    std::printf("  两群中间三天：每游戏分钟云量、雨区最多变 %.4f；中点的权 开局那群 %.2f / 邻群 %.2f / 背景 %.2f\n", worst_t, last.n > 0 ? last.weights[0] : 0.0,
            last.n > 1 ? last.weights[1] : 0.0, last.bg_weight);
    check(worst_t < 0.02, "两群中间换天是渐变（每游戏分钟云量、雨区 < 0.02）");
    // 4. 云团场一路连续（几群按权混、权过门槛时也不跳）：开局那群 → 邻群每 10 m 取一次
    double worst_field = 0.0, prev_field = -1.0;
    for (int k = 0; k <= 8440; k++) {
        double lat = 0.0, lon = 0.0;
        great_circle_at(n51.lat, n51.lon, n50.lat, n50.lon, k / 8440.0, lat, lon);
        const double fv = pw->field(33.3, lat, lon);
        if (prev_field >= 0.0) {
            worst_field = std::max(worst_field, std::abs(fv - prev_field));
        }
        prev_field = fv;
    }
    std::printf("  云团场 开局那群 → 邻群每 10 m 最多变 %.4f\n", worst_field);
    check(worst_field < 0.03, "云团场一路连续（每 10 m 变不到 0.03）");
    // 5. 云团按权混方差不变：过渡带里按门槛取的面积份额还对得上覆盖率
    {
        const double cov = 0.5, thr = wx_threshold(cov);
        int in = 0, n = 0;
        for (int i = 0; i < 60; i++) {
            for (int j = 0; j < 60; j++) {
                double lat = 0.0, lon = 0.0;
                great_circle_at(n51.lat, n51.lon, n50.lat, n50.lon, 0.35 + 0.3 * i / 60.0, lat, lon);
                lon += (j - 30) * 0.004;
                in += pw->field(50.0 + 0.001 * j, lat, lon) >= thr ? 1 : 0;
                n++;
            }
        }
        std::printf("  过渡带里云团场过门槛的份额 %.3f（覆盖率 0.5）\n", double(in) / n);
        check(std::abs(double(in) / n - cov) < 0.12, "过渡带里按覆盖率取门槛：面积份额还对得上（±0.12）");
    }
    // 6. 赤道永暴带、G 定点永暴：风暴、禁航、把船往外推
    {
        const PlanetRegional eq = pw->sample(100.0, 1.0, -150.0, 0.0);
        const PlanetRegional g = pw->sample(100.0, pd.g_lat + 1.0, pd.g_lon, 0.0);
        std::printf("  赤道（北纬 1°、西经 150°）：带 %d、风暴 %.2f、能出航 %.2f、推 (%.0f, %.0f) m/s；G 圈里：带 %d、风暴 %.2f、推 (%.0f, %.0f)\n", eq.zone_kind, eq.state.storm,
                eq.state.sailable, eq.push_x, eq.push_z, g.zone_kind, g.state.storm, g.push_x, g.push_z);
        check(eq.zone_kind == 1 && eq.state.storm > 0.9 && eq.state.sailable < 0.1 && eq.bg_weight > 0.9, "赤道永暴带：风暴、禁航（背景接手）");
        check(std::hypot(eq.push_x, eq.push_z) > 100.0, "赤道永暴带的核心把船往外推");
        check(g.zone_kind == 2 && g.state.storm > 0.9 && g.state.sailable < 0.1 && g.push_z < -50.0, "G 圈：风暴、禁航，北边的点往北推（z 南为正）");
    }
    // 7. 群间开阔的云海上：风 = ② + 当天的扰动，风向大多跟 ② 走；离群远了（背景接手）就是 ②
    {
        const PlanetGrid *u = pd.grid("s04.wind_local.u"), *v = pd.grid("s04.wind_local.v");
        int agree = 0, n = 0, bg_n = 0, bg_agree = 0;
        std::vector<double> angles;
        for (int k = 0; k < 4000; k++) {
            const double lat = -70.0 + 140.0 * (k % 80) / 79.0, lon = -180.0 + 7.2 * (k / 80);
            const double ue = u->sample(lat, lon), vn = v->sample(lat, lon);
            const int near = pd.nearest_node(lat, lon);
            const double gap = great_circle_m(lat, lon, pd.node(near).lat, pd.node(near).lon, pw->get_radius()) / 1000.0 - pw->reach_km(near);
            if (std::hypot(ue, vn) < 3.0 || gap < 150.0) {
                continue;
            }
            for (double day : { 30.25, 130.5, 230.75 }) {
                const PlanetRegional r = pw->sample(day, lat, lon, 0.0);
                if (r.zone > 0.01) {
                    continue;
                }
                const double we = r.state.wind_x, wn = -r.state.wind_z;
                const double ang = std::abs(std::atan2(we * vn - wn * ue, we * ue + wn * vn)) * 180.0 / 3.14159265358979;
                angles.push_back(ang);
                agree += ang < 45.0 ? 1 : 0;
                n++;
                if (r.bg_weight > 0.9) {
                    bg_n++;
                    bg_agree += ang < 5.0 ? 1 : 0;
                }
            }
        }
        std::sort(angles.begin(), angles.end());
        const double median = angles.empty() ? 180.0 : angles[angles.size() / 2];
        std::printf("  离群 150 km 以外的云海上 %d 次（② 风 ≥ 3 m/s）：风向与 ② 差 45° 以内 %d 次（%.0f%%），中位差 %.1f°；背景接手的 %d 次里差 5° 以内 %d 次\n", n, agree,
                100.0 * agree / std::max(1, n), median, bg_n, bg_agree);
        check(n > 200 && agree >= n * 0.8 && median < 20.0, "群间开阔的云海上风向大多跟 ② 走（八成差 45° 以内、中位差 20° 以内）");
        check(bg_n == 0 || bg_agree == bg_n, "背景接手的地方就是 ② 的风");
    }
    // 8. 细版为准：换一份逐日表，场跟着换
    {
        Climate hot = c51;
        for (ClimateDay &d : hot.days) {
            d.temp_c += 10.0;
        }
        const double before = pw->sample(5.5, n51.lat, n51.lon, c51.ref_m).state.temp;
        pw->set_group_climate(START, hot);
        const double after = pw->sample(5.5, n51.lat, n51.lon, c51.ref_m).state.temp;
        pw->set_group_climate(START, c51);
        check_near(after - before, 10.0, 1e-9, "细版为准：set_group_climate 换了表，场里那一群跟着换");
    }
    // 9. 全行星的天气贴图：群上是群的天气、赤道永暴带是风暴
    {
        const auto t0 = std::chrono::steady_clock::now();
        std::shared_ptr<const PlanetWeather::BakeGrid> grid = pw->prepare_bake(256, 128);
        const double prep_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::vector<float> rgba;
        const auto t1 = std::chrono::steady_clock::now();
        pw->bake(*grid, 12.25, rgba);
        const double bake_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        const int W = 256, H = 128;
        auto texel = [&](double lat, double lon) {
            const int r = std::clamp(int((lat + 90.0) / 180.0 * H), 0, H - 1), c = std::clamp(int((lon + 180.0) / 360.0 * W), 0, W - 1);
            return &rgba[(size_t(r) * W + size_t(c)) * 4];
        };
        const float *t51 = texel(n51.lat, n51.lon), *teq = texel(0.3, -150.0);
        const PlanetRegional r51 = pw->sample(12.25, -90.0 + (int((n51.lat + 90.0) / 180.0 * H) + 0.5) * 180.0 / H, -180.0 + (int((n51.lon + 180.0) / 360.0 * W) + 0.5) * 360.0 / W, 0.0);
        std::printf("  贴图 256 × 128：备 %.0f ms、烘 %.0f ms；开局那群那格云量 %.2f（取样 %.2f）群 %.2f；赤道那格风暴 %.2f、群 %.2f\n", prep_ms, bake_ms, t51[0], r51.state.cloud, t51[2],
                teq[3], teq[2]);
        check(std::abs(t51[0] - r51.state.cloud) < 0.05 && t51[2] > 0.95, "贴图上开局那群那格的云量就是场在那一点的（±0.05）");
        check(teq[3] > 0.9 && teq[2] < 0.1, "贴图上赤道永暴带是风暴、背景");
    }
    // 10. 用时
    {
        const auto t0 = std::chrono::steady_clock::now();
        double acc = 0.0;
        for (int k = 0; k < 2000; k++) {
            double lat = 0.0, lon = 0.0;
            great_circle_at(n51.lat, n51.lon, n50.lat, n50.lon, k / 2000.0, lat, lon);
            acc += pw->sample(k * 0.013, lat, lon, 0.0).state.cloud;
        }
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 2000.0;
        const auto t1 = std::chrono::steady_clock::now();
        for (int k = 0; k < 2000; k++) {
            acc += attached.sample(k * 0.013, -40000.0 - 20.0 * k, 60000.0 + 10.0 * k, 300.0).cloud;
        }
        const double us2 = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t1).count() / 2000.0;
        std::printf("  取样：场 %.1f µs / 次、挂场的模型在两群之间 %.1f µs / 次（%.0f）\n", us, us2, acc);
    }
}

std::string read_bytes(const std::string &p_path);

// 浮高计划 F3：岛底（连岩棱与起伏的最深处）至少高出云带顶 1000 m，超了收短龙骨（world_terrain 的 island_body；远处低模 far_group_mesh 照抄）
void test_island_floor() {
    std::printf("[island_floor]\n");
    const double floor_y = VerticalDatum::ISLAND_BOTTOM_MIN_Y;
    check_near(floor_y, VerticalDatum::CLOUD_TOP_Y + 1000.0, 1e-9, "岛底最低 = 云带顶之上 1000 m");
    // 本机生成过的几个大群里最大的岛（sites.tsv 的 island 行照抄：岸缘、崖高、龙骨、最深岸距）：龙骨照 25·√R 要垂进云带
    struct Big {
        const char *group;
        double rim, cliff, keel, shore;
        double before; // 夹前的岛心底（岸缘 − 崖壁 − 龙骨），2026-09-27 量的
    };
    const Big bigs[] = { { "4684", 137.4, 26.2, 111.2, 42104.6, -5152.0 }, { "4981", 112.8, 25.6, 87.2, 38415.8, -4947.0 },
        { "4855", 50.0, 25.0, 25.0, 33781.7, -4705.0 } };
    for (const Big &g : bigs) {
        WorldIsland isl;
        isl.rim_m = g.rim;
        isl.cliff_m = g.cliff;
        isl.keel_m = g.keel;
        isl.max_shore_m = g.shore;
        const IslandBody b = island_body(isl);
        const double full = 25.0 * std::sqrt(g.shore);
        const std::string who = std::string(g.group) + " 主岛";
        check(std::abs(g.rim - b.wall_m - full - g.before) < 1.0, who + "：夹前的岛心底对得上量的数");
        check_near(b.wall_m, 160.0, 1e-9, who + "：崖壁不动（照未收短的龙骨定）");
        check(b.keel_m < full, who + "：龙骨收短了");
        check_near(island_lowest(isl, b), floor_y, 1e-6, who + "：岛底连岩棱正好收到云带顶之上 1000 m");
        std::printf("  %s：龙骨 %.0f → %.0f m，岛心底 %.0f → %.0f m，连岩棱最深 %.0f m\n", who.c_str(), full, b.keel_m, g.before, g.rim - b.wall_m - b.keel_m,
                island_lowest(isl, b));
    }
    // 够不着那么深的岛不动：龙骨照旧 max(生成器, 25·√R)
    {
        WorldIsland isl; // 开局那群的主岛（sites.tsv 的 island 0 照抄）
        isl.rim_m = 610.9;
        isl.cliff_m = 310.9;
        isl.keel_m = 300.0;
        isl.max_shore_m = 22568.3;
        const IslandBody b = island_body(isl);
        check_near(b.keel_m, 25.0 * std::sqrt(22568.3), 1e-9, "像开局那群主岛那样的（最深岸距 22.6 km）：龙骨不收");
        check(island_lowest(isl, b) > floor_y, "…岛底本来就在最低线之上");
        isl.rim_m = -4500.0; // 岸缘都在最低线以下（不该有）：龙骨收到零，不出负数
        check(island_body(isl).keel_m == 0.0, "岸缘低过最低线：龙骨收到零");
    }
    // 远处岛群的低模（行星包的粗版）：同一条夹法，这几群的岛底也收到最低线之上（远处不画岩棱，比近处还浅一点）
    {
        PlanetData pd;
        std::string err;
        if (pd.load_files(std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42", err)) {
            const double R = PlanetShape().radius();
            FarGroupRules loose;
            loose.bottom_min_y = -1e9; // 不夹：看夹前多深
            auto deepest = [](const FarGroupMesh &p_m) {
                double d = 0.0;
                for (size_t v = 0; v < p_m.vertex_count(); v++) {
                    d = std::min(d, double(p_m.uvs[v * 2]));
                }
                return d;
            };
            for (int node : { 4684, 4981, 4855, START, 6611 }) {
                const PlanetLod lod = pd.lod(node);
                const double before = deepest(build_far_group_mesh(lod, R, loose)), after = deepest(build_far_group_mesh(lod, R));
                std::printf("  粗版 %d（%.0f m 格）：低模岛底最深 %.0f → %.0f m\n", node, lod.res_m, before, after);
                check(after >= floor_y - 1e-3, "粗版 " + std::to_string(node) + " 的低模岛底在最低线之上（" + std::to_string(after) + "）");
                check(before < floor_y || after == before, "粗版 " + std::to_string(node) + "：够不着最低线的不动");
            }
        } else {
            check(false, "行星包读得出：" + err);
        }
    }
    // 仓库里的两群细版（开局那群与邻群；浮高以后岸缘 35–1642 m）：每座岛都在最低线之上，岛体厚度与浮高无关
    for (int node : { START, NEIGHBOR }) {
        WorldSites sites;
        std::string perr;
        const std::string tsv = read_bytes(group_dir(node) + "/sites.tsv");
        if (!parse_world_sites(tsv, sites, perr)) {
            check(false, "#" + std::to_string(node) + " 的 sites.tsv 能解析：" + perr);
            continue;
        }
        double low = 0.0, rim_lo = 1e9, rim_hi = -1e9;
        int clamped = 0;
        for (const WorldIsland &isl : sites.terrain_islands()) {
            const IslandBody b = island_body(isl);
            low = std::min(low, island_lowest(isl, b));
            clamped += b.keel_m < 25.0 * std::sqrt(std::max(1.0, isl.max_shore_m)) - 1e-9 ? 1 : 0;
            rim_lo = std::min(rim_lo, isl.rim_m);
            rim_hi = std::max(rim_hi, isl.rim_m);
        }
        std::printf("  仓库 #%d：%zu 座岛，岸缘 %.0f–%.0f m，岛底连岩棱最深 %.0f m（收短 %d 座）\n", node, sites.islands.size(), rim_lo, rim_hi, low, clamped);
        check(low >= floor_y - 1e-6, "仓库 #" + std::to_string(node) + " 的岛底都在最低线之上");
        check(clamped == 0, "仓库 #" + std::to_string(node) + "：一座都不用收短龙骨");
    }
    // 浮高只是整座平移（生成器的岸缘与 keel_m——崖脚的高程——一起加 δ）：岛体（龙骨、崖壁）不跟着变
    {
        WorldIsland lo; // 浮高计划 Z2 时量的 #2051（那时的开局）岛 #24，浮高以前
        lo.rim_m = 228.3;
        lo.cliff_m = 76.0;
        lo.keel_m = 152.3;
        lo.max_shore_m = 1264.9;
        WorldIsland hi = lo; // 浮高 +1325.4 m 以后（仓库里现在的数）
        hi.rim_m = 1553.8;
        hi.keel_m = 1477.7;
        const IslandBody a = island_body(lo), b = island_body(hi);
        check_near(a.keel_m, 25.0 * std::sqrt(1264.9), 1e-9, "2051 #24 浮高以前：龙骨 25·√R");
        check(a.keel_m == b.keel_m && a.wall_m == b.wall_m, "2051 #24 浮高 +1325 m：龙骨、崖壁不变（原先拿崖脚高程 1478 m 当龙骨，高岛反而更厚）");
        check_near(island_lowest(hi, b) - island_lowest(lo, a), 1325.5, 1e-6, "…岛底跟着整座往上挪");
    }
    // 进程内现场生成几个巨岛群（seed42，仓库里的 config.tsv）：主岛的龙骨照 25·√R 要垂进云带，夹过以后每座岛都在最低线之上。
    // 生成器的主岛不浮，所以夹前的数与上面照抄的一样（原先扫本机 %APPDATA% 里生成过的群，结果随机器变；那一段挪到了 zhouzhu_site_survey --island-floor）
    {
        GenConfig cfg;
        std::string error;
        if (!parse_gen_config(read_bytes(std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42/config.tsv"), cfg, error)) {
            check(false, "读 seed42 的 config.tsv（" + error + "）");
            return;
        }
        const std::shared_ptr<const gen_core::PlanetState> ps = gen_core::planet_state(cfg, 42);
        for (const Big &g : bigs) {
            const int node = std::atoi(g.group);
            const auto t0 = std::chrono::steady_clock::now();
            const skyisle::island::Group grp = gen_core::generate_group(*ps, node);
            gen_export::GroupMeta meta;
            meta.node = node;
            meta.seed = 42;
            meta.run = "seed42";
            meta.cls = int(ps->world.islands.cls[size_t(node)]);
            meta.tilt_deg = ps->world.planet.axial_tilt_deg;
            const gen_export::GroupFiles f = gen_export::group_files(grp, meta);
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            WorldSites sites;
            std::string perr;
            if (!parse_world_sites(f.sites_tsv, sites, perr)) {
                check(false, std::string("现场生成的 ") + g.group + " 的 sites.tsv 能解析：" + perr);
                continue;
            }
            const std::vector<WorldIsland> isls = sites.terrain_islands();
            const WorldIsland &m = isls[0];
            check(std::abs(m.rim_m - grp.islands[0].rim_j) < 0.051 && std::abs(m.cliff_m - grp.islands[0].cliff_j) < 0.051 && m.cores.size()==grp.islands[0].cores.size(),
                    std::string("现场生成的 ") + g.group + " 主岛：岸缘、崖高、集水核数量与本次生成器相同");
            double low = 0.0, low_before = 0.0, rim_lo = 1e9;
            int clamped = 0;
            for (const WorldIsland &isl : isls) {
                const IslandBody b = island_body(isl);
                const double full = 25.0 * std::sqrt(std::max(1.0, isl.max_shore_m));
                low = std::min(low, island_lowest(isl, b));
                low_before = std::min(low_before, isl.rim_m - b.wall_m - full);
                clamped += b.keel_m < full - 1e-9 ? 1 : 0;
                rim_lo = std::min(rim_lo, isl.rim_m);
            }
            std::printf("  现场生成 %s（%.1f s）：%zu 座岛，夹前岛心底最深 %.0f m → 连岩棱最深 %.0f m（收短 %d 座），最低的岸缘 %.0f m\n", g.group, s, isls.size(),
                    low_before, low, clamped, rim_lo);
            check(low >= floor_y - 1e-6, std::string("现场生成的 ") + g.group + " 每座岛的岛底都在最低线之上");
            check(clamped >= 1 && island_body(m).keel_m < 25.0 * std::sqrt(m.max_shore_m), std::string("现场生成的 ") + g.group + " 主岛收短了龙骨");
        }
    }
}

// 行星计划 P7：云带之下——云带的垂直剖面（雾、光、颠簸）、船的最低高度、海浪、白礁、y < 0 的气温（D13）、颠簸的噪声
void test_sea_descent() {
    std::printf("[sea_descent]\n");
    const CloudBandRules rules;
    // 按垂直结构取高（浮高计划 F4 起整体下移 1200 m：云带 −5000…−6200、海面 −6800）
    const double TOP = VerticalDatum::CLOUD_TOP_Y, BASE = VerticalDatum::CLOUD_BASE_Y, SEA = VerticalDatum::SEA_Y;
    const double ABOVE = TOP + 800.0, MID = 0.5 * (TOP + BASE), UNDER = BASE - 300.0, FLOOR = SEA + 15.0;
    check(TOP - BASE == 1200.0 && BASE - SEA == 600.0, "云带厚 1.2 km、云底以下 600 m 净空（相对海面不变）");
    check_near(PlanetShape().radius() - PlanetShape().sea_radius_m, -SEA, 1e-9, "基准球 R = 海面半径 + 6800 m");
    {
        const CloudBandState a = cloud_band_at(ABOVE, 0.9);
        check(a.inside == 0.0 && a.under == 0.0 && a.below == 0.0 && a.depth == 0.0, "云带顶上：不在云里、天空照旧");
        check_near(a.sun, 1.0, 1e-12, "云带顶上：直射全在");
        check_near(a.diffuse, 1.0, 1e-12, "云带顶上：漫射光全在");
        check_near(a.turbulence, 0.0, 1e-12, "云带顶上：不颠");
        const CloudBandState m = cloud_band_at(MID, 0.9);
        check_near(m.inside, 1.0, 1e-12, "云带当中：雾最浓");
        check_near(m.turbulence, rules.turbulence_in, 1e-12, "云带当中：颠");
        check(m.sun < 0.1 && m.sun > 0.0, "云带当中：直射只剩一点（" + std::to_string(m.sun) + "）");
        check(m.diffuse < 1.0 && m.diffuse > rules.under_diffuse, "云带当中：漫射光在顶上与云底下之间（" + std::to_string(m.diffuse) + "）");
        const CloudBandState u = cloud_band_at(UNDER, 0.9);
        check(u.inside == 0.0 && u.under == 1.0 && u.below == 1.0, "云底下：出了云、天空从下往上看");
        check(u.sun < 0.01, "云底下：日面看不见（直射 " + std::to_string(u.sun) + "）");
        check_near(u.diffuse, rules.under_diffuse, 1e-9, "云底下：阴天式的漫射光");
        check_near(u.turbulence, rules.turbulence_under, 1e-12, "云底下：只剩一点颠");
        check_near(cloud_band_at(FLOOR, 0.9).sea_height, 15.0, 1e-9, "离海面多高：船的最低处离海面 15 m");
        // 太阳低时云里暗得快
        check(cloud_band_at(MID, 0.15).sun < m.sun && cloud_band_at(MID, 0.15).diffuse < m.diffuse, "太阳低：直射与漫射都更暗");
        // 连续：每 0.25 m 一步从 +200 m 走到海面，各读数变得不多（两处软边各几十米）
        double worst = 0.0;
        CloudBandState prev = cloud_band_at(200.0, 0.8);
        for (double y = 199.75; y >= SEA; y -= 0.25) {
            const CloudBandState s = cloud_band_at(y, 0.8);
            worst = std::max({ worst, std::abs(s.inside - prev.inside), std::abs(s.under - prev.under), std::abs(s.below - prev.below), std::abs(s.sun - prev.sun),
                    std::abs(s.diffuse - prev.diffuse), std::abs(s.turbulence - prev.turbulence) });
            prev = s;
        }
        std::printf("  剖面：每 0.25 m 最多变 %.4f；云带当中直射 %.3f、漫射 %.2f；云底下漫射 %.2f\n", worst, m.sun, m.diffuse, u.diffuse);
        check(worst < 0.02, "升降一趟读数连续（每 0.25 m 最多变 " + std::to_string(worst) + "）");
        check_near(ship_floor_y(true, 15.0), FLOOR, 1e-9, "船的最低高度：海上是海面之上 15 m");
        check_near(ship_floor_y(false, 15.0), 15.0, 1e-9, "船的最低高度：没有行星数据的地图照旧世界 y = 15 m");
    }
    {
        // 海浪：没风也有涌，风越大浪越高越长；风暴按大风算、封顶
        const SeaWaves calm = sea_waves(0.0, 0.0);
        check_near(calm.height_m, SeaWaveRules().calm_height_m, 1e-12, "没风：只有涌");
        check(calm.foam == 0.0 && calm.chop == 0.0, "没风：没有白浪");
        const SeaWaves ten = sea_waves(10.0, 0.0);
        check_near(ten.height_m, 2.14, 0.05, "10 m/s：有效波高约 2.1 m（Pierson–Moskowitz）");
        check_near(ten.length_m, 83.0, 3.0, "10 m/s：主浪波长约 83 m");
        bool mono = true;
        SeaWaves last = sea_waves(0.0, 0.0);
        for (int k = 1; k <= 40; k++) {
            const SeaWaves w = sea_waves(k * 0.75, 0.0);
            mono = mono && w.height_m >= last.height_m && w.length_m >= last.length_m && w.foam >= last.foam;
            last = w;
        }
        check(mono, "风越大浪越高、越长、白浪越多");
        const SeaWaves storm = sea_waves(3.0, 1.0);
        check_near(storm.height_m, SeaWaveRules().max_height_m, 1e-9, "风暴：浪封顶");
        check(storm.chop > 0.99 && storm.foam > 0.99, "风暴：满是白浪、飞沫");
        std::printf("  浪：无风 %.2f m / %.0f m，10 m/s %.2f m / %.0f m，20 m/s %.2f m / %.0f m，风暴 %.2f m / %.0f m\n", calm.height_m, calm.length_m, ten.height_m,
                ten.length_m, sea_waves(20.0, 0.0).height_m, sea_waves(20.0, 0.0).length_m, storm.height_m, storm.length_m);
        // 法线贴图：确定、可平铺（左右、上下两边接得上）、斜率平均为零
        std::vector<uint8_t> a, b;
        bake_wave_normals(64, 7, a);
        bake_wave_normals(64, 7, b);
        check(a == b && a.size() == size_t(64 * 64 * 4), "海浪法线贴图：同一个种子逐位相同");
        std::vector<uint8_t> big;
        bake_wave_normals(256, 7, big);
        double edge = 0.0, inner = 0.0, mean_x = 0.0;
        for (int j = 0; j < 256; j++) {
            auto at = [&](int i, int c) { return double(big[(size_t(j) * 256 + size_t((i + 256) % 256)) * 4 + size_t(c)]); };
            edge = std::max(edge, std::abs(at(0, 2) - at(255, 2)));
            inner = std::max(inner, std::abs(at(128, 2) - at(127, 2)));
            for (int i = 0; i < 256; i++) {
                mean_x += at(i, 0) - 127.5;
            }
        }
        mean_x /= 256.0 * 256.0;
        check(edge <= inner + 2.0, "海浪法线贴图：左右两边接得上（边上相邻差 " + std::to_string(edge) + "，里面 " + std::to_string(inner) + "）");
        check(std::abs(mean_x) < 3.0, "海浪法线贴图：斜率平均为零（" + std::to_string(mean_x) + "）");
    }
    {
        // 颠簸的噪声：有界、平滑、平均为零、确定；不同的种子不一样
        double lo = 1e9, hi = -1e9, mean = 0.0, jump = 0.0;
        double prev = flight_bump(0.0, 3);
        const int n = 60 * 600;
        for (int k = 1; k <= n; k++) {
            const double v = flight_bump(k / 60.0, 3);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            mean += v;
            jump = std::max(jump, std::abs(v - prev));
            prev = v;
        }
        mean /= n;
        std::printf("  颠簸：%.2f…%.2f，平均 %.3f，一帧最多变 %.3f\n", lo, hi, mean, jump);
        check(lo >= -1.0 && hi <= 1.0 && hi - lo > 1.0, "颠簸：在 −1..1 里、起伏够大");
        check(std::abs(mean) < 0.1, "颠簸：平均约为零");
        check(jump < 0.1, "颠簸：平滑（一帧最多变 " + std::to_string(jump) + "）");
        check(flight_bump(12.3, 3) == flight_bump(12.3, 3) && flight_bump(12.3, 3) != flight_bump(12.3, 4), "颠簸：确定、种子不同就不同");
        // 飞行模型：颠簸直接叠在升降上，不被升降的惯性抹平；拿掉了升降照舵
        const FlightParams params;
        FlightState s;
        FlightInput in;
        in.bump = 2.0;
        s = flight_step(s, in, params, 1.0 / 60.0);
        check_near(s.vy, 2.0, 1e-9, "颠簸：这一帧就往上推 2 m/s");
        in.bump = -1.5;
        s = flight_step(s, in, params, 1.0 / 60.0);
        check_near(s.vy, -1.5, 1e-9, "颠簸：下一帧换成往下推");
        in.bump = 0.0;
        in.lift = 1.0;
        for (int k = 0; k < 60 * 10; k++) {
            s = flight_step(s, in, params, 1.0 / 60.0);
        }
        check_near(s.vy, params.max_climb, 0.01, "颠簸停了：升降照舵");
    }
    {
        // y < 0 的气温取 y = 0 处的（D13：云带是一层逆温）；y > 0 照直减率
        Climate c;
        if (load_start_climate(c)) {
            const WeatherModel wx(c, 0.0, 2250.0);
            const double x = 12000.0, z = -8000.0, day = 3.5;
            const double t0 = wx.sample(day, x, z, 0.0).temp_c;
            check_near(wx.sample(day, x, z, ABOVE).temp_c, t0, 1e-12, "y = −4200（云带顶上）的气温同 y = 0");
            check_near(wx.sample(day, x, z, FLOOR).temp_c, t0, 1e-12, "海面上 15 m 的气温同 y = 0（生成器的海面温度）");
            check(wx.sample(day, x, z, 1000.0).temp_c < t0 - 3.0, "y > 0 照直减率往上减");
            check_near(wx.mean_temperature(day, MID), wx.mean_temperature(day, 0.0), 1e-12, "日均气温：y < 0 同 y = 0");
            check_near(wx.elevation(day, MID).temp_mean, wx.elevation(day, 0.0).temp_mean, 1e-12, "海拔表：y < 0 同 y = 0");
            check(wx.sample(day, x, z, UNDER).fog <= wx.sample(day, x, z, MID).fog, "云底下不在云海漫顶的雾里");
            std::printf("  气温：y = 0 %.1f °C、−4200 %.1f、−6785 %.1f、+1000 %.1f\n", t0, wx.sample(day, x, z, ABOVE).temp_c, wx.sample(day, x, z, FLOOR).temp_c,
                    wx.sample(day, x, z, 1000.0).temp_c);
        }
    }
    {
        // 白礁：按行星包，老岛群附近多、别处零星；烘成等经纬的一张与逐点的式子对得上
        const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42";
        PlanetData pd;
        std::string err;
        if (!pd.load_files(dir, err)) {
            check(false, "行星包读得出：" + err);
            return;
        }
        const ReefRules rr;
        int oldest = -1, youngest = -1;
        for (int k = 0; k < pd.node_count(); k++) {
            if (oldest < 0 || pd.node(k).age > pd.node(oldest).age) {
                oldest = k;
            }
            if (youngest < 0 || pd.node(k).age < pd.node(youngest).age) {
                youngest = k;
            }
        }
        int by_age[5] = { 0, 0, 0, 0, 0 };
        for (int k = 0; k < pd.node_count(); k++) {
            by_age[std::clamp(int(pd.node(k).age * 5.0), 0, 4)]++;
        }
        std::printf("  岛龄：0–0.2 %d 群、0.2–0.4 %d、0.4–0.6 %d、0.6–0.8 %d、0.8–1 %d\n", by_age[0], by_age[1], by_age[2], by_age[3], by_age[4]);
        const double d_old = reef_density_at(pd, pd.node(oldest).lat, pd.node(oldest).lon, rr);
        const double d_young =reef_density_at(pd, pd.node(youngest).lat, pd.node(youngest).lon, rr);
        const double d_home = reef_density_at(pd, pd.node(START).lat, pd.node(START).lon, rr);
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<uint8_t> tex;
        bake_reef_density(pd, 1024, 512, tex, rr);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        double worst = 0.0, sum = 0.0, over = 0.0, area = 0.0;
        for (int r = 0; r < 512; r++) {
            const double lat = -90.0 + (r + 0.5) * 180.0 / 512;
            const double wa = std::cos(lat * 3.14159265358979 / 180.0);
            for (int c = 0; c < 1024; c++) {
                const double v = tex[size_t(r) * 1024 + size_t(c)] / 255.0;
                sum += v * wa;
                over += v > 0.3 ? wa : 0.0;
                area += wa;
            }
        }
        for (int k = 0; k < 40; k++) {
            const int r = (k * 37 + 11) % 512, c = (k * 131 + 7) % 1024;
            const double lat = -90.0 + (r + 0.5) * 180.0 / 512, lon = -180.0 + (c + 0.5) * 360.0 / 1024;
            worst = std::max(worst, std::abs(tex[size_t(r) * 1024 + size_t(c)] / 255.0 - reef_density_at(pd, lat, lon, rr)));
        }
        std::printf("  白礁：最老的群 #%d（岛龄 %.2f）%.2f、最年轻的 #%d（%.2f）%.2f、开局那群（%.2f）%.2f；全行星平均 %.3f、三成以上的面积 %.1f%%；烘 1024 × 512 %.0f ms\n", oldest,
                pd.node(oldest).age, d_old, youngest, pd.node(youngest).age, d_young, pd.node(START).age, d_home, sum / area, 100.0 * over / area, ms);
        check(d_old > 0.5, "最老的岛群附近白礁多");
        check(d_young < d_old, "年轻的岛群附近比老的少");
        check(worst < 0.01, "烘的贴图与逐点的式子对得上（最多差 " + std::to_string(worst) + "）");
        check(sum / area > rr.base && sum / area < 0.5, "全行星平均：比零星多、远没到处都是");
    }
}

namespace isl = skyisle::island;

// ---------------------------------------------------------------- 进程内生成（行星计划 P6e）

std::string read_bytes(const std::string &p_path) {
    std::string s;
    gen_core::read_file(p_path, s);
    return s;
}

std::string lf(const std::string &p_s) {
    std::string out;
    out.reserve(p_s.size());
    for (size_t k = 0; k < p_s.size(); k++) {
        if (p_s[k] == '\r' && k + 1 < p_s.size() && p_s[k + 1] == '\n') {
            continue;
        }
        out.push_back(p_s[k]);
    }
    return out;
}

void test_gen_format() {
    std::printf("[gen_format]\n");
    // Rectangular, off-centre coast: a centred square would conceal an axis swap.
    skyisle::GridD phi(7,11,0.0);
    for (int r=0;r<phi.H;r++) for (int c=0;c<phi.W;c++) phi(r,c)=3.25-c;
    const auto coast=skyisle::island::coast_distance(phi,100.0);
    check_near(coast(5,1),225.0,1e-8,"亚格岸距：陆地到竖直岸线 225 m");
    check_near(coast(1,7),-375.0,1e-8,"亚格岸距：岸外为负，距离 375 m");
    // Python 的 repr / format(x, "g") / round（与 Python 逐字对过 90 万个值，这里留几个边界）
    const std::pair<double, const char *> reprs[] = { { 0.0, "0.0" }, { -0.0, "-0.0" }, { 1.0, "1.0" }, { 0.1, "0.1" }, { 1e16, "1e+16" },
        { 1e15, "1000000000000000.0" }, { 1e-4, "0.0001" }, { 1e-5, "1e-05" }, { 1.0 / 3.0, "0.3333333333333333" }, { 123456.789, "123456.789" },
        { -9.143091999999999e+17, "-9.143091999999999e+17" }, { 5e-324, "5e-324" }, { 2.5e-7, "2.5e-07" } };
    for (const auto &r : reprs) {
        check(pyfmt::repr(r.first) == r.second, std::string("repr ") + r.second + "（得到 " + pyfmt::repr(r.first) + "）");
    }
    check(pyfmt::fmt_g(126.0) == "126" && pyfmt::fmt_g(1234567.0) == "1.23457e+06" && pyfmt::fmt_g(0.5) == "0.5" && pyfmt::fmt_g(1.3) == "1.3", "format(x, 'g')");
    check(pyfmt::fmt_f(-31.65234, 4) == "-31.6523" && pyfmt::fmt_f(224.45, 1) == "224.4", "format(x, '.Nf')（二进制值的精确舍入）");
    check(pyfmt::sha1_hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1(abc)");
    check(pyfmt::sha1_hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1(空)");
    {
        skyisle::Json o = skyisle::Json::obj();
        o.set("名", "村001");
        o.set("n", 3);
        o.set("x", 2.0);
        skyisle::Json a = skyisle::Json::arr();
        a.push(skyisle::Json());
        a.push(true);
        a.push("a\"b\\c\n");
        o.set("a", a);
        o.set("e", skyisle::Json::arr());
        o.set("o", skyisle::Json::obj());
        const std::string want = "{\n \"名\": \"村001\",\n \"n\": 3,\n \"x\": 2.0,\n \"a\": [\n  null,\n  true,\n  \"a\\\"b\\\\c\\n\"\n ],\n \"e\": [],\n \"o\": {}\n}";
        check(pyfmt::json_dumps(o, 1) == want, "json.dumps(ensure_ascii=False, indent=1)（得到 " + pyfmt::json_dumps(o, 1) + "）");
        check(pyfmt::json_dumps(o, 1, true) == [&] {
            std::string c;
            for (char ch : want) {
                if (ch == '\n') {
                    c += '\r';
                }
                c += ch;
            }
            return c;
        }(),
                "json.dumps 的换行写成 \\r\\n");
    }
    // scipy 的特征变换：并列时挑哪个（十字形的背景；与 scipy 1.18 逐格对过 400 张随机图）
    {
        const uint8_t m[5 * 5] = { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 };
        std::vector<int32_t> fr, fc;
        scipy_nd::feature_transform(m, 5, 5, fr, fc);
        check(fr[0] == 2 && fc[0] == 0, "特征变换：(0,0) 离 (0,2)、(2,0) 一样远，scipy 取 (2,0)（得到 (" + std::to_string(fr[0]) + "," + std::to_string(fc[0]) + ")）");
        check(fr[6] == 2 && fc[6] == 1, "特征变换：(1,1) 离 (1,2)、(2,1) 一样远，scipy 取 (2,1)（得到 (" + std::to_string(fr[6]) + "," + std::to_string(fc[6]) + ")）");
        const std::vector<double> d = scipy_nd::distance_edt(m, 5, 5);
        check(d[0] == 2.0 && d[6] == 1.0 && d[12] == 0.0, "距离变换");
    }
    // config.tsv：\r\n 与 \n 读成同一份（原文按 \n 存、哈希一样）
    {
        GenConfig a, b;
        std::string e1, e2;
        const std::string t = "# x\nplanet\tnum\ts01.a\t1.5\nplanet\tvec\ts01.v\t1.0,2.0\nplanet\tstr\ts01.s\ta\\tb\nisland\tnum\tlayout.n0\t30.0\nisland\tvec\tempty\t\nengine\tnum\tthreads\t4.0\n";
        std::string crlf;
        for (char c : t) {
            if (c == '\n') {
                crlf += '\r';
            }
            crlf += c;
        }
        check(parse_gen_config(t, a, e1) && parse_gen_config(crlf, b, e2), "config.tsv 读得出来（" + e1 + e2 + "）");
        check(a.sha1 == b.sha1 && a.text == b.text, "config.tsv：换行不同、哈希一样");
        check(a.planet.get("s01.a") == 1.5 && a.planet.list("s01.v").size() == 2 && a.planet.gets("s01.s") == "a\tb" && a.island.list("empty").empty() &&
                        a.threads() == 4,
                "config.tsv 的三种值");
    }
}

// seed42：进程内生成的行星包与岛群包，与仓库里的（game/planet/seed42、它底下 groups/ 里开局那群与邻群的细版）逐字节相同。
// 行星包原是 Python 那条路导出的；两群细版 PLAN-LAND P0 起由 tools/zhouzhu_gen.cpp 导（与 Python 那条路逐字节相同，行星计划 P6e 验过），这里守着不变
void test_gen_seed42() {
    std::printf("[gen_seed42]\n");
    const std::string root = ZHOUZHU_SOURCE_DIR;
    GenConfig cfg;
    std::string error;
    check(parse_gen_config(read_bytes(root + "/game/planet/seed42/config.tsv"), cfg, error), "读 seed42 的 config.tsv（" + error + "）");
    const auto t0 = std::chrono::steady_clock::now();
    const std::shared_ptr<const gen_core::PlanetState> ps = gen_core::planet_state(cfg, 42);
    const double planet_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // Reproduce the fixture with its recorded export provenance. A later source
    // commit must not force rewriting historical asset metadata or mask data changes.
    // Only lod.* differs: a new world has no coarse entries yet.
    {
        const std::string repo = read_bytes(root + "/game/planet/seed42/planet.bin");
        PlanetData fixture;
        const bool parsed=fixture.parse_planet(reinterpret_cast<const uint8_t *>(repo.data()),repo.size(),error);
        check(parsed && !fixture.source.empty(), "planet.bin：历史导出来源可读（"+error+"）");
        if (!parsed) return;
        const std::string mine = gen_export::planet_bin(ps->world, ps->society, 42, fixture.source);
        auto split = [](const std::string &b, size_t &r_lod_begin, size_t &r_lod_end) {
            uint32_t n = 0, ncol = 0, hb = 0;
            std::memcpy(&n, b.data() + 12, 4);
            std::memcpy(&ncol, b.data() + 16, 4);
            std::memcpy(&hb, b.data() + 24, 4);
            size_t off = 28 + hb;
            r_lod_begin = r_lod_end = 0;
            const int sz[4] = { 4, 4, 2, 1 };
            for (uint32_t k = 0; k < ncol; k++) {
                const uint8_t len = uint8_t(b[off]);
                const std::string name = b.substr(off + 1, len);
                const size_t start = off;
                off += 1 + len;
                const uint8_t t = uint8_t(b[off]);
                off += 1 + size_t(n) * size_t(sz[t]);
                if (name.rfind("lod.", 0) == 0 && !r_lod_begin) {
                    r_lod_begin = start;
                }
            }
            r_lod_end = off;
        };
        size_t a0 = 0, a1 = 0, b0 = 0, b1 = 0;
        split(mine, a0, a1);
        split(repo, b0, b1);
        check(a0 > 0 && a0 == b0 && mine.compare(0, a0, repo, 0, b0) == 0, "planet.bin：头与 lod.* 之前的 39 列逐字节相同");
        check(mine.size() - a1 == repo.size() - b1 && mine.compare(a1, std::string::npos, repo, b1, std::string::npos) == 0, "planet.bin：31 张场逐字节相同");
    }
    auto meta = [&](int node) {
        gen_export::GroupMeta m;
        m.node = node;
        m.seed = 42;
        m.run = "seed42";
        m.cls = int(ps->world.islands.cls[size_t(node)]);
        m.tilt_deg = ps->world.planet.axial_tilt_deg;
        return m;
    };
    for (int node : { NEIGHBOR, START }) {
        const auto t1 = std::chrono::steady_clock::now();
        gen_export::LodEntry lod;
        gen_export::WeatherEntry wx;
        const isl::Group g = gen_core::generate_group(*ps, node, &lod, &wx);
        const gen_export::GroupFiles f = gen_export::group_files(g, meta(node));
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        if (node == NEIGHBOR) {
            // 拆开来一步一步生成（为了在水系之后取粗版）与生成器的 island::generate 逐字节相同；顺带的粗版与单跑粗版（同 skyisle island lod）逐字节相同
            isl::NodeInputs x = skyisle::planet::node_inputs(ps->world.islands, ps->world.climate, node, 42, cfg.planet);
            skyisle::planet::apply_polity(ps->society.polity, node, cfg.planet, x);
            skyisle::planet::apply_routes(ps->world.islands, ps->society.routes, node, x);
            const gen_export::GroupFiles f2 = gen_export::group_files(isl::generate(x, ps->view, cfg.island, 0, 5, 0.0, cfg.threads()), meta(node));
            check(f.terrain_bin == f2.terrain_bin && f.world_json == f2.world_json && f.sites_tsv == f2.sites_tsv && f.resources_tsv == f2.resources_tsv &&
                            f.weather_tsv == f2.weather_tsv,
                    "邻群：拆开来生成与 island::generate 的包逐字节相同");
            gen_export::LodEntry lod2;
            gen_export::WeatherEntry wx2;
            gen_core::lod_only(*ps, node, lod2, wx2);
            const std::array<double, 3> pf = gen_core::precip_formula(cfg);
            check(gen_export::lods_bin({ lod }) == gen_export::lods_bin({ lod2 }), "邻群：顺带的粗版与单跑粗版逐字节相同");
            check(gen_export::weather_bin({ wx }, pf, "") == gen_export::weather_bin({ wx2 }, pf, ""), "邻群：顺带的逐日天气与单跑粗版的逐字节相同");
            check(lod.rows > 0 && lod.cols > 0 && lod.n_islands == int(g.islands.size()) && lod.res_m == 2000.0, "邻群：粗版 2 km、岛数对");
        }
        const std::string dir = group_dir(node);
        const std::string n = "#" + std::to_string(node) + " ";
        check(f.terrain_bin == read_bytes(dir + "/terrain.bin"), n + "terrain.bin 逐字节相同");
        check(lf(f.world_json) == lf(read_bytes(dir + "/world.json")), n + "world.json 逐字节相同（换行一律按 \\n 比：git 检出时会换）");
        check(f.sites_tsv == lf(read_bytes(dir + "/sites.tsv")), n + "sites.tsv 逐字节相同");
        check(f.resources_tsv == lf(read_bytes(dir + "/resources.tsv")), n + "resources.tsv 逐字节相同");
        check(f.weather_tsv == lf(read_bytes(dir + "/weather.tsv")), n + "weather.tsv 逐字节相同");
        std::printf("  #%d：生成 + 转换 %.2f s\n", node, s);
    }
    // 开局周围那一圈 500 m 的粗版（D11；tools/zhouzhu_gen.cpp near-lods 写进 lods.bin）：开局那群自己的那一条与现场单跑 500 m 粗版逐字节相同
    {
        gen_export::LodEntry fine;
        gen_export::WeatherEntry wx;
        gen_core::lod_only(*ps, START, fine, wx, 500.0);
        const std::string lods = read_bytes(root + "/game/planet/seed42/lods.bin");
        bool found = false, same = false;
        uint32_t count = 0;
        if (lods.size() >= 12) {
            std::memcpy(&count, lods.data() + 8, 4);
        }
        for (uint32_t k = 0; k < count && 12 + size_t(k + 1) * 36 <= lods.size(); k++) {
            const char *e = lods.data() + 12 + size_t(k) * 36;
            uint32_t node = 0, bytes = 0;
            float res = 0.0f, ox = 0.0f, oy = 0.0f;
            uint16_t rows = 0, cols = 0, nis = 0;
            uint64_t off = 0;
            std::memcpy(&node, e, 4);
            std::memcpy(&res, e + 4, 4);
            std::memcpy(&ox, e + 8, 4);
            std::memcpy(&oy, e + 12, 4);
            std::memcpy(&rows, e + 16, 2);
            std::memcpy(&cols, e + 18, 2);
            std::memcpy(&nis, e + 20, 2);
            std::memcpy(&off, e + 24, 8);
            std::memcpy(&bytes, e + 32, 4);
            if (int(node) != START || res != float(fine.res_m)) {
                continue;
            }
            found = true;
            same = ox == float(fine.origin_x) && oy == float(fine.origin_y) && rows == fine.rows && cols == fine.cols && nis == fine.n_islands &&
                    off + bytes <= lods.size() && lods.compare(size_t(off), bytes, fine.blob) == 0;
        }
        check(found && same && fine.res_m == 500.0, "开局那群的 500 m 粗版：lods.bin 里那一条与现场生成的逐字节相同");
    }
    std::printf("  行星层 ①–⑨（⑧ 跳过）%.2f s\n", planet_s);
}

// ---------------------------------------------------------------- 切片十一：改地形与搭建

void test_terrain_edits() {
    std::printf("terrain_edits\n");
    EditGrid g;
    g.x0 = -100.0;
    g.z0 = -50.0;
    check(g.cell_j(-100.0) == 0 && g.cell_j(-99.01) == 0 && g.cell_j(-100.01) == -1 && g.cell_i(-50.0) == 0, "格号向下取整（负的也对）");
    check(EditGrid::chunk_of(15) == 0 && EditGrid::chunk_of(16) == 1 && EditGrid::chunk_of(-1) == -1 && EditGrid::chunk_of(-16) == -1 && EditGrid::chunk_of(-17) == -2,
            "块号向下取整");
    int ci = 0, cj = 0;
    EditGrid::chunk_unkey(EditGrid::chunk_key(-3, 7), ci, cj);
    check(ci == -3 && cj == 7, "块键往返（负的块号）");

    const TerrainEdits empty(g);
    const TerrainEdits a = empty.with_step(5, 6, -3);
    check(empty.empty() && empty.step_at(5, 6) == 0, "改一格不动原来那份");
    check(a.step_at(5, 6) == -3 && a.chunk_count() == 1 && a.cell_count() == 1 && a.get_revision() == 1, "改了一格：一块、一格、版本加一");
    check_near(a.delta_at(g.cell_x(6) + 0.5, g.cell_z(5) + 0.5), -1.5, 1e-12, "点所在那格低 1.5 m");
    check_near(a.delta_at(g.cell_x(7) + 0.5, g.cell_z(5) + 0.5), 0.0, 1e-12, "隔壁格没改");
    const TerrainEdits b = a.with_step(-1, -20, 4).with_step(5, 6, 0);
    check(b.step_at(5, 6) == 0 && b.step_at(-1, -20) == 4 && b.chunk_count() == 1, "改回 0 的块删掉；负格号的块另记");
    check(!b.near_edits(g.cell_x(6), g.cell_z(5)) && b.near_edits(g.cell_x(-20) + 0.5, g.cell_z(-1) + 0.5), "外包框只罩着改过的块");
    const std::vector<int64_t> in = b.chunks_in(g.cell_x(-30), g.cell_z(-5), g.cell_x(-10), g.cell_z(3));
    check(in.size() == 1 && in[0] == EditGrid::chunk_key(-1, -2), "框里的块");
    check(b.with_step(-1, -20, 4).get_revision() == b.get_revision(), "改成一样的级数不算改");

    TerrainEdits c = empty;
    for (int i = 0; i < 40; i++) {
        c = c.with_step(i, 3 * i - 20, (i % 7) - 3);
    }
    c = c.with_step(100, 100, 127).with_step(101, 100, -127);
    TerrainEdits back;
    std::string err;
    const std::string text = save_terrain_edits(c);
    check(load_terrain_edits(text, back, err), "改地形的文本读得回：" + err);
    bool same = back.get_grid().x0 == c.get_grid().x0 && back.get_grid().z0 == c.get_grid().z0 && back.chunk_count() == c.chunk_count() && back.cell_count() == c.cell_count();
    for (int i = -5; i < 130 && same; i++) {
        for (int j = -30; j < 130 && same; j++) {
            same = back.step_at(i, j) == c.step_at(i, j);
        }
    }
    check(same, "读回逐格一样（格子原点、每一格）");
    check(save_terrain_edits(back) == text, "再存一遍一字不差");
    check(!load_terrain_edits("chunk\t0\t0\t1*300\n", back, err), "一块多于 256 格拒收");
}

void test_edit_patch() {
    std::printf("edit_patch\n");
    const WorldGrid g = synthetic_grid();
    const WorldTerrain t(g, { synthetic_island() }, 7);
    const TileLayout layout(g);
    const TileKey here = layout.leaf_at(100.0, 50.0);
    const EditGrid eg = layout.edit_grid();
    double ox = 0.0, oz = 0.0;
    layout.corner(here, ox, oz);
    // 叶块里第 (2, 3) 块：挖一个 2 × 2 格、2 m 深的坑，另垒一格 1 m 的土台
    const int ci = here.iz * 8 + 2, cj = here.ix * 8 + 3;
    const int gi = ci * 16 + 5, gj = cj * 16 + 6;
    TerrainEdits e(eg);
    for (int di = 0; di < 2; di++) {
        for (int dj = 0; dj < 2; dj++) {
            e = e.with_step(gi + di, gj + dj, -4);
        }
    }
    e = e.with_step(gi + 6, gj + 6, 2);
    const std::vector<int64_t> holes = tile_edit_chunks(layout, e, here);
    check(holes.size() == 1 && holes[0] == EditGrid::chunk_key(ci, cj), "叶块里改过的块");
    check(layout.leaf_of_chunk(ci, cj) == here, "块落在哪个叶块");
    const TileMesh plain = build_tile_mesh(t, layout, here);
    const TileMesh holed = build_tile_mesh(t, layout, here, holes);
    check(plain.indices.size() - holed.indices.size() == size_t(8 * 8 * 2 * 3), "留洞：块里 64 个粗格的顶面不出（" + std::to_string(plain.indices.size() - holed.indices.size()) + " 个下标）");
    check(holed.land_quads == plain.land_quads, "留洞不改陆地格数");

    const TileMesh patch = build_edit_patch(t, layout, e, ci, cj);
    check(patch.origin_x == ox && patch.origin_z == oz, "补丁与叶块同一个原点");
    check(patch.land_quads == 256, "补丁：块里 256 格都出顶面");
    // 没改过的格在粗格节点上的顶点与叶块逐位相同
    std::map<std::pair<float, float>, float> tile_tops;
    for (size_t k = 0; k < plain.positions.size() / 3; k++) {
        if (plain.normals[k * 3 + 1] > 0.5f) {
            tile_tops[{ plain.positions[k * 3], plain.positions[k * 3 + 2] }] = plain.positions[k * 3 + 1];
        }
    }
    int node_hits = 0, node_same = 0, walls = 0;
    const float pit_x = float(eg.cell_x(gj) - ox), pit_z = float(eg.cell_z(gi) - oz);
    for (size_t k = 0; k < patch.positions.size() / 3; k++) {
        const float x = patch.positions[k * 3], y = patch.positions[k * 3 + 1], z = patch.positions[k * 3 + 2];
        if (patch.normals[k * 3 + 1] == 0.0f) {
            walls++;
            continue;
        }
        const bool in_pit = x >= pit_x - 1e-4f && x <= pit_x + 2.0001f && z >= pit_z - 1e-4f && z <= pit_z + 2.0001f;
        const bool in_mound = x >= pit_x + 6.0f - 1e-4f && x <= pit_x + 7.0001f && z >= pit_z + 6.0f - 1e-4f && z <= pit_z + 7.0001f;
        const auto found = tile_tops.find({ x, z });
        // 裙边的下沿（原地形往下 0.3 m）不算
        if (found == tile_tops.end() || in_pit || in_mound || patch.normals[k * 3 + 1] < 0.5f || y < found->second - 0.2f) {
            continue;
        }
        node_hits++;
        node_same += y == found->second ? 1 : 0;
    }
    check(node_hits > 50 && node_same == node_hits, "没改过的格在粗格节点上与叶块逐位相同（" + std::to_string(node_same) + " / " + std::to_string(node_hits) + "）");
    // 坎：坑一圈 8 条边 × 4 级 + 土台 4 条边 × 2 级 = 40 块竖面，每块 4 个顶点
    check(walls == 40 * 4, "坎的竖面：坑 8 边 × 4 级、土台 4 边 × 2 级（" + std::to_string(walls / 4) + " 块）");
    check(patch.collision.size() / 9 == size_t(256 * 2 + 40 * 2), "补丁的碰撞：顶面 + 坎（" + std::to_string(patch.collision.size() / 9) + " 个面）");
    // 坑底比原地形低 2 m
    double natural = 0.0;
    t.surface_natural(eg.cell_x(gj) + 0.5, eg.cell_z(gi) + 0.5, natural);
    float pit_floor = 1e9f;
    for (size_t k = 0; k < patch.positions.size() / 3; k++) {
        const float x = patch.positions[k * 3], z = patch.positions[k * 3 + 2];
        if (std::abs(x - (pit_x + 0.5f)) < 0.6f && std::abs(z - (pit_z + 0.5f)) < 0.6f && patch.normals[k * 3 + 1] > 0.5f) {
            pit_floor = std::min(pit_floor, patch.positions[k * 3 + 1]);
        }
    }
    check_near(pit_floor, natural - 2.0, 0.1, "坑底 = 原地形 − 2 m");

    // 叶块边上的坎往低的那格让 1 cm（邻块的裙边在那条线上）
    {
        const int ci2 = here.iz * 8 + 4, cj2 = here.ix * 8; // 叶块西边那一列块
        const TerrainEdits e2 = TerrainEdits(eg).with_step(ci2 * 16 + 3, cj2 * 16, -2);
        const TileMesh edge = build_edit_patch(t, layout, e2, ci2, cj2);
        int inset = 0;
        for (size_t k = 0; k < edge.positions.size() / 3; k++) {
            inset += edge.normals[k * 3 + 1] == 0.0f && std::abs(edge.positions[k * 3] - 0.01f) < 1e-4f ? 1 : 0;
        }
        check(inset == 2 * 4, "叶块边上的坎往坑里让 1 cm（" + std::to_string(inset) + " 个顶点）");
    }
    // 两块都有改动：块边上的坎只出一次（归高的那格）
    {
        const int gi3 = ci * 16 + 8, gj3 = cj * 16 + 15; // 块东边那一列
        const TerrainEdits e3 = TerrainEdits(eg).with_step(gi3, gj3, 2).with_step(gi3, gj3 + 1, -2);
        const TileMesh left = build_edit_patch(t, layout, e3, ci, cj), right = build_edit_patch(t, layout, e3, ci, cj + 1);
        // 立在块边那条线上的竖面（法线沿 x；与块边垂直的坎有一头也在这条线上，不算）
        auto walls_on = [&](const TileMesh &m, float p_x) {
            int n = 0;
            for (size_t k = 0; k < m.positions.size() / 3; k++) {
                n += m.normals[k * 3 + 1] == 0.0f && m.normals[k * 3] != 0.0f && std::abs(m.positions[k * 3] - p_x) < 1e-3f ? 1 : 0;
            }
            return n;
        };
        const float border = float(eg.cell_x(gj3 + 1) - ox);
        check(walls_on(left, border) == 4 * 4 && walls_on(right, border) == 0, "块边上高 +2、低 −2：四级的坎全归高的那块（" + std::to_string(walls_on(left, border) / 4) + " / " +
                std::to_string(walls_on(right, border) / 4) + "）");
    }
    // 土层：可耕地两米、林地两米
    check_near(soil_depth_at(t, 0.0, 0.0), 2.0, 1e-9, "可耕地的土层 2 m");

    // 地形取样叠上改动（玩法问 surface），原地形不变（散布、规划问 surface_natural）
    WorldTerrain t2(g, { synthetic_island() }, 7);
    const std::shared_ptr<const TerrainEdits> shared = std::make_shared<const TerrainEdits>(e);
    t2.set_edits(shared);
    const double px = eg.cell_x(gj) + 0.4, pz = eg.cell_z(gi) + 0.7;
    double y_edit = 0.0, y_nat = 0.0, y_far = 0.0, y_far_nat = 0.0;
    t2.surface(px, pz, y_edit);
    t2.surface_natural(px, pz, y_nat);
    t2.surface(px + 300.0, pz, y_far);
    t2.surface_natural(px + 300.0, pz, y_far_nat);
    check_near(y_edit - y_nat, -2.0, 1e-9, "surface 叠上改动：坑里低 2 m");
    check(y_far == y_far_nat, "框外的点不受影响");
    t2.set_edits(nullptr);
    t2.surface(px, pz, y_edit);
    check(y_edit == y_nat && !t2.get_edits(), "换成没有改动：回到原地形");
    // 线程安全：后台线程一直量、主线程一直换
    {
        t2.set_edits(shared);
        std::atomic<bool> stop{ false };
        std::atomic<int> bad{ 0 };
        std::thread reader([&]() {
            while (!stop.load()) {
                double y = 0.0;
                t2.surface(px, pz, y);
                const double d = y - y_nat;
                if (std::abs(d) > 1e-9 && std::abs(d + 2.0) > 1e-9 && std::abs(d + 1.5) > 1e-9) {
                    bad++;
                }
            }
        });
        const std::shared_ptr<const TerrainEdits> other = std::make_shared<const TerrainEdits>(e.with_step(gi, gj, -3));
        for (int k = 0; k < 2000; k++) {
            t2.set_edits(k % 3 == 0 ? nullptr : k % 3 == 1 ? shared : other);
        }
        stop = true;
        reader.join();
        check(bad == 0, "边换边量：只看得到完整的某一份");
    }
}

void test_earthworks() {
    std::printf("earthworks\n");
    EarthFacts f;
    f.solid = true;
    f.shore = 50.0;
    f.soil_depth = 1.0;
    EarthCheck c = plan_earthwork(f, 0, EarthAction::DIG, "wood_shovel", false);
    check(c.ok && c.after == -1 && c.item == "earth" && !c.stone && c.seconds == EarthRules().shovel_s, "木锹挖土层：出土");
    c = plan_earthwork(f, -2, EarthAction::DIG, "wood_shovel", false);
    check(!c.ok && c.why.find("石镐") != std::string::npos, "挖到土层以下：木锹挖不动");
    c = plan_earthwork(f, -2, EarthAction::DIG, "stone_pick", false);
    check(c.ok && c.stone && c.item == "stone" && c.seconds == EarthRules().pick_stone_s, "石镐凿石：出石料");
    c = plan_earthwork(f, 3, EarthAction::DIG, "wood_shovel", false);
    check(c.ok && c.item == "earth" && !c.stone, "垒上来的土挖回去出土");
    c = plan_earthwork(f, 0, EarthAction::DIG, "", false);
    check(!c.ok, "空手挖不动");
    c = plan_earthwork(f, -2, EarthAction::DIG, "", true);
    check(c.ok && c.seconds == EarthRules().free_s, "god 模式不要家伙");
    c = plan_earthwork(f, -16, EarthAction::DIG, "stone_pick", false);
    check(!c.ok && c.why.find("8") != std::string::npos, "最深 8 m");
    c = plan_earthwork(f, 0, EarthAction::RAISE, "earth", false);
    check(c.ok && c.after == 1 && c.item == "earth" && c.seconds == 0.0, "拿着土垒一级");
    c = plan_earthwork(f, 0, EarthAction::RAISE, "wood_shovel", false);
    check(!c.ok, "手上没土垒不了");
    c = plan_earthwork(f, 8, EarthAction::RAISE, "earth", false);
    check(!c.ok, "最高 4 m");
    f.under_feet = true;
    check(!plan_earthwork(f, 0, EarthAction::RAISE, "earth", false).ok && plan_earthwork(f, 0, EarthAction::DIG, "wood_shovel", false).ok, "脚下垒不了、挖得了");
    f.under_feet = false;
    f.blocked = "这是田";
    c = plan_earthwork(f, 0, EarthAction::DIG, "wood_shovel", false);
    check(!c.ok && c.why == "这是田", "压着东西：说压着什么");
    f.blocked.clear();
    f.shore = 2.0;
    check(!plan_earthwork(f, 0, EarthAction::DIG, "wood_shovel", false).ok, "离岸太近");
    f.shore = 50.0;
    f.solid = false;
    check(!plan_earthwork(f, 0, EarthAction::DIG, "wood_shovel", false).ok, "崖边");
    check(earth_digs_with("wood_shovel") && earth_digs_with("stone_pick") && !earth_digs_with("stone_axe") && earth_raises_with("earth"), "挖的家伙、垒的土");
}

namespace {

// 搭建件测试用的地面：斜坡 y = 10 + 0.05·x，x > 40 是水，x < −40 没有地
class SlopeGround : public LandingGround {
public:
    double tilt = 0.05;
    bool height(double p_x, double p_z, double &r_y) const override {
        r_y = 10.0 + tilt * p_x;
        return p_x > -40.0;
    }
    bool water(double p_x, double p_z) const override { return p_x > 40.0; }
};

// 准星对准一座里某格（局部）的某一点
PieceAim aim_local(const Structure &p_s, double p_lx, double p_ly, double p_lz, int p_turns = 0) {
    PieceAim a;
    p_s.to_host(p_lx, p_ly, p_lz, a.x, a.y, a.z);
    a.from_x = a.x + 1.5;
    a.from_z = a.z + 1.5;
    a.from_y = a.y;
    a.look_yaw = 0.3;
    a.turns = p_turns;
    return a;
}

bool place(Structures &r_list, const std::string &p_item, const PieceAim &p_aim, const LandingGround &p_ground, PiecePlan *r_plan = nullptr) {
    const PiecePlan plan = plan_piece(r_list, *pieces::find(p_item), p_aim, p_ground);
    if (r_plan) {
        *r_plan = plan;
    }
    if (plan.ok) {
        r_list.add(p_item, plan);
    }
    return plan.ok;
}

} // namespace

void test_build_pieces() {
    std::printf("build_pieces\n");
    const SlopeGround ground;
    Structures list;
    check(pieces::COUNT == 11 && pieces::find("earth_wall") && pieces::find("tile_roof")->role == PieceRole::ROOF && !pieces::find("plank"), "搭建件表");
    for (int k = 0; k < pieces::COUNT; k++) {
        check(catalog::item(pieces::DEFS[k].item) != nullptr, std::string("搭建件是物品：") + pieces::DEFS[k].item);
        bool craftable = false;
        for (int r = 0; r < recipes::RECIPE_COUNT; r++) {
            craftable = craftable || std::string(recipes::RECIPES[r].product) == pieces::DEFS[k].item;
        }
        check(craftable, std::string("搭建件有配方：") + pieces::DEFS[k].item);
    }

    // 新开一座：地坪 = 格里最高的地面 + 0.2 m，朝向按视线取整到 15°
    PieceAim first;
    first.x = 5.0, first.y = 10.25, first.z = 3.0;
    first.from_x = 7.0, first.from_z = 6.0;
    first.look_yaw = 0.3; // 17° → 15°
    PiecePlan plan;
    check(place(list, "foundation", first, ground, &plan) && plan.structure == -1, "对准地面摆地基：新开一座");
    const Structure *s = list.find(list.get()[0].id);
    check_near(s->yaw, 15.0 * 3.14159265358979323846 / 180.0, 1e-9, "朝向取整到 15°");
    double hi = -1e9, lo = 1e9;
    for (double u : { 0.05, 1.0, 1.95 }) {
        for (double v : { 0.05, 1.0, 1.95 }) {
            double hx, hy, hz;
            s->to_host(u, 0.0, v, hx, hy, hz);
            hi = std::max(hi, 10.0 + 0.05 * hx);
            lo = std::min(lo, 10.0 + 0.05 * hx);
        }
    }
    check_near(s->y, hi + PieceRules().min_plinth_m, 1e-9, "地坪 = 最高的地面 + 露出的一截");
    check_near(s->pieces[0].bottom, lo - s->y - 0.3, 1e-9, "台基垫到最低处再埋 0.3 m");
    double cx, cy, cz;
    s->to_host(1.0, 0.0, 1.0, cx, cy, cz);
    check_near(cx, 5.0, 1e-9, "格 (0, 0) 的中心落在准星上");
    // 局部 ↔ 宿主往返
    double lx, ly, lz, hx, hy, hz;
    s->to_host(3.3, 1.2, -0.7, hx, hy, hz);
    s->to_local(hx, hy, hz, lx, ly, lz);
    check_near(lx, 3.3, 1e-9, "局部 ↔ 宿主往返 x");
    check_near(lz, -0.7, 1e-9, "局部 ↔ 宿主往返 z");

    // 接着铺成 2 × 2：对准旁边那格的地面
    Structure frame = *s;
    check(place(list, "foundation", aim_local(frame, 3.0, -0.3, 1.0), ground), "对准旁边的地面：接上一块地基");
    check(place(list, "foundation", aim_local(frame, 1.0, -0.3, 3.0), ground) && place(list, "foundation", aim_local(frame, 3.0, -0.3, 3.0), ground), "铺满 2 × 2");
    check(list.size() == 1 && list.get()[0].pieces.size() == 4, "还是同一座、四块地基");
    // 准星落在隔一格的地上：吸到挨着这一座的那一格（只看计划，不摆）
    plan = plan_piece(list, *pieces::find("foundation"), aim_local(frame, 7.0, -0.3, 1.0), ground);
    check(plan.ok && plan.structure == 0 && plan.spot.x == 2 && plan.spot.z == 0, "准星隔一格：吸到挨着的 (2, 0)");
    // 离这一座太近、又接不上：不另开一座
    {
        PieceAim a = aim_local(frame, -1.2, -0.3, 5.1);
        plan = plan_piece(list, *pieces::find("foundation"), a, ground);
        check(!plan.ok || plan.structure == 0, "挨着已有的一座不另开（" + plan.why + "）");
    }
    // 高处：地面高过地坪
    {
        SlopeGround steep;
        steep.tilt = 2.0;
        Structures other;
        PieceAim a = first;
        const bool ok = place(other, "foundation", a, steep, &plan);
        check(!ok && plan.why.find("陡") != std::string::npos, "太陡：开不了（" + plan.why + "）");
    }

    // 墙：准星在地基面上离哪条边近就是哪条边
    frame = list.get()[0];
    check(place(list, "earth_wall", aim_local(frame, 1.0, 0.0, 0.15), ground, &plan) && plan.spot.slot == PieceSlot::EDGE && plan.spot.dir == 0 && plan.spot.x == 0 &&
                    plan.spot.z == 0 && plan.spot.level == 0,
            "准星靠北边：墙在格 (0, 0) 北边");
    plan = plan_piece(list, *pieces::find("earth_wall"), aim_local(list.get()[0], 1.0, 0.0, 0.15), ground);
    check(!(plan.ok && plan.spot.dir == 0 && plan.spot.x == 0 && plan.spot.z == 0), "同一条边不再摆第二面（换到别的边或不让摆）");
    // 围一圈：北 2、南 2、西 2、东 2（南边中间一扇门）
    const struct {
        const char *item;
        double x, z;
    } ring[] = { { "earth_wall", 3.0, 0.15 }, { "earth_wall", 0.15, 1.0 }, { "earth_wall", 0.15, 3.0 }, { "earth_wall", 3.85, 1.0 }, { "window_wall", 3.85, 3.0 },
        { "earth_wall", 1.0, 3.85 }, { "door_wall", 3.0, 3.85 } };
    for (const auto &w : ring) {
        check(place(list, w.item, aim_local(list.get()[0], w.x, 0.3, w.z), ground), std::string("围墙：") + w.item);
    }
    HouseReport house = check_house(list.get()[0]);
    // 门墙在 0 层：门外铺台阶下到地面（台基至少露出 0.45 m，人不会自己上坎）
    {
        const Piece *door_piece = nullptr;
        for (const Piece &p : list.get()[0].pieces) {
            door_piece = p.item == "door_wall" ? &p : door_piece;
        }
        check(door_piece && door_piece->bottom < -0.25 && door_outside(list.get()[0], door_piece->spot) == 1, "门墙记下门外地面（比地坪低、在南边）");
        Structure no_ramp = list.get()[0];
        for (Piece &p : no_ramp.pieces) {
            p.bottom = p.item == "door_wall" ? 0.0 : p.bottom;
        }
        check(build_structure_mesh(list.get()[0]).collision.size() == build_structure_mesh(no_ramp).collision.size() + 12 * 9, "门外多一块斜板的碰撞（12 个面）");
    }
    check(!house.complete && house.rooms.size() >= 1 && house.rooms[0].enclosed && house.rooms[0].doors == 1 && house.rooms[0].missing_roof == 4,
            "围好了还没顶：不完整（缺 " + house.missing + "）");
    // 顶：对准墙顶，默认往外低：北两格往北低、南两格往南低
    frame = list.get()[0];
    const struct {
        double x, z;
        int dir;
    } roofs[] = { { 1.0, 0.1, 2 }, { 3.0, 0.1, 2 }, { 1.0, 3.9, 0 }, { 3.0, 3.9, 0 } };
    for (const auto &r : roofs) {
        const bool ok = place(list, "thatch_roof", aim_local(frame, r.x, 3.0, r.z), ground, &plan);
        check(ok && plan.spot.tier == 0 && plan.spot.dir == r.dir, "坡顶：对准墙顶放在里面那格、往外低（dir " + std::to_string(plan.spot.dir) + "）");
    }
    house = check_house(list.get()[0]);
    check(!house.complete && house.rooms[0].missing_roof == 0 && house.rooms[0].missing_gables == 4 && house.rooms[0].roof_gaps == 0,
            "两坡顶在屋脊接上；两头山面各缺两片山墙（缺 " + house.missing + "）");
    // 山墙：对准两山墙顶上
    const double gables[4][2] = { { 0.1, 1.0 }, { 0.1, 3.0 }, { 3.9, 1.0 }, { 3.9, 3.0 } };
    for (const auto &g : gables) {
        const bool ok = place(list, "gable", aim_local(list.get()[0], g[0], 3.4, g[1]), ground, &plan);
        check(ok, "山墙（" + plan.why + "）");
    }
    house = check_house(list.get()[0]);
    check(house.complete && house.complete_rooms == 1 && house.missing.empty(), "四面墙、门、顶、山墙：一座完整的屋");
    check(inside_complete_room(list.get()[0], house, 2.0, 0.9, 2.0) && !inside_complete_room(list.get()[0], house, 5.0, 0.9, 2.0), "屋里算、屋外不算");

    // 拆：山墙靠着墙，墙拆不了；先拆山墙再拆墙，屋就不完整了
    const Structure &built = list.get()[0];
    int west_wall = -1, west_gable = -1, door = -1;
    for (const Piece &p : built.pieces) {
        const PieceDef *def = pieces::find(p.item);
        if (p.spot.slot == PieceSlot::EDGE && p.spot.dir == 1 && p.spot.x == 0 && p.spot.z == 0) {
            (def->role == PieceRole::GABLE ? west_gable : west_wall) = p.id;
        }
        door = def->role == PieceRole::DOOR ? p.id : door;
    }
    std::string why;
    const int sid = built.id;
    check(!list.remove(sid, west_wall, why) && why.find("山墙") != std::string::npos, "山墙靠着的墙拆不了（" + why + "）");
    check(list.remove(sid, west_gable, why) && list.remove(sid, west_wall, why), "先拆山墙再拆墙");
    house = check_house(*list.find(sid));
    check(!house.complete && house.rooms[0].missing_walls == 1, "少一面墙：不完整（缺 " + house.missing + "）");
    // 墙装回去、山墙装回去：又完整了
    check(place(list, "earth_wall", aim_local(*list.find(sid), 0.15, 0.3, 1.0), ground) && place(list, "gable", aim_local(*list.find(sid), 0.1, 3.4, 1.0), ground), "装回去");
    check(check_house(*list.find(sid)).complete, "又是一座完整的屋");

    // 门开关：碰撞跟着变
    const StructureMesh closed = build_structure_mesh(*list.find(sid));
    check(list.set_open(sid, door, 1), "开门");
    const StructureMesh opened = build_structure_mesh(*list.find(sid));
    check(closed.triangle_count() > 200 && !closed.collision.empty() && closed.collision != opened.collision, "门扇开着关着碰撞不一样");
    // 点选：墙面上的一点落在那面墙上
    {
        const Structure &cur = *list.find(sid);
        const int hit = piece_at(cur, 2.0 + 1.0, 1.5, 0.12);
        const Piece *p = cur.find(hit);
        check(p && pieces::find(p->item)->role == PieceRole::WALL && p->spot.dir == 0 && p->spot.z == 0, "点选：北墙外皮上的一点是北墙");
    }

    // 二层：楼板搭在墙上；梯子在一格里，那一格楼上留梯口
    {
        Structures two = list;
        Structure &cur = *two.edit(sid);
        cur.pieces.erase(std::remove_if(cur.pieces.begin(), cur.pieces.end(), [](const Piece &p) { return pieces::find(p.item)->role == PieceRole::ROOF || pieces::find(p.item)->role == PieceRole::GABLE; }), cur.pieces.end());
        two.touch();
        const bool ladder_ok = place(two, "ladder", aim_local(*two.find(sid), 1.0, 0.1, 0.4), ground, &plan);
        check(ladder_ok && plan.spot.level == 0 && plan.spot.dir == 2, "梯子靠着北墙（dir " + std::to_string(plan.spot.dir) + "）");
        Piece hatch;
        hatch.item = "floorboard";
        hatch.spot.slot = PieceSlot::CELL;
        hatch.spot.x = 0, hatch.spot.z = 0, hatch.spot.level = 1;
        check(!piece_supported(*two.find(sid), hatch), "梯子上面那格不许铺楼板（梯口）");
        plan = plan_piece(two, *pieces::find("floorboard"), aim_local(*two.find(sid), 1.0, 3.0, 1.0), two.find(sid) ? ground : ground);
        check(!(plan.ok && plan.spot.x == 0 && plan.spot.z == 0), "对准梯口铺楼板：不铺在梯口上");
        check(place(two, "floorboard", aim_local(*two.find(sid), 3.0, 3.0, 1.0), ground, &plan) && plan.spot.level == 1 && plan.spot.x == 1, "楼板搭在墙上");
        const StructureMesh m = build_structure_mesh(*two.find(sid));
        check(m.ladders.size() == 1 && m.ladders[0].top_y - m.ladders[0].foot_y == piece_dims::STOREY_M, "梯子一个爬梯区域、爬一层");
    }

    // 整座样式：存成样式、再摆一座，一样完整
    const StructureStyle style = style_from_structure(*list.find(sid), "test", "测试屋");
    StructureStyle back;
    back.id = "test";
    std::string err;
    check(parse_structure_style(save_structure_style(style), back, err) && back.pieces.size() == style.pieces.size() && back.name == "测试屋", "样式的文本往返：" + err);
    const auto bill = style_bill(style);
    int walls = 0;
    for (const auto &b : bill) {
        walls += b.first == "earth_wall" ? b.second : 0;
    }
    check(walls == 6, "样式要几件：夯土墙 6（" + std::to_string(walls) + "）");
    PieceAim far = first;
    far.x += 20.0;
    far.from_x += 20.0;
    const StylePlan sp = plan_structure_style(list, back, far, ground);
    const bool style_complete = sp.ok && check_house(sp.structure).complete;
    check(style_complete, "照样式再摆一座：完整（" + sp.why + "）");
    PieceAim near = first;
    check(!plan_structure_style(list, back, near, ground).ok, "压着已有的一座：摆不了");
    const int second = list.add_structure(sp.structure);
    check(list.size() == 2 && list.find(second)->pieces.size() == style.pieces.size(), "摆下一整座：座与件重新编号");

    // 营造账的文本往返：改地形、两座搭建件、一只装了东西的木箱
    GroundWorks works;
    EditGrid eg;
    eg.x0 = -512.0, eg.z0 = 256.5;
    works.edits = std::make_shared<const TerrainEdits>(TerrainEdits(eg).with_step(3, -4, -5).with_step(40, 40, 2));
    works.structures = list;
    PlaceCheck pc;
    pc.ok = true;
    pc.x = 1.25, pc.y = 10.5, pc.z = -3.5, pc.low = 10.25, pc.quarter = 3;
    const int chest = works.props.add("chest", pc);
    works.props.find(chest)->contents.set(4, "stone", 12);
    works.props.find(chest)->contents.set(17, "earth", 3);
    const std::string text = save_ground_works(works);
    GroundWorks read;
    const bool read_ok = load_ground_works(text, read, err);
    check(read_ok, "营造账读得回：" + err);
    if (!read_ok) {
        return;
    }
    check(save_ground_works(read) == text, "再存一遍一字不差");
    check(read.edits && read.edits->step_at(3, -4) == -5 && read.structures.piece_count() == list.piece_count() && read.props.size() == 1 &&
                    read.props.get_props()[0].contents.count("stone") == 12 && read.props.get_props()[0].quarter == 3,
            "改地形、搭建件、木箱里的东西都在");
    check(check_house(*read.structures.find(sid)).complete, "读回来的那座还是完整的屋");
    check(!load_ground_works("piece\t9\t1\tearth_wall\t0\t0\t0\t0\t0\t0\t0\n", read, err), "件的座没出现：拒收");
}

void test_structure_styles_repo() {
    std::printf("structure_styles（仓库里的样式）\n");
    const std::string dir = std::string(ZHOUZHU_SOURCE_DIR) + "/game/structures";
    int n = 0;
    const SlopeGround ground;
    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".txt") {
            continue;
        }
        std::ifstream f(entry.path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        StructureStyle style;
        style.id = entry.path().stem().string();
        std::string err;
        check(parse_structure_style(text, style, err), "样式读得了：" + style.id + " " + err);
        PieceAim a;
        a.x = 0.0, a.y = 10.0, a.z = 0.0, a.from_x = 2.0, a.from_z = 2.0;
        const StylePlan plan = plan_structure_style(Structures(), style, a, ground);
        check(plan.ok, "样式摆得下：" + style.id + " " + plan.why);
        const HouseReport house = check_house(plan.structure);
        check(house.complete, "样式是一座完整的屋：" + style.id + "（缺 " + house.missing + "）");
        n++;
    }
    check(n >= 2, "仓库里至少两套样式（" + std::to_string(n) + "）");
}

void test_god_works() {
    std::printf("god_works\n");
    EditGrid g;
    const TerrainEdits empty(g);
    const TerrainEdits one = empty.with_step(3, 4, -2).with_step(3, 20, 1);
    std::vector<EditCellStep> cells;
    for (int i = 0; i < 5; i++) {
        for (int j = 14; j < 19; j++) {
            cells.push_back({ i, j, -1 }); // 跨两块
        }
    }
    cells.push_back({ 3, 20, 0 }); // 改回原样：那一格
    const TerrainEdits many = one.with_steps(cells);
    check(one.step_at(0, 14) == 0 && one.step_at(3, 20) == 1, "批量改不动原来那份");
    check(many.step_at(0, 14) == -1 && many.step_at(4, 18) == -1 && many.step_at(3, 20) == 0 && many.step_at(3, 4) == -2, "批量改：25 格一级、改回的那格为 0、别处照旧");
    check(many.cell_count() == 26 && many.chunk_count() == 2, "两块、26 格改过");
    check(many.get_revision() == one.get_revision() + 26, "版本按真改了的格数加");
    TerrainEdits serial = one;
    for (const EditCellStep &c : cells) {
        serial = serial.with_step(c.i, c.j, c.step);
    }
    bool same = true;
    for (int i = -2; i < 8 && same; i++) {
        for (int j = 0; j < 40 && same; j++) {
            same = serial.step_at(i, j) == many.step_at(i, j);
        }
    }
    check(same && serial.chunk_count() == many.chunk_count() && serial.near_edits(g.cell_x(18) + 0.5, g.cell_z(4) + 0.5) == many.near_edits(g.cell_x(18) + 0.5, g.cell_z(4) + 0.5),
            "与逐格改一样");
    check(many.with_steps(cells).get_revision() == many.get_revision(), "一格没变就原样返回");
    const TerrainEdits back = many.with_steps({ { 3, 4, 0 }, { 3, 4, 5 }, { 3, 4, 0 } });
    check(back.step_at(3, 4) == 0, "同一格写几回取最后一回");
    std::vector<EditCellStep> undo;
    for (const EditCellStep &c : cells) {
        undo.push_back({ c.i, c.j, 0 });
    }
    undo.push_back({ 3, 4, 0 });
    check(many.with_steps(undo).empty(), "全改回 0：块都删掉");

    const std::vector<GodTool> tools = god_tool_list(2);
    check(int(tools.size()) == 2 + pieces::COUNT + 2, "工具：挖、垒、十一种件、两套样式");
    check(tools[0].kind == GodToolKind::DIG && tools[1].kind == GodToolKind::RAISE && tools[2].kind == GodToolKind::PIECE && tools.back().kind == GodToolKind::STYLE && tools.back().style == 1,
            "工具的次序");
    check(earth_digs_with(god_tool_held(tools[0])) && earth_raises_with(god_tool_held(tools[1])) && pieces::is_piece(god_tool_held(tools[2])) && god_tool_held(tools.back()).empty(),
            "工具当成手上拿着的物品");
    check(god_tool_brushes(tools[0]) && god_tool_brushes(tools[1]) && !god_tool_brushes(tools[2]), "挖垒才有笔刷");
    EarthFacts ok_facts;
    ok_facts.solid = true;
    ok_facts.shore = 50.0;
    ok_facts.soil_depth = 0.5;
    const EarthCheck stone = plan_earthwork(ok_facts, -1, EarthAction::DIG, god_tool_held(tools[0]), true);
    check(stone.ok && stone.stone && stone.item == "stone", "god 的挖：土层以下照样挖得动");

    check(god_brush_step(1, 1) == 3 && god_brush_step(5, 1) == 9 && god_brush_step(9, 1) == 9 && god_brush_step(1, -1) == 1 && god_brush_step(4, 0) == 3, "笔刷换档到头不绕");
    const std::vector<std::pair<int, int>> b1 = god_brush_cells(-3, 7, 1);
    check(b1.size() == 1 && b1[0] == std::make_pair(-3, 7), "1 × 1 就是那一格");
    const std::vector<std::pair<int, int>> b5 = god_brush_cells(10, -2, 5);
    bool inside = b5.size() == 25 && b5[0] == std::make_pair(10, -2);
    for (size_t k = 0; k < b5.size() && inside; k++) {
        inside = std::abs(b5[k].first - 10) <= 2 && std::abs(b5[k].second + 2) <= 2;
        const auto d = [](const std::pair<int, int> &c) { return (c.first - 10) * (c.first - 10) + (c.second + 2) * (c.second + 2); };
        inside = inside && (k == 0 || d(b5[k - 1]) <= d(b5[k]));
    }
    std::vector<std::pair<int, int>> sorted = b5;
    std::sort(sorted.begin(), sorted.end());
    check(inside && std::unique(sorted.begin(), sorted.end()) == sorted.end(), "5 × 5：心在最前、由近到远、不重复");
    check(god_brush_cells(0, 0, 9).size() == 81, "9 × 9 八十一格");
}

void test_dev_manual() {
    std::printf("dev_manual\n");
    // input_actions.h 里注释写着「开发者：」的动作，说明里都得有一条
    std::ifstream in(std::string(ZHOUZHU_SOURCE_DIR) + "/src/input_actions.h", std::ios::binary);
    check(bool(in), "读得到 src/input_actions.h");
    std::string line;
    int dev_actions = 0;
    std::string missing;
    while (std::getline(in, line)) {
        if (line.find("// 开发者：") == std::string::npos) {
            continue;
        }
        const size_t q0 = line.find('"');
        const size_t q1 = q0 == std::string::npos ? q0 : line.find('"', q0 + 1);
        if (q1 == std::string::npos) {
            continue;
        }
        const std::string action = line.substr(q0 + 1, q1 - q0 - 1);
        dev_actions++;
        if (!dev_manual_covers(action)) {
            missing += " " + action;
        }
    }
    check(dev_actions >= 8, "input_actions.h 里至少八个开发者动作（" + std::to_string(dev_actions) + "）");
    check(missing.empty(), "每个开发者动作都有说明，缺：" + missing);
    std::set<std::string> keys, titles;
    bool filled = true;
    for (const DevManualEntry &e : dev_manual()) {
        filled = filled && std::string(e.keys).size() > 0 && std::string(e.title).size() > 0 && std::string(e.where).size() > 0 && !e.lines.empty();
        for (const std::string &l : e.lines) {
            filled = filled && !l.empty();
        }
        keys.insert(e.keys);
        titles.insert(e.title);
    }
    check(filled, "每条都有键、名字、在哪儿、说明");
    check(keys.size() == dev_manual().size() && titles.size() == dev_manual().size(), "键、名字不重复");
    check(!dev_manual_intro().empty() && dev_manual_covers("god_mode") && !dev_manual_covers("interact"), "开头几行；查得到 god、不认玩法动作");
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetErrorMode(0x0001 | 0x0002); // fail critical errors and report crashes without a JIT dialog
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // 只跑几段（开发时用）：zhouzhu_core_tests inflate weather_pack planet_weather
    if (argc > 1) {
        const std::map<std::string, void (*)()> only = { { "inflate", test_inflate }, { "weather_pack", test_weather_pack }, { "planet_weather", test_planet_weather },
            { "weather_model", test_weather_model }, { "group_weather", test_group_weather }, { "sea_descent", test_sea_descent },
            { "gen_format", test_gen_format }, { "gen_seed42", test_gen_seed42 }, { "island_floor", test_island_floor },
            { "terrain_edits", test_terrain_edits }, { "edit_patch", test_edit_patch }, { "earthworks", test_earthworks }, { "build_pieces", test_build_pieces },
            { "structure_styles", test_structure_styles_repo }, { "god_works", test_god_works }, { "dev_manual", test_dev_manual },
            // 读开局那群的几段（换开局、重导时逐段重记金标）
            { "almanac", test_almanac }, { "climate", test_climate }, { "local_season", test_local_season }, { "village_seasons", test_village_seasons },
            { "world_start", test_world_start }, { "village_prep", test_village_prep_start }, { "terrain_v3", test_terrain_v3_start },
            { "world_sites", test_world_sites }, { "world_rivers", test_world_rivers }, { "river_contract", test_river_contract }, { "map_grounds", test_map_grounds }, { "freight", test_freight },
            { "gather", test_gather }, { "scatter", test_scatter }, { "goldens", test_goldens }, { "planet_data", test_planet_data },
            { "planet_add_lods", test_planet_add_lods }, { "planet_frame", test_planet_frame }, { "far_group_mesh", test_far_group_mesh },
            { "planet_surface", test_planet_surface } };
        for (int k = 1; k < argc; k++) {
            const auto it = only.find(argv[k]);
            if (it != only.end()) {
                it->second();
            }
        }
        std::printf("%d 项检查，%d 项失败\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }
    test_rng();
    test_almanac();
    test_weather_noise();
    test_climate();
    test_weather_model();
    test_local_season();
    test_job_pool();
    test_resume_clock();
    test_group_weather();
    test_stream_meter();
    test_building_mesh();
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
    test_refit_pick();
    test_ship_cabins();
    test_inventory();
    test_crops();
    test_farm();
    test_flight();
    test_settlement();
    test_villagers();
    test_village_sim();
    test_village_seasons();
    test_world_grid();
    test_world_terrain();
    test_cover_blend();
    test_terrain_tiles();
    test_map_image();
    test_world_start();
    test_village_prep_start();
    test_terrain_v3_start();
    test_dock_site();
    test_village_plan();
    test_world_sites();
    test_world_rivers();
    test_river_contract();
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
    test_terrain_edits();
    test_god_works();
    test_dev_manual();
    test_edit_patch();
    test_earthworks();
    test_build_pieces();
    test_structure_styles_repo();
    test_goldens();
    test_planet_data();
    test_planet_add_lods();
    test_group_memory();
    test_planet_frame();
    test_far_group_mesh();
    test_island_floor();
    test_planet_squeeze_height();
    test_planet_float_orbit();
    test_planet_sky();
    test_planet_surface();
    test_inflate();
    test_weather_pack();
    test_planet_weather();
    test_sea_descent();
    test_gen_format();
    test_gen_seed42();
    std::printf("%d 项检查，%d 项失败\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
