#include "world_lakes.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace {

RiverChunkKey chunk_at(double p_x, double p_z, double p_chunk_m) {
    return RiverChunkKey{ int(std::floor(p_x / p_chunk_m)), int(std::floor(p_z / p_chunk_m)) };
}

} // namespace

bool parse_lakes(const std::string &p_text, double p_cell_m, WorldLakes &r_lakes, std::string &r_error) {
    r_lakes = WorldLakes();
    r_lakes.cell_m = p_cell_m;
    std::istringstream in(p_text);
    std::string line;
    bool header = false;
    WorldLake *open = nullptr;
    int line_no = 0;
    auto fail = [&](const std::string &p_what) {
        r_error = "lakes.tsv 第 " + std::to_string(line_no) + " 行：" + p_what;
        r_lakes = WorldLakes();
        return false;
    };
    while (std::getline(in, line)) {
        line_no++;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream row(line);
        std::string tag;
        if (!(row >> tag)) {
            continue;
        }
        if (tag == "lakes") {
            int version = 0;
            if (!(row >> version) || version != 1) {
                return fail("只认 v1");
            }
            header = true;
        } else if (!header) {
            return fail("缺 lakes 版本行");
        } else if (tag == "cycles_cut") {
            row >> r_lakes.cycles_cut;
        } else if (tag == "lake") {
            WorldLake L;
            std::string status;
            if (!(row >> L.id >> L.island >> status >> L.down >> L.channel_cells >> L.spill_z >> L.bottom_z >> L.level_z >> L.area_full_m2 >> L.volume_full_m3 >> L.area_m2 >>
                        L.volume_m3 >> L.catchment_km2 >> L.q_catch >> L.q_up >> L.q_shore >> L.p_lake >> L.e_lake >> L.q_out)) {
                return fail("湖的字段不全");
            }
            if (status == "dry") {
                L.status = LAKE_DRY;
            } else if (status == "closed") {
                L.status = LAKE_CLOSED;
            } else if (status == "full") {
                L.status = LAKE_FULL;
            } else {
                return fail("不认识的状态 " + status);
            }
            if (L.id != int(r_lakes.lakes.size()) || !(L.level_z >= L.bottom_z - 1e-9) || !(L.level_z <= L.spill_z + 1e-9)) {
                return fail("湖号不连续或水位不在湖底与溢口之间");
            }
            r_lakes.lakes.push_back(L);
            open = &r_lakes.lakes.back();
        } else if (tag == "cells") {
            if (!open || !open->cell_x.empty()) {
                return fail("cells 行不跟在 lake 行后面");
            }
            double x, z, h;
            while (row >> x >> z >> h) {
                open->cell_x.push_back(x);
                open->cell_z.push_back(z);
                open->cell_h.push_back(h);
            }
            if (open->cell_x.empty()) {
                return fail("湖没有范围格");
            }
            const auto [x0, x1] = std::minmax_element(open->cell_x.begin(), open->cell_x.end());
            const auto [z0, z1] = std::minmax_element(open->cell_z.begin(), open->cell_z.end());
            open->min_x = *x0;
            open->max_x = *x1;
            open->min_z = *z0;
            open->max_z = *z1;
        }
        // basis 等说明行：只给人看
    }
    for (const WorldLake &L : r_lakes.lakes) {
        if (L.cell_x.empty()) {
            return fail("湖 " + std::to_string(L.id) + " 没有范围格");
        }
    }
    return true;
}

std::vector<RiverChunkKey> lake_chunks(const WorldLakes &p_lakes, double p_chunk_m) {
    std::set<std::pair<int, int>> seen;
    std::vector<RiverChunkKey> out;
    for (const WorldLake &L : p_lakes.lakes) {
        if (!L.drawn()) {
            continue;
        }
        const RiverChunkKey key = chunk_at(L.center_x(), L.center_z(), p_chunk_m);
        if (seen.insert({ key.ix, key.iz }).second) {
            out.push_back(key);
        }
    }
    return out;
}

