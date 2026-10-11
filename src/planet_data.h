#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

// 行星包（tools/export_planet.py → game/planet/<seed>/planet.bin、lods.bin；格式见脚本开头；docs/PLAN-PLANET.md P0）。不含 Godot 头。
// planet.bin：行星参数、8000 个岛群的逐节点属性（按名字的列）、1° 的场（风、气候、板块、带界……第 0 行在南、格心对齐、经度绕回）。
// lods.bin：每个岛群一到几份粗版（远处看的低模）：块内陆地占比、最高点、地表类、岛号，外加各岛的岸缘 / 岛底 / 峰。
// 粗版按需解开（整份全解开要一百多 MB）：文件的字节留在内存里，lod() 每次解一条。
// 行星计划 P4：游戏里按需生成的粗版（存档目录的 lods/<节点>.bin，同格式、一条）随时 add_lods 加进来——远处的岛群、行星贴图的后台活照样在读，
// 所以粗版的索引与字节由一把读写锁护着（读的只在挑哪一份时拿一下锁，解格子不拿）。

struct PlanetNode {
    double lat = 0.0, lon = 0.0; // 度（群心，东正）
    double area_km2 = 0.0, main_area_km2 = 0.0; // 群的陆地、主岛
    double height_m = 0.0; // 主岛台面（陆地高程中位数，云带顶以上）
    double peak_m = 0.0, rim_m = 0.0; // 主岛峰高、岸缘（粗版量的；没有粗版为 NaN）
    double territory_km2 = 0.0; // 势力范围
    double age = 0.0; // 岛龄 0..1
    int cls = 0; // 0 密接 1 中疏 2 稀疏 3 孤悬
    bool layered = false;
    double precip = 0.0; // 相对降水 0..1
    double temp_c = 0.0; // 台面高度的年均温
    double temp_sea_c = 0.0; // 海面（高度 0）的年均温
    double season_range_c = 0.0;
    double storm = 0.0;
    double pop = 0.0; // 人口（⑨）
    int state = -1, polity = -1; // 邦（−1 = 船团 / 部落）、政体
};

struct PlanetGrid {
    int rows = 0, cols = 0;
    double lat0 = 0.0, lon0 = 0.0, dlat = 1.0, dlon = 1.0; // 第 0 行第 0 列的格心、步长（度）
    std::vector<float> values; // 行主序，第 0 行在南

    float at(int p_row, int p_col) const { return values[size_t(p_row) * size_t(cols) + size_t(p_col)]; }
    // 双线性（纬度夹在网格内，经度绕回）
    double sample(double p_lat_deg, double p_lon_deg) const;
};

struct PlanetLodIsland {
    double center_x_km = 0.0, center_y_km = 0.0; // 质心（群平面，km，x 东 y 北）
    double rim_m = 0.0, keel_m = 0.0, peak_m = 0.0, surface_m = 0.0;
};

struct PlanetLod {
    int node = -1;
    double res_m = 0.0;
    double origin_x_km = 0.0, origin_y_km = 0.0; // 栅格左上角（群平面，km）
    int rows = 0, cols = 0;
    std::vector<PlanetLodIsland> islands;
    std::vector<uint16_t> height; // 块内陆地的最高点（米，云带顶以上）；虚空 0
    std::vector<uint8_t> land; // 陆地占比 0..255
    std::vector<uint8_t> landcover; // 生成器的地表类（0 虚空 1 崖缘 2 裸岩 3 高山草甸 4 林地 5 灌丛 6 草坡 7 可耕地 8 梯田 9 湿地 10 河道 11 湖）
    std::vector<uint8_t> island;

    bool empty() const { return rows == 0; }
    size_t index(int p_row, int p_col) const { return size_t(p_row) * size_t(cols) + size_t(p_col); }
    // 格心的游戏坐标（米，x 东、z 南，原点 = 群心）
    void cell_center(int p_row, int p_col, double &r_x, double &r_z) const;
    double land_km2() const;
};

struct PlanetCalendarInfo {
    int year_days = 336, seasons = 4, months_per_season = 3, days_per_month = 28;
    double solstice_n = 126.0;
};

class PlanetData {
    struct LodEntry {
        int node = -1;
        double res_m = 0.0, origin_x_km = 0.0, origin_y_km = 0.0;
        int rows = 0, cols = 0, n_islands = 0;
        uint64_t offset = 0;
        uint32_t bytes = 0;
        int blob = 0; // 在哪一份字节里（0 = lods.bin；add_lods 加的一份一个）
    };

