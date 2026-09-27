#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "planet_data.h"
#include "planet_frame.h"
#include "weather.h"

// 行星天气场（行星计划 P5，docs/PLAN-PLANET.md 第 4.5 节）。不含 Godot 头。
//
// 真值是各岛群当天的天气：它自己的逐日表（行星包的 weather.bin，生成器粗版顺带出的第 0 年；有细版的群以细版的 weather.tsv 为准）
// 加上锋面到达时刻（锋面在子夜前后经过群心、顺着那天的风以 25 km/游戏钟头扫过，同 WeatherModel）。任意经纬度、任意时刻：
//   1. 权（按距离与风向）：一群管多大一片 = 它的陆地往各个方向铺多远（粗版的陆地格按群心的方位分八个扇区量最远的，相邻扇区拉平一点；
//      没有粗版的群按陆地面积估、不过到最近邻群心距离的一半）；某点离第 k 群的「管辖差」
//      s_k = 顺风压过的距离 − 它往这一点的方向管多远（顺着 ② 的风，距离按 along_squeeze 压：一群的天气顺着风管得更远）。
//      权 ∝ exp(−(s_k − s_min) / B)，B = blend_km + blend_grow × max(0, s_min)——陆地上差 blend_km 就是 1 : e，离群越远过渡越宽；
//      背景（④ 的气候场）也算一份，它的管辖差是 background_km（永暴带、G 圈里提前接手），离所有群都远了才轮到它；权不到最大的 cutoff 的丢掉。
//   2. 大势：各群各天的大势（WeatherState）按权混成「这一点的第 j 天」；锋面到达时刻也按权插值（各群按自己那天的风量顺风量），
//      于是锋面在群与群之间是一条连着的线；再照 WeatherModel 的锋面过渡把前一天、这一天、后一天接起来。
//      群自己的陆地上别的群的权都丢了：场就是那一群的 WeatherModel（逐位一样）。
//   3. 风：② 的 1° 风场 + 当天的扰动——离一群的陆地远了，它的风换成「此地 ② 的风 + 它当天比它群心 ② 的风多出的那份」；
//      背景是 ② 的风。赤道永暴带、G 定点永暴圈里（背景接手的地方）是风暴、禁航，还有一股把船往外推的「禁航」推力。
//   4. 云团（给地上判雨、天上画云）：各群自己平面上的 fbm（weather_noise，随它自己的逐日风漂，同 WeatherModel），
//      几群按权混、方差不变（0.5 + Σ w (N − 0.5) / √Σw²）——群自己的陆地上就是它自己的云团，没有公共平面，离多远都不歪。
// 线程：查询是 const、可并行；加表（add_pack / set_group_climate）与覆盖（set_override）拿写锁。

// ---- 逐日天气包（tools/export_planet.py 的 weather.bin；单群的 lods/<节点>.wx 同格式）
struct WeatherPackDay {
    uint8_t kind_sail = 0; // WeatherKind | sailable << 4
    uint8_t storm = 0; // 风暴号
    uint16_t precip10 = 0; // 0.1 mm
    int16_t temp10 = 0; // 0.1 °C（台面）
    uint16_t wind_from = 0; // 度（0–360 原样）
    uint16_t wind10 = 0; // 0.1 m/s
    ClimateDay unpack() const;
};

struct WeatherPackEntry {
    int node = -1;
    int season_set = 0;
    double ref_m = 300.0, lapse = 6.0, rim_m = 200.0, fog_rise_m = 150.0, snow_temp = 0.5, precip_mm = 0.0, precip_climate_mm = 0.0;
};

struct WeatherPack {
    int year_days = 336;
    int seasons = 4, months_per_season = 3, days_per_month = 28;
    double solstice_n = 126.0;
    int year = 0;
    double precip_min = 150.0, precip_max = 4000.0, precip_exp = 1.3; // ④ 的相对降水 → 年降水 mm
    std::vector<std::vector<std::string>> season_sets;
    std::string source;
    std::vector<WeatherPackEntry> entries;
    std::vector<WeatherPackDay> days; // 条目 × year_days

    bool parse(const uint8_t *p_data, size_t p_size, std::string &r_error);
    const WeatherPackDay *days_of(int p_entry) const { return &days[size_t(p_entry) * size_t(year_days)]; }
    // 第 p_entry 条折回一份 Climate（与细版 weather.tsv 读出来的同样的数；纬度、经度、倾角另给）
    Climate climate(int p_entry, double p_lat, double p_lon, double p_tilt) const;
};

