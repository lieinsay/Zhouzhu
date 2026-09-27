#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "almanac.h"
#include "planet_frame.h"

// 天气：一处岛群一年的逐日天气（skyisle-gen 给的 weather.tsv）→ 某一刻、某一点的天气。不含 Godot 头。
//
// 三层叠起来：
//  1. 大势（逐日）：一天一行——天气类型、雨量、台面气温、风。按年循环。
//  2. 锋面（时间上的过渡）：第 d 天的天气由一道锋面带来：它在子夜前后（按天抖 ±front_jitter_h）经过群心，顺着那天的风
//     以 front_kmh 扫过岛群；某一点在锋面到达前后 front_width_h 里从前一天的天气渐渐换成这一天的。
//     所以换天不是子夜一刀切，而是一条线从上风扫到下风，岛群两头换天差几个钟头。
//  3. 云团（空间上的分布）：云与雨不是满天一样——一张随风漂的 fbm 场（weather_noise.h），场值高过「按覆盖率取的门槛」的地方
//     在云里 / 雨里，边缘是软的；雨区核心更大，整片平均对得上那天的雨量。天上的云（sky.gdshader）用同一张场，抬头看见的就是头顶的。
//  再按海拔：气温 = 台面气温 + 直减率 × (台面 − 海拔) + 日变化（午后最暖、黎明最冷，阴雨天小）；雨量随海拔加（地形抬升）；
//  雨雪按气温在 snow_temp 上下渐变；云海漫顶的日子雾顶 = 岸缘 + fog_rise，雾顶以下起雾。
//
// 慢变量按海拔分箱逐日算好一年（从年初转两遍到周期稳态）：积雪（度日融雪）、冰（负积温）、物候（绿度、叶、秋色、春芽）、
// 作物季相、日均温。某一刻取前后两天插值。
// 开发者覆盖（WeatherOverride）把大势换成指定的天气、按 blend 渐入（上层按真实时间推 blend）；覆盖成下雨下雪时雨区铺满（强弱照旧随云团起伏），
// 开发者站在哪儿都看得见；气温偏移按直减率等效成海拔挪动。
// 线程：构造之后只读（覆盖由主线程改，读它的也都在主线程）。
// 行星计划 P5：挂上行星天气场（attach_planet）后，大势、锋面、云团改从场里取（见 planet_weather.h）；没挂的（甲乙两岛、平板岛、单测）一切照旧。

enum class WeatherKind : uint8_t {
    CLEAR, // 晴
    CLOUDY, // 多云
    RAIN, // 小雨
    HEAVY_RAIN, // 大雨
    SEA_FOG, // 云海漫顶
    STORM, // 风暴
    SNOW, // 小雪
    HEAVY_SNOW, // 大雪
    BLIZZARD, // 暴风雪
    COUNT,
};

struct WeatherKindDef {
    WeatherKind kind;
    const char *id; // "clear" / "cloudy" / …
    const char *name; // 与生成器的类型名一致：晴、多云、小雨……
    double cloud; // 云量（覆盖率）
    double coverage; // 雨区覆盖率
    double min_mm; // 一天至少下这么多（数据里的「小雨」可能只有零点几毫米：画出来得像小雨）
    double wind_min_ms; // 风至少这么大
    double storm; // 风暴（阵风、闪电）
    double fog; // 云海漫顶
    bool snowy; // 覆盖时强迫下雪（台面气温压到 snow_override_c 以下）
};

namespace weather_kinds {
const WeatherKindDef &at(WeatherKind p_kind);
const WeatherKindDef *by_id(const std::string &p_id); // 没有返回 nullptr
const WeatherKindDef *by_name(const std::string &p_name);
} // namespace weather_kinds

struct ClimateDay {
    WeatherKind kind = WeatherKind::CLEAR;
    double precip_mm = 0.0;
    double temp_c = 18.0; // 台面处的日均气温
    double wind_from_deg = 90.0; // 风从哪来（从北顺时针）
    double wind_ms = 2.0;
    bool sailable = true;
    int storm = 0; // 风暴事件号，0 = 不是风暴日
};

struct Climate {
    CalendarRules calendar;
    double precip_mm = 0.0; // 年降水
    double ref_m = 300.0; // 台面：逐日气温是这个高度的
    double lapse = 6.0; // 直减率 °C/km
    double rim_m = 200.0; // 主岛岸缘
    double fog_rise_m = 150.0; // 云海漫顶时漫到岸缘以上多高
    double snow_temp = 0.5; // 低于它的降水是雪
    std::vector<std::string> season_names;
    std::vector<ClimateDay> days; // 空 = 没有数据（常年晴、18 °C）
};

// 解析 weather.tsv（格式见 tools/export_skyisle.py；旧版只有 day precip temp type 四列也读）
bool parse_climate(const std::string &p_text, Climate &r_climate, std::string &r_error);

