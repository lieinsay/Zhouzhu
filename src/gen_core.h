#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "gen_config.h"
#include "gen_export.h"
#include "skyisle/island/types.hpp"
#include "skyisle/planet/civ.hpp"
#include "skyisle/planet/planet.hpp"

// 进程内的生成（行星计划 P6e，docs/PLAN-PLANET.md 第 4.7–4.9 节、第五节 P6e）：生成器的 C++ 核心库（extern/skyisle-gen/core）编进游戏，
// 新建世界、按需生成岛群与粗版都在这里做完、写出与 Python 那条路（tools/zhouzhu_gen.py）同样的文件——上层（GroupProvider、新建世界）
// 照旧只认「交一件活、读进度、在世界的存档目录拿结果文件」。不含 Godot 头：游戏（后台任务池里跑）与命令行工具 zhouzhu_gen 共用。
// 路径一律是 UTF-8 的 std::string（存档目录在「州渚」下，Windows 上按宽字符开文件）。
namespace gen_core {

// 编进来的生成器提交号（CMake 配置时读子模块 extern/skyisle-gen 的 HEAD）
const char *generator_commit();

// 进度（线程安全）：后台的活往里写，主线程取
class GenStatus {
public:
    struct Event {
        std::string key; // lod_done / lod_failed
        int64_t node = -1;
        std::string text;
    };
    struct Snapshot {
        std::string stage, text, error;
        double progress = 0.0;
        bool done = false, failed = false;
        std::vector<std::pair<std::string, double>> seconds; // 做完时各段用时
        std::vector<Event> events; // 取走后清空
    };

    void progress(const std::string &p_stage, double p_value, const std::string &p_text);
    void event(const std::string &p_key, int64_t p_node, const std::string &p_text);
    void finish(const std::vector<std::pair<std::string, double>> &p_seconds);
    void fail(const std::string &p_error);
    bool settled(); // 说过好了或出错了
    Snapshot take();
    // 撤：一批一批做的活（补粗版）在两群之间看它，停下来（在算的那一群照样算完）
    void cancel() { cancelled_flag.store(true); }
    bool cancelled() const { return cancelled_flag.load(); }

private:
    std::mutex mutex;
    Snapshot state;
    std::atomic<bool> cancelled_flag{ false };
};

// 一个世界的行星层：①–⑨（⑧ 跳过：游戏只要人口与邦）+ 第三层要的 PlanetView。算一回、之后只读，几件活可以同时读
struct PlanetState {
    GenConfig config;
    uint64_t seed = 0;
    std::string run; // 生成器 run 目录的名字：seed<N>（岛群包 world.json 的 source.run 照写）
    skyisle::planet::World world;
    skyisle::planet::Society society;
    skyisle::island::PlanetView view;
    double seconds = 0.0; // 算行星层用了多久
};
// 进程里按（配置的 SHA-1、种子）缓存最近的两份；同一份同时要的等先来的那个算完
std::shared_ptr<const PlanetState> planet_state(const GenConfig &p_config, uint64_t p_seed);
bool has_planet_state(const GenConfig &p_config, uint64_t p_seed);
void clear_planet_states();

// 一群的生成器第三层：与 island::generate（steps = 5、第 0 年）同，r_lod / r_weather 给了就在水系之后顺带出这一群的粗版与逐日天气
// （同 skyisle island lod 另跑一遍地形 + 水系的结果，逐字节相同）
skyisle::island::Group generate_group(const PlanetState &p_planet, int64_t p_node, gen_export::LodEntry *r_lod = nullptr,
        gen_export::WeatherEntry *r_weather = nullptr);
// 只出粗版（地形 + 水系 + 四季与一年的逐日天气，不跑资源、聚落：同 skyisle island lod）
void lod_only(const PlanetState &p_planet, int64_t p_node, gen_export::LodEntry &r_lod, gen_export::WeatherEntry &r_weather);
// [island.climate] precip_mm_min / max / exp（逐日天气包头里的 precip_formula）
std::array<double, 3> precip_formula(const GenConfig &p_config);

// 一群：生成器第三层（地形 → 水系 → 资源 → 四季 → 天气 → 聚落）→ 岛群包的五份 + stamp.json，先写 <out>.tmp、写好再改名成 <out>；
// p_lod_dir 非空（生存世界）：顺带出这一群 2 km 的粗版 <lod_dir>/<节点>.bin 与第 0 年的逐日天气 <lod_dir>/<节点>.wx（天气先写）。
// 进度报在 [p0, p1] 里。p_config_file 只记进 stamp.json。
struct GroupTimes {
    double generate = 0.0, write = 0.0, lod = 0.0, total = 0.0;
};
bool make_group(const PlanetState &p_planet, int64_t p_node, const std::string &p_out_dir, const std::string &p_lod_dir, const std::string &p_config_file,
        GenStatus *p_status, double p_p0, double p_p1, GroupTimes &r_times, std::string &r_error);
// 一群的粗版（地形 + 水系 + 四季与一年的逐日天气，不跑资源、聚落：同 skyisle island lod）→ <lod_dir>/<节点>.wx、<节点>.bin
bool make_lod(const PlanetState &p_planet, int64_t p_node, const std::string &p_lod_dir, double &r_seconds, std::string &r_error);

// 新建世界（生存模式，计划 4.8、D15）：行星层 → <world>/planet/（planet.bin、lods.bin 0 条、weather.bin 0 条、config.tsv）→ 出生的岛群
// （groups/<节点>/ 与它的粗版）→ world.json。进度报在 [0, 1]。
struct WorldResult {
    int64_t spawn = -1;
    std::string spawn_reason;
    std::vector<std::pair<std::string, double>> seconds;
};
bool make_world(const GenConfig &p_config, uint64_t p_seed, const std::string &p_world_dir, GenStatus *p_status, WorldResult &r_result, std::string &r_error);

// 出生的岛群：生成器 ⑨ 开局候选 A（边陲小邦）第一个邦的都城（D1：世界设计由生成器定）；没有就挑人口最多的中疏群
int64_t pick_spawn(const PlanetState &p_planet, std::string &r_reason);

// 文件（UTF-8 路径）：先写 <路径>.tmp 再改名
bool write_file(const std::string &p_path, const std::string &p_data, std::string &r_error);
bool read_file(const std::string &p_path, std::string &r_data);
bool file_exists(const std::string &p_path);

} // namespace gen_core
