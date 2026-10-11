// 故事群细包更新时，已有各档粗版必须一起替换；未更新的群及后台旧快照保持各自版本。
void test_story_lods() {
    std::printf("[story_lods]\n");
    auto native = [](bool p_new) {
        isl::Group g;
        g.H = g.W = 40;
        g.res_km = 0.1;
        g.x0 = g.origin_x = -2.0;
        g.y0 = g.origin_y = 2.0;
        g.height = skyisle::GridD(g.H, g.W, std::numeric_limits<double>::quiet_NaN());
        g.island_id = skyisle::Grid<int16_t>(g.H, g.W, -1);
        g.landcover = skyisle::Grid<uint8_t>(g.H, g.W, 0);
        g.river = skyisle::Grid<uint8_t>(g.H, g.W, 0);
        g.lake = skyisle::Grid<uint8_t>(g.H, g.W, 0);
        g.has_hydro = true;
        const int lo = p_new ? 10 : 5, hi = p_new ? 30 : 35;
        for (int i = lo; i < hi; i++) {
            for (int j = lo; j < hi; j++) {
                g.island_id(i, j) = 0;
                g.height(i, j) = p_new ? 300.0 : 900.0;
                g.landcover(i, j) = 3;
            }
        }
        g.islands.resize(1);
        g.islands[0].rim_j = p_new ? 300.0 : 900.0;
        g.islands[0].keel_j = 0.0;
        g.islands[0].peak_j = g.islands[0].rim_j;
        g.islands[0].surface = g.islands[0].rim_j;
        return g;
    };
    const isl::Group old_source = native(false), new_source = native(true);
    const std::vector<double> heights = new_source.height.v;
    const std::vector<int16_t> ids = new_source.island_id.v;
    const std::vector<uint8_t> covers = new_source.landcover.v;
    const std::vector<double> resolutions = { 2000.0, 1000.0, 500.0 };
    const auto updated = gen_core::group_lods(6070, new_source, { 500.0, 2000.0, 1000.0, 1000.0 });
    check(updated.size() == 3 && updated[0].res_m == 2000.0 && updated[1].res_m == 1000.0 && updated[2].res_m == 500.0,
            "故事群已有三档从同一原生群导出（重复格距合并，不硬编码只出一档）");
    check(std::memcmp(heights.data(), new_source.height.v.data(), heights.size() * sizeof(double)) == 0 &&
                    ids == new_source.island_id.v && covers == new_source.landcover.v,
            "出粗版不改变原生高程、岛号和地表");
    const auto survival = gen_core::group_lods(6070, new_source);
    check(survival.size() == 1 && survival[0].res_m == 2000.0 && survival[0].blob == updated[0].blob,
            "生存模式仍只出同一份 2 km 粗版");

    PlanetData pd;
    std::string err;
    const std::string planet = read_bytes(std::string(ZHOUZHU_SOURCE_DIR) + "/game/planet/seed42/planet.bin");
    if (!pd.parse_planet(reinterpret_cast<const uint8_t *>(planet.data()), planet.size(), err)) {
        check(false, "故事 LOD fixture 行星包：" + err);
        return;
    }
    auto packed = gen_core::group_lods(6070, old_source, resolutions);
    const auto neighbor = gen_core::group_lods(6071, old_source, resolutions);
    packed.insert(packed.end(), neighbor.begin(), neighbor.end());
    const std::string initial = gen_export::lods_bin(packed);
    check(pd.parse_lods(reinterpret_cast<const uint8_t *>(initial.data()), initial.size(), err), "初始两群三档粗版载入：" + err);
    const uint64_t target_gen = pd.lod_generation(6070), neighbor_gen = pd.lod_generation(6071);
    std::vector<PlanetLod> old_lods, neighbor_lods;
    for (double res : resolutions) {
        uint64_t snapshot_gen = 0;
        old_lods.push_back(pd.lod(6070, res, &snapshot_gen));
        neighbor_lods.push_back(pd.lod(6071, res));
        check(snapshot_gen == target_gen && !old_lods.back().empty(), "后台旧字节与该群旧代次一起取出");
    }
    const std::string bundle = gen_export::lods_bin(updated);
    std::vector<int> nodes;
    check(pd.add_lods(reinterpret_cast<const uint8_t *>(bundle.data()), bundle.size(), err, &nodes) && nodes == std::vector<int>{ 6070 },
            "同源 bundle 一次替换三档，仅报告一个更新群：" + err);
    check(pd.lod_count() == 6 && pd.lod_resolutions(6070) == resolutions, "替换后各档各一份，邻群档位不丢失");
    check(pd.lod_generation(6070) > target_gen && pd.lod_generation(6071) == neighbor_gen,
            "目标普通粗网格也能检测代次变化，邻群无需重建");
    for (size_t k = 0; k < resolutions.size(); k++) {
        uint64_t fresh_gen = 0;
        const PlanetLod fresh = pd.lod(6070, resolutions[k], &fresh_gen), untouched = pd.lod(6071, resolutions[k]);
        check(fresh_gen == pd.lod_generation(6070) && fresh_gen != target_gen && fresh.height != old_lods[k].height && fresh.land != old_lods[k].land,
                "各档都已换成新高程/岸缘，后台旧结果的代次过期");
        check(std::all_of(fresh.height.begin(), fresh.height.end(), [](uint16_t h) { return h == 0 || h == 300; }) &&
                        std::all_of(old_lods[k].height.begin(), old_lods[k].height.end(), [](uint16_t h) { return h == 0 || h == 900; }),
                "新数据没有混入旧峰高，已取出的旧快照仍稳定");
        check(untouched.height == neighbor_lods[k].height && untouched.land == neighbor_lods[k].land && untouched.island == neighbor_lods[k].island,
                "邻群各档字节的解码结果保持原值");
    }
    auto invalid = updated;
    invalid.back().node = 999999;
    const std::string bad = gen_export::lods_bin(invalid);
    const uint64_t before_bad = pd.lod_generation();
    check(!pd.add_lods(reinterpret_cast<const uint8_t *>(bad.data()), bad.size(), err) && pd.lod_generation() == before_bad &&
                    pd.lod_generation(6071) == neighbor_gen,
            "含无效群号的 bundle 整份拒绝，代次及邻群不变");
    for (bool truncated : { false, true }) {
        auto broken = updated;
        if (truncated) {
            broken.back().blob.pop_back();
        } else {
            broken.back().blob[24] = broken.back().blob[25] = 0; // 一座岛的元数据之后，第一段格记录长度为零。
        }
        const std::string corrupt = gen_export::lods_bin(broken);
        const auto intact = pd.lod(6070);
        check(!pd.add_lods(reinterpret_cast<const uint8_t *>(corrupt.data()), corrupt.size(), err) &&
                        pd.lod_generation() == before_bad && pd.lod(6070).height == intact.height,
                "索引有效但格记录损坏的 bundle 整份拒绝，不混入半份新粗版");
    }
}
