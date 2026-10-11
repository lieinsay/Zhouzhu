// 粗包的相邻格可以属于不同高度的岛；网格不能把岛号边界插成斜坡或省掉侧壁。
static PlanetLod far_island_fixture(bool p_split) {
    PlanetLod lod;
    lod.node = 6070;
    lod.rows = lod.cols = 8;
    lod.res_m = 500.0;
    lod.origin_x_km = -2.0;
    lod.origin_y_km = 2.0;
    lod.islands.resize(p_split ? 2 : 1);
    lod.islands[0].rim_m = lod.islands[0].surface_m = lod.islands[0].peak_m = 1000.0;
    if (p_split) lod.islands[1].rim_m = lod.islands[1].surface_m = lod.islands[1].peak_m = 3000.0;
    for (int r = 0; r < lod.rows; ++r) {
        for (int c = 0; c < lod.cols; ++c) {
            const bool high = p_split && c >= 4;
            lod.height.push_back(high ? 3000 : 1000);
            lod.land.push_back(255);
            lod.landcover.push_back(3);
            lod.island.push_back(high ? 1 : 0);
        }
    }
    return lod;
}

static void test_far_islands() {
    std::printf("\n远景粗版：相邻岛体的高度、岛底和身份边闭壁\n");
    const PlanetLod split = far_island_fixture(true);
    const auto height_before = split.height;
    const auto ids_before = split.island;
    const FarGroupMesh mesh = build_far_group_mesh(split, 6371000.0);
    int mixed_top = 0, mixed_body = 0, identity_walls = 0;
    bool valid = true, owned_top = true;
    for (size_t k = 0; k < mesh.vertex_count(); ++k) {
        if (mesh.uvs[k * 2 + 1] > 0.5f) {
            const float y = mesh.uvs[k * 2];
            owned_top = owned_top && (std::abs(y - 1000.0f) < 0.001f || std::abs(y - 3000.0f) < 0.001f);
        }
    }
    for (size_t k = 0; k < mesh.indices.size(); k += 3) {
        float lo = 1e9f, hi = -1e9f;
        bool top = true, body = true, wall = true;
        for (size_t j = 0; j < 3; ++j) {
            const int id = mesh.indices[k + j];
            if (id < 0 || size_t(id) >= mesh.vertex_count()) { valid = false; continue; }
            lo = std::min(lo, mesh.uvs[size_t(id) * 2]);
            hi = std::max(hi, mesh.uvs[size_t(id) * 2]);
            top = top && mesh.uvs[size_t(id) * 2 + 1] > 0.5f;
            body = body && mesh.uvs[size_t(id) * 2 + 1] < 0.5f;
            wall = wall && std::abs(mesh.positions[size_t(id) * 3]) < 0.001f &&
                    std::abs(mesh.normals[size_t(id) * 3]) > 0.99f;
        }
        mixed_top += top && hi - lo > 1.0f;
        mixed_body += body && hi - lo > 1000.0f;
        identity_walls += body && wall;
    }
    std::printf("  mixed_top=%d mixed_body=%d identity_wall_triangles=%d\n", mixed_top, mixed_body, identity_walls);
    check(valid, "远景所有索引有效");
    check(mesh.solid_cells == 64, "保留粗包实地格，不推测格内物理缺口");
    check(owned_top && mixed_top == 0, "不同岛顶面不平均、不连接高低岛");
    check(mixed_body == 0, "不同岛岛底不连接");
    check(identity_walls == 32, "相邻岛号边各自闭壁");
    check(split.height == height_before && split.island == ids_before, "不改源高度与语义岛号");
    const auto single = build_far_group_mesh(far_island_fixture(false), 6371000.0);
    check(single.solid_cells == 64 && single.indices.size() / 3 == 320, "单岛旧规则顶底与外岸侧壁数量不变");
    bool flat = true;
    for (size_t k = 0; k < single.vertex_count(); ++k) {
        if (single.uvs[k * 2 + 1] > 0.5f) flat = flat && std::abs(single.uvs[k * 2] - 1000.0f) < 0.001f;
    }
    check(flat, "单岛平顶保留原高程");
}