struct PlanetWeatherRules {
    WeatherRules weather; // 锋面、云团（与各群的 WeatherModel 同一套）
    double blend_km = 10.0;
    double blend_grow = 0.25;
    double along_squeeze = 1.5;
    double reach_cap = 0.5; // 没有粗版的群：管的半径 ≤ 到最近邻群心的这么多
    double reach_min_km = 3.0, reach_max_km = 150.0;
    double reach_pad_km = 1.0; // 陆地格心往外再算这么多（半格）
    double reach_neighbor = 0.8; // 一个扇区至少是相邻扇区的这么多（扇区之间没陆地时别一下子缩回群心）
    double background_km = 300.0;
    double zone_background_km = 250.0;
    double cutoff = 0.01;
    double anomaly_from_km = 0.0, anomaly_to_km = 150.0; // 离一群的管辖多远起，它的风换成「② + 它的扰动」
    double max_along_km = 250.0; // 锋面到达时刻的顺风量夹在这以内（偏 ≤ 10 h，日子的先后不乱）
    double storm_push_ms = 150.0; // 永暴带、G 圈的核心把船往外推多快
    double storm_wind_ms = 25.0; // 永暴带、G 圈里至少这么大的风
    double zone_edge_deg = 1.5; // 赤道永暴带的边软多宽（度）
    double g_edge = 0.15; // G 圈的边软多宽（半径的份）
    // 背景（④ 的气候场）的云量：按生成器逐日天气的统计把 ④ 的降水、风暴折成云量（全行星 8000 群拟合，r² 0.88）
    double clim_cloud0 = 0.315, clim_cloud_precip = 0.274, clim_cloud_storm = 0.176;
    // 生成器 ④ 的带界随太阳摆动（island/climate.py：Δφ = k_shift × 倾角 × A_sea × cos(相位 − 滞后)，海洋混合层 τ = 110 天）
    double band_k_shift = 0.35, band_tau_days = 110.0;
};

// 某一点某一刻的「大势」（没有云团）
struct PlanetRegional {
    WeatherState state; // temp：p_h_ref 高处的气温；风：此地的地理东、南（x 东 z 南，m/s）
    double front = 0.0; // 离锋面多近（0..1）
    int day_index = 0; // 锋面过后的那一天
    double fog_top = -1e9; // 云海漫顶的雾顶（按有雾的几群的岸缘 + 漫多高混；没有 −1e9）
    double group_share = 0.0; // 群的份（1 − 背景的份）
    double zone = 0.0; // 永暴带 / G 圈（当季，0..1）
    int zone_kind = 0; // 0 没有、1 赤道永暴带、2 G 定点永暴
    double push_x = 0.0, push_z = 0.0; // 禁航：把船往外推（m/s，此地的东、南）
    int n = 0; // 有权的群
    int nodes[4] = { -1, -1, -1, -1 };
    double weights[4] = {}; // 各群的权（与背景的一起归一）
    double bg_weight = 0.0;
};

// 某群平面上的一点（WeatherModel 挂上场时问的）
struct PlanetGroupSample {
    WeatherState state; // 风在这一群的平面轴上；temp 在调用者给的台面高处
    double front = 0.0;
    int day_index = 0;
    double field = 0.5; // 云团场
    double fog_top = -1e9;
    double ref_m = 0.0; // 几群的台面按权混（地形抬升、高处的阵风按它算：换锚那一刻调用者换了，这个不跳）
    bool own = true; // 只有这一群自己（与它自己的 WeatherModel 逐位一样）
};

// 群的平面轴相对此地（单位向量 p_unit，平面坐标 p_x / p_z）的地理轴转了多少（弧度：平面上的方位 = 地理方位 + γ；群心为 0）。
// AEQD 的径向方位在群心与此地不一样，差的就是它（离群心一百公里、纬度三十度时零点几度）
double planet_plane_turn(const GroupFrame &p_frame, const PlanetVec &p_unit, double p_x, double p_z);

// 着色器近处的云团（≤ 4 群）：候选
struct PlanetCloudCandidate {
    int node = -1;
    double s_km = 0.0; // 相机处的管辖差
    double reach_km = 0.0; // 往相机的方向管多远
};

