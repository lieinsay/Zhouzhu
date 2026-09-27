#include "planet_weather.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "inflate.h"
#include "weather_noise.h"

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;
constexpr int KIND_COUNT = int(WeatherKind::COUNT);

double smooth01(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

long long pmod(long long p_a, long long p_n) {
    const long long m = p_a % p_n;
    return m < 0 ? m + p_n : m;
}

std::vector<std::string> split(const std::string &p_text, char p_sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p_text) {
        if (c == p_sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

struct Reader {
    const uint8_t *p = nullptr;
    size_t size = 0, pos = 0;
    bool ok = true;
    template <typename T>
    T get() {
        T v{};
        if (pos + sizeof(T) > size) {
            ok = false;
            return v;
        }
        std::memcpy(&v, p + pos, sizeof(T));
        pos += sizeof(T);
        return v;
    }
};

int64_t unzigzag(uint32_t p_v) {
    return (p_v & 1u) ? -int64_t((p_v + 1u) >> 1) : int64_t(p_v >> 1);
}

// 此地（单位向量 p）的地理东、北
void east_north(const PlanetVec &p_unit, PlanetVec &r_east, PlanetVec &r_north) {
    const PlanetVec z(0.0, 0.0, 1.0);
    PlanetVec e = z.cross(p_unit);
    if (e.length() < 1e-12) {
        e = PlanetVec(0.0, 1.0, 0.0); // 极点：随便挑一个
    }
    r_east = e.normalized();
    r_north = p_unit.cross(r_east);
}

} // namespace

double planet_plane_turn(const GroupFrame &p_frame, const PlanetVec &p_unit, double p_x, double p_z) {
    const double s = std::sqrt(p_x * p_x + p_z * p_z);
    if (s < 1.0) {
        return 0.0;
    }
    const double beta_plane = std::atan2(p_x, -p_z); // 平面上：x 东、−z 北
    const PlanetVec c = p_frame.up;
    const PlanetVec r = (p_unit * p_unit.dot(c) - c); // 此地背着群心的方向（切向）
    if (r.length() < 1e-15) {
        return 0.0;
    }
    PlanetVec e, n;
    east_north(p_unit, e, n);
    const double beta_here = std::atan2(r.dot(e), r.dot(n));
    double g = beta_plane - beta_here;
    while (g > PI) {
        g -= 2.0 * PI;
    }
    while (g < -PI) {
        g += 2.0 * PI;
    }
    return g;
}

namespace {

// 地理的（东、北）→ 方位角转 γ 之后的（x 东、z 南）
void turn_en(double p_e, double p_n, double p_gamma, double &r_x, double &r_z) {
    if (p_gamma == 0.0) {
        r_x = p_e;
        r_z = -p_n;
        return;
    }
    const double c = std::cos(p_gamma), s = std::sin(p_gamma);
    r_x = p_e * c + p_n * s;
    r_z = -(p_n * c - p_e * s);
}

} // namespace

// ---------------------------------------------------------------- 逐日天气包

ClimateDay WeatherPackDay::unpack() const {
    ClimateDay d;
    d.kind = WeatherKind(std::min(int(kind_sail & 15), KIND_COUNT - 1));
    d.sailable = (kind_sail >> 4) & 1;
    d.storm = storm;
    d.precip_mm = precip10 / 10.0;
    d.temp_c = temp10 / 10.0;
    d.wind_from_deg = double(wind_from);
    d.wind_ms = wind10 / 10.0;
    return d;
}

bool WeatherPack::parse(const uint8_t *p_data, size_t p_size, std::string &r_error) {
    if (p_size < 32 || std::memcmp(p_data, "ZZWX", 4) != 0) {
        r_error = "不是逐日天气包（weather.bin）";
        return false;
    }
    Reader r{ p_data, p_size };
    r.pos = 4;
    const uint32_t version = r.get<uint32_t>();
    if (version != 1) {
        r_error = "weather.bin 版本 " + std::to_string(version) + " 不认识";
        return false;
    }
    const uint32_t D = r.get<uint32_t>(), N = r.get<uint32_t>(), head_len = r.get<uint32_t>(), packed_len = r.get<uint32_t>(), raw_len = r.get<uint32_t>();
    if (D == 0 || D > 10000 || r.pos + head_len + size_t(N) * 36 + packed_len > p_size) {
        r_error = "weather.bin 截断（头）";
        return false;
    }
    year_days = int(D);
    std::istringstream head(std::string(reinterpret_cast<const char *>(p_data + r.pos), head_len));
    r.pos += head_len;
    std::string line;
    season_sets.clear();
    while (std::getline(head, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "types") {
            std::string names;
            ls >> names;
            const std::vector<std::string> t = split(names, ',');
            if (int(t.size()) != KIND_COUNT) {
                r_error = "weather.bin 的天气类型表不是九种";
                return false;
            }
            for (int k = 0; k < KIND_COUNT; k++) {
                if (t[size_t(k)] != weather_kinds::at(WeatherKind(k)).name) {
                    r_error = "weather.bin 的天气类型表与游戏的不一样：" + t[size_t(k)];
                    return false;
                }
            }
        } else if (key == "calendar") {
            ls >> seasons >> months_per_season >> days_per_month >> solstice_n;
        } else if (key == "year") {
            ls >> year;
        } else if (key == "precip_formula") {
            ls >> precip_min >> precip_max >> precip_exp;
        } else if (key == "season_names") {
            int k = 0;
            std::string names;
            ls >> k >> names;
            if (k >= 0 && k < 256) {
                if (int(season_sets.size()) <= k) {
                    season_sets.resize(size_t(k) + 1);
                }
                season_sets[size_t(k)] = split(names, ',');
            }
        } else if (key == "source") {
            std::getline(ls, source);
            source.erase(0, source.find_first_not_of(' '));
        }
    }
    entries.assign(N, WeatherPackEntry());
    for (uint32_t k = 0; k < N; k++) {
        WeatherPackEntry &e = entries[k];
        e.node = int(r.get<uint32_t>());
        e.season_set = r.get<uint8_t>();
        r.get<uint8_t>();
        r.get<uint16_t>();
        e.ref_m = r.get<float>();
        e.lapse = r.get<float>();
        e.rim_m = r.get<float>();
        e.fog_rise_m = r.get<float>();
        e.snow_temp = r.get<float>();
        e.precip_mm = r.get<float>();
        e.precip_climate_mm = r.get<float>();
    }
    if (!r.ok) {
        r_error = "weather.bin 截断（索引）";
        return false;
    }
    std::vector<uint8_t> raw;
    if (!zlib_inflate(p_data + r.pos, packed_len, raw, raw_len, r_error)) {
        r_error = "weather.bin 解不开：" + r_error;
        return false;
    }
    // 按列：type|sail、湿日的雨量（u16 两个字节面）、风暴日的风暴号、气温 / 风向 / 风速的 zigzag 差（各 u16 两个字节面）
    const size_t n = size_t(N) * size_t(D);
    days.assign(n, WeatherPackDay());
    size_t at = 0;
    if (raw.size() < n) {
        r_error = "weather.bin 数据太短";
        return false;
    }
    size_t wet = 0, storms = 0;
    for (size_t k = 0; k < n; k++) {
        days[k].kind_sail = raw[at + k];
        const int kind = raw[at + k] & 15;
        if (kind >= KIND_COUNT) {
            r_error = "weather.bin 里有不认识的天气类型";
            return false;
        }
        wet += kind >= 2 ? 1 : 0;
        storms += (kind == int(WeatherKind::STORM) || kind == int(WeatherKind::BLIZZARD)) ? 1 : 0;
    }
    at += n;
    auto u16_planes = [&](size_t p_count, size_t p_index) -> uint32_t { return uint32_t(raw[at + p_index]) | (uint32_t(raw[at + p_count + p_index]) << 8); };
    if (raw.size() < at + 2 * wet + storms + 6 * n) {
        r_error = "weather.bin 数据长度对不上";
        return false;
    }
    size_t j = 0;
    for (size_t k = 0; k < n; k++) {
        if ((days[k].kind_sail & 15) >= 2) {
            days[k].precip10 = uint16_t(u16_planes(wet, j++));
        }
    }
    at += 2 * wet;
    j = 0;
    for (size_t k = 0; k < n; k++) {
        const int kind = days[k].kind_sail & 15;
        if (kind == int(WeatherKind::STORM) || kind == int(WeatherKind::BLIZZARD)) {
            days[k].storm = raw[at + j++];
        }
    }
    at += storms;
    for (int col = 0; col < 3; col++) {
        for (size_t e = 0; e < N; e++) {
            int64_t v = 0;
            for (size_t d = 0; d < D; d++) {
                const size_t k = e * D + d;
                v += unzigzag(u16_planes(n, k));
                if (col == 0) {
                    days[k].temp10 = int16_t(v);
                } else if (col == 1) {
                    days[k].wind_from = uint16_t(v);
                } else {
                    days[k].wind10 = uint16_t(v);
                }
            }
        }
        at += 2 * n;
    }
    if (at != raw.size()) {
        r_error = "weather.bin 数据多了 / 少了";
        return false;
    }
    return true;
}

Climate WeatherPack::climate(int p_entry, double p_lat, double p_lon, double p_tilt) const {
    Climate c;
    const WeatherPackEntry &e = entries[size_t(p_entry)];
    c.calendar.year_days = year_days;
    c.calendar.seasons = seasons;
    c.calendar.months_per_season = months_per_season;
    c.calendar.days_per_month = days_per_month;
    c.calendar.synodic_days = double(days_per_month);
    c.calendar.solstice_n = solstice_n;
    c.calendar.latitude_deg = p_lat;
    c.calendar.longitude_deg = p_lon;
    c.calendar.tilt_deg = p_tilt;
    c.precip_mm = e.precip_mm;
    c.ref_m = e.ref_m;
    c.lapse = e.lapse;
    c.rim_m = e.rim_m;
    c.fog_rise_m = e.fog_rise_m;
    c.snow_temp = e.snow_temp;
    if (e.season_set >= 0 && e.season_set < int(season_sets.size())) {
        c.season_names = season_sets[size_t(e.season_set)];
    }
    const WeatherPackDay *d = days_of(p_entry);
    c.days.reserve(size_t(year_days));
    for (int k = 0; k < year_days; k++) {
        c.days.push_back(d[k].unpack());
    }
    return c;
}

// ---------------------------------------------------------------- 一群的逐日表

struct PlanetWeather::Table {
    double ref_m = 300.0, lapse = 6.0, rim_m = 200.0, fog_rise_m = 150.0, snow_temp = 0.5;
    int year = 336;
    std::shared_ptr<const std::vector<WeatherPackDay>> pack; // 行星包的（packed 指进来）
    const WeatherPackDay *packed = nullptr;
    std::vector<ClimateDay> days; // 细版的 weather.tsv
    bool fine = false;
    // 云团漂移：每天子夜累计漂了多远（与 WeatherModel::build_tables 同一个加法次序），头一回要时算
    mutable std::once_flag drift_once;
    mutable std::vector<double> drift_x, drift_z;

    ClimateDay day(long long p_day) const {
        const long long k = pmod(p_day, (long long)year);
        return fine ? days[size_t(k)] : packed[size_t(k)].unpack();
    }
};

// ---------------------------------------------------------------- 场

PlanetWeather::PlanetWeather(std::shared_ptr<const PlanetData> p_planet, const PlanetWeatherRules &p_rules) :
        planet(std::move(p_planet)), rules(p_rules) {
    PlanetShape shape;
    shape.sea_radius_m = planet->radius_km * 1000.0;
    radius_m = shape.radius();
    year_days = std::max(1, planet->calendar.year_days);
    solstice_n = planet->calendar.solstice_n;
    tilt_deg = planet->tilt_deg;
    const int n = planet->node_count();
    frames.reserve(size_t(n));
    for (int k = 0; k < n; k++) {
        frames.emplace_back(planet->node(k).lat, planet->node(k).lon, shape);
    }
    g_precip = planet->grid("s04.climate_grid.precip");
    g_storm = planet->grid("s04.climate_grid.storm");
    g_temp = planet->grid("s04.climate_grid.temp");
    g_range = planet->grid("s04.climate_grid.season_range");
    g_u = planet->grid("s04.wind_local.u");
    g_v = planet->grid("s04.wind_local.v");
    g_edges = planet->grid("s04.band_local.edges");
    g_dir = planet_unit(planet->g_lat, planet->g_lon);
    g_radius = planet->g_radius_deg * DEG;
    // 找附近的群
    bucket_rows = int(std::ceil(180.0 / BUCKET_DEG));
    bucket_cols = int(std::ceil(360.0 / BUCKET_DEG));
    buckets.assign(size_t(bucket_rows) * size_t(bucket_cols), std::vector<int>());
    for (int k = 0; k < n; k++) {
        const int r = std::clamp(int((planet->node(k).lat + 90.0) / BUCKET_DEG), 0, bucket_rows - 1);
        double lon = std::fmod(planet->node(k).lon + 180.0, 360.0);
        lon += lon < 0.0 ? 360.0 : 0.0;
        const int c = std::clamp(int(lon / BUCKET_DEG), 0, bucket_cols - 1);
        buckets[size_t(r) * size_t(bucket_cols) + size_t(c)].push_back(k);
    }
    // 一群管多大：陆地铺多远（粗版量的），不过到最近邻群心距离的一半
    const std::vector<double> *extent = planet->column("lod.extent_km");
    reach.assign(size_t(n), rules.reach_min_km);
    wind2_center.assign(size_t(n), PlanetVec());
    for (int k = 0; k < n; k++) {
        const PlanetNode &nd = planet->node(k);
        double nn = 1e9;
        for (double r : { 300.0, 1500.0, 6000.0, 21000.0 }) {
            for_nodes_within(nd.lat, nd.lon, r, [&](int p_j, double p_d_km, const PlanetVec &) {
                if (p_j != k) {
                    nn = std::min(nn, p_d_km);
                }
            });
            if (nn < r) {
                break;
            }
        }
        double ext = extent && size_t(k) < extent->size() ? (*extent)[size_t(k)] : std::nan("");
        if (!std::isfinite(ext)) {
            ext = 1.5 * std::sqrt(std::max(0.0, nd.area_km2) / PI); // 还没有粗版（生存世界）：按陆地面积估
        }
        reach[size_t(k)] = std::clamp(std::min(ext, rules.reach_cap * nn), rules.reach_min_km, rules.reach_max_km);
        reach_max = std::max(reach_max, reach[size_t(k)]);
        double e = 0.0, nn2 = 0.0;
        wind2_at(nd.lat, nd.lon, e, nn2);
        wind2_center[size_t(k)] = PlanetVec(e, nn2, 0.0);
    }
    tables.assign(size_t(n), nullptr);
    over_nodes.assign(size_t(n), 0);
}

PlanetWeather::~PlanetWeather() = default;

template <typename F>
void PlanetWeather::for_nodes_within(double p_lat, double p_lon, double p_radius_km, F &&p_fn) const {
    const PlanetVec q = planet_unit(p_lat, p_lon);
    const double R_km = radius_m / 1000.0;
    const double ang = std::min(PI, p_radius_km / R_km);
    const double dlat = ang / DEG;
    const int r_lo = std::clamp(int((p_lat - dlat + 90.0) / BUCKET_DEG), 0, bucket_rows - 1);
    const int r_hi = std::clamp(int((p_lat + dlat + 90.0) / BUCKET_DEG), 0, bucket_rows - 1);
    const double coslat = std::cos(std::min(89.9, std::abs(p_lat) + dlat) * DEG);
    const bool all_cols = dlat + std::abs(p_lat) >= 89.0;
    const int half = all_cols ? bucket_cols : int(std::ceil(dlat / coslat / BUCKET_DEG)) + 1;
    double lon = std::fmod(p_lon + 180.0, 360.0);
    lon += lon < 0.0 ? 360.0 : 0.0;
    const int c_mid = int(lon / BUCKET_DEG);
    const bool wrap_all = 2 * half + 1 >= bucket_cols;
    const double cos_ang = std::cos(ang);
    for (int r = r_lo; r <= r_hi; r++) {
        const int n_cols = wrap_all ? bucket_cols : 2 * half + 1;
        for (int i = 0; i < n_cols; i++) {
            const int c = wrap_all ? i : ((c_mid - half + i) % bucket_cols + bucket_cols) % bucket_cols;
            for (int k : buckets[size_t(r) * size_t(bucket_cols) + size_t(c)]) {
                const PlanetVec &u = frames[size_t(k)].up;
                const double cd = q.dot(u);
                if (cd < cos_ang - 1e-12) {
                    continue;
                }
                const double d_km = std::atan2(q.cross(u).length(), cd) * R_km;
                p_fn(k, d_km, u);
            }
        }
    }
}

void PlanetWeather::wind2_at(double p_lat, double p_lon, double &r_e, double &r_n) const {
    r_e = g_u ? g_u->sample(p_lat, p_lon) : 0.0;
    r_n = g_v ? g_v->sample(p_lat, p_lon) : 0.0;
    if (!std::isfinite(r_e) || !std::isfinite(r_n)) {
        r_e = r_n = 0.0;
    }
}

double PlanetWeather::precip_mm_of(double p_rel) const {
    return precip_min + (precip_max - precip_min) * std::pow(std::clamp(p_rel, 0.0, 1.0), precip_exp);
}

double PlanetWeather::climate_cloud(double p_lat, double p_lon) const {
    const double p = g_precip ? g_precip->sample(p_lat, p_lon) : 0.35;
    const double s = g_storm ? g_storm->sample(p_lat, p_lon) : 0.25;
    return std::clamp(rules.clim_cloud0 + rules.clim_cloud_precip * p + rules.clim_cloud_storm * s, 0.05, 0.95);
}

double PlanetWeather::climate_precip_mm(double p_lat, double p_lon) const {
    return precip_mm_of(g_precip ? g_precip->sample(p_lat, p_lon) : 0.35);
}

double PlanetWeather::band_shift(double p_day) const {
    const double year = double(year_days);
    const double w = 2.0 * PI / year;
    const double amp = rules.band_k_shift * tilt_deg / std::sqrt(1.0 + (w * rules.band_tau_days) * (w * rules.band_tau_days));
    const double doy = p_day - std::floor(p_day / year) * year;
    return amp * std::cos(2.0 * PI * (doy - solstice_n) / year - std::atan(w * rules.band_tau_days));
}

double PlanetWeather::zone_at(double p_day, double p_lat, double p_lon, int &r_kind, double &r_out_e, double &r_out_n) const {
    r_kind = 0;
    r_out_e = r_out_n = 0.0;
    // 赤道永暴带：带界随太阳摆动（按当季往北挪）
    const double lat_eff = p_lat - band_shift(p_day);
    double edge = 6.0;
    if (g_edges && g_edges->rows == 8) {
        double lon = std::fmod(p_lon - g_edges->lon0, 360.0);
        lon += lon < 0.0 ? 360.0 : 0.0;
        const int c = std::clamp(int(std::floor(lon / g_edges->dlon + 0.5)) % g_edges->cols, 0, g_edges->cols - 1);
        edge = std::abs(g_edges->at(lat_eff >= 0.0 ? 0 : 4, c));
    }
    const double eq = 1.0 - smooth01(edge - rules.zone_edge_deg, edge + rules.zone_edge_deg, std::abs(lat_eff));
    // G 定点永暴
    const PlanetVec p = planet_unit(p_lat, p_lon);
    const double gd = std::atan2(p.cross(g_dir).length(), p.dot(g_dir));
    const double gz = 1.0 - smooth01(1.0 - rules.g_edge, 1.0, gd / std::max(1e-6, g_radius));
    if (eq <= 0.0 && gz <= 0.0) {
        return 0.0;
    }
    if (eq >= gz) {
        r_kind = 1;
        r_out_n = lat_eff >= 0.0 ? 1.0 : -1.0;
    } else {
        r_kind = 2;
        PlanetVec e, n;
        east_north(p, e, n);
        const PlanetVec away = p * p.dot(g_dir) - g_dir;
        const double l = std::hypot(away.dot(e), away.dot(n));
        if (l > 1e-12) {
            r_out_e = away.dot(e) / l;
            r_out_n = away.dot(n) / l;
        }
    }
    return std::max(eq, gz);
}

double PlanetWeather::zone_static(const PlanetVec &p_unit, double p_lat, double p_lon) const {
    // 权里用的是年均的带界（不随季节）：权只跟位置有关，全行星的天气贴图能预先算好
    double edge = 6.0;
    if (g_edges && g_edges->rows == 8) {
        double lon = std::fmod(p_lon - g_edges->lon0, 360.0);
        lon += lon < 0.0 ? 360.0 : 0.0;
        const int c = std::clamp(int(std::floor(lon / g_edges->dlon + 0.5)) % g_edges->cols, 0, g_edges->cols - 1);
        edge = std::abs(g_edges->at(p_lat >= 0.0 ? 0 : 4, c));
    }
    const double eq = 1.0 - smooth01(edge - rules.zone_edge_deg, edge + rules.zone_edge_deg, std::abs(p_lat));
    const double gd = std::atan2(p_unit.cross(g_dir).length(), p_unit.dot(g_dir));
    const double gz = 1.0 - smooth01(1.0 - rules.g_edge, 1.0, gd / std::max(1e-6, g_radius));
    return std::max(eq, gz);
}

WeatherState PlanetWeather::climate_state(double p_day, double p_lat, double p_lon, double p_h_ref) const {
    WeatherState s;
    s.cloud = climate_cloud(p_lat, p_lon);
    s.coverage = std::clamp(0.6 * s.cloud - 0.08, 0.03, 0.6);
    s.mm = climate_precip_mm(p_lat, p_lon) / double(year_days);
    // 气温：零点处的年均温 + 半个年温差 × 余弦（最暖 = 该半球夏至 + 45 天，同 P2 的行星贴图）；往上按 6 °C/km
    const double t0 = g_temp ? g_temp->sample(p_lat, p_lon) : 12.0;
    const double range = g_range ? g_range->sample(p_lat, p_lon) : 20.0;
    const double year = double(year_days);
    const double warm = solstice_n + 45.0 + (p_lat < 0.0 ? 0.5 * year : 0.0);
    s.temp = t0 + 0.5 * range * std::cos(2.0 * PI * (p_day - warm) / year) - 6.0 * p_h_ref / 1000.0;
    double e = 0.0, n = 0.0;
    wind2_at(p_lat, p_lon, e, n);
    s.wind_x = e;
    s.wind_z = -n;
    s.storm = 0.0;
    s.fog = 0.0;
    s.sailable = 1.0;
    const WeatherKind kind = s.cloud < 0.35 ? WeatherKind::CLEAR : (s.cloud < 0.7 ? WeatherKind::CLOUDY : WeatherKind::RAIN);
    s.w[int(kind)] = 1.0;
    // 永暴带、G 圈：风暴、禁航
    int zk = 0;
    double oe = 0.0, on = 0.0;
    const double z = zone_at(p_day, p_lat, p_lon, zk, oe, on);
    if (z > 0.0) {
        WeatherState st = s;
        st.cloud = 1.0;
        st.coverage = 0.9;
        st.mm = std::max(s.mm, 30.0);
        st.storm = 1.0;
        const double sp = std::hypot(e, n);
        const double want = std::max(sp, rules.storm_wind_ms);
        if (sp > 0.5) {
            st.wind_x = e / sp * want;
            st.wind_z = -n / sp * want;
        } else {
            st.wind_x = oe * want; // 没有风向：往外吹
            st.wind_z = -on * want;
        }
        st.sailable = 0.0;
        std::fill(st.w, st.w + KIND_COUNT, 0.0);
        st.w[int(s.temp < 0.5 ? WeatherKind::BLIZZARD : WeatherKind::STORM)] = 1.0;
        s.mix_toward(st, z);
    }
    return s;
}

std::shared_ptr<const PlanetWeather::Table> PlanetWeather::table(int p_node) const {
    return p_node >= 0 && p_node < int(tables.size()) ? tables[size_t(p_node)] : nullptr;
}

int PlanetWeather::add_pack(const WeatherPack &p_pack) {
    auto days = std::make_shared<const std::vector<WeatherPackDay>>(p_pack.days);
    std::vector<std::pair<int, std::shared_ptr<const Table>>> made;
    for (int k = 0; k < int(p_pack.entries.size()); k++) {
        const WeatherPackEntry &e = p_pack.entries[size_t(k)];
        if (e.node < 0 || e.node >= int(frames.size())) {
            continue;
        }
        auto t = std::make_shared<Table>();
        t->ref_m = e.ref_m;
        t->lapse = e.lapse;
        t->rim_m = e.rim_m;
        t->fog_rise_m = e.fog_rise_m;
        t->snow_temp = e.snow_temp;
        t->year = p_pack.year_days;
        t->pack = days;
        t->packed = days->data() + size_t(k) * size_t(p_pack.year_days);
        made.push_back({ e.node, t });
    }
    std::unique_lock<std::shared_mutex> lock(mutex);
    precip_min = p_pack.precip_min;
    precip_max = p_pack.precip_max;
    precip_exp = p_pack.precip_exp;
    int added = 0;
    for (auto &m : made) {
        const std::shared_ptr<const Table> &old = tables[size_t(m.first)];
        if (old && old->fine) {
            continue; // 细版为准
        }
        if (!old) {
            table_n++;
        }
        tables[size_t(m.first)] = m.second;
        added++;
    }
    rev++;
    return added;
}

void PlanetWeather::set_group_climate(int p_node, const Climate &p_climate) {
    if (p_node < 0 || p_node >= int(frames.size()) || p_climate.days.empty()) {
        return;
    }
    auto t = std::make_shared<Table>();
    t->ref_m = p_climate.ref_m;
    t->lapse = p_climate.lapse;
    t->rim_m = p_climate.rim_m;
    t->fog_rise_m = p_climate.fog_rise_m;
    t->snow_temp = p_climate.snow_temp;
    t->year = int(p_climate.days.size());
    t->days = p_climate.days;
    t->fine = true;
    std::unique_lock<std::shared_mutex> lock(mutex);
    if (!tables[size_t(p_node)]) {
        table_n++;
    }
    tables[size_t(p_node)] = t;
    rev++;
}

bool PlanetWeather::has_table(int p_node) const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    return table(p_node) != nullptr;
}

int PlanetWeather::table_count() const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    return table_n;
}

