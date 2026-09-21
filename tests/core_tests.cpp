// 纯算法文件（不含 Godot 头的那几个）的单元测试。不引测试框架：一个可执行文件，失败即非零退出。
//
//   cmake --build build --config Debug --target zhouzhu_core_tests
//   ctest --test-dir build -C Debug --output-on-failure

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "island_field.h"
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
    for (double s = -9.0; s <= 9.0; s += 1.0) {
        for (double t = d.pier_m; t <= d.pier_m + 8.0; t += 1.0) {
            berth_clear = berth_clear && !f.sample_height(d.x + t * d.out_x - s * d.out_z, d.z + t * d.out_z + s * d.out_x, y);
        }
    }
    check(berth_clear, "泊位（栈桥头外 8 m × 两侧 9 m）里没有陆地");
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

} // namespace

int main() {
    test_rng();
    test_noise();
    test_island("新岛 · 火山锥", 0.1, IslandAge::YOUNG);
    test_island("中年 · 山脊", 0.5, IslandAge::MID);
    test_island("老岛 · 台地", 0.8, IslandAge::OLD);
    std::printf("%d 项检查，%d 项失败\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