struct WeatherRules {
    double front_kmh = 25.0; // 锋面扫过的速度（每游戏钟头多少公里）
    double front_width_h = 2.5; // 锋面过渡带（钟头）
    double front_jitter_h = 2.0; // 锋面过群心的时刻在子夜前后抖多少
    double cell_m = 2600.0; // 云团的特征尺度（噪声一格）
    double drift_scale = 0.1; // 云团随风漂：每游戏钟头漂 风速 × 3600 × 这个（米）；一天 40 分钟时看着像真的风速
    double rain_edge = 0.05; // 雨区的软边（场值）
    double cloud_edge = 0.08;
    double orographic_per_km = 0.4; // 每高出台面 1 km 雨量多这么多
    double diurnal_c = 4.0; // 晴天气温日变化的半幅
    double snow_band_c = 1.5; // 雨雪渐变带的半宽
    double snow_override_c = -4.0; // 覆盖成下雪时台面气温压到这以下
    double melt_mm_per_c = 3.0; // 度日融雪：每度每天融多少毫米水当量
    int bins = 48; // 海拔分几箱
    double pheno_tau_d = 10.0; // 物候跟着多少天的滑动气温走
    double leaf_tau_d = 20.0; // 落叶、发芽跟着的更慢
    double crop_base_c = 6.0; // 作物季相：积温的基点
    double crop_gdd = 1100.0; // 积满这么多度日熟一季
    uint64_t seed = 0x5EA50Aull;
};

// 某一刻某一点的天气
struct WeatherSample {
    WeatherKind kind = WeatherKind::CLEAR; // 此地此刻看着像什么（雨区外的「小雨天」只是多云）
    WeatherKind day_kind = WeatherKind::CLEAR; // 这一天群里的大势（锋面过后的那天）
    double cloud = 0.0; // 头顶的云 0..1
    double cloud_cover = 0.0; // 这一片的云量（覆盖率，给天空着色器）
    double coverage = 0.0; // 这一片的雨区覆盖率
    double precip = 0.0; // 降水强度 0..1（1 ≈ 暴雨）
    double precip_mm_h = 0.0;
    double snow_frac = 0.0; // 降水里雪的份额
    double temp_c = 18.0; // 此地此刻（含海拔、日变化）
    double wind_x = 0.0, wind_z = 0.0; // 风往哪吹（m/s，x 东 z 南）
    double wind_ms = 0.0;
    double storm = 0.0; // 0..1
    double fog = 0.0; // 该高度上的雾 0..1（云海漫顶、雨雾）
    double fog_top = -1e9; // 云海漫顶的雾顶（世界 y）；没有为 −1e9
    double sea_fog = 0.0; // 这一片云海漫顶的程度 0..1（不论高低；站在高处看得见雾顶以下一片白）
    double wet = 0.0; // 地湿 0..1
    double front = 0.0; // 离锋面多近（0 = 不在过渡带，1 = 正在锋面上）
    bool sailable = true;
    // 该海拔的慢变量
    double snow_mm = 0.0; // 积雪水当量
    double ice = 0.0; // 负积温指数（0..60；≥ 8 溪涧冻、≥ 20 河冻）
    double green = 1.0, leaf = 1.0, autumn = 0.0, spring = 0.0, crop = 0.0;
};

// 某一海拔在某一天（插值）的慢变量
struct ElevationState {
    double snow_mm = 0.0, ice = 0.0, green = 1.0, leaf = 1.0, autumn = 0.0, spring = 0.0, crop = 0.0, temp_mean = 18.0;
};

// 开发者覆盖：上层按真实时间把 blend 推向 1（开）/ 0（关），fresh_snow_mm 由上层积（覆盖成下雪时地上新落的雪）。
// kind = COUNT：天气照自动，只偏气温
struct WeatherOverride {
    bool active = false;
    WeatherKind kind = WeatherKind::COUNT;
    double temp_offset_c = 0.0;
    double blend = 0.0;
    double fresh_snow_mm = 0.0;
};

// 一天的大势（逐日表的一行折成的数：云量、雨区、雨量、台面气温、风、风暴、云海漫顶、能否出航、各类型的份额）；
// 锋面过渡、群与群之间（行星计划 P5 的行星天气场）都按分量线性地混。风：x 东、z 南（m/s）
struct WeatherState {
    double cloud = 0.0, coverage = 0.0, mm = 0.0, temp = 18.0, wind_x = 0.0, wind_z = 0.0, storm = 0.0, fog = 0.0, sailable = 1.0;
    double precip = 0.0; // 逐日表那天的雨量（mm；mm 是画雨用的，小雨、大雨有下限）：行星天气场的年降水对账用
    double w[int(WeatherKind::COUNT)] = {};

    void mix_toward(const WeatherState &p_other, double p_t);
    // 按权加上另一份（群与群之间混：先清零、各份按权 add、权和为 1）
    void add(const WeatherState &p_other, double p_w);
    void clear();
    WeatherKind dominant() const;
};
// 逐日表的一行 → 这一天的大势
WeatherState weather_day_state(const ClimateDay &p_day);
// 开发者覆盖成 p_over.kind 时这一天的大势（风向照 p_day 的、风速照 p_base 的；下雨下雪的覆盖让雨区铺满）
WeatherState weather_override_state(const WeatherState &p_base, const ClimateDay &p_day, const WeatherOverride &p_over, const WeatherRules &p_rules);
// 风从 p_from_deg 来，往哪吹（x 东、z 南），单位向量
void weather_wind_to(double p_from_deg, double &r_x, double &r_z);
// 锋面在第 p_day 天子夜前后抖多少（钟头，−1..1 × front_jitter_h；各群同一个数）
double weather_front_jitter_h(long long p_day, const WeatherRules &p_rules);

