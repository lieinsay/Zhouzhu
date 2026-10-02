#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "world_grid.h"

// 岛群的河：河网（河在哪、多宽多深、水面多高）与水情（今天水多大）。不含 Godot 头，建好之后只读。
//
// v6+：读取生成器中线及水文属性，格点间作保留源点/汇流锚点的平滑插值；不额外添加随机曲流或渗漏门槛。
// 当天 Q=Q_mean*index，d=d_bf*(Q/Q_bf)^0.40，w=w_bf*(Q/Q_bf)^0.26，流速 Q/(w*d)。
// 左右漫滩分别限宽；source_surface 单独保留 surf_m，便于发现源水位与 bed+d_bf 的口径差。
// 下面的 D8 推导、放大、噪声与降水水库只服务旧版兼容资产。
//
// 河网：栅格的 D8 流向是一棵树（每岛一片，出口是岸缘），汇流面积到 min_acc_km2 的格是河道。
//   从每个出口逆流而上，每到一格挑汇流最大的上游格接着走，就是一条「主干」；其余上游格各自开一条支流，
//   支流的末点落在干流上。格心连成的折线先抽稀（Douglas–Peucker）、再用 Chaikin 磨圆、按 step_m 重采样，再左右摆（两头不摆）：
//   大尺度的 wander（D8 的直线段摆开）加上随宽度的曲流。
//   河口（流出岸缘的主干）在岸线处截断，水从那里跌进云海。
// 沿程每一点：
//   平岸流量 = 年均流量 × bankfull_ratio，年均流量 = 汇流面积 × 年降水 × 径流系数 / 一年的秒数；
//   平岸宽 = width_k · Q^width_exp、深 = depth_k · Q^depth_exp（水力几何，宽窄再按噪声 ±width_jitter 起伏），源头一段渐宽渐深；
//   平岸水面 = 平滑地面（栅格双三次，不含细节噪声）− entrench_m，从下游往上游保证不降（支流末点接干流的水面）；
//   栅格 v3 带生成器的河道（它已把河床下切、两岸压成河谷，河宽水深按它的水力几何）：宽深与上面的公式取大，
//   水面不高于那一格的 河床 + 水深；
//   河漫滩宽 = floodplain_k × 平岸宽（夹在 min..max），滩面从平岸水面按 floodplain_slope 缓缓升起。
// 横断面（river_profile）：河床中间一段平底（bed_flat × 平岸半宽，= 平岸水面 − 深），两侧岸坡按 u² 升到平岸水面，外面是河漫滩，再外 blend_m 渐回原地形。
//
// 水情：逐日降水进快、慢两个线性水库，出流按全年平均归一成径流指数 r（1 = 年均）。某一点的流量 = 年均 × max(0, r − 渗漏门槛) / (1 − 门槛)，
//   门槛按汇流面积定（dry_k · A^−dry_exp）：只有源头的小溪旱季会干，汇流几 km² 以上的常年有水、只是枯水时变窄变浅。
//   水深 = 平岸深 × (流量 / 平岸流量)^stage_exp；没过平岸就漫上河漫滩。水面宽由河床形状与水深推出——所以河道宽窄随地点变、水面宽窄随时间变。
//   水面着色器（game/shaders/river.gdshader）按同一公式在顶点里算水位，改这里要一起改。

