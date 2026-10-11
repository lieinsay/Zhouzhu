#include "world_grid.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace {

constexpr uint32_t VERSION = 8; // 最新版；1（没有水文）、2（没有河道与地形区）、3（高程是 u16 厘米，封顶 655 m）、4（没有资源栅格）也读
constexpr const char *FIELD_KEYS[WorldGrid::FIELD_COUNT] = { "ore", "sulfur", "placer", "clay", "gravel", "stone", "salt" };
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
    if (source_landscape && island.size() == height.size() && !island.empty()) {
        const double fc = col_of(p_x), fr = row_of(p_z);
        const int c = int(std::floor(fc)), r = int(std::floor(fr));
        const uint8_t id = island[nearest_cell(p_x, p_z)];
        auto cell = [&](int rr, int cc) {
            return index(std::clamp(rr, 0, rows - 1), std::clamp(cc, 0, cols - 1));
        };
        bool one_island = true;
        for (int rr = r - 1; rr <= r + 2 && one_island; ++rr) {
            for (int cc = c - 1; cc <= c + 2; ++cc) {
                if (island[cell(rr, cc)] != id) { one_island = false; break; }
            }
        }
        if (!one_island) {
            // 虚空高程按最近陆格外推；另一岛的延拓不属于本岛岸坡。
            // 对同岛的 Catmull-Rom 权重归一；外圈以零权重进出支持，
            // 保持原格锚点和本岛曲面的连续性，不因切换插值阶数留下格线。
            const double tx = fc - c, tz = fr - r;
            double wx[4], wz[4];
            for (int k = 0; k < 4; ++k) {
                wx[k] = catmull_rom(k == 0, k == 1, k == 2, k == 3, tx);
                wz[k] = catmull_rom(k == 0, k == 1, k == 2, k == 3, tz);
            }
            double sum = 0.0, weight = 0.0;
            for (int dz = 0; dz < 4; ++dz) for (int dx = 0; dx < 4; ++dx) {
                const size_t k = cell(r + dz - 1, c + dx - 1);
                if (island[k] != id) continue;
                const double w = wx[dx] * wz[dz];
                if (w == 0.0) continue;
                sum += w * height[k];
                weight += w;
            }
            return weight > 0.0 ? sum / weight : height[nearest_cell(p_x, p_z)];
        }
    }
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

const char *WorldGrid::field_key(int p_field) {
    return p_field >= 0 && p_field < FIELD_COUNT ? FIELD_KEYS[p_field] : "";
}

int WorldGrid::field_of(const char *p_key) {
    for (int k = 0; k < FIELD_COUNT; k++) {
        if (std::strcmp(p_key, FIELD_KEYS[k]) == 0) {
            return k;
        }
    }
    return -1;
}

double WorldGrid::field_at(int p_field, double p_x, double p_z) const {
    if (field.empty() || p_field < 0 || p_field >= FIELD_COUNT) {
        return 0.0;
    }
    const double fc = col_of(p_x), fr = row_of(p_z);
    const int c0 = int(std::floor(fc)), r0 = int(std::floor(fr));
    const double tc = fc - c0, tr = fr - r0;
    const uint8_t *layer = field.data() + size_t(p_field) * height.size();
    auto at = [&](int r, int c) { return r < 0 || c < 0 || r >= rows || c >= cols ? 0.0 : double(layer[index(r, c)]); };
    const double top = at(r0, c0) + (at(r0, c0 + 1) - at(r0, c0)) * tc;
    const double bottom = at(r0 + 1, c0) + (at(r0 + 1, c0 + 1) - at(r0 + 1, c0)) * tc;
    return (top + (bottom - top) * tr) / 255.0;
}

