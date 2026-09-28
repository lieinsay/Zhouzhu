#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// 改地形的账（切片十一，方格台阶）：地面按 1 m 见方的格子记「比原地形高 / 低几级」，一级 0.5 m。不含 Godot 头。
//
// 格子与岛群地面块对齐：格 (i, j) 占 x ∈ [x0 + j, x0 + j + 1)、z ∈ [z0 + i, z0 + i + 1)（i 沿 z、j 沿 x，与地块格同一个习惯），
// (x0, z0) 取地面块四叉树根块的西北角（TileLayout），所以两格正好是叶块的一个粗格（2 m），16 格一块正好是叶块的八分之一。
// 一格的顶面 = 原地形 + 级数 × 0.5 m：格子里顶面顺着原来的坡，格与格之间级数不同就是一道竖直的坎（渲染与碰撞见 terrain_tile.h 的补丁）。
// 只记改过的块（16 × 16 格）；块不可变——改一格就换一块新的（写时复制），账本身是值，拷贝只拷块的指针。
// 所以上层可以把整份账包成 shared_ptr<const TerrainEdits> 交给后台线程（建地面块、量地面高），主线程改的时候换一份新的，互不相扰。

struct EditGrid {
    static constexpr double CELL_M = 1.0;
    static constexpr double STEP_M = 0.5; // 一级多高
    static constexpr int CHUNK = 16; // 一块每边几格

    double x0 = 0.0, z0 = 0.0; // 格 (0, 0) 的西北角

    int cell_i(double p_z) const; // 点落在第几行（向下取整）
    int cell_j(double p_x) const;
    double cell_x(int p_j) const { return x0 + p_j * CELL_M; } // 格的西北角
    double cell_z(int p_i) const { return z0 + p_i * CELL_M; }
    static int chunk_of(int p_cell); // 格所在的块（向下取整）
    static int64_t chunk_key(int p_ci, int p_cj) { return int64_t((uint64_t(uint32_t(p_ci)) << 32) | uint64_t(uint32_t(p_cj))); }
    static void chunk_unkey(int64_t p_key, int &r_ci, int &r_cj) {
        r_ci = int(int32_t(uint32_t(uint64_t(p_key) >> 32)));
        r_cj = int(int32_t(uint32_t(uint64_t(p_key) & 0xFFFFFFFFu)));
    }
};

// 一格改成几级（批量改用）
struct EditCellStep {
    int i = 0, j = 0;
    int step = 0;
};

struct EditChunk {
    int8_t steps[EditGrid::CHUNK * EditGrid::CHUNK] = {}; // 行主序：k = (i − ci·16)·16 + (j − cj·16)
    int nonzero = 0;

    int at(int p_di, int p_dj) const { return steps[p_di * EditGrid::CHUNK + p_dj]; }
};

class TerrainEdits {
    EditGrid grid;
    std::map<int64_t, std::shared_ptr<const EditChunk>> chunks; // 只有改过的块（全零的块删掉）
    uint64_t revision = 0; // 每改一格加一
    // 改过的块的外包框（世界坐标）：量地面高时先看点在不在框里，不在就不用查块
    double min_x = 0.0, min_z = 0.0, max_x = 0.0, max_z = 0.0;
    void update_bounds();

public:
    TerrainEdits() = default;
    explicit TerrainEdits(const EditGrid &p_grid) :
            grid(p_grid) {}

    const EditGrid &get_grid() const { return grid; }
    uint64_t get_revision() const { return revision; }
    bool empty() const { return chunks.empty(); }
    int chunk_count() const { return int(chunks.size()); }

    // 某格的级数（没改过为 0）
    int step_at(int p_i, int p_j) const;
    // 点所在那格比原地形高多少米（没改过为 0）
    double delta_at(double p_x, double p_z) const;
    // 点在不在改过的块的外包框里（外扩 p_margin）
    bool near_edits(double p_x, double p_z, double p_margin = 0.0) const;

    // 某格改成 p_step 级：返回改了以后的一份新账（本份不变）
    TerrainEdits with_step(int p_i, int p_j, int p_step) const;
    // 一次改多格（开发者的笔刷）：只换一份账，同一块只拷一回；版本按真改了的格数加（一格没变就原样返回）。同一格写了几回取最后一回
    TerrainEdits with_steps(const std::vector<EditCellStep> &p_cells) const;
    // 一次拼好整份账（读回用）：块键 → 块；全零的块不收
    static TerrainEdits from_chunks(const EditGrid &p_grid, const std::vector<std::pair<int64_t, EditChunk>> &p_chunks, uint64_t p_revision = 0);

    // 块：有没有改动、块里的级数（没改过返回 nullptr）
    bool chunk_active(int p_ci, int p_cj) const { return chunks.count(EditGrid::chunk_key(p_ci, p_cj)) > 0; }
    const EditChunk *chunk(int p_ci, int p_cj) const;
    // 改过的块（键见 EditGrid::chunk_key）
    std::vector<int64_t> chunk_keys() const;
    // 与 [x0, x1] × [z0, z1] 相交的改过的块
    std::vector<int64_t> chunks_in(double p_x0, double p_z0, double p_x1, double p_z1) const;
    // 数格子：改过的格一共几格、挖下去 / 垒上来的级数之和（测试、HUD）
    int cell_count() const;
};

// 存成文本 / 读回（按块写：改过的块一行，256 个级数，连着相同的写成「值*个数」）。格式见 terrain_edits.cpp 开头
std::string save_terrain_edits(const TerrainEdits &p_edits);
bool load_terrain_edits(const std::string &p_text, TerrainEdits &r_edits, std::string &r_error);