class PlanetWeather {
public:
    struct Table; // 一群的逐日表（行星包的一条或细版的 weather.tsv）
    struct BakeGrid; // 全行星天气贴图各格的群与权（静的：只跟位置有关）

private:
    std::shared_ptr<const PlanetData> planet;
    PlanetWeatherRules rules;
    double radius_m = PLANET_DEFAULT_R;
    int year_days = 336;
    double solstice_n = 126.0;
    double tilt_deg = 34.0;
    double precip_min = 150.0, precip_max = 4000.0, precip_exp = 1.3;
    std::vector<GroupFrame> frames; // 各群的平面系（行星包的群心）
public:
    static constexpr int SECTORS = 8; // 管多远按群心的方位分几个扇区（北起顺时针，扇区心在 0°、45°……）
private:
    std::unique_ptr<std::atomic<float>[]> reach; // [群 × SECTORS] 各群往各方位管多远（km）；生存世界的粗版来了会更新
    std::atomic<float> reach_max{ 0.0f };
    std::atomic<uint64_t> reach_rev{ 0 };
    std::vector<double> nn_km; // 各群到最近邻群心（km）
    void compute_reach(int p_node);
    std::vector<PlanetVec> wind2_center; // 各群群心处 ② 的风（东、北，m/s）
    // 找附近的群：2° 经纬格
    static constexpr double BUCKET_DEG = 2.0;
    int bucket_rows = 0, bucket_cols = 0;
    std::vector<std::vector<int>> buckets;
    const PlanetGrid *g_precip = nullptr, *g_storm = nullptr, *g_temp = nullptr, *g_range = nullptr, *g_u = nullptr, *g_v = nullptr, *g_edges = nullptr;
    PlanetVec g_dir;
    double g_radius = 0.07;

    mutable std::shared_mutex mutex; // 护着 tables、override
    std::vector<std::shared_ptr<const Table>> tables; // 各群的逐日表（没有为空）
    std::shared_ptr<const std::vector<WeatherPackDay>> pack_days; // 行星包的逐日（Table 指进来）
    int table_n = 0;
    WeatherOverride over;
    std::vector<uint8_t> over_nodes; // 覆盖抄给哪几群
    std::atomic<uint64_t> rev{ 0 }; // 逐日表、覆盖变一回加一（行星贴图据此重烘）

    struct Weights {
        int n = 0;
        int nodes[4];
        double w[4]; // 与背景的一起归一
        double s[4];
        double d_km[4];
        double bg = 0.0;
        double s_min = 0.0, blend = 10.0;
    };
    void weights_at(const PlanetVec &p_unit, double p_lat, double p_lon, Weights &r_out) const;
    template <typename F>
    void for_nodes_within(double p_lat, double p_lon, double p_radius_km, F &&p_fn) const;
    double zone_static(const PlanetVec &p_unit, double p_lat, double p_lon) const;
    std::shared_ptr<const Table> table(int p_node) const; // 拿着读锁时用
    WeatherState group_day(const Table *p_table, int p_node, long long p_day, double p_h_ref) const;
    double arrival(const Table *p_table, int p_node, long long p_day, double p_x, double p_z) const;
    void table_drift(const Table *p_table, int p_node, double p_day, double &r_x, double &r_z) const;
    // 一点：各群（p_flat 是这一点在各群平面的坐标）→ 大势（各群的风已经换到「此地的地理东南」或 p_caller 的平面轴）
    struct Mix;
    void mix(const Weights &p_w, const PlanetVec &p_unit, double p_lat, double p_lon, double p_day, double p_h_ref, int p_caller, const GroupFrame *p_caller_frame,
            double p_caller_x, double p_caller_z, bool p_override, bool p_field, Mix &r_mix) const;

public:
    explicit PlanetWeather(std::shared_ptr<const PlanetData> p_planet, const PlanetWeatherRules &p_rules = PlanetWeatherRules());
    ~PlanetWeather();

    // 逐日表：整包（行星包 weather.bin）/ 单群（按需生成的 .wx）；返回加了几群
    int add_pack(const WeatherPack &p_pack);
    // 细版的 weather.tsv（细版为准；空的 Climate = 撤掉细版的、回到包里的）
    void set_group_climate(int p_node, const Climate &p_climate);
    bool has_table(int p_node) const;
    int table_count() const;
    // 这一群的逐日表折成 Climate（没有返回 false）
    bool group_climate(int p_node, Climate &r_climate) const;