int WorldGrid::patch_at(double p_x, double p_z) const {
    return patch.empty() ? -1 : patch_cell(nearest_cell(p_x, p_z));
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
    if (!in.u32(version) || version < 1 || version > VERSION || version == 6 || version == 7) {
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
    const size_t payload = n * (version == 8 ? 36 : version >= 5 ? 19 + 6 + 2 : version >= 4 ? 19 : version >= 3 ? 17 : version >= 2 ? 12 : 7);
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
    g.shore.resize(n);
    if (version == 8) in.take(g.shore.data(), n * 4);
    else in.take(shore.data(), n * 2);
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
    if (version >= 5) {
        g.field.resize(n * WorldGrid::FIELD_COUNT, 0);
        g.patch.resize(n);
        in.take(g.field.data(), n * (version == 8 ? WorldGrid::FIELD_COUNT : 6));
        in.take(g.patch.data(), n * 2);
    }
    if (version == 8) {
        g.source_landscape = true;
        g.lith.resize(n);
        g.rockwall_dir.resize(n);
        g.rockwall.resize(n);
        in.take(g.lith.data(), n);
        in.take(g.rockwall_dir.data(), n);
        in.take(g.rockwall.data(), n * 4);
        for (size_t k = 0; k < n; ++k) {
            if (!std::isfinite(g.shore[k]) || !std::isfinite(g.rockwall[k]) ||
                    g.rockwall[k] < 0 || g.lith[k] > 5 || (g.rockwall_dir[k] > 15 && g.rockwall_dir[k] != 255)) {
                r_error = "terrain.bin 地貌字段无效";
                return false;
            }
        }
    }
    g.height.resize(n);
    g.shore.resize(n);
    for (size_t k = 0; k < n; k++) {
        g.height[k] = version >= 4 ? height_m[k] : float(height[k] / HEIGHT_SCALE);
        if (version != 8) g.shore[k] = float(shore[k]);
    }
    r_grid = std::move(g);
    return true;
}

bool parse_source_rivers(const std::string &text, WorldGrid &g, std::string &error) {
    std::vector<WorldGrid::SourceStem> stems;
    std::vector<std::vector<double>> basins;
    std::vector<double> threshold;
    double we=0.26,de=0.4;
    std::istringstream input(text);
    std::string line,key;
    bool header=false;
    auto fail=[&]() { error="rivers.tsv 契约、索引或数值无效"; return false; };
    while(std::getline(input,line)) {
        std::istringstream row(line);
        if(!(row>>key) || key[0]=='#') continue;
        if(key=="rivers") { int v=0; if(header || !(row>>v) || v!=1) return fail(); header=true; }
        else if(!header) return fail();
        else if(key=="exponents") { if(!(row>>we>>de) || !(we>0 && de>0 && we<1 && de<1)) return fail(); }
        else if(key=="basin" || key=="threshold") {
            if(key=="basin") { int id=-1; if(!(row>>id) || id!=int(basins.size()) || id>=256) return fail(); basins.emplace_back(); }
            auto &values=key=="basin"?basins.back():threshold;
            double value;
            while(row>>value) { if(!std::isfinite(value) || value<0) return fail(); values.push_back(value); }
            if(values.empty() || !row.eof()) return fail();
        } else if(key=="stem") {
            int id=-1; WorldGrid::SourceStem stem;
            if(!(row>>id>>stem.down>>stem.join>>stem.basin>>stem.island>>stem.exit) || id!=int(stems.size())) return fail();
            stems.push_back(stem);
        } else if(key=="point") {
            if(stems.empty()) return fail();
            WorldGrid::SourcePoint p;
            if(!(row>>p.x>>p.z>>p.surface>>p.bed>>p.width>>p.depth>>p.mean_width>>p.mean_depth>>p.acc>>p.q_mean>>p.left>>p.right>>p.level)) return fail();
            const double numbers[]={p.x,p.z,p.surface,p.bed,p.width,p.depth,p.mean_width,p.mean_depth,p.acc,p.q_mean,p.left,p.right};
            for(double v:numbers) if(!std::isfinite(v)) return fail();
            if(p.width<=0 || p.depth<=0 || p.mean_width<0 || p.mean_depth<0 || p.acc<0 || p.q_mean<0 || p.left<0 || p.right<0 || p.level<0 || p.level>3) return fail();
            stems.back().points.push_back(p);
        } else return fail();
    }
    if(!header || basins.empty() || threshold.empty()) return fail();
    for(const auto &b:basins) if(b.size()!=threshold.size()) return fail();
    for(size_t i=0;i<stems.size();++i) {
        const auto &s=stems[i];
        if(s.points.empty() || s.basin < -1 || s.basin>=int(basins.size()) || s.down < -1 || s.down>=int(i) ||
                (s.down>=0 && (s.join<0 || s.join>=int(stems[size_t(s.down)].points.size())))) return fail();
    }
    g.source_rivers=true; g.station_width_exp=we; g.station_depth_exp=de;
    g.source_stems=std::move(stems); g.basin_daily=std::move(basins); g.stream_min_daily=std::move(threshold);
    return true;
}
