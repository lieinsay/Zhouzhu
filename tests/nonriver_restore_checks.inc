// 仅由 core_tests.cpp 引入：恢复内容的跨版本数据契约检查。
void test_nonriver_restore() try {
    std::printf("[nonriver_restore]\n");
    std::string error;
    for (int node : {START, NEIGHBOR}) {
        const std::string bytes = read_bytes(group_dir(node) + "/terrain.bin");
        WorldGrid old;
        const bool ok = parse_world_grid(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), old, error);
        check(ok && !old.source_landscape && old.lith.empty(), "旧开局 v5 包可读，不补猜岩性");
        if (ok) check(old.field_cell(WorldGrid::FIELD_SALT, 0) == 0, "旧资源包没有盐层时保持零");
    }
    check(deposits::ore_item("金") == "gold_ore" && deposits::ore_item("金银") == "gold_ore", "新旧金矿名称都能采得正确物品");
    check(deposits::ore_item("锡钨") == "tin_ore" && deposits::ore_item("锡") == "tin_ore", "旧锡钨名称兼容");
    check(deposits::ore_item("铬") == "chromium_ore" && deposits::ore_item("镍") == "nickel_ore", "新增矿种不会退成铜矿");
    check(deposits::kind("salt") && deposits::kind("saltspring") && deposits::kind("fossil"), "盐与化石种类可识别");
    {
        // 行星包头独立改变高度，不重生成气候或节点。
        auto package = [](const std::string &head) {
            std::string out = "ZZPL";
            const uint32_t words[] = {1, 42, 0, 0, 0, uint32_t(head.size())};
            out.append(reinterpret_cast<const char *>(words), sizeof(words));
            return out + head;
        };
        PlanetData data;
        const std::string valid = package("vertical -8200 -7400 -5800 700 1.1 8.5 0.21\n");
        check(data.parse_planet(reinterpret_cast<const uint8_t *>(valid.data()), valid.size(), error), "非默认海面云层基准可读");
        check(data.vertical.sea == -8200 && data.vertical.top == -5800 && data.vertical.clearance == 700, "高度来自包头而非硬编码");
        const std::string legacy = package("");
        check(data.parse_planet(reinterpret_cast<const uint8_t *>(legacy.data()), legacy.size(), error) && data.vertical.sea == PlanetData::Vertical{}.sea, "加载旧包恢复旧高度基准");
        const std::string bad = package("vertical -5000 -7400 -5800 700 1.1 8.5 0.21\n");
        check(!data.parse_planet(reinterpret_cast<const uint8_t *>(bad.data()), bad.size(), error), "上下顺序错误的垂直结构拒收");
    }
    GenConfig cfg;
    const char *config_path = std::getenv("ZHOUZHU_RESTORE_CONFIG");
    if (!config_path || !parse_gen_config(read_bytes(config_path), cfg, error)) {
        check(false, "请通过 tests/run_nonriver_restore.py 提供当前生成器配置：" + error);
        return;
    }
    std::shared_ptr<const gen_core::PlanetState> planet;
    try { planet = gen_core::planet_state(cfg, 42); }
    catch (const std::exception &e) { check(false, std::string("生成器异常：") + e.what()); return; }
    std::puts("  当前行星配置生成完成，检查岛群导出");
    const auto source = gen_core::generate_group(*planet, START);
    gen_export::GroupMeta meta;
    meta.node = START; meta.seed = 42; meta.run = "restore-check";
    const auto files = gen_export::group_files(source, meta); // 仅内存，不覆盖开局文件。
    WorldGrid grid;
    const bool loaded = parse_world_grid(reinterpret_cast<const uint8_t *>(files.terrain_bin.data()), files.terrain_bin.size(), grid, error);
    check(loaded, "v8 导出与读包端互通：" + error);
    if (!loaded) return;
    check(grid.source_landscape && files.terrain_bin.size() == 40 + source.height.size() * 36, "v8 只有固定非河流扩展，没有河流时序尾段");
    check(grid.lith == source.lith.v && grid.rockwall_dir == source.rockwall_dir.v, "岩性和崖层方向逐格来自生成器");
    double coast_error = 0, wall_error = 0, salt_error = 0;
    for (size_t k = 0; k < grid.height.size(); ++k) {
        coast_error = std::max(coast_error, std::abs(double(grid.shore[k]) - double(float(source.coast_dist.v[k]))));
        wall_error = std::max(wall_error, std::abs(double(grid.rockwall[k]) - double(float(source.rockwall_m.v[k]))));
        const double salt = source.island_id.v[k] >= 0 ? source.res_field[6].v[k] / 255.0 : 0;
        salt_error = std::max(salt_error, std::abs(grid.field_cell(WorldGrid::FIELD_SALT, k) - salt));
    }
    check(coast_error == 0 && wall_error == 0 && salt_error == 0, "岸线、崖层和盐赋存逐格一致");
    WorldGrid untouched = grid;
    std::string broken = files.terrain_bin;
    broken.pop_back();
    check(!parse_world_grid(reinterpret_cast<const uint8_t *>(broken.data()), broken.size(), untouched, error), "截断 v8 拒收");
    check(untouched.lith == grid.lith, "失败读取不破坏已载入数据");
    broken = files.terrain_bin;
    const uint32_t withdrawn = 7;
    std::memcpy(&broken[4], &withdrawn, 4);
    check(!parse_world_grid(reinterpret_cast<const uint8_t *>(broken.data()), broken.size(), untouched, error), "不把已撤回 v7 河流包错当新地貌包");
    std::vector<WorldIsland> islands;
    for (size_t k = 0; k < source.islands.size(); ++k) {
        WorldIsland i;
        i.id = int(k); i.rim_m = source.islands[k].rim_j; i.cliff_m = source.islands[k].cliff_j;
        i.keel_m = source.islands[k].keel_j; i.max_shore_m = 1000;
        islands.push_back(i);
    }
    WorldTerrain terrain(grid, islands, 42);
    const size_t cell = size_t(std::distance(grid.shore.begin(), std::max_element(grid.shore.begin(), grid.shore.end())));
    const double x = grid.x0 + (cell % size_t(grid.cols)) * grid.res;
    const double z = grid.z0 + (cell / size_t(grid.cols)) * grid.res;
    const auto sample = terrain.sample(x, z);
    check(sample.lith == grid.lith[cell] && sample.rockwall == grid.rockwall[cell], "渲染取样读取真实地貌字段");
    check_near(sample.shore, grid.shore[cell], 1e-6, "源岸线上不叠加另一份随机岸线");
    check(files.world_json.find("\"landforms\"") != std::string::npos, "导出原始地貌记录");
    {
        // 斜岸穿过叶块：画面和碰撞均应沿解析岸线闭合，不能再缩到整格边缘。
        WorldGrid coast;
        coast.rows = coast.cols = 17; coast.res = 32; coast.x0 = coast.z0 = -256;
        const size_t count = size_t(coast.rows) * coast.cols;
        coast.height.assign(count, 100); coast.island.assign(count, 0);
        coast.landcover.assign(count, 2); coast.stream.assign(count, 0);
        coast.shore.resize(count); coast.lith.assign(count, 3); coast.source_landscape = true;
        for (int r = 0; r < coast.rows; ++r) for (int c = 0; c < coast.cols; ++c)
            coast.shore[coast.index(r,c)] = float(30.3 - (coast.x0 + c * coast.res) - 0.25 * (coast.z0 + r * coast.res));
        WorldIsland island; island.rim_m = 100;
        WorldTerrain terrain(coast, {island}, 42);
        TileLayout layout(coast);
        const auto mesh = build_tile_mesh(terrain, layout, layout.leaf_at(30, 20));
        size_t wall_vertices = 0;
        double wall_error = 0;
        for (size_t k = 0; k < mesh.positions.size(); k += 3) {
            if (std::abs(mesh.normals[k+1]) < 0.01) {
                ++wall_vertices;
                wall_error = std::max(wall_error, std::abs(30.3 - (mesh.origin_x + mesh.positions[k]) - 0.25 * (mesh.origin_z + mesh.positions[k+2])));
            }
        }
        check(wall_vertices > 0 && wall_error < 1e-4, "斜岸崖壁沿原始岸距零线，误差小于 0.1 mm");
        double collision_error = 0;
        for (size_t k = 0; k < mesh.collision.size(); k += 3)
            collision_error = std::max(collision_error, -terrain.sample(mesh.origin_x + mesh.collision[k], mesh.origin_z + mesh.collision[k+2]).shore);
        check(!mesh.collision.empty() && collision_error < 1e-4, "碰撞没有延伸到源岸线以外");
    }
} catch (const std::exception &e) {
    check(false, std::string("非河流恢复检查异常：") + e.what());
}
