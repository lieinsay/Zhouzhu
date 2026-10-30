// 湖塘（lakes.tsv v1，复核稿 W1a）：契约读取、哪些画、湖面网格只盖从格心连通的湿区；开局那群的包每处年水账闭合
void test_world_lakes() {
    std::printf("[world_lakes]\n");
    // 5×5、100 m 一格，中间一格低 1 m；它是满溢的塘（水位 99.5），另有一处碰着河槽、一处干洼都不画
    WorldGrid g;
    g.rows = g.cols = 5;
    g.res = 100;
    g.x0 = g.z0 = 0;
    g.height.assign(25, 100.0f);
    g.height[12] = 99.0f;
    g.height[0] = 98.0f; // 西北角更低，但离塘格超过一格、中间隔着 100 m 的地：不能被淹
    g.shore.assign(25, 1000.0f);
    g.island.assign(25, 0);
    g.landcover.assign(25, 6);
    g.stream.assign(25, 0);
    const std::string head = "lakes 1\nbasis evaporation=thornthwaite_pet seepage=none season=annual_mean depth_limit=pit_keep\ncycles_cut 0\n";
    const std::string pond = "lake 0 0 full -1 0 100 99 99.5 10000 5000 10000 2500 0.5 40000 0 1000 6000 5000 42000\ncells 200 200 99\n";
    const std::string channel = "lake 1 0 full -1 1 100 99.99 100 10000 50 10000 50 3 900000 0 0 6000 5000 901000\ncells 400 400 99.99\n";
    const std::string dry = "lake 2 0 dry -1 0 100 99.9 99.9 10000 50 0 0 0.01 0 0 0 0 0 0\ncells 0 400 99.9\n";
    WorldLakes lakes;
    std::string error;
    check(parse_lakes(head + pond + channel + dry, g.res, lakes, error) && lakes.lakes.size() == 3, "湖塘契约读取：" + error);
    check(lakes.lakes[0].drawn() && !lakes.lakes[1].drawn() && !lakes.lakes[2].drawn(), "碰河槽的与干洼不画");
    WorldLakes bad;
    check(!parse_lakes(head + "lake 0 0 full -1 0 100 99 100.5 1 1 1 1 1 0 0 0 0 0 0\ncells 200 200 99\n", g.res, bad, error), "水位高过溢口拒绝");
    check(!parse_lakes("lake 0 0 full -1 0 100 99 99.5 1 1 1 1 1 0 0 0 0 0 0\ncells 200 200 99\n", g.res, bad, error), "缺版本行拒绝");
    WorldTerrain terrain(g, {}, 42);
    // 塘格及外面一圈不加细节噪声：塘心的地面就是生成器的格高
    auto calm = std::make_shared<std::vector<uint8_t>>(25, 0);
    for (int r = 1; r <= 3; r++) {
        for (int c = 1; c <= 3; c++) {
            (*calm)[size_t(r * 5 + c)] = r == 2 && c == 2 ? WorldTerrain::CALM_POND : WorldTerrain::CALM_RIM;
        }
    }
    terrain.set_calm_cells(calm);
    double calm_y = 0.0;
    check(terrain.surface(200, 200, calm_y) && std::abs(calm_y - 99.0) < 1e-6, "塘心不加细节噪声");
    const RiverMeshRules rules;
    const std::vector<RiverChunkKey> keys = lake_chunks(lakes, rules.chunk_m);
    check(keys.size() == 1 && keys[0].ix == 0 && keys[0].iz == 0, "要画的湖塘落在一块里");
    const LakeMesh mesh = build_lake_mesh(terrain, lakes, keys[0], rules.chunk_m);
    bool level = true, near = true;
    for (size_t k = 0; k < mesh.positions.size(); k += 3) {
        level = level && mesh.positions[k + 1] == 99.5f;
        near = near && std::abs(mesh.positions[k] - 200.0f) <= 104.0f && std::abs(mesh.positions[k + 2] - 200.0f) <= 104.0f;
    }
    check(!mesh.empty() && mesh.lakes == 1 && mesh.indices.size() % 6 == 0, "满溢的塘有水面");
    check(level && near, "水面平在水位、只在塘格一格以内（西北角的低地不连通、不淹）");
    // 湿区：中心那一步在水下，离中心 80 m 的地（插值后高过水位）不出水面
    double y = 0.0;
    check(terrain.surface(200, 200, y) && y < 99.5 && terrain.surface(280, 280, y) && y > 99.5, "塘心低于水位、塘边高于水位");
    // 每块水面至少有一角的真实地面低于水位（湿区外不铺）
    size_t dry_quads = 0;
    for (size_t k = 0; k < mesh.indices.size(); k += 6) {
        bool wet_corner = false;
        for (int c : { 0, 1, 2, 5 }) {
            const size_t v = size_t(mesh.indices[k + size_t(c)]) * 3;
            double ground = 0.0;
            wet_corner = wet_corner || (terrain.surface(mesh.positions[v], mesh.positions[v + 2], ground) && ground < 99.5);
        }
        dry_quads += !wet_corner;
    }
    check(dry_quads == 0, "湿区外不铺水面");

    // 开局那群的包
    std::ifstream in(start_dir() + "/lakes.tsv", std::ios::binary);
    if (!in) {
        std::printf("  （跳过开局那群：没有 lakes.tsv）\n");
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    WorldLakes start;
    check(parse_lakes(text, 100.0, start, error), "开局那群的 lakes.tsv 能读：" + error);
    size_t drawn = 0, full = 0;
    double worst = 0.0;
    for (const WorldLake &L : start.lakes) {
        drawn += L.drawn();
        full += L.status == LAKE_FULL;
        const double in_sum = L.q_catch + L.q_up + L.q_shore + L.p_lake;
        worst = std::max(worst, std::abs(in_sum - L.e_lake - L.q_out) / std::max(1.0, in_sum));
    }
    std::printf("  开局那群 %zu 处洼地，满溢 %zu，要画 %zu，年水账最大相对残差 %.3g\n", start.lakes.size(), full, drawn, worst);
    check(start.cycles_cut == 0 && !start.lakes.empty() && drawn > 0, "开局那群有要画的塘、溢流路由无环");
    check(worst < 1e-9, "每处洼地年水账闭合");
}