bool PlanetWeather::group_climate(int p_node, Climate &r_climate) const {
    std::shared_ptr<const Table> t;
    {
        std::shared_lock<std::shared_mutex> lock(mutex);
        t = table(p_node);
    }
    if (!t) {
        return false;
    }
    Climate c;
    c.calendar.year_days = t->year;
    c.calendar.latitude_deg = planet->node(p_node).lat;
    c.calendar.longitude_deg = planet->node(p_node).lon;
    c.calendar.tilt_deg = tilt_deg;
    c.calendar.solstice_n = solstice_n;
    c.ref_m = t->ref_m;
    c.lapse = t->lapse;
    c.rim_m = t->rim_m;
    c.fog_rise_m = t->fog_rise_m;
    c.snow_temp = t->snow_temp;
    for (int k = 0; k < t->year; k++) {
        c.days.push_back(t->day(k));
        c.precip_mm += c.days.back().precip_mm;
    }
    r_climate = c;
    return true;
}

void PlanetWeather::set_override(const WeatherOverride &p_over, const std::vector<int> &p_nodes) {
    std::vector<uint8_t> nodes(over_nodes.size(), 0);
    for (int n : p_nodes) {
        if (n >= 0 && n < int(nodes.size())) {
            nodes[size_t(n)] = 1;
        }
    }
    std::unique_lock<std::shared_mutex> lock(mutex);
    // 没变就不动（天与光每帧都抄一回）
    const bool same = over.active == p_over.active && over.kind == p_over.kind && over.temp_offset_c == p_over.temp_offset_c && over.blend == p_over.blend &&
            nodes == over_nodes;
    over = p_over;
    if (!same) {
        over_nodes.swap(nodes);
        rev++;
    }
}