struct RiverRules {
    double min_acc_km2 = 1.0; // 汇流面积到这么大才有河道（同 skyisle-gen 的 stream_min_km2）
    double precip_mm = 900.0; // 年降水（weather.tsv 的头；没有就用这个）
    double year_days = 336.0; // 一年几天（同上）
    double runoff = 0.35; // 径流系数：降水里流进河的份额
    double bankfull_ratio = 3.0; // 平岸流量是年均流量的几倍
    // 宽深按游戏尺度放大：真实的水力几何约 3.2·Q^0.5 与 0.32·Q^0.4，第三人称下 5 m 的河像条沟（房子当初也放大过），宽 ×3、深 ×2
    double width_k = 9.6, width_exp = 0.5; // 平岸宽 = k · Q^exp（米，Q 为 m³/s）
    double depth_k = 0.64, depth_exp = 0.4; // 平岸深
    double min_width_m = 6.0, min_depth_m = 0.6; // 再小的溪也有这么宽深的河床
    double bed_flat = 0.55; // 河床平底占平岸半宽的份额（外面是岸坡）：水浅时也整片盖住河床，不被 2 m 一格的地面拱穿成一个个水洼
    double width_jitter = 0.25; // 沿程宽窄起伏
    double floodplain_k = 1.2, floodplain_min_m = 6.0, floodplain_max_m = 30.0; // 河漫滩宽（单侧）
    double floodplain_slope = 0.04; // 滩面横坡
    double blend_m = 30.0; // 滩外渐回原地形
    double entrench_m = 0.4; // 平岸水面低于平滑地面多少
    double min_fall = 0.0005; // 水面顺流每米至少降这么多
    double step_m = 8.0; // 中线点距
    double simplify_m = 45.0; // 格心折线先抽稀（离原线不超过这么远）再磨圆
    double meander_k = 1.2, meander_min_m = 6.0, meander_max_m = 30.0; // 曲流摆幅 ≈ 宽 × k
    double meander_wave_k = 12.0; // 曲流波长 ≈ 宽 × 这个（夹在 60..300 m）
    double wander_m = 55.0, wander_wave_m = 900.0; // 大尺度的摆：D8 的格心连线是一段段横平竖直的长直线，靠它摆开
    double head_ramp_m = 80.0; // 源头这么长一段渐宽渐深
    double dry_k = 0.7, dry_exp = 1.1, dry_min = 0.02, dry_max = 0.95; // 渗漏门槛：1 km² 0.7（旱季常干）、3 km² 0.2、5 km² 起约 0.1（常年有水）
    double stage_exp = 0.4; // 水深 ∝ 流量^这个
    double flood_max = 3.0; // 流量最多到平岸的几倍（漫滩的上限）
    double wet_depth_m = 0.03; // 比这浅就算干了

    // 年均流量（m³/s）
    double mean_discharge(double p_acc_km2) const;
    // 渗漏门槛（径流指数低于它就断流）
    double dry_threshold(double p_acc_km2) const;
};

struct RiverVertex {
    double x = 0.0, z = 0.0;
    double s = 0.0; // 从源头起的弧长
    double surface = 0.0; // 平岸水面
    double source_surface = 0.0; // 生成器 surf_m，独立保留，不拿 bed + d_bf 覆盖它
    double width = 0.0; // 平岸宽
    double depth = 0.0; // 参考满槽的中心最大深度，非断面平均深度
    double floodplain = 0.0; // 河漫滩宽（单侧）
    double acc = 0.0; // 汇流面积 km²
    double dry = 0.0; // 渗漏门槛
    double mean_flow = 0.0, bankfull_flow = 0.0;
    bool authoritative = false;
    bool source_data() const { return authoritative || bankfull_flow > 0.0; }
    double slope=0.0, d50_mm=0.0, ssc_mg_l=0.0;
    int level=0, bed_class=0, planform=0, confine=0;
    double floodplain_left=0.0,floodplain_right=0.0;
    double floodplain_at(double side) const { return source_data() ? (side>=0 ? floodplain_left : floodplain_right) : floodplain; }
    double core_at(double side) const { return half()+floodplain_at(side); }
    int basin = -1;
    int flow_regime = -1; // -1 旧数据未知；0 临时路径；1 常驻河

    double half() const { return 0.5 * width; }
    double core() const { return 0.5 * width + floodplain; } // 河道 + 滩（地形全按横断面）
    double bed() const { return surface - depth; }
};

struct RiverStem {
    std::vector<RiverVertex> v; // 自上游到下游，至少两点
    int parent = -1; // 汇入哪一条（−1 = 自己流出岸缘，或流进洼地）
    bool mouth = false; // 末点在岸缘上，水从这里跌下去
    int island = 0;
    double outlet_drop = 0.0; // 导出的崖瀑落差；未提供时不补固定高度的瀑布
};

