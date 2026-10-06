#pragma once

#include <cstdint>
#include <vector>

#include "world_terrain.h"

// 河的水面网格：按 chunk_m 见方的块切（段的中点落在哪块就归哪块），一块一个网格，由上层流式挂载。不含 Godot 头。
//
// 每段一条横断面五列的带子：−core、−平岸半宽、中线、+平岸半宽、+core（core = 平岸半宽 + 河漫滩）。
// 顶点的 y 是河床（只给包围盒用），真正的水位在着色器里按径流指数算（公式同 river_stage）：
//   custom0 = (河床 y, 平岸深, 渗漏门槛, 平岸半宽)，custom1 = (河漫滩宽, 跌水, 流速系数, 0)，
//   uv = (顺水弧长, 离中线的横距，左正右负)。着色器丢掉横距超出此刻水面半宽的片元——水面宽窄随水情变，且不会漫出河谷。
// 近岸处带子收窄到离岸线的距离（河口一带岸线斜切过河，不收就伸到云海上空）。
// 河口多一道跌水帘：顶边接水面、底边往下垂（custom1.y = 1 的顶点由着色器往下拉），水跌进云海。
// 船坞坪上不画（坪凿平了，选坪时已避开河道与河漫滩）；停机坪照画——坪让给河，河从码头边流过去。

struct RiverChunkKey {
    int ix = 0, iz = 0;
    int64_t packed() const { return (int64_t(ix) << 32) ^ int64_t(uint32_t(iz)); }
};

struct RiverMesh {
    RiverChunkKey key;
    double origin_x = 0.0, origin_z = 0.0; // 块的西北角：顶点相对它
    std::vector<float> positions; // xyz
    std::vector<float> normals; // xyz
    std::vector<float> uvs; // uv
    std::vector<float> custom0; // rgba
    std::vector<float> custom1; // rgba
    std::vector<float> custom2; // 源河：年均宽、年均深、流域号、溪涧标志
    std::vector<int32_t> indices; // 正面顺时针（Godot 约定），从上往下看
    double min_y = 0.0, max_y = 0.0; // 河床的高低（包围盒）

    bool empty() const { return indices.empty(); }
};

struct RiverMeshRules {
    double chunk_m = 512.0;
    double fall_m = 150.0; // 跌水帘垂多长（崖壁几十到一百多米高，帘子沿着它散成白雾）
    double fall_throw_m = 14.0; // 帘底往外抛多远
    double yard_margin_m = 6.0; // 离船坞坪这么近就不画
    double coast_margin_m = 1.0; // 带子两侧离岸线至少留这么多
};

// 有河段的块（按河网一次算好）
std::vector<RiverChunkKey> river_chunks(const WorldTerrain &p_terrain, const RiverMeshRules &p_rules = RiverMeshRules());
RiverMesh build_river_mesh(const WorldTerrain &p_terrain, const RiverChunkKey &p_key, const RiverMeshRules &p_rules = RiverMeshRules());
