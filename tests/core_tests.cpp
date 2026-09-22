// 纯算法文件（不含 Godot 头的那几个）的单元测试。不引测试框架：一个可执行文件，失败即非零退出。
//
//   cmake --build build --config Debug --target zhouzhu_core_tests
//   ctest --test-dir build -C Debug --output-on-failure

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "crops.h"
#include "farm_model.h"
#include "flight_model.h"
#include "inventory.h"
#include "island_field.h"
#include "settlement_model.h"
#include "ship_layout.h"
#include "value_noise.h"

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

    const FarmSurvey survey = survey_farmland(f, p.dock_apron_m, FarmRules());
    int on_trail = 0;
    for (uint8_t a : survey.arability) {
        on_trail += Arability(a) == Arability::TRAIL ? 1 : 0;
    }
    check(on_trail > 0 && survey.arable_count() >= 60, "路上不能垦，路外的河滩照样能垦");
}

void test_farm() {
    std::printf("farm_model\n");
    const IslandParams p = river_island_params(0.8);
    const IslandField f = generate_island_field(p);
    FarmRules rules;
    const FarmSurvey survey = survey_farmland(f, p.dock_apron_m, rules);

    // 勘察
    int water = 0, dry = 0;
    bool arable_ok = true;
    int near_i = -1, near_j = -1, far_i = -1, far_j = -1, dry_i = -1, dry_j = -1;
    for (int i = 0; i < f.n - 1; i++) {
        for (int j = 0; j < f.n - 1; j++) {
            const Arability a = survey.at(i, j);
            water += a == Arability::WATER ? 1 : 0;
            dry += a == Arability::TOO_DRY ? 1 : 0;
            if (a == Arability::TOO_DRY) {
                dry_i = i;
                dry_j = j;
            }
            if (a != Arability::ARABLE) {
                continue;
            }
            const float m = survey.moisture[survey.index(i, j)];
            arable_ok = arable_ok && f.quad_solid(i, j) && f.river_depth[f.index(i, j)] == 0.0f && m >= 0.0f && m <= 1.0f;
            if (near_i < 0 || m > survey.moisture[survey.index(near_i, near_j)]) {
                near_i = i;
                near_j = j;
            }
            if (far_i < 0 || m < survey.moisture[survey.index(far_i, far_j)]) {
                far_i = i;
                far_j = j;
            }
        }
    }
    check(survey.arable_count() >= 60, "河边有成片可垦的地（" + std::to_string(survey.arable_count()) + " 格）");
    check(water > 0 && dry > survey.arable_count(), "河里不能垦，离河远的地占多数");
    check(arable_ok, "可垦的格子都有地面、不在水里");
    check(survey.at(-1, 0) == Arability::NO_GROUND && survey.at(0, f.n - 1) == Arability::NO_GROUND, "界外算没有地面");
    IslandParams no_river = p;
    no_river.river = false;
    check(survey_farmland(generate_island_field(no_river), p.dock_apron_m, rules).arable_count() == 0, "没有河的岛无地可垦");
    if (near_i < 0 || dry_i < 0) {
        return;
    }

    // 地块状态机
    const CropDef *millet = catalog::crop("millet");
    const CropDef *bean = catalog::crop("bean");
    FarmModel farm(survey, rules);
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
    const double near_m = survey.moisture[survey.index(near_i, near_j)], far_m = survey.moisture[survey.index(far_i, far_j)];
    check_near(near_ripe_at, rules.grow_seconds / (rules.dry_growth + (1 - rules.dry_growth) * near_m), 0.1, "河边的地按墒情算的时间成熟");
    check_near(far_ripe_at, rules.grow_seconds / (rules.dry_growth + (1 - rules.dry_growth) * far_m), 0.1, "远处的地也是");
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
    const IslandYard &y = f.yard;
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
    check(buildings::COUNT == 4 && &buildings::at(0) == &house, "表里四种");

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
    SettlementLayout layout;
    // 停机坪：岸缘点周围几米是掩膜的锯齿边，往岛内退 0.7 个坪的半径（14 m）
    const double apron_x = f.dock.x - 0.7 * p.dock_apron_m * f.dock.out_x, apron_z = f.dock.z - 0.7 * p.dock_apron_m * f.dock.out_z;
    check(survey_site(f, p.dock_apron_m, house, apron_x, apron_z, 0.0, layout, rules).reason == "停机坪上不能盖", "停机坪被拒");
    check(survey_site(f, p.dock_apron_m, house, 2.0 * f.half_extent(), 0.0, 0.0, layout, rules).reason.find("没有地面") != std::string::npos, "岛外被拒");
    const RiverPoint &rp = f.river[f.river.size() / 2];
    check(survey_site(f, p.dock_apron_m, house, rp.x, rp.z, 0.0, layout, rules).reason == "这是河，盖不了", "河里被拒");
    check(f.yard.present && survey_site(f, p.dock_apron_m, house, f.yard.x, f.yard.z, 0.0, layout, rules).reason == "这是船坞的坪，盖不得", "船坞坪被拒");
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
                const std::string why = survey_site(f, p.dock_apron_m, house, t.x, t.z, 0.0, layout, rules).reason;
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
            const SiteReport r = survey_site(f, p.dock_apron_m, house, f.coord(j), f.coord(i), 0.0, layout, rules);
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
    const SiteReport site = survey_site(f, p.dock_apron_m, house, site_x, site_z, 0.0, layout, rules);
    check(site.ok() && site.ground_max - site.ground_min <= rules.max_rise_m, "选中的地高差在限内");
    check_near(site.floor_y, site.ground_max + house.plinth_m, 1e-9, "地坪 = 最高地面 + 台基");
    check_near(site.plinth_bottom, site.ground_min - rules.bury_m, 1e-9, "台基埋到最低地面之下");
    check(settlement_place(layout, f, p.dock_apron_m, "house", site_x, site_z, 0.0, rules).empty() && layout.size() == 1, "放一座");
    check(settlement_place(layout, f, p.dock_apron_m, "house", site_x, site_z, 0.0, rules) == "和民居挨得太近" && layout.size() == 1, "同一处再放被拒");
    check(survey_site(f, p.dock_apron_m, house, site_x, site_z, 0.0, layout, rules, 0).ok(), "忽略自己就能过");
    check(settlement_place(layout, f, p.dock_apron_m, "palace", site_x, site_z, 0.0, rules) == "没有这种建筑：palace", "不认识的建筑");
    check(settlement_place(layout, f, p.dock_apron_m, "house", apron_x, apron_z, 0.0, rules) == "停机坪上不能盖" && layout.size() == 1, "被拒时清单不动");
    check(settlement_find(layout, site_x, site_z) == 0, "放下的那座找得到");
    layout.erase(0);
    check(layout.empty(), "拆掉");
}

} // namespace

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
    std::printf("%d 项检查，%d 项失败\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
