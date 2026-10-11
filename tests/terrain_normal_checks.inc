// 走实际源河切分网格：河心保留纵坡，两岸保留横坡；厘米槽不借两米网格放宽。
void test_terrain_normals() {
    std::printf("[terrain_normals]\n");
    constexpr double center_x = 37.123;
    constexpr double longitudinal_slope = -0.25;
    for (double width : { 20.0, 0.02 }) {
        WorldGrid g;
        g.rows = g.cols = 5;
        g.res = 100.0;
        g.x0 = g.z0 = -200.0;
        g.height.resize(25);
        g.shore.assign(25, 1000.0f);
        g.island.assign(25, 0);
        g.landcover.assign(25, 6);
        g.stream.assign(25, 0);
        for (int r = 0; r < g.rows; ++r) {
            for (int c = 0; c < g.cols; ++c) {
                g.height[g.index(r, c)] = float(100.0 + longitudinal_slope * (g.z0 + r * g.res));
            }
        }
        g.source_rivers = true;
        g.basin_daily = { { 1.0 } };
        g.stream_min_daily = { 0.1 };
        WorldGrid::SourceStem stem;
        stem.basin = 0;
        const double depth = std::min(2.0, width), half = width * 0.5;
        for (double z : { -200.0, 200.0 }) {
            WorldGrid::SourcePoint p;
            p.x = center_x;
            p.z = z;
            p.surface = 100.0 + longitudinal_slope * z;
            p.bed = p.surface - depth;
            p.width = p.mean_width = width;
            p.depth = p.mean_depth = depth;
            p.left = p.right = 20.0;
            p.acc = p.q_mean = 1.0;
            p.level = 1;
            stem.points.push_back(p);
        }
        g.source_stems.push_back(stem);
        WorldTerrain terrain(g, {}, 42);
        TileLayout layout(g);
        const TileMesh mesh = build_tile_mesh(terrain, layout, layout.leaf_at(center_x, 40.0));
        const double exponent = g.station_depth_exp / g.station_width_exp;
        const double cross_slope = depth / half * exponent * std::pow(0.5, exponent - 1.0);
        int centers = 0, left = 0, right = 0;
        bool unit = true, along = true, across = true, geometry = true;
        for (size_t k = 0; k < mesh.positions.size() / 3; ++k) {
            const double x = mesh.origin_x + mesh.positions[k * 3];
            const double z = mesh.origin_z + mesh.positions[k * 3 + 2];
            if (z < 10.0 || z > 100.0) continue;
            const double offset = x - center_x;
            const bool center = std::abs(offset) < 1e-4;
            const bool bank = std::abs(std::abs(offset) - half * 0.5) < 1e-4;
            if (!center && !bank) continue;
            const double expected_y = 100.0 + longitudinal_slope * z - depth +
                    (bank ? depth * std::pow(0.5, exponent) : 0.0);
            if (std::abs(mesh.positions[k * 3 + 1] - expected_y) > 2e-4) continue; // 岛底与裙边不算顶面。
            const double nx = mesh.normals[k * 3], ny = mesh.normals[k * 3 + 1], nz = mesh.normals[k * 3 + 2];
            unit = unit && std::isfinite(nx) && std::isfinite(ny) && std::isfinite(nz) &&
                    std::abs(nx * nx + ny * ny + nz * nz - 1.0) < 1e-5;
            along = along && ny > 0.0 && std::abs(nz / ny + longitudinal_slope) < 1e-4;
            geometry = geometry && std::abs(terrain.sample(x, z).top - mesh.positions[k * 3 + 1]) < 2e-4;
            if (center) {
                ++centers;
                across = across && std::abs(nx) < 1e-4;
            } else {
                offset < 0.0 ? ++left : ++right;
                across = across && nx * offset < 0.0 &&
                        std::abs(std::abs(nx / ny) - cross_slope) < cross_slope * 0.08;
            }
        }
        std::printf("  width=%.6f center=%d left=%d right=%d\n", width, centers, left, right);
        const std::string label = "源河宽 " + std::to_string(width) + " m：";
        check(centers > 10 && left > 10 && right > 10, label + "河心与两岸确实插入实际顶面");
        check(unit && along, label + "单位法线保留真实纵坡");
        check(across, label + "两岸法线方向相反且保留横坡，不被粗格距抹平");
        check(geometry, label + "顶点仍按源河槽取样，法线修复不改几何");
    }
}