void PlanetWeather::weights_at(const PlanetVec &p_unit, double p_lat, double p_lon, Weights &r_out) const {
    r_out = Weights();
    double we = 0.0, wn = 0.0;
    wind2_at(p_lat, p_lon, we, wn);
    const double speed = std::hypot(we, wn);
    const double squeeze = 1.0 + (rules.along_squeeze - 1.0) * smooth01(0.5, 3.0, speed);
    const double ue = speed > 1e-9 ? we / speed : 1.0, un = speed > 1e-9 ? wn / speed : 0.0; // 没风：随便挑个方向，反正不压
    PlanetVec east, north;
    east_north(p_unit, east, north);
    const double s_bg = rules.background_km - rules.zone_background_km * zone_static(p_unit, p_lat, p_lon);
    const double lncut = std::log(1.0 / rules.cutoff);
    // 由近及远放大找：圈外的群管辖差 ≥ 圈半径 / 压缩 − 最大的管辖半径，够远了就停
    struct Cand {
        int node;
        double s, d;
    };
    Cand best[16];
    int nb = 0;
    double s_min = 1e18;
    for (double radius : { 200.0, 500.0, 1200.0, 3000.0, 8000.0, 21000.0 }) {
        nb = 0;
        s_min = 1e18;
        for_nodes_within(p_lat, p_lon, radius, [&](int p_k, double p_d_km, const PlanetVec &p_u) {
            // 此地看那一群的方向（此地的东、北）→ 顺风、横风
            const PlanetVec t = p_u - p_unit * p_unit.dot(p_u);
            const double tl = std::hypot(t.dot(east), t.dot(north));
            double along = 0.0, across = p_d_km;
            if (tl > 1e-15) {
                const double de = t.dot(east) / tl, dn = t.dot(north) / tl;
                along = p_d_km * (de * ue + dn * un);
                across = p_d_km * (de * un - dn * ue);
            }
            const double d_eff = std::sqrt((along / squeeze) * (along / squeeze) + across * across);
            const double s = d_eff - reach[size_t(p_k)];
            s_min = std::min(s_min, s);
            // 留管辖差最小的 16 个
            if (nb < 16) {
                best[nb++] = { p_k, s, p_d_km };
            } else {
                int worst = 0;
                for (int i = 1; i < 16; i++) {
                    worst = best[i].s > best[worst].s ? i : worst;
                }
                if (s < best[worst].s) {
                    best[worst] = { p_k, s, p_d_km };
                }
            }
        });
        if (nb == 0) {
            continue;
        }
        const double blend = rules.blend_km + rules.blend_grow * std::max(0.0, s_min);
        const double need = std::min(s_min, s_bg) + blend * lncut;
        if (radius / rules.along_squeeze - reach_max >= need || radius >= 21000.0) {
            break;
        }
    }
    const double blend = rules.blend_km + rules.blend_grow * std::max(0.0, s_min);
    r_out.s_min = s_min;
    r_out.blend = blend;
    std::sort(best, best + nb, [](const Cand &a, const Cand &b) { return a.s < b.s || (a.s == b.s && a.node < b.node); });
    double total = 0.0;
    for (int i = 0; i < nb && r_out.n < 4; i++) {
        const double w = std::exp(-(best[i].s - s_min) / blend);
        if (w < rules.cutoff) {
            break;
        }
        r_out.nodes[r_out.n] = best[i].node;
        r_out.w[r_out.n] = w;
        r_out.s[r_out.n] = best[i].s;
        r_out.d_km[r_out.n] = best[i].d;
        r_out.n++;
        total += w;
    }
    double bg = nb > 0 ? std::exp(-(s_bg - s_min) / blend) : 1.0;
    if (bg < rules.cutoff) {
        bg = 0.0;
    }
    // 背景压倒一切的地方，群的权不到它的百分之一也丢掉（但云团还按群的来：field 另算）
    total += bg;
    for (int i = 0; i < r_out.n; i++) {
        r_out.w[i] /= total;
    }
    r_out.bg = bg / total;
}

