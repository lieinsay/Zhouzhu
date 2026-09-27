#include "terrain_edits.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

// 文本格式（UTF-8，一行一条，字段用制表符分开）：
//   # 注释
//   grid <x0> <z0>                    格 (0, 0) 的西北角（%.17g，读回逐位一样）
//   chunk <ci> <cj> <级数…>           一块 256 格、行主序，空格分开；连着 n 个相同的值写成「值*n」
// 读回时 grid 与现有的不一样（地形包换了、格子对不上）由上层决定怎么办：这里照读。

namespace {

std::string num(double p_v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", p_v);
    return buf;
}

} // namespace

int EditGrid::cell_i(double p_z) const {
    return int(std::floor((p_z - z0) / CELL_M));
}

int EditGrid::cell_j(double p_x) const {
    return int(std::floor((p_x - x0) / CELL_M));
}

int EditGrid::chunk_of(int p_cell) {
    return p_cell >= 0 ? p_cell / CHUNK : -((-p_cell + CHUNK - 1) / CHUNK);
}

void TerrainEdits::update_bounds() {
    if (chunks.empty()) {
        min_x = min_z = max_x = max_z = 0.0;
        return;
    }
    int ci0 = 1 << 30, cj0 = 1 << 30, ci1 = -(1 << 30), cj1 = -(1 << 30);
    for (const auto &kv : chunks) {
        int ci = 0, cj = 0;
        EditGrid::chunk_unkey(kv.first, ci, cj);
        ci0 = std::min(ci0, ci);
        cj0 = std::min(cj0, cj);
        ci1 = std::max(ci1, ci);
        cj1 = std::max(cj1, cj);
    }
    min_x = grid.cell_x(cj0 * EditGrid::CHUNK);
    min_z = grid.cell_z(ci0 * EditGrid::CHUNK);
    max_x = grid.cell_x((cj1 + 1) * EditGrid::CHUNK);
    max_z = grid.cell_z((ci1 + 1) * EditGrid::CHUNK);
}

int TerrainEdits::step_at(int p_i, int p_j) const {
    const int ci = EditGrid::chunk_of(p_i), cj = EditGrid::chunk_of(p_j);
    const auto found = chunks.find(EditGrid::chunk_key(ci, cj));
    if (found == chunks.end()) {
        return 0;
    }
    return found->second->at(p_i - ci * EditGrid::CHUNK, p_j - cj * EditGrid::CHUNK);
}

double TerrainEdits::delta_at(double p_x, double p_z) const {
    if (!near_edits(p_x, p_z)) {
        return 0.0;
    }
    return step_at(grid.cell_i(p_z), grid.cell_j(p_x)) * EditGrid::STEP_M;
}

bool TerrainEdits::near_edits(double p_x, double p_z, double p_margin) const {
    return !chunks.empty() && p_x >= min_x - p_margin && p_x < max_x + p_margin && p_z >= min_z - p_margin && p_z < max_z + p_margin;
}

TerrainEdits TerrainEdits::with_step(int p_i, int p_j, int p_step) const {
    TerrainEdits out = *this;
    const int step = std::clamp(p_step, -127, 127);
    const int ci = EditGrid::chunk_of(p_i), cj = EditGrid::chunk_of(p_j);
    const int64_t key = EditGrid::chunk_key(ci, cj);
    const auto found = chunks.find(key);
    const int k = (p_i - ci * EditGrid::CHUNK) * EditGrid::CHUNK + (p_j - cj * EditGrid::CHUNK);
    const int old = found == chunks.end() ? 0 : found->second->steps[k];
    if (old == step) {
        return out;
    }
    std::shared_ptr<EditChunk> chunk = found == chunks.end() ? std::make_shared<EditChunk>() : std::make_shared<EditChunk>(*found->second);
    chunk->steps[k] = int8_t(step);
    chunk->nonzero += (step != 0 ? 1 : 0) - (old != 0 ? 1 : 0);
    if (chunk->nonzero == 0) {
        out.chunks.erase(key);
    } else {
        out.chunks[key] = chunk;
    }
    out.revision = revision + 1;
    out.update_bounds();
    return out;
}

TerrainEdits TerrainEdits::from_chunks(const EditGrid &p_grid, const std::vector<std::pair<int64_t, EditChunk>> &p_chunks, uint64_t p_revision) {
    TerrainEdits out(p_grid);
    for (const auto &kv : p_chunks) {
        std::shared_ptr<EditChunk> chunk = std::make_shared<EditChunk>(kv.second);
        chunk->nonzero = 0;
        for (int8_t v : chunk->steps) {
            chunk->nonzero += v != 0 ? 1 : 0;
        }
        if (chunk->nonzero > 0) {
            out.chunks[kv.first] = chunk;
        }
    }
    out.revision = p_revision;
    out.update_bounds();
    return out;
}

const EditChunk *TerrainEdits::chunk(int p_ci, int p_cj) const {
    const auto found = chunks.find(EditGrid::chunk_key(p_ci, p_cj));
    return found == chunks.end() ? nullptr : found->second.get();
}

