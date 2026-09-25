#include "world_grid.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr uint32_t VERSION = 4; // 最新版；1（没有水文）、2（没有河道与地形区）、3（高程是 u16 厘米，封顶 655 m）也读
constexpr double HEIGHT_SCALE = 100.0; // v1–v3 的高程：u16 = 米 × 100

struct Reader {
    const uint8_t *data;
    size_t size;
    size_t at = 0;

    bool take(void *p_out, size_t p_n) {
        if (at + p_n > size) {
            return false;
        }
        std::memcpy(p_out, data + at, p_n);
        at += p_n;
        return true;
    }
    // 文件是小端；本机（x86-64 / arm64）也是，直接拷
    bool u32(uint32_t &r) { return take(&r, 4); }
    bool f64(double &r) { return take(&r, 8); }
};

double catmull_rom(double p0, double p1, double p2, double p3, double t) {
    return p1 + 0.5 * t * (p2 - p0 + t * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * (3.0 * (p1 - p2) + p3 - p0)));
}

double bicubic(const WorldGrid &g, const std::vector<float> &p_values, double p_x, double p_z) {
    const double fc = g.col_of(p_x), fr = g.row_of(p_z);
    const int c1 = int(std::floor(fc)), r1 = int(std::floor(fr));
    const double tc = fc - c1, tr = fr - r1;
    auto at = [&](int r, int c) {
        r = std::clamp(r, 0, g.rows - 1);
        c = std::clamp(c, 0, g.cols - 1);
        return double(p_values[g.index(r, c)]);
    };
    double row[4];
    for (int k = 0; k < 4; k++) {
        const int r = r1 - 1 + k;
        row[k] = catmull_rom(at(r, c1 - 1), at(r, c1), at(r, c1 + 1), at(r, c1 + 2), tc);
    }
    return catmull_rom(row[0], row[1], row[2], row[3], tr);
}

// 与 skyisle-gen 的 landcover.classes 同序；调子向游戏里原有的草色 / 岩色靠
constexpr GroundColor PALETTE[] = {
    { 0.36f, 0.48f, 0.22f }, // 虚空（岸线外推出来的地，当草坡）
    { 0.40f, 0.40f, 0.31f }, // 崖缘（岩与草杂着）
    { 0.46f, 0.43f, 0.39f }, // 裸岩
    { 0.47f, 0.52f, 0.30f }, // 高山草甸
    { 0.21f, 0.35f, 0.16f }, // 林地
    { 0.33f, 0.41f, 0.20f }, // 灌丛
    { 0.36f, 0.48f, 0.22f }, // 草坡
    { 0.52f, 0.51f, 0.29f }, // 可耕地
    { 0.54f, 0.48f, 0.28f }, // 梯田
    { 0.26f, 0.40f, 0.30f }, // 湿地
    { 0.40f, 0.50f, 0.55f }, // 河道
    { 0.35f, 0.50f, 0.62f }, // 湖
};

} // namespace

GroundColor landcover_color(int p_class) {
    constexpr int n = int(sizeof(PALETTE) / sizeof(PALETTE[0]));
    return PALETTE[p_class >= 0 && p_class < n ? p_class : 0];
}

double WorldGrid::height_at(double p_x, double p_z) const {
    return bicubic(*this, height, p_x, p_z);
}

double WorldGrid::shore_at(double p_x, double p_z) const {
    return bicubic(*this, shore, p_x, p_z);
}

size_t WorldGrid::nearest_cell(double p_x, double p_z) const {
    const int c = std::clamp(int(std::lround(col_of(p_x))), 0, cols - 1);
    const int r = std::clamp(int(std::lround(row_of(p_z))), 0, rows - 1);
    return index(r, c);
}

int WorldGrid::land_count() const {
    return int(std::count_if(shore.begin(), shore.end(), [](float s) { return s > 0.0f; }));
}

bool parse_world_grid(const uint8_t *p_data, size_t p_size, WorldGrid &r_grid, std::string &r_error) {
    Reader in{ p_data, p_size };
    char magic[4];
    uint32_t version = 0, rows = 0, cols = 0;
    WorldGrid g;
    if (!in.take(magic, 4) || std::memcmp(magic, "ZZWG", 4) != 0) {
        r_error = "不是 terrain.bin（开头应为 ZZWG）";
        return false;
    }
    if (!in.u32(version) || version < 1 || version > VERSION) {
        r_error = "terrain.bin 版本不对：" + std::to_string(version);
        return false;
    }
    if (!in.u32(rows) || !in.u32(cols) || !in.f64(g.res) || !in.f64(g.x0) || !in.f64(g.z0) || rows == 0 || cols == 0 || !(g.res > 0.0)) {
        r_error = "terrain.bin 文件头残缺";
        return false;
    }
    g.rows = int(rows);
    g.cols = int(cols);
    const size_t n = size_t(rows) * size_t(cols);
    const size_t payload = n * (version >= 4 ? 19 : version >= 3 ? 17 : version >= 2 ? 12 : 7);
    if (in.size - in.at != payload) {
        r_error = "terrain.bin 长度不对：应有 " + std::to_string(payload) + " 字节数据，实有 " + std::to_string(in.size - in.at);
        return false;
    }
    std::vector<uint16_t> height;
    std::vector<float> height_m;
    std::vector<int16_t> shore(n);
    g.island.resize(n);
    g.landcover.resize(n);
    g.stream.resize(n);
    if (version >= 4) {
        height_m.resize(n);
        in.take(height_m.data(), n * 4);
    } else {
        height.resize(n);
        in.take(height.data(), n * 2);
    }
    in.take(shore.data(), n * 2);
    in.take(g.island.data(), n);
    in.take(g.landcover.data(), n);
    in.take(g.stream.data(), n);
    if (version >= 2) {
        g.flow.resize(n);
        g.flow_acc.resize(n);
        in.take(g.flow.data(), n);
        in.take(g.flow_acc.data(), n * 4);
    }
    if (version >= 3) {
        std::vector<uint16_t> width(n), depth(n);
        in.take(width.data(), n * 2);
        in.take(depth.data(), n * 2);
        g.zone.resize(n);
        in.take(g.zone.data(), n);
        g.river_width.resize(n);
        g.river_depth.resize(n);
        for (size_t k = 0; k < n; k++) {
            g.river_width[k] = float(width[k] / 10.0);
            g.river_depth[k] = float(depth[k] / 100.0);
        }
    }
    g.height.resize(n);
    g.shore.resize(n);
    for (size_t k = 0; k < n; k++) {
        g.height[k] = version >= 4 ? height_m[k] : float(height[k] / HEIGHT_SCALE);
        g.shore[k] = float(shore[k]);
    }
    r_grid = std::move(g);
    return true;
}
