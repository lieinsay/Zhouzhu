#include "planet_data.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;

// 小端读取器
struct Reader {
    const uint8_t *p = nullptr;
    size_t size = 0, pos = 0;
    bool ok = true;

    bool need(size_t p_n) {
        if (pos + p_n > size) {
            ok = false;
        }
        return ok;
    }
    template <typename T>
    T get() {
        T v{};
        if (need(sizeof(T))) {
            std::memcpy(&v, p + pos, sizeof(T));
            pos += sizeof(T);
        }
        return v;
    }
    std::string name() {
        const uint8_t n = get<uint8_t>();
        if (!need(n)) {
            return std::string();
        }
        std::string s(reinterpret_cast<const char *>(p + pos), n);
        pos += n;
        return s;
    }
    // 按类型码读 p_n 个值（0 f32、1 i32、2 i16、3 u8）
    template <typename T>
    bool values(uint8_t p_type, size_t p_n, std::vector<T> &r_out) {
        static const size_t SIZES[4] = { 4, 4, 2, 1 };
        if (p_type > 3 || !need(p_n * SIZES[p_type])) {
            ok = false;
            return false;
        }
        r_out.resize(p_n);
        for (size_t k = 0; k < p_n; k++) {
            switch (p_type) {
                case 0: r_out[k] = T(get<float>()); break;
                case 1: r_out[k] = T(get<int32_t>()); break;
                case 2: r_out[k] = T(get<int16_t>()); break;
                default: r_out[k] = T(get<uint8_t>()); break;
            }
        }
        return true;
    }
};

double col_or(const std::map<std::string, std::vector<double>> &p_cols, const char *p_name, size_t p_k, double p_default) {
    auto it = p_cols.find(p_name);
    return it != p_cols.end() && p_k < it->second.size() ? it->second[p_k] : p_default;
}

double central_angle(double p_lat1, double p_lon1, double p_lat2, double p_lon2) {
    const double f1 = p_lat1 * DEG, f2 = p_lat2 * DEG, dl = (p_lon2 - p_lon1) * DEG;
    const double c = std::sin(f1) * std::sin(f2) + std::cos(f1) * std::cos(f2) * std::cos(dl);
    return std::acos(std::clamp(c, -1.0, 1.0));
}

