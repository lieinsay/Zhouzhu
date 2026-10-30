#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "river_mesh.h"
#include "world_terrain.h"

// 岛群的湖塘（lakes.tsv v1，复核稿 W1a）：生成器在最终地形上找出的封闭洼地，每处的年均水位与年水账由生成器 core 的 lake_balance 算好。
// 游戏只还原：在自己的地形高度上、该湖范围格（格心）一格以内，从格心起按水位往外连通地求湿区，画出平的水面；水位与范围都来自数据，不补造。
// 游戏给地面加的米级细节噪声比 2 m 以内的塘还深、Catmull-Rom 在塘岸外陡降处会冲到水位以下，所以范围格及外面一圈不加细节、
// 塘岸那一圈不低于格间线性内插（WorldTerrain::set_calm_cells）：塘的范围与水位照生成器的数据。
// 与河道 / 溪涧重叠的洼地（channel_cells > 0）是河槽在 100 m 格上的起伏，水位归河，W2 再衔接；本版不画它们。
// 只有年均水位：季节涨落、取水、涉水、改地都不在这一版。不含 Godot 头。

enum LakeStatus : uint8_t {
    LAKE_DRY = 0,
    LAKE_CLOSED = 1,
    LAKE_FULL = 2,
};

struct WorldLake {
    int id = 0, island = -1, down = -1, channel_cells = 0;
    LakeStatus status = LAKE_DRY;
    double spill_z = 0, bottom_z = 0, level_z = 0;
    double area_full_m2 = 0, volume_full_m3 = 0, area_m2 = 0, volume_m3 = 0, catchment_km2 = 0;
    double q_catch = 0, q_up = 0, q_shore = 0, p_lake = 0, e_lake = 0, q_out = 0; // m³/年
    std::vector<double> cell_x, cell_z, cell_h; // 范围格的格心与格高（生成器栅格）
    double min_x = 0, max_x = 0, min_z = 0, max_z = 0; // 格心的外框

    double center_x() const { return 0.5 * (min_x + max_x); }
    double center_z() const { return 0.5 * (min_z + max_z); }
    bool drawn() const { return status != LAKE_DRY && channel_cells == 0 && area_m2 > 0.0; }
};

struct WorldLakes {
    std::vector<WorldLake> lakes;
    double cell_m = 100.0; // 生成器格距（范围格往外最多一格是插值够得着的地面）
    int cycles_cut = 0;

    bool empty() const { return lakes.empty(); }
};

// lakes.tsv v1；p_cell_m = terrain.bin 的格距
bool parse_lakes(const std::string &p_text, double p_cell_m, WorldLakes &r_lakes, std::string &r_error);

// 湖面网格按河面网格的块（RiverMeshRules::chunk_m）挂：外框中心落在哪块就归哪块
std::vector<RiverChunkKey> lake_chunks(const WorldLakes &p_lakes, double p_chunk_m);

struct LakeMeshRules {
    double step_m = 4.0; // 湿区取样与水面网格的步长：比地面最细一级还粗一点，岸线由深度测试按真实地面切
};

struct LakeMesh {
    std::vector<float> positions; // xyz，相对块的西北角；y = 水位
    std::vector<float> uvs; // 世界 x、z（水纹按它）
    std::vector<int32_t> indices; // 正面朝上
    double min_x = 0, max_x = 0, min_y = 0, max_y = 0, min_z = 0, max_z = 0; // 相对块原点
    int lakes = 0;

    bool empty() const { return indices.empty(); }
};

// 一块里的湖面：取样点在该湖某个范围格心的一格以内、地面低于水位，且从格心起四连通才算湿；有湿角的取样方格出一块平面
LakeMesh build_lake_mesh(const WorldTerrain &p_terrain, const WorldLakes &p_lakes, const RiverChunkKey &p_key, double p_chunk_m, const LakeMeshRules &p_rules = LakeMeshRules());