WeatherState PlanetWeather::group_day(const Table *p_table, int p_node, long long p_day, double p_h_ref) const {
    if (!p_table) {
        const PlanetNode &nd = planet->node(p_node);
        return climate_state(double(p_day) + 0.5, nd.lat, nd.lon, p_h_ref); // 没有逐日表（生存世界还没生成的群）：它群心处的气候
    }
    WeatherState s = weather_day_state(p_table->day(p_day));
    const double dh = p_table->lapse * (p_table->ref_m - p_h_ref) / 1000.0;
    if (dh != 0.0) {
        s.temp += dh;
    }
    return s;
}

double PlanetWeather::arrival(const Table *p_table, int p_node, long long p_day, double p_x, double p_z) const {
    double tx = 0.0, tz = 0.0;
    if (p_table) {
        weather_wind_to(p_table->day(p_day).wind_from_deg, tx, tz);
    } else {
        const PlanetVec &w = wind2_center[size_t(p_node)];
        const double sp = std::hypot(w.x, w.y);
        if (sp > 1e-9) {
            tx = w.x / sp;
            tz = -w.y / sp;
        }
    }
    const double along_km = std::clamp((p_x * tx + p_z * tz) / 1000.0, -rules.max_along_km, rules.max_along_km);
    return double(p_day) + (weather_front_jitter_h(p_day, rules.weather) + along_km / std::max(1.0, rules.weather.front_kmh)) / 24.0;
}