    std::vector<PlanetNode> nodes;
    std::map<std::string, std::vector<double>> columns;
    std::map<std::string, PlanetGrid> grids;
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> lod_blobs;
    std::vector<LodEntry> lod_entries;
    std::map<int, std::vector<int>> lods_of; // 节点 → lod_entries 下标（按格距从粗到细）
    std::map<int, uint64_t> lod_versions; // 某群最后一次替换粗版的代次（同一 bundle 各档一起变）
    mutable std::shared_mutex lod_mutex; // 护着 lod_blobs / lod_entries / lods_of
    std::atomic<uint64_t> lod_gen{ 0 }; // parse_lods / add_lods 一次加一
    // 经纬格分桶（2° × 2°）：找附近的群
    static constexpr double BUCKET_DEG = 2.0;
    std::vector<std::vector<int>> buckets;
    int bucket_rows = 0, bucket_cols = 0;

    void build_buckets();
    int pick_lod(int p_node, double p_max_res_m) const; // lod_entries 的下标；没有 −1（调用方拿着锁）
    // 读一份 lods.bin 格式的字节：条目的偏移按这份字节算（blob 下标 p_blob）
    static bool read_lod_index(const uint8_t *p_data, size_t p_size, int p_blob, std::vector<LodEntry> &r_entries, std::string &r_error);
    void index_node(int p_node); // lods_of[p_node] 按格距从粗到细排（拿着写锁）

public:
    int seed = 0;
    struct Vertical {
        double sea = -6800, base = -6200, top = -5000, clearance = 1000;
        double pressure = 1, scale_km = 8, oxygen = 0.2095;
    } vertical;
    double radius_km = 6371.0;
    double rotation_hr = 24.0;
    double tilt_deg = 34.0;
    PlanetCalendarInfo calendar;
    double band_tops_deg[4] = { 8.0, 28.0, 36.0, 62.0 }; // 赤道风暴带、信风带、无风带、西风带的顶
    double g_lat = 37.5, g_lon = -10.0, g_radius_deg = 4.0; // G 定点永暴
    std::string source; // 「run 目录名 生成器提交号」

    bool parse_planet(const uint8_t *p_data, size_t p_size, std::string &r_error);
    bool parse_lods(const uint8_t *p_data, size_t p_size, std::string &r_error);
    // 从文件读（给纯 C++ 的测试与体检工具；游戏里用 FileAccess 读成字节再 parse）
    bool load_files(const std::string &p_dir, std::string &r_error);

    int node_count() const { return int(nodes.size()); }
    const PlanetNode &node(int p_id) const { return nodes[size_t(p_id)]; }
    const std::vector<double> *column(const std::string &p_name) const;
    const PlanetGrid *grid(const std::string &p_name) const;
    const std::map<std::string, PlanetGrid> &all_grids() const { return grids; }

    // 离这个经纬度最近的群（大圆）
    int nearest_node(double p_lat_deg, double p_lon_deg) const;
    // 群心离这个经纬度 p_radius_m 以内的群（按距离排好）
    std::vector<int> nodes_within(double p_lat_deg, double p_lon_deg, double p_radius_m) const;
    double node_distance_m(int p_a, int p_b) const;

    int lod_count() const;
    // 按需生成的粗版（lods/<节点>.bin，lods.bin 同格式）加进来：同一群、同一格距已有的换成新的；r_nodes 给出加了哪几群。线程安全
    bool add_lods(const uint8_t *p_data, size_t p_size, std::string &r_error, std::vector<int> *r_nodes = nullptr);
    bool has_lod(int p_node) const;
    // parse_lods / add_lods 做过几回（远处的岛群、行星贴图据此知道该重做了）
    uint64_t lod_generation() const { return lod_gen.load(); }
    uint64_t lod_generation(int p_node) const;
    // 该群有哪几种格距的粗版（米，从粗到细）
    std::vector<double> lod_resolutions(int p_node) const;
    // 解开一份粗版：格距不超过 p_max_res_m 里最粗的一份（都比它粗就给最细的）；没有粗版返回空的
    // r_generation 与所选字节在同一把锁下取出，后台网格可以据此丢掉被替换前的结果。
    PlanetLod lod(int p_node, double p_max_res_m = 1e9, uint64_t *r_generation = nullptr) const;
    // 只要那一份的栅格范围（不解格子）：格距、左上角、行列；没有粗版返回 false
    bool lod_extent(int p_node, double p_max_res_m, double &r_res_m, double &r_origin_x_km, double &r_origin_y_km, int &r_rows, int &r_cols) const;
};