struct RiverHit {
    int stem = -1, segment = -1; // 段 = v[segment] → v[segment + 1]
    double t = 0.0; // 段上的位置 0..1
    double distance = 1e18; // 到中线
    double side = 0.0; // 顺水看左正右负
    RiverVertex at; // 插值出来的那一点
};

// 某一刻的水：水深、水位、水面半宽
struct RiverStage {
    double depth = 0.0; // 当前中心最大深度
    double level = 0.0; // 水面 y
    double half_width = 0.0;
    double half_left = 0.0, half_right = 0.0;
    double area = 0.0; // 实际湿断面积 m²（中心最大深度 × 水宽并不等于面积）
    double discharge = 0.0, speed = 0.0; // m³/s、m/s，Q / area
    double flow = 0.0; // 流量 / 平岸流量
    bool wet = false;
};

// 横断面：离中线 p_d 米处地面该多高（平底河床 → 岸坡 → 平岸 → 河漫滩），p_d 超过 core 按 core 算
double river_runoff_index(const std::vector<float> &p_index,double p_day);

double river_profile(const RiverVertex &p_at, double p_d, const RiverRules &p_rules);
// 径流指数为 p_runoff 时这一点的水
RiverStage river_stage(const RiverVertex &p_at, double p_runoff, const RiverRules &p_rules);

class WorldRivers {
public:
    static constexpr double BUCKET_M = 64.0;
    static constexpr int MAX_HITS = 8;

    // 含岸线噪声的岸距（陆地为正）
    using ShoreFn = std::function<double(double, double)>;

    WorldRivers() = default;
    // 栅格没有水文（v1）就是空河网
    static std::shared_ptr<const WorldRivers> build(const WorldGrid &p_grid, const RiverRules &p_rules, uint64_t p_seed, const ShoreFn &p_shore);

    bool empty() const { return stems.empty(); }
    const RiverRules &get_rules() const { return rules; }
    const std::vector<RiverStem> &get_stems() const { return stems; }
    double max_reach() const { return reach_max; }
    // 该点落在哪些河的影响范围（core + blend）里：每条河取最近的一段，写进 r_hits（最多 p_cap 个），返回个数
    int near(double p_x, double p_z, RiverHit *r_hits, int p_cap) const;
    // 最近的一条河（中线距离 ≤ p_max_distance，且 ≤ max_reach）
    bool nearest(double p_x, double p_z, double p_max_distance, RiverHit &r_hit) const;
    // 框里（外扩 p_margin）的段，逐个回调 (stem, segment)
    void segments_in(double p_x0, double p_z0, double p_x1, double p_z1, const std::function<void(int, int)> &p_visit) const;
    // 统计：总长（米）、段数
    double total_length() const;

private:
    RiverRules rules;
    std::vector<RiverStem> stems;
    std::vector<uint32_t> seg_stem; // 段号 → 河
    std::vector<uint32_t> seg_index; // 段号 → 河里第几段
    std::unordered_map<int64_t, std::vector<uint32_t>> buckets; // 桶 → 段号
    double reach_max = 0.0;

    void index_segments();
    RiverHit measure(uint32_t p_segment, double p_x, double p_z) const;
};

// 水情：一年的逐日径流指数（1 = 年均）
class RiverRegime {
    std::vector<double> daily; // 径流指数，按日
    double precip = 900.0;
    double days = 336.0;

public:
    static RiverRegime from_index(const std::vector<float> &p_index);
    // 解析 weather.tsv（格式见 export_skyisle.py）；失败返回 false
    static bool parse(const std::string &p_text, RiverRegime &r_regime, std::string &r_error);
    // 逐日降水（mm）→ 径流指数：快、慢两个线性水库，先空转三年到周期稳态再取一年
    static RiverRegime from_precip(const std::vector<double> &p_precip_mm, double p_annual_mm);
    // 常年一个样（没有 weather.tsv 时）
    static RiverRegime steady(double p_runoff = 1.0);

    bool empty() const { return daily.empty(); }
    double precip_mm() const { return precip; }
    double year_days() const { return days; }
    const std::vector<double> &get_daily() const { return daily; }
    // 第 p_day 天（可带小数、可超过一年，按年循环）的径流指数；日与日之间线性过渡
    double runoff(double p_day) const;
};