void PlanetWeather::table_drift(const Table *p_table, int p_node, double p_day, double &r_x, double &r_z) const {
    const double per_day_scale = 24.0 * 3600.0 * rules.weather.drift_scale;
    if (!p_table) {
        const PlanetVec &w = wind2_center[size_t(p_node)];
        r_x = w.x * per_day_scale * p_day;
        r_z = -w.y * per_day_scale * p_day;
        return;
    }
    const int year = p_table->year;
    std::call_once(p_table->drift_once, [&]() {
        // 与 WeatherModel::build_tables 的云团漂移同一个加法次序（这一群自己的 WeatherModel 逐位一样）
        p_table->drift_x.assign(size_t(year) + 1, 0.0);
        p_table->drift_z.assign(size_t(year) + 1, 0.0);
        for (int d = 0; d < year; d++) {
            const ClimateDay cd = p_table->day(d);
            double tx = 0.0, tz = 0.0;
            weather_wind_to(cd.wind_from_deg, tx, tz);
            p_table->drift_x[size_t(d) + 1] = p_table->drift_x[size_t(d)] + tx * cd.wind_ms * per_day_scale;
            p_table->drift_z[size_t(d) + 1] = p_table->drift_z[size_t(d)] + tz * cd.wind_ms * per_day_scale;
        }
    });
    const double whole = std::floor(p_day);
    const double years = std::floor(whole / year);
    const long long doy = (long long)(whole - years * year);
    const double frac = p_day - whole;
    const ClimateDay cd = p_table->day(doy);
    double tx = 0.0, tz = 0.0;
    weather_wind_to(cd.wind_from_deg, tx, tz);
    const double per_day = per_day_scale * cd.wind_ms;
    r_x = years * p_table->drift_x[size_t(year)] + p_table->drift_x[size_t(doy)] + tx * per_day * frac;
    r_z = years * p_table->drift_z[size_t(year)] + p_table->drift_z[size_t(doy)] + tz * per_day * frac;
}