std::vector<int64_t> TerrainEdits::chunk_keys() const {
    std::vector<int64_t> out;
    out.reserve(chunks.size());
    for (const auto &kv : chunks) {
        out.push_back(kv.first);
    }
    return out;
}

std::vector<int64_t> TerrainEdits::chunks_in(double p_x0, double p_z0, double p_x1, double p_z1) const {
    std::vector<int64_t> out;
    if (chunks.empty() || p_x1 < min_x || p_x0 >= max_x || p_z1 < min_z || p_z0 >= max_z) {
        return out;
    }
    const int ci0 = EditGrid::chunk_of(grid.cell_i(p_z0)), ci1 = EditGrid::chunk_of(grid.cell_i(p_z1));
    const int cj0 = EditGrid::chunk_of(grid.cell_j(p_x0)), cj1 = EditGrid::chunk_of(grid.cell_j(p_x1));
    if (int64_t(ci1 - ci0 + 1) * int64_t(cj1 - cj0 + 1) > int64_t(chunks.size())) {
        for (const auto &kv : chunks) {
            int ci = 0, cj = 0;
            EditGrid::chunk_unkey(kv.first, ci, cj);
            if (ci >= ci0 && ci <= ci1 && cj >= cj0 && cj <= cj1) {
                out.push_back(kv.first);
            }
        }
        return out;
    }
    for (int ci = ci0; ci <= ci1; ci++) {
        for (int cj = cj0; cj <= cj1; cj++) {
            if (chunk_active(ci, cj)) {
                out.push_back(EditGrid::chunk_key(ci, cj));
            }
        }
    }
    return out;
}

int TerrainEdits::cell_count() const {
    int n = 0;
    for (const auto &kv : chunks) {
        n += kv.second->nonzero;
    }
    return n;
}

std::string save_terrain_edits(const TerrainEdits &p_edits) {
    std::string out = "# 州渚 · 改地形（切片十一：1 m 一格、一级 0.5 m）\n";
    out += "grid\t" + num(p_edits.get_grid().x0) + "\t" + num(p_edits.get_grid().z0) + "\n";
    constexpr int N = EditGrid::CHUNK * EditGrid::CHUNK;
    for (int64_t key : p_edits.chunk_keys()) {
        int ci = 0, cj = 0;
        EditGrid::chunk_unkey(key, ci, cj);
        const EditChunk *c = p_edits.chunk(ci, cj);
        out += "chunk\t" + std::to_string(ci) + "\t" + std::to_string(cj) + "\t";
        for (int k = 0; k < N;) {
            int run = 1;
            while (k + run < N && c->steps[k + run] == c->steps[k]) {
                run++;
            }
            if (k > 0) {
                out += " ";
            }
            out += std::to_string(int(c->steps[k]));
            if (run > 1) {
                out += "*" + std::to_string(run);
            }
            k += run;
        }
        out += "\n";
    }
    return out;
}

bool load_terrain_edits(const std::string &p_text, TerrainEdits &r_edits, std::string &r_error) {
    EditGrid grid;
    struct Row {
        int ci, cj;
        std::vector<int> steps;
    };
    std::vector<Row> rows;
    size_t pos = 0;
    int line_no = 0;
    auto fail = [&](const std::string &p_why) {
        r_error = "第 " + std::to_string(line_no) + " 行：" + p_why;
        return false;
    };
    constexpr int N = EditGrid::CHUNK * EditGrid::CHUNK;
    while (pos < p_text.size()) {
        size_t end = p_text.find('\n', pos);
        if (end == std::string::npos) {
            end = p_text.size();
        }
        std::string line = p_text.substr(pos, end - pos);
        pos = end + 1;
        line_no++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, '\t')) {
            f.push_back(item);
        }
        if (f[0] == "grid" && f.size() >= 3) {
            grid.x0 = std::strtod(f[1].c_str(), nullptr);
            grid.z0 = std::strtod(f[2].c_str(), nullptr);
        } else if (f[0] == "chunk" && f.size() >= 4) {
            Row row{ std::atoi(f[1].c_str()), std::atoi(f[2].c_str()), {} };
            std::stringstream vs(f[3]);
            std::string token;
            while (vs >> token) {
                const size_t star = token.find('*');
                const int v = std::atoi(token.substr(0, star).c_str());
                const int run = star == std::string::npos ? 1 : std::atoi(token.substr(star + 1).c_str());
                if (run < 1 || v < -127 || v > 127 || int(row.steps.size()) + run > N) {
                    return fail("级数不对：" + token);
                }
                row.steps.insert(row.steps.end(), size_t(run), v);
            }
            if (int(row.steps.size()) != N) {
                return fail("一块应有 256 格，只有 " + std::to_string(row.steps.size()));
            }
            rows.push_back(std::move(row));
        } else {
            return fail("认不得：" + f[0]);
        }
    }
    std::vector<std::pair<int64_t, EditChunk>> chunks;
    for (const Row &row : rows) {
        EditChunk c;
        for (int k = 0; k < N; k++) {
            c.steps[k] = int8_t(row.steps[size_t(k)]);
        }
        chunks.push_back({ EditGrid::chunk_key(row.ci, row.cj), c });
    }
    r_edits = TerrainEdits::from_chunks(grid, chunks, 1);
    return true;
}