bool read_file(const std::string &p_path, std::vector<uint8_t> &r_bytes) {
    std::ifstream f(p_path, std::ios::binary);
    if (!f) {
        return false;
    }
    r_bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

double PlanetGrid::sample(double p_lat_deg, double p_lon_deg) const {
    if (rows == 0 || cols == 0) {
        return 0.0;
    }
    const double fr = std::clamp((p_lat_deg - lat0) / dlat, 0.0, double(rows - 1));
    double fc = std::fmod((p_lon_deg - lon0) / dlon, double(cols));
    if (fc < 0.0) {
        fc += cols;
    }
    const int r0 = std::min(int(fr), rows - 1), r1 = std::min(r0 + 1, rows - 1);
    const int c0 = int(fc) % cols, c1 = (c0 + 1) % cols;
    const double tr = fr - r0, tc = fc - std::floor(fc);
    const double a = at(r0, c0) * (1.0 - tc) + at(r0, c1) * tc;
    const double b = at(r1, c0) * (1.0 - tc) + at(r1, c1) * tc;
    return a * (1.0 - tr) + b * tr;
}

void PlanetLod::cell_center(int p_row, int p_col, double &r_x, double &r_z) const {
    const double res_km = res_m / 1000.0;
    r_x = (origin_x_km + (p_col + 0.5) * res_km) * 1000.0;
    r_z = -(origin_y_km - (p_row + 0.5) * res_km) * 1000.0;
}

double PlanetLod::land_km2() const {
    double s = 0.0;
    for (uint8_t v : land) {
        s += v / 255.0;
    }
    return s * (res_m / 1000.0) * (res_m / 1000.0);
}

bool PlanetData::parse_planet(const uint8_t *p_data, size_t p_size, std::string &r_error) {
    Reader r{ p_data, p_size };
    if (p_size < 4 || std::memcmp(p_data, "ZZPL", 4) != 0) {
        r_error = "不是行星包（planet.bin）";
        return false;
    }
    r.pos = 4;
    const uint32_t version = r.get<uint32_t>();
    if (version != 1) {
        r_error = "planet.bin 版本 " + std::to_string(version) + " 不认识";
        return false;
    }
    seed = int(r.get<uint32_t>());
    const uint32_t n = r.get<uint32_t>(), n_cols = r.get<uint32_t>(), n_grids = r.get<uint32_t>(), head_len = r.get<uint32_t>();
    if (!r.need(head_len)) {
        r_error = "planet.bin 截断（头）";
        return false;
    }
    std::istringstream head(std::string(reinterpret_cast<const char *>(p_data + r.pos), head_len));
    r.pos += head_len;
    std::string line;
    while (std::getline(head, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "radius_km") {
            ls >> radius_km;
        } else if (key == "rotation_hr") {
            ls >> rotation_hr;
        } else if (key == "tilt_deg") {
            ls >> tilt_deg;
        } else if (key == "calendar") {
            ls >> calendar.year_days >> calendar.seasons >> calendar.months_per_season >> calendar.days_per_month >> calendar.solstice_n;
        } else if (key == "bands") {
            ls >> band_tops_deg[0] >> band_tops_deg[1] >> band_tops_deg[2] >> band_tops_deg[3];
        } else if (key == "g_storm") {
            ls >> g_lat >> g_lon >> g_radius_deg;
        } else if (key == "source") {
            std::getline(ls, source);
            source.erase(0, source.find_first_not_of(' '));
        }
    }
    columns.clear();
    for (uint32_t k = 0; k < n_cols && r.ok; k++) {
        const std::string name = r.name();
        const uint8_t type = r.get<uint8_t>();
        std::vector<double> v;
        r.values(type, n, v);
        columns[name] = std::move(v);
    }
    grids.clear();
    for (uint32_t k = 0; k < n_grids && r.ok; k++) {
        const std::string name = r.name();
        PlanetGrid g;
        g.rows = r.get<uint16_t>();
        g.cols = r.get<uint16_t>();
        g.lat0 = r.get<float>();
        g.lon0 = r.get<float>();
        g.dlat = r.get<float>();
        g.dlon = r.get<float>();
        const uint8_t type = r.get<uint8_t>();
        r.values(type, size_t(g.rows) * size_t(g.cols), g.values);
        grids[name] = std::move(g);
    }
    if (!r.ok) {
        r_error = "planet.bin 截断";
        return false;
    }
    nodes.assign(n, PlanetNode());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (size_t k = 0; k < n; k++) {
        PlanetNode &p = nodes[k];
        p.lat = col_or(columns, "s03.lat", k, 0.0);
        p.lon = col_or(columns, "s03.lon", k, 0.0);
        p.area_km2 = col_or(columns, "s03.area_km2", k, 0.0);
        p.main_area_km2 = col_or(columns, "s03.main_area_km2", k, 0.0);
        p.height_m = col_or(columns, "s03.height_m", k, 0.0);
        p.peak_m = col_or(columns, "lod.peak_m", k, nan);
        p.rim_m = col_or(columns, "lod.rim_m", k, nan);
        p.territory_km2 = col_or(columns, "s03.territory_km2", k, 0.0);
        p.age = col_or(columns, "s03.age", k, 0.0);
        p.cls = int(col_or(columns, "s03.cls", k, 1.0));
        p.layered = col_or(columns, "s03.layered", k, 0.0) != 0.0;
        p.precip = col_or(columns, "s04.precip", k, 0.0);
        p.temp_c = col_or(columns, "s04.temp", k, 0.0);
        p.temp_sea_c = col_or(columns, "s04.temp_sea", k, 0.0);
        p.season_range_c = col_or(columns, "s04.season_range", k, 0.0);
        p.storm = col_or(columns, "s04.storm", k, 0.0);
        p.pop = col_or(columns, "s09.pop", k, 0.0);
        p.state = int(col_or(columns, "s09.state", k, -1.0));
        p.polity = int(col_or(columns, "s09.polity", k, -1.0));
    }
    build_buckets();
    return true;
}

bool PlanetData::parse_lods(const uint8_t *p_data, size_t p_size, std::string &r_error) {
    if (p_size < 12 || std::memcmp(p_data, "ZZLD", 4) != 0) {
        r_error = "不是粗版包（lods.bin）";
        return false;
    }
    Reader r{ p_data, p_size };
    r.pos = 4;
    const uint32_t version = r.get<uint32_t>();
    if (version != 1) {
        r_error = "lods.bin 版本 " + std::to_string(version) + " 不认识";
        return false;
    }
    const uint32_t n = r.get<uint32_t>();
    lod_entries.clear();
    lods_of.clear();
    for (uint32_t k = 0; k < n && r.ok; k++) {
        LodEntry e;
        e.node = int(r.get<uint32_t>());
        e.res_m = r.get<float>();
        e.origin_x_km = r.get<float>();
        e.origin_y_km = r.get<float>();
        e.rows = r.get<uint16_t>();
        e.cols = r.get<uint16_t>();
        e.n_islands = r.get<uint16_t>();
        r.get<uint16_t>();
        e.offset = r.get<uint64_t>();
        e.bytes = r.get<uint32_t>();
        if (e.offset + e.bytes > p_size) {
            r.ok = false;
        }
        lod_entries.push_back(e);
    }
    if (!r.ok) {
        r_error = "lods.bin 截断";
        lod_entries.clear();
        return false;
    }
    lod_bytes.assign(p_data, p_data + p_size);
    for (int k = 0; k < int(lod_entries.size()); k++) {
        lods_of[lod_entries[size_t(k)].node].push_back(k);
    }
    for (auto &kv : lods_of) {
        std::sort(kv.second.begin(), kv.second.end(), [&](int p_a, int p_b) { return lod_entries[size_t(p_a)].res_m > lod_entries[size_t(p_b)].res_m; });
    }
    return true;
}

bool PlanetData::load_files(const std::string &p_dir, std::string &r_error) {
    std::vector<uint8_t> bytes;
    if (!read_file(p_dir + "/planet.bin", bytes)) {
        r_error = "读不了 " + p_dir + "/planet.bin";
        return false;
    }
    if (!parse_planet(bytes.data(), bytes.size(), r_error)) {
        return false;
    }
    if (!read_file(p_dir + "/lods.bin", bytes)) {
        r_error = "读不了 " + p_dir + "/lods.bin";
        return false;
    }
    return parse_lods(bytes.data(), bytes.size(), r_error);
}

const std::vector<double> *PlanetData::column(const std::string &p_name) const {
    auto it = columns.find(p_name);
    return it != columns.end() ? &it->second : nullptr;
}

const PlanetGrid *PlanetData::grid(const std::string &p_name) const {
    auto it = grids.find(p_name);
    return it != grids.end() ? &it->second : nullptr;
}

void PlanetData::build_buckets() {
    bucket_rows = int(std::ceil(180.0 / BUCKET_DEG));
    bucket_cols = int(std::ceil(360.0 / BUCKET_DEG));
    buckets.assign(size_t(bucket_rows) * size_t(bucket_cols), std::vector<int>());
    for (int k = 0; k < int(nodes.size()); k++) {
        const int r = std::clamp(int((nodes[size_t(k)].lat + 90.0) / BUCKET_DEG), 0, bucket_rows - 1);
        double lon = std::fmod(nodes[size_t(k)].lon + 180.0, 360.0);
        if (lon < 0.0) {
            lon += 360.0;
        }
        const int c = std::clamp(int(lon / BUCKET_DEG), 0, bucket_cols - 1);
        buckets[size_t(r) * size_t(bucket_cols) + size_t(c)].push_back(k);
    }
}

std::vector<int> PlanetData::nodes_within(double p_lat_deg, double p_lon_deg, double p_radius_m) const {
    std::vector<std::pair<double, int>> hits;
    if (nodes.empty()) {
        return {};
    }
    const double R = radius_km * 1000.0;
    const double ang = p_radius_m / R; // 弧度
    const double dlat = ang / DEG;
    const int r_lo = std::clamp(int((p_lat_deg - dlat + 90.0) / BUCKET_DEG), 0, bucket_rows - 1);
    const int r_hi = std::clamp(int((p_lat_deg + dlat + 90.0) / BUCKET_DEG), 0, bucket_rows - 1);
    // 经度方向要扫多宽：按圈的最高纬度处的纬圈缩放；靠近极点或圈很大就整圈扫
    const double coslat = std::cos(std::min(89.9, std::abs(p_lat_deg) + dlat) * DEG);
    const int half = dlat + std::abs(p_lat_deg) >= 89.0 ? bucket_cols : int(std::ceil(dlat / coslat / BUCKET_DEG)) + 1;
    double lon = std::fmod(p_lon_deg + 180.0, 360.0);
    if (lon < 0.0) {
        lon += 360.0;
    }
    const int c_mid = int(lon / BUCKET_DEG);
    std::vector<int> cols_to_scan;
    if (2 * half + 1 >= bucket_cols) {
        for (int c = 0; c < bucket_cols; c++) {
            cols_to_scan.push_back(c);
        }
    } else {
        for (int dc = -half; dc <= half; dc++) {
            cols_to_scan.push_back(((c_mid + dc) % bucket_cols + bucket_cols) % bucket_cols);
        }
    }
    for (int r = r_lo; r <= r_hi; r++) {
        for (int c : cols_to_scan) {
            for (int k : buckets[size_t(r) * size_t(bucket_cols) + size_t(c)]) {
                const double a = central_angle(p_lat_deg, p_lon_deg, nodes[size_t(k)].lat, nodes[size_t(k)].lon);
                if (a <= ang) {
                    hits.push_back({ a, k });
                }
            }
        }
    }
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
    std::vector<int> out;
    out.reserve(hits.size());
    for (const auto &h : hits) {
        out.push_back(h.second);
    }
    return out;
}

int PlanetData::nearest_node(double p_lat_deg, double p_lon_deg) const {
    // 由近及远放大搜索圈；最稀的孤悬群也在两千公里内有邻居，这里给到半个星球
    for (double r = 200000.0; r < 2.1e7; r *= 3.0) {
        const std::vector<int> v = nodes_within(p_lat_deg, p_lon_deg, r);
        if (!v.empty()) {
            return v.front();
        }
    }
    return nodes.empty() ? -1 : 0;
}

double PlanetData::node_distance_m(int p_a, int p_b) const {
    const PlanetNode &a = nodes[size_t(p_a)], &b = nodes[size_t(p_b)];
    return central_angle(a.lat, a.lon, b.lat, b.lon) * radius_km * 1000.0;
}

std::vector<double> PlanetData::lod_resolutions(int p_node) const {
    std::vector<double> out;
    auto it = lods_of.find(p_node);
    if (it != lods_of.end()) {
        for (int k : it->second) {
            out.push_back(lod_entries[size_t(k)].res_m);
        }
    }
    return out;
}

int PlanetData::pick_lod(int p_node, double p_max_res_m) const {
    auto it = lods_of.find(p_node);
    if (it == lods_of.end() || it->second.empty()) {
        return -1;
    }
    int pick = it->second.back(); // 最细的
    for (int k : it->second) { // 从粗到细：第一份不超过 p_max_res_m 的
        if (lod_entries[size_t(k)].res_m <= p_max_res_m + 1e-6) {
            pick = k;
            break;
        }
    }
    return pick;
}

bool PlanetData::lod_extent(int p_node, double p_max_res_m, double &r_res_m, double &r_origin_x_km, double &r_origin_y_km, int &r_rows, int &r_cols) const {
    const int pick = pick_lod(p_node, p_max_res_m);
    if (pick < 0) {
        return false;
    }
    const LodEntry &e = lod_entries[size_t(pick)];
    r_res_m = e.res_m;
    r_origin_x_km = e.origin_x_km;
    r_origin_y_km = e.origin_y_km;
    r_rows = e.rows;
    r_cols = e.cols;
    return true;
}

PlanetLod PlanetData::lod(int p_node, double p_max_res_m) const {
    PlanetLod out;
    const int pick = pick_lod(p_node, p_max_res_m);
    if (pick < 0) {
        return out;
    }
    const LodEntry &e = lod_entries[size_t(pick)];
    out.node = e.node;
    out.res_m = e.res_m;
    out.origin_x_km = e.origin_x_km;
    out.origin_y_km = e.origin_y_km;
    out.rows = e.rows;
    out.cols = e.cols;
    Reader r{ lod_bytes.data() + e.offset, e.bytes };
    for (int k = 0; k < e.n_islands; k++) {
        PlanetLodIsland i;
        i.center_x_km = r.get<float>();
        i.center_y_km = r.get<float>();
        i.rim_m = r.get<float>();
        i.keel_m = r.get<float>();
        i.peak_m = r.get<float>();
        i.surface_m = r.get<float>();
        out.islands.push_back(i);
    }
    const size_t n = size_t(e.rows) * size_t(e.cols);
    out.height.assign(n, 0);
    out.land.assign(n, 0);
    out.landcover.assign(n, 0);
    out.island.assign(n, 0);
    size_t k = 0;
    while (k < n && r.ok) {
        const uint16_t head = r.get<uint16_t>();
        const size_t cnt = head & 0x7FFF;
        if (head & 0x8000) {
            for (size_t j = 0; j < cnt && k + j < n && r.ok; j++) {
                out.height[k + j] = r.get<uint16_t>();
                const uint8_t lc_land = r.get<uint8_t>(); // 地表类 << 4 | 陆地占比（0–15）
                out.landcover[k + j] = uint8_t(lc_land >> 4);
                out.land[k + j] = uint8_t((lc_land & 0x0F) * 17);
                out.island[k + j] = r.get<uint8_t>();
            }
        }
        k += cnt;
    }
    if (!r.ok || k != n) {
        return PlanetLod();
    }
    return out;
}