void PlanetWeather::drift(int p_node, double p_day, double &r_x, double &r_z) const {
    std::shared_ptr<const Table> t;
    {
        std::shared_lock<std::shared_mutex> lock(mutex);
        t = table(p_node);
    }
    table_drift(t.get(), p_node, p_day, r_x, r_z);
}

struct PlanetWeather::Mix {
    WeatherState state;
    double front = 0.0;
    int day_index = 0;
    double fog_top = -1e9;
    double field = 0.5;
};

void PlanetWeather::mix(const Weights &p_w, const PlanetVec &p_unit, double p_lat, double p_lon, double p_day, double p_h_ref, int p_caller,
        const GroupFrame *p_caller_frame, double p_caller_x, double p_caller_z, bool p_override, bool p_field, Mix &r_mix) const {
    const long long k = (long long)std::floor(p_day);
    std::shared_ptr<const Table> tabs[4];
    WeatherOverride ov;
    bool ov_on[4] = { false, false, false, false };
    {
        std::shared_lock<std::shared_mutex> lock(mutex);
        for (int i = 0; i < p_w.n; i++) {
            tabs[i] = table(p_w.nodes[i]);
            ov_on[i] = p_override && over.blend > 0.0 && over_nodes[size_t(p_w.nodes[i])] != 0;
        }
        ov = over;
    }
    // 风换到哪个轴：调用者的平面（它自己的风不转；别的群的风与 ② 当作此地的地理方向，再转到调用者的平面）；没有调用者就是此地的地理东南
    double gamma = 0.0;
    if (p_caller_frame) {
        gamma = planet_plane_turn(*p_caller_frame, p_unit, p_caller_x, p_caller_z);
    }
    double w2e = 0.0, w2n = 0.0;
    wind2_at(p_lat, p_lon, w2e, w2n);
    double gx[4], gz[4];
    double wsum = 0.0;
    for (int i = 0; i < p_w.n; i++) {
        const int node = p_w.nodes[i];
        if (node == p_caller) {
            gx[i] = p_caller_x;
            gz[i] = p_caller_z;
        } else {
            const PlanetVec f = frames[size_t(node)].to_flat(p_unit * radius_m);
            gx[i] = f.x;
            gz[i] = f.z;
        }
        wsum += p_w.w[i];
    }
    WeatherState days[3];
    double arrive[3];
    double fog_num = 0.0, fog_den = 0.0, fog_single = -1e9;
    WeatherState bg;
    if (p_w.bg > 0.0) {
        bg = climate_state(p_day, p_lat, p_lon, p_h_ref);
        turn_en(bg.wind_x, -bg.wind_z, gamma, bg.wind_x, bg.wind_z); // climate_state 的风是地理的（x 东 z 南）
    }
    for (int j = 0; j < 3; j++) {
        const long long d = k - 1 + j;
        WeatherState &acc = days[j];
        acc.clear();
        double a_sum = 0.0;
        for (int i = 0; i < p_w.n; i++) {
            const int node = p_w.nodes[i];
            const Table *t = tabs[i].get();
            WeatherState s = group_day(t, node, d, p_h_ref);
            if (!t) {
                turn_en(s.wind_x, -s.wind_z, node == p_caller ? 0.0 : gamma, s.wind_x, s.wind_z);
            } else if (node != p_caller && gamma != 0.0) {
                turn_en(s.wind_x, -s.wind_z, gamma, s.wind_x, s.wind_z);
            }
            // 离这一群的陆地远了：它的风换成「此地 ② 的风 + 它当天比群心 ② 的风多出的那份」
            const double lam = smooth01(reach[size_t(node)] + rules.anomaly_from_km, reach[size_t(node)] + rules.anomaly_to_km, p_w.d_km[i]);
            if (lam > 0.0) {
                const PlanetVec &c2 = wind2_center[size_t(node)];
                double ax = 0.0, az = 0.0;
                turn_en(w2e - c2.x, w2n - c2.y, gamma, ax, az);
                s.wind_x += lam * ax;
                s.wind_z += lam * az;
            }
            if (ov_on[i]) {
                const ClimateDay cd = t ? t->day(d) : ClimateDay();
                WeatherState o = ov.kind == WeatherKind::COUNT ? s : weather_override_state(s, cd, ov, rules.weather);
                o.temp += ov.temp_offset_c;
                s.mix_toward(o, std::clamp(ov.blend, 0.0, 1.0));
            }
            acc.add(s, p_w.w[i]);
            a_sum += p_w.w[i] * arrival(t, node, d, gx[i], gz[i]);
            if (j == 1 && t) {
                const double f = std::max({ weather_day_state(t->day(d - 1)).fog, s.fog, weather_day_state(t->day(d + 1)).fog });
                if (f > 1e-3) {
                    fog_num += p_w.w[i] * f * (t->rim_m + t->fog_rise_m);
                    fog_den += p_w.w[i] * f;
                    fog_single = t->rim_m + t->fog_rise_m;
                }
            }
        }
        if (p_w.bg > 0.0) {
            acc.add(bg, p_w.bg);
        }
        arrive[j] = wsum > 0.0 ? (p_w.n == 1 ? a_sum / p_w.w[0] : a_sum / wsum) : double(d) + weather_front_jitter_h(d, rules.weather) / 24.0;
    }
    if (p_w.n == 1 && p_w.bg <= 0.0) {
        // 一群（权 1）：到达时刻就是它自己的（别让 a·w / w 的舍入带出一点差）
        for (int j = 0; j < 3; j++) {
            arrive[j] = arrival(tabs[0].get(), p_w.nodes[0], k - 1 + j, gx[0], gz[0]);
        }
    }
    const double half = 0.5 * rules.weather.front_width_h / 24.0;
    const double wk = smooth01(arrive[1] - half, arrive[1] + half, p_day);
    const double wn = smooth01(arrive[2] - half, arrive[2] + half, p_day);
    WeatherState s = days[0];
    s.mix_toward(days[1], wk);
    s.mix_toward(days[2], wn);
    r_mix.state = s;
    r_mix.front = std::max(1.0 - std::fabs(2.0 * wk - 1.0), 1.0 - std::fabs(2.0 * wn - 1.0));
    r_mix.day_index = int(wn > 0.5 ? k + 1 : (wk > 0.5 ? k : k - 1));
    if (fog_den > 1e-12) {
        r_mix.fog_top = p_w.n == 1 ? fog_single : fog_num / fog_den;
    }
    if (p_field && wsum > 0.0) {
        // 云团：各群自己平面上的 fbm（随它自己的风漂），只按群的权（背景没有云团）归一，混起来方差不变
        double num = 0.0, sq = 0.0;
        double one = 0.5;
        for (int i = 0; i < p_w.n; i++) {
            double dx = 0.0, dz = 0.0;
            table_drift(tabs[i].get(), p_w.nodes[i], p_day, dx, dz);
            const double cell = rules.weather.cell_m;
            const double n = wx_fbm((gx[i] - dx) / cell, (gz[i] - dz) / cell);
            const double w = p_w.w[i] / wsum;
            one = n;
            num += w * (n - 0.5);
            sq += w * w;
        }
        r_mix.field = p_w.n == 1 ? one : 0.5 + num / std::sqrt(sq);
    }
}