LakeMesh build_lake_mesh(const WorldTerrain &p_terrain, const WorldLakes &p_lakes, const RiverChunkKey &p_key, double p_chunk_m, const LakeMeshRules &p_rules) {
    LakeMesh mesh;
    const double ox = p_key.ix * p_chunk_m, oz = p_key.iz * p_chunk_m;
    const double cell = p_lakes.cell_m, step = p_rules.step_m;
    bool first = true;
    std::vector<uint8_t> allowed, below, wet;
    std::vector<int32_t> vertex_of;
    std::vector<size_t> stack;
    for (const WorldLake &L : p_lakes.lakes) {
        if (!L.drawn()) {
            continue;
        }
        const RiverChunkKey key = chunk_at(L.center_x(), L.center_z(), p_chunk_m);
        if (key.ix != p_key.ix || key.iz != p_key.iz) {
            continue;
        }
        // 取样格：范围格心的外框各往外一格
        const double x0 = L.min_x - cell, z0 = L.min_z - cell;
        const int nx = int(std::ceil((L.max_x - L.min_x + 2.0 * cell) / step)) + 1;
        const int nz = int(std::ceil((L.max_z - L.min_z + 2.0 * cell) / step)) + 1;
        const size_t n = size_t(nx) * size_t(nz);
        allowed.assign(n, 0);
        below.assign(n, 0);
        wet.assign(n, 0);
        // 范围格心一格以内（地面在格心之间按插值连着，再远就是别的格的事）
        for (size_t c = 0; c < L.cell_x.size(); c++) {
            const int a0 = std::max(0, int(std::ceil((L.cell_x[c] - cell - x0) / step + 1e-9)));
            const int a1 = std::min(nx - 1, int(std::floor((L.cell_x[c] + cell - x0) / step - 1e-9)));
            const int b0 = std::max(0, int(std::ceil((L.cell_z[c] - cell - z0) / step + 1e-9)));
            const int b1 = std::min(nz - 1, int(std::floor((L.cell_z[c] + cell - z0) / step - 1e-9)));
            for (int b = b0; b <= b1; b++) {
                for (int a = a0; a <= a1; a++) {
                    allowed[size_t(b) * nx + a] = 1;
                }
            }
        }
        for (int b = 0; b < nz; b++) {
            for (int a = 0; a < nx; a++) {
                const size_t k = size_t(b) * nx + a;
                double y = 0.0;
                below[k] = allowed[k] && p_terrain.surface(x0 + a * step, z0 + b * step, y) && y < L.level_z ? 1 : 0;
            }
        }
        // 从各范围格心起四连通地淹：低于水位却隔着溢口的地（出口外的下坡）不算
        stack.clear();
        for (size_t c = 0; c < L.cell_x.size(); c++) {
            const int a = int(std::lround((L.cell_x[c] - x0) / step)), b = int(std::lround((L.cell_z[c] - z0) / step));
            if (a >= 0 && b >= 0 && a < nx && b < nz) {
                const size_t k = size_t(b) * nx + a;
                if (below[k] && !wet[k]) {
                    wet[k] = 1;
                    stack.push_back(k);
                }
            }
        }
        while (!stack.empty()) {
            const size_t k = stack.back();
            stack.pop_back();
            const int a = int(k % size_t(nx)), b = int(k / size_t(nx));
            const int nb[4][2] = { { a - 1, b }, { a + 1, b }, { a, b - 1 }, { a, b + 1 } };
            for (const auto &p : nb) {
                if (p[0] < 0 || p[1] < 0 || p[0] >= nx || p[1] >= nz) {
                    continue;
                }
                const size_t q = size_t(p[1]) * nx + p[0];
                if (below[q] && !wet[q]) {
                    wet[q] = 1;
                    stack.push_back(q);
                }
            }
        }
        // 有湿角的取样方格出一块水面（比湿区外扩不到一步，岸线交给深度测试按真实地面切）
        vertex_of.assign(n, -1);
        auto vertex = [&](int a, int b) {
            const size_t k = size_t(b) * nx + a;
            if (vertex_of[k] < 0) {
                const double x = x0 + a * step, z = z0 + b * step;
                vertex_of[k] = int32_t(mesh.positions.size() / 3);
                mesh.positions.insert(mesh.positions.end(), { float(x - ox), float(L.level_z), float(z - oz) });
                mesh.uvs.insert(mesh.uvs.end(), { float(x), float(z) });
                if (first) {
                    mesh.min_x = mesh.max_x = x - ox;
                    mesh.min_z = mesh.max_z = z - oz;
                    mesh.min_y = mesh.max_y = L.level_z;
                    first = false;
                } else {
                    mesh.min_x = std::min(mesh.min_x, x - ox);
                    mesh.max_x = std::max(mesh.max_x, x - ox);
                    mesh.min_z = std::min(mesh.min_z, z - oz);
                    mesh.max_z = std::max(mesh.max_z, z - oz);
                    mesh.min_y = std::min(mesh.min_y, L.level_z);
                    mesh.max_y = std::max(mesh.max_y, L.level_z);
                }
            }
            return vertex_of[k];
        };
        bool any = false;
        for (int b = 0; b + 1 < nz; b++) {
            for (int a = 0; a + 1 < nx; a++) {
                const size_t k = size_t(b) * nx + a;
                if (!(wet[k] || wet[k + 1] || wet[k + size_t(nx)] || wet[k + size_t(nx) + 1])) {
                    continue;
                }
                const int32_t v00 = vertex(a, b), v10 = vertex(a + 1, b), v01 = vertex(a, b + 1), v11 = vertex(a + 1, b + 1);
                mesh.indices.insert(mesh.indices.end(), { v00, v10, v11, v00, v11, v01 });
                any = true;
            }
        }
        mesh.lakes += any ? 1 : 0;
    }
    return mesh;
}
