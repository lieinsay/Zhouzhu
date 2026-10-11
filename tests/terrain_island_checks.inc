// 岸外按最近陆地外推的高度属于各自岛体，不能让另一座岛的高程参与岸坡插值。
void test_terrain_island_height() {
    std::printf("[terrain_island_height]\n");
    for (bool along_z : { false, true }) {
        WorldGrid g;
        g.rows = g.cols = 9; g.res = 100.0; g.x0 = g.z0 = -400.0;
        const size_t n = size_t(g.rows) * g.cols;
        g.height.resize(n); g.island.resize(n); g.shore.resize(n);
        g.source_landscape = true;
        for (int r = 0; r < g.rows; ++r) for (int c = 0; c < g.cols; ++c) {
            const double across = along_z ? g.z0 + r * g.res : g.x0 + c * g.res;
            const double tangent = along_z ? g.x0 + c * g.res : g.z0 + r * g.res;
            const size_t k = g.index(r, c);
            g.island[k] = across <= 0.0 ? 0 : 22;
            g.height[k] = float((across <= 0.0 ? 300.0 : 800.0) + 0.2 * tangent);
            // 两岸分别在 -50/150 米，岸外两个原格由不同岛的高程延拓。
            g.shore[k] = float(std::max(-50.0 - across, across - 150.0));
        }
        auto height = [&](const WorldGrid &grid, double across, double tangent = 0.0) {
            return grid.height_at(along_z ? tangent : across, along_z ? across : tangent);
        };
        const std::string axis = along_z ? "南北两岛：" : "东西两岛：";
        const double low = height(g, -52.0), high = height(g, 152.0);
        std::printf("  axis=%s coast_height=%.6f/%.6f expected=300/800\n", along_z ? "z" : "x", low, high);
        check(std::abs(low - 300.0) < 1e-8 && std::abs(high - 800.0) < 1e-8,
                axis + "连续岸线的两侧顶面保留本岛高程，不产生另一岛造成的岸坡凹口");
        check(std::abs(height(g, 48.0) - 300.0) < 1e-8 && std::abs(height(g, 52.0) - 800.0) < 1e-8,
                axis + "狭缝内两座独立岛体的高度延拓不混合成连接斜坡");
        check(std::abs(height(g, -52.0, 25.0) - 305.0) < 1e-8 && std::abs(height(g, 152.0, -25.0) - 795.0) < 1e-8,
                axis + "岸边仍插值本岛的沿岸真实起伏");
        bool anchors = true;
        for (int r = 0; r < g.rows; ++r) for (int c = 0; c < g.cols; ++c) {
            anchors = anchors && g.height_at(g.x0 + c*g.res, g.z0 + r*g.res) == g.height[g.index(r,c)];
        }
        check(anchors, axis + "全部源原格高程锚点保持原值");
        const auto ids = g.island;
        (void)height(g, 48.0); (void)height(g, 52.0);
        check(g.island == ids && g.island[g.nearest_cell(along_z ? 0.0 : 48.0, along_z ? 48.0 : 0.0)] == 0 &&
                        g.island[g.nearest_cell(along_z ? 0.0 : 52.0, along_z ? 52.0 : 0.0)] == 22,
                axis + "保留源最近岛号与独立岛语义");
        WorldGrid old = g; old.source_landscape = false;
        check(std::abs(height(old, 50.0) - 550.0) < 1e-8 && std::abs(height(old, -52.0) - 270.048) < 1e-6,
                axis + "旧格式维持历史双三次读取行为");
        WorldGrid one = g; one.island.assign(n, 0);
        for (int r = 0; r < g.rows; ++r) for (int c = 0; c < g.cols; ++c) {
            one.height[one.index(r,c)] = float(400.0 + 0.4*r*r - 0.3*c*c + r*c);
        }
        WorldGrid one_old = one; one_old.source_landscape = false;
        check(height(one, 48.0, -72.0) == height(one_old, 48.0, -72.0) &&
                        height(one, -52.0, 25.0) == height(one_old, -52.0, 25.0),
                axis + "单岛内部仍保留原有双三次曲面");
        WorldGrid no_ids = g; no_ids.island.clear();
        check(height(no_ids, 50.0) == height(old, 50.0), axis + "未提供岛号的旧输入保留既有高程读取");
    }
}