PlanetRegional PlanetWeather::sample(double p_day, double p_lat, double p_lon, double p_h_ref, bool p_override) const {
    PlanetRegional out;
    const PlanetVec p = planet_unit(p_lat, p_lon);
    Weights w;
    weights_at(p, p_lat, p_lon, w);
    Mix m;
    mix(w, p, p_lat, p_lon, p_day, p_h_ref, -1, nullptr, 0.0, 0.0, p_override, false, m);
    out.state = m.state;
    out.front = m.front;
    out.day_index = m.day_index;
    out.fog_top = m.fog_top;
    out.n = w.n;
    for (int i = 0; i < w.n; i++) {
        out.nodes[i] = w.nodes[i];
        out.weights[i] = w.w[i];
    }
    out.bg_weight = w.bg;
    out.group_share = 1.0 - w.bg;
    double oe = 0.0, on = 0.0;
    out.zone = zone_at(p_day, p_lat, p_lon, out.zone_kind, oe, on);
    const double push = rules.storm_push_ms * out.zone * out.zone * w.bg;
    out.push_x = oe * push;
    out.push_z = -on * push;
    return out;
}

double PlanetWeather::field(double p_day, double p_lat, double p_lon) const {
    const PlanetVec p = planet_unit(p_lat, p_lon);
    Weights w;
    weights_at(p, p_lat, p_lon, w);
    Mix m;
    mix(w, p, p_lat, p_lon, p_day, 0.0, -1, nullptr, 0.0, 0.0, false, true, m);
    return m.field;
}

PlanetGroupSample PlanetWeather::sample_group(int p_node, const GroupFrame &p_frame, double p_h_ref, double p_day, double p_x, double p_z, bool p_field) const {
    PlanetGroupSample out;
    const PlanetVec pos = p_frame.to_planet(PlanetVec(p_x, 0.0, p_z));
    const PlanetVec p = pos.normalized();
    double lat = 0.0, lon = 0.0;
    planet_latlon(p, lat, lon);
    Weights w;
    weights_at(p, lat, lon, w);
    if (w.n == 1 && w.nodes[0] == p_node && w.bg <= 0.0) {
        out.own = true; // 只有这一群自己：调用者照它自己的逐日表算（与没挂场时逐位一样）
        return out;
    }
    out.own = false;
    Mix m;
    mix(w, p, lat, lon, p_day, p_h_ref, p_node, &p_frame, p_x, p_z, false, p_field, m);
    out.state = m.state;
    out.front = m.front;
    out.day_index = m.day_index;
    out.fog_top = m.fog_top;
    out.field = m.field;
    return out;
}

void PlanetWeather::cloud_candidates(double p_lat, double p_lon, int p_max, double p_margin_km, std::vector<PlanetCloudCandidate> &r_out, double &r_dir_e,
        double &r_dir_n, double &r_squeeze) const {
    r_out.clear();
    const PlanetVec p = planet_unit(p_lat, p_lon);
    double we = 0.0, wn = 0.0;
    wind2_at(p_lat, p_lon, we, wn);
    const double speed = std::hypot(we, wn);
    r_squeeze = 1.0 + (rules.along_squeeze - 1.0) * smooth01(0.5, 3.0, speed);
    r_dir_e = speed > 1e-9 ? we / speed : 1.0; // 没风：随便挑个方向，反正不压
    r_dir_n = speed > 1e-9 ? wn / speed : 0.0;
    PlanetVec east, north;
    east_north(p, east, north);
    std::vector<PlanetCloudCandidate> all;
    for (double radius : { 300.0, 900.0, 3000.0, 21000.0 }) {
        all.clear();
        for_nodes_within(p_lat, p_lon, radius, [&](int p_k, double p_d_km, const PlanetVec &p_u) {
            const PlanetVec t = p_u - p * p.dot(p_u);
            const double tl = std::hypot(t.dot(east), t.dot(north));
            double along = 0.0, across = p_d_km;
            if (tl > 1e-15) {
                const double de = t.dot(east) / tl, dn = t.dot(north) / tl;
                along = p_d_km * (de * r_dir_e + dn * r_dir_n);
                across = p_d_km * (de * r_dir_n - dn * r_dir_e);
            }
            PlanetCloudCandidate c;
            c.node = p_k;
            c.s_km = std::sqrt((along / r_squeeze) * (along / r_squeeze) + across * across) - reach[size_t(p_k)];
            c.reach_km = reach[size_t(p_k)];
            all.push_back(c);
        });
        if (int(all.size()) >= p_max || radius >= 21000.0) {
            break;
        }
    }
    std::sort(all.begin(), all.end(), [](const PlanetCloudCandidate &a, const PlanetCloudCandidate &b) { return a.s_km < b.s_km || (a.s_km == b.s_km && a.node < b.node); });
    if (all.empty()) {
        return;
    }
    const double s_min = all.front().s_km;
    const double blend = rules.blend_km + rules.blend_grow * std::max(0.0, s_min);
    const double limit = s_min + blend * std::log(1.0 / rules.cutoff) + p_margin_km;
    for (const PlanetCloudCandidate &c : all) {
        if (int(r_out.size()) >= p_max || c.s_km > limit) {
            break;
        }
        r_out.push_back(c);
    }
}

// ---------------------------------------------------------------- 全行星的天气贴图

struct PlanetWeather::BakeGrid {
    int width = 0, height = 0;
    struct Cell {
        uint16_t node[3] = { 0, 0, 0 };
        uint16_t wq[3] = { 0, 0, 0 }; // 权 × 65535
        float x[3] = {}, z[3] = {}; // 这一格在各群平面上的坐标（米）
        uint16_t bgq = 0;
        uint8_t n = 0;
    };
    std::vector<Cell> cells;
};

int PlanetWeather::bake_width(const BakeGrid &p_grid) {
    return p_grid.width;
}