    const PlanetWeatherRules &get_rules() const { return rules; }
    const PlanetData &get_planet() const { return *planet; }
    double get_radius() const { return radius_m; }
    // 第 p_node 群往方位 p_bearing（弧度，北起顺时针，在它的平面里）管多远（km）；reach_km 是最远的那个扇区
    double reach_at(int p_node, double p_bearing) const;
    double reach_km(int p_node) const;
    // 八个扇区（km）
    void reach_sectors(int p_node, float r_out[SECTORS]) const;
    // 生存世界：这一群的粗版刚加进来（PlanetData::add_lods），按它重量管多远；reach_revision 加一（行星贴图的格据此重备）
    void refresh_reach(int p_node);
    uint64_t reach_revision() const { return reach_rev.load(); }
    const GroupFrame &frame(int p_node) const { return frames[size_t(p_node)]; }

    // 某经纬度、某历日：大势（p_h_ref：气温按这个高给；p_override：载入了的群按开发者覆盖）
    PlanetRegional sample(double p_day, double p_lat, double p_lon, double p_h_ref = 0.0, bool p_override = true) const;
    // 云团场在某经纬度的值（几群按权混）
    double field(double p_day, double p_lat, double p_lon) const;
    // 某群平面上的一点（WeatherModel 用）：风在这一群的平面轴上、气温在 p_h_ref；p_field 为真时另给云团场
    PlanetGroupSample sample_group(int p_node, const GroupFrame &p_frame, double p_h_ref, double p_day, double p_x, double p_z, bool p_field) const;

    // 一群的云团此刻漂到哪（米，它的平面 x 东 z 南）——与它自己的 WeatherModel::cloud_drift 同一式
    void drift(int p_node, double p_day, double &r_x, double &r_z) const;
    // 赤道永暴带这几天往北挪了几度（生成器 ④ 的带界随太阳摆动）
    double band_shift(double p_day) const;
    // 永暴带 / G 圈（当季）：0..1，r_kind 1 赤道 2 G；r_out_e / r_out_n 往外的方向（此地的东、北，单位向量）
    double zone_at(double p_day, double p_lat, double p_lon, int &r_kind, double &r_out_e, double &r_out_n) const;
    // ② 的 1° 风场在这一点（东、北，m/s）
    void wind2_at(double p_lat, double p_lon, double &r_e, double &r_n) const;
    // 背景（④ 的气候场）：这一点的云量、年降水（mm）、这一天的大势
    double climate_cloud(double p_lat, double p_lon) const;
    double climate_precip_mm(double p_lat, double p_lon) const;
    WeatherState climate_state(double p_day, double p_lat, double p_lon, double p_h_ref) const;
    double precip_mm_of(double p_rel) const;

    // 开发者覆盖：抄给这几群（载入了的；p_nodes 空 = 没有覆盖）
    void set_override(const WeatherOverride &p_over, const std::vector<int> &p_nodes);
    // 逐日表、覆盖变了几回（行星贴图据此重烘）
    uint64_t revision() const { return rev.load(); }

    // 着色器近处的云团：相机处按管辖差排的几群（≤ p_max，差得太多的不要）；r_dir_e / r_dir_n 是相机处 ② 的风向（单位，东、北；没风为 0）
    void cloud_candidates(double p_lat, double p_lon, int p_max, double p_margin_km, std::vector<PlanetCloudCandidate> &r_out, double &r_dir_e, double &r_dir_n,
            double &r_squeeze) const;

    // 全行星的天气贴图（等经纬，W × H，第 0 行在南、格心对齐）：先备各格的群与权（静的；Debug 下 1024 × 512 一条线程十秒上下，
    // 所以能分行交给几条线程：new_bake_grid 之后 prepare_bake_rows 各备一段），每回 bake 按历日填
    // RGBA：云量、雨（雨区 × 雨量折成 0..1）、群的份、风暴
    std::shared_ptr<BakeGrid> new_bake_grid(int p_width, int p_height) const;
    void prepare_bake_rows(BakeGrid &r_grid, int p_row_from, int p_row_to) const;
    std::shared_ptr<const BakeGrid> prepare_bake(int p_width, int p_height) const;
    void bake(const BakeGrid &p_grid, double p_day, std::vector<float> &r_rgba) const;
    static int bake_width(const BakeGrid &p_grid);
    static int bake_height(const BakeGrid &p_grid);
};