class PlanetWeather;

class WeatherModel {
    Climate climate;
    WeatherRules rules;
    bool data = false;
    double h_lo = 0.0, h_hi = 1.0; // 分箱覆盖的海拔
    int bins = 1;
    int year = 1;
    static constexpr int FIELDS = 8; // snow ice green leaf autumn spring crop temp
    std::vector<float> table; // [天][箱][FIELDS]，记的是那天过完时
    std::vector<double> soil_wet; // [天] 地湿 0..1（前期降水指数）
    std::vector<double> drift_x, drift_z; // [天] 那天子夜时云团已经漂了多远（米）；多一个 = 一整年
    WeatherOverride over;
    // 行星计划 P5：挂上行星天气场之后，某一点的大势、锋面、云团都从场里取（这一群的平面坐标换成经纬度）——
    // 这一群自己的陆地上场就是它自己的逐日表与云团（一模一样），群与群之间按距离与风向过渡；海拔、慢变量、日变化照旧是这一群的
    std::shared_ptr<const PlanetWeather> planet;
    int planet_node = -1;
    GroupFrame planet_frame;

    // 从场里取了的那一份（群与群之间）：云团场、雾顶
    struct PlanetPart {
        bool used = false;
        double field = 0.5;
        double fog_top = -1e9;
        double ref_m = 0.0; // 几群的台面按权混
    };
    WeatherState day_state(long long p_day) const;
    WeatherState override_state(long long p_day, const WeatherState &p_base) const;
    WeatherState blended(double p_day, double p_x, double p_z, double &r_front, int &r_day_index, PlanetPart *r_planet = nullptr) const;
    double front_arrival(long long p_day, double p_x, double p_z) const;
    void build_tables();
    const float *row(long long p_day) const;

public:
    WeatherModel(); // 没有数据：常年晴、18 °C、纬度 30°
    WeatherModel(const Climate &p_climate, double p_h_min, double p_h_max, const WeatherRules &p_rules = WeatherRules());

    const Climate &get_climate() const { return climate; }
    const WeatherRules &get_rules() const { return rules; }
    const CalendarRules &calendar() const { return climate.calendar; }
    bool has_data() const { return data; }
    // 历日 p_day（整数）那天的数据，按年循环；没有数据给常年晴
    const ClimateDay &day(long long p_day) const;
    std::string season_name(int p_season) const;

    WeatherSample sample(double p_day, double p_x, double p_z, double p_h) const;
    ElevationState elevation(double p_day, double p_h) const;
    // 日均气温（不含日变化、不含雨区降温），给庄稼与作息
    double mean_temperature(double p_day, double p_h) const;
    // 积雪（> 1 mm 水当量）最低处的海拔；哪儿都没雪返回 1e9
    double snow_line(double p_day) const;
    // 负积温 ≥ p_threshold 最低处的海拔（p_threshold 8 = 溪涧冻，20 = 河冻）；哪儿都不冻返回 1e9
    double ice_line(double p_day, double p_threshold) const;

    // 着色器用的海拔表：bins 个箱、每箱 FIELDS 个数（积雪 mm、负积温、绿、叶、秋、春芽、作物、日均温），含覆盖
    int get_bins() const { return bins; }
    static constexpr int table_fields() { return FIELDS; }
    double get_h_lo() const { return h_lo; }
    double get_h_hi() const { return h_hi; }
    void elevation_table(double p_day, std::vector<float> &r_out) const;
    // 云团此刻漂到哪（米；场在 (x − 漂移) / cell_m 处取）
    void cloud_drift(double p_day, double &r_x, double &r_z) const;

    // 行星天气场（行星计划 P5）：p_node 是这一群的节点号、p_frame 是它的平面系；空指针 = 摘掉（照这一群自己的逐日表）
    void attach_planet(const std::shared_ptr<const PlanetWeather> &p_planet, int p_node, const GroupFrame &p_frame);
    bool has_planet() const { return planet != nullptr; }
    const PlanetWeather *get_planet() const { return planet.get(); }
    int get_planet_node() const { return planet_node; }
    const GroupFrame &get_planet_frame() const { return planet_frame; }

    // 开发者覆盖
    void set_override(const WeatherOverride &p_override) { over = p_override; }
    const WeatherOverride &get_override() const { return over; }
    WeatherOverride &edit_override() { return over; }
};

// 雨雪相变（雪的份额）、「天气字」等给界面与测试的小工具
double snow_fraction(double p_temp_c, double p_snow_temp, double p_band_c);
// 风向的中文（「东南风」：风从东南来）
std::string wind_name(double p_to_x, double p_to_z);