int PlanetWeather::bake_height(const BakeGrid &p_grid) {
    return p_grid.height;
}

std::shared_ptr<const PlanetWeather::BakeGrid> PlanetWeather::prepare_bake(int p_width, int p_height) const {
    auto g = std::make_shared<BakeGrid>();
    g->width = p_width;
    g->height = p_height;
    g->cells.resize(size_t(p_width) * size_t(p_height));
    for (int r = 0; r < p_height; r++) {
        const double lat = -90.0 + (r + 0.5) * 180.0 / p_height;
        for (int c = 0; c < p_width; c++) {
            const double lon = -180.0 + (c + 0.5) * 360.0 / p_width;
            const PlanetVec p = planet_unit(lat, lon);
            Weights w;
            weights_at(p, lat, lon, w);
            BakeGrid::Cell &cell = g->cells[size_t(r) * size_t(p_width) + size_t(c)];
            const int n = std::min(3, w.n);
            double kept = w.bg;
            for (int i = 0; i < n; i++) {
                kept += w.w[i];
            }
            kept = kept > 0.0 ? kept : 1.0;
            for (int i = 0; i < n; i++) {
                cell.node[i] = uint16_t(w.nodes[i]);
                cell.wq[i] = uint16_t(std::lround(std::clamp(w.w[i] / kept, 0.0, 1.0) * 65535.0));
                const PlanetVec f = frames[size_t(w.nodes[i])].to_flat(p * radius_m);
                cell.x[i] = float(f.x);
                cell.z[i] = float(f.z);
            }
            cell.bgq = uint16_t(std::lround(std::clamp(w.bg / kept, 0.0, 1.0) * 65535.0));
            cell.n = uint8_t(n);
        }
    }
    return g;
}

void PlanetWeather::bake(const BakeGrid &p_grid, double p_day, std::vector<float> &r_rgba) const {
    const int W = p_grid.width, H = p_grid.height;
    r_rgba.assign(size_t(W) * size_t(H) * 4, 0.0f);
    const long long k = (long long)std::floor(p_day);
    const double half = 0.5 * rules.weather.front_width_h / 24.0;
    // 各群三天的大势（云量、雨、风暴）按需算：一群算一次
    struct Three {
        float cloud[3], rain[3], storm[3];
        double a_wind[3][2]; // 那天的风向（平面，单位）
    };
    std::vector<int> index(frames.size(), -1);
    std::vector<Three> cache;
    std::vector<std::shared_ptr<const Table>> tabs;
    WeatherOverride ov;
    std::vector<uint8_t> ovn;
    {
        std::shared_lock<std::shared_mutex> lock(mutex);
        tabs = tables;
        ov = over;
        ovn = over_nodes;
    }
    auto three = [&](int p_node) -> const Three & {
        int &slot = index[size_t(p_node)];
        if (slot < 0) {
            Three t;
            const Table *tab = tabs[size_t(p_node)].get();
            for (int j = 0; j < 3; j++) {
                const long long d = k - 1 + j;
                WeatherState s = group_day(tab, p_node, d, 0.0);
                if (ov.blend > 0.0 && ovn[size_t(p_node)]) {
                    const ClimateDay cd = tab ? tab->day(d) : ClimateDay();
                    WeatherState o = ov.kind == WeatherKind::COUNT ? s : weather_override_state(s, cd, ov, rules.weather);
                    s.mix_toward(o, std::clamp(ov.blend, 0.0, 1.0));
                }
                t.cloud[j] = float(s.cloud);
                t.rain[j] = float(std::clamp(s.coverage * (1.0 - std::exp(-s.mm / 10.0)), 0.0, 1.0));
                t.storm[j] = float(s.storm);
                double tx = 0.0, tz = 0.0;
                if (tab) {
                    weather_wind_to(tab->day(d).wind_from_deg, tx, tz);
                } else {
                    const PlanetVec &w = wind2_center[size_t(p_node)];
                    const double sp = std::hypot(w.x, w.y);
                    tx = sp > 1e-9 ? w.x / sp : 0.0;
                    tz = sp > 1e-9 ? -w.y / sp : 0.0;
                }
                t.a_wind[j][0] = tx;
                t.a_wind[j][1] = tz;
            }
            slot = int(cache.size());
            cache.push_back(t);
        }
        return cache[size_t(slot)];
    };
    double jitter[3];
    for (int j = 0; j < 3; j++) {
        jitter[j] = weather_front_jitter_h(k - 1 + j, rules.weather);
    }
    const double v = std::max(1.0, rules.weather.front_kmh);
    for (int r = 0; r < H; r++) {
        const double lat = -90.0 + (r + 0.5) * 180.0 / H;
        for (int c = 0; c < W; c++) {
            const BakeGrid::Cell &cell = p_grid.cells[size_t(r) * size_t(W) + size_t(c)];
            float cloud[3] = {}, rain[3] = {}, storm[3] = {};
            double arrive[3] = {};
            double wsum = 0.0;
            for (int i = 0; i < cell.n; i++) {
                const double w = cell.wq[i] / 65535.0;
                const Three &t = three(cell.node[i]);
                for (int j = 0; j < 3; j++) {
                    cloud[j] += float(w) * t.cloud[j];
                    rain[j] += float(w) * t.rain[j];
                    storm[j] += float(w) * t.storm[j];
                    const double along = std::clamp((cell.x[i] * t.a_wind[j][0] + cell.z[i] * t.a_wind[j][1]) / 1000.0, -rules.max_along_km, rules.max_along_km);
                    arrive[j] += w * (double(k - 1 + j) + (jitter[j] + along / v) / 24.0);
                }
                wsum += w;
            }
            const double bgw = cell.bgq / 65535.0;
            if (bgw > 0.0) {
                const double lon = -180.0 + (c + 0.5) * 360.0 / W;
                const WeatherState b = climate_state(p_day, lat, lon, 0.0);
                const float rb = float(std::clamp(b.coverage * (1.0 - std::exp(-b.mm / 10.0)), 0.0, 1.0));
                for (int j = 0; j < 3; j++) {
                    cloud[j] += float(bgw * b.cloud);
                    rain[j] += float(bgw) * rb;
                    storm[j] += float(bgw * b.storm);
                }
            }
            for (int j = 0; j < 3; j++) {
                arrive[j] = wsum > 0.0 ? arrive[j] / wsum : double(k - 1 + j) + jitter[j] / 24.0;
            }
            const double wk = smooth01(arrive[1] - half, arrive[1] + half, p_day);
            const double wn = smooth01(arrive[2] - half, arrive[2] + half, p_day);
            auto blend3 = [&](const float *p_v) {
                double s = p_v[0];
                s += (p_v[1] - s) * wk;
                s += (p_v[2] - s) * wn;
                return float(s);
            };
            float *o = &r_rgba[(size_t(r) * size_t(W) + size_t(c)) * 4];
            o[0] = blend3(cloud);
            o[1] = blend3(rain);
            o[2] = float(1.0 - bgw);
            o[3] = blend3(storm);
        }
    }
}
