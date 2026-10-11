// 源岩性仍是类别；实际顶面只平滑调色，不让最近格边界把连续陡坡切成两片。
void test_terrain_colors() {
    std::printf("[terrain_colors]\n");
    constexpr GroundColor pale{ 0.72f, 0.71f, 0.65f }, dark{ 0.24f, 0.28f, 0.28f }, rock{ 0.46f, 0.43f, 0.39f };
    auto color_distance = [](const GroundColor &a, const GroundColor &b) {
        return std::sqrt((a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b));
    };
    for (bool along_z : { false, true }) {
        WorldGrid g;
        g.rows = g.cols = 9; g.res = 100.0; g.x0 = g.z0 = -400.0;
        const size_t cells = size_t(g.rows) * g.cols;
        g.height.resize(cells); g.shore.assign(cells, 1000.0f); g.island.assign(cells, 0);
        g.landcover.assign(cells, 2); g.stream.assign(cells, 0); g.lith.resize(cells);
        g.source_landscape = true;
        for (int r = 0; r < g.rows; ++r) for (int c = 0; c < g.cols; ++c) {
            const double x = g.x0 + c * g.res, z = g.z0 + r * g.res;
            g.height[g.index(r, c)] = float(3000.0 + 2.5 * (along_z ? z : x) + 0.25 * (along_z ? x : z));
            g.lith[g.index(r, c)] = (along_z ? r : c) <= 4 ? 1 : 3;
        }
        WorldTerrain terrain(g, {}, 42);
        TileLayout layout(g);
        std::map<TileKey, TileMesh> meshes;
        struct Vertex { bool found = false; GroundColor color{}; std::array<double, 3> normal{}; double y = 0.0; };
        auto vertex_at = [&](double across) {
            const double x = along_z ? 0.0 : across, z = along_z ? across : 0.0;
            const TileKey key = layout.leaf_at(x, z);
            if (meshes.find(key) == meshes.end()) meshes.emplace(key, build_tile_mesh(terrain, layout, key));
            const TileMesh &m = meshes.at(key);
            Vertex out;
            for (size_t k = 0; k < m.positions.size() / 3; ++k) {
                if (std::abs(m.origin_x + m.positions[k * 3] - x) > 1e-5 || std::abs(m.origin_z + m.positions[k * 3 + 2] - z) > 1e-5 ||
                        m.normals[k * 3 + 1] <= 0.0 || std::abs(m.positions[k * 3 + 1] - terrain.sample(x, z).top) > 0.001) continue;
                out.found = true; out.y = m.positions[k * 3 + 1];
                out.color = { m.colors[k * 4], m.colors[k * 4 + 1], m.colors[k * 4 + 2] };
                for (int j = 0; j < 3; ++j) out.normal[j] = m.normals[k * 3 + j];
                break;
            }
            return out;
        };
        auto unshade = [&](const Vertex &v, double across) {
            const double x = along_z ? 0.0 : across, z = along_z ? across : 0.0;
            const float shade = float(1.0 + 0.07 * hash_fbm(0x51EDC0105ull + 2, x, z, 30.0, 3));
            return GroundColor{ v.color.r / shade, v.color.g / shade, v.color.b / shade };
        };
        const Vertex before = vertex_at(48.0), after = vertex_at(50.0), anchor_pale = vertex_at(0.0), anchor_dark = vertex_at(100.0),
                far_pale = vertex_at(-150.0), far_dark = vertex_at(250.0);
        const std::string axis = along_z ? "南北边界：" : "东西边界：";
        check(before.found && after.found && anchor_pale.found && anchor_dark.found && far_pale.found && far_dark.found, axis + "检查的是真实陡坡顶面顶点");
        double normal_delta = 0.0;
        for (int j = 0; j < 3; ++j) normal_delta += (before.normal[j] - after.normal[j]) * (before.normal[j] - after.normal[j]);
        check(before.normal[1] < 0.7 && after.normal[1] < 0.7 && std::sqrt(normal_delta) < 0.03, axis + "连续陡坡法线已排除草岩坡度阈值变化");
        const double edge_delta = color_distance(unshade(before, 48.0), unshade(after, 50.0));
        std::printf("  axis=%s native_boundary=50m adjacent=48/50m color_delta=%.6f normal_delta=%.6f\n", along_z ? "z" : "x", edge_delta, std::sqrt(normal_delta));
        check(edge_delta < 0.12, axis + "两米近邻越过原格最近类别边界时颜色连续过渡");
        const auto left = unshade(anchor_pale, 0.0), right = unshade(anchor_dark, 100.0);
        check(color_distance(left, pale) < 2e-6 && color_distance(right, dark) < 2e-6, axis + "原格中心保留两种源岩性的调色锚点");
        check(color_distance(unshade(far_pale, -150.0), pale) < 2e-6 && color_distance(unshade(far_dark, 250.0), dark) < 2e-6 &&
                        far_pale.color.r - far_dark.color.r > 0.3, axis + "同类区域保持原色与可辨明暗差");
        const auto a = unshade(before, 48.0), b = unshade(after, 50.0);
        check(a.b > 0.0f && b.b > 0.0f && a.g / a.b >= 0.99f && a.g / a.b < 1.13f && b.g / b.b >= 0.99f && b.g / b.b < 1.13f,
                axis + "混合源调色而不把岩性编号平均成未提供的棕色类别");
        check(terrain.sample(along_z ? 0.0 : 48.0, along_z ? 48.0 : 0.0).lith == 1 &&
                        terrain.sample(along_z ? 0.0 : 50.0, along_z ? 50.0 : 0.0).lith == 3, axis + "语义查询仍保留源最近格类别");
        WorldGrid zero = g, absent = g; zero.lith.assign(cells, 0); absent.lith.clear();
        WorldTerrain zero_terrain(zero, {}, 42), old_terrain(absent, {}, 42);
        const TileKey key = layout.leaf_at(48.0, 48.0);
        const TileMesh zero_mesh = build_tile_mesh(zero_terrain, layout, key), old_mesh = build_tile_mesh(old_terrain, layout, key), new_mesh = build_tile_mesh(terrain, layout, key);
        check(zero_mesh.colors == old_mesh.colors, axis + "岩性零值与旧包缺层仍用原有裸岩调色");
        check(new_mesh.positions == old_mesh.positions && new_mesh.normals == old_mesh.normals && new_mesh.collision == old_mesh.collision && new_mesh.season == old_mesh.season,
                axis + "调色不改变高程、法线、碰撞与季相");
        if (!old_mesh.colors.empty()) {
            const float shade = float(1.0 + 0.07 * hash_fbm(0x51EDC0105ull + 2, old_mesh.origin_x, old_mesh.origin_z, 30.0, 3));
            check(color_distance({ old_mesh.colors[0] / shade, old_mesh.colors[1] / shade, old_mesh.colors[2] / shade }, rock) < 2e-6,
                    axis + "无岩性旧包陡坡仍保留原 ROCK 色");
        }
    }
}
