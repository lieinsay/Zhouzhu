#include "gen_core.h"

#include "skyisle/island/landforms.hpp"
#include "skyisle/island/rivernet.hpp"

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <list>
#include <sstream>

#include "skyisle/island/build.hpp"
#include "skyisle/island/climate.hpp"
#include "skyisle/island/generate.hpp"
#include "skyisle/island/resources.hpp"
#include "skyisle/island/settle.hpp"
#include "skyisle/planet/view.hpp"

#include "gen_export.h"
#include "py_format.h"

#ifndef ZHOUZHU_GENERATOR_COMMIT
#define ZHOUZHU_GENERATOR_COMMIT "?"
#endif
#ifndef ZHOUZHU_GENERATOR_DIRTY
#define ZHOUZHU_GENERATOR_DIRTY 0
#endif

using skyisle::Json;
using skyisle::pyround;
namespace fs = std::filesystem;
namespace isl = skyisle::island;
namespace pln = skyisle::planet;

namespace gen_core {

const char *generator_commit() {
    return ZHOUZHU_GENERATOR_COMMIT;
}

namespace {

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

fs::path upath(const std::string &p_utf8) {
    return fs::u8path(p_utf8);
}

// datetime.now().isoformat(timespec="seconds")
std::string now_iso() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

#ifdef _WIN32
constexpr bool CRLF = true; // Python 的 write_text 在 Windows 上把 \n 写成 \r\n（stamp.json、world.json）
#else
constexpr bool CRLF = false;
#endif

Json generator_json() {
    Json g = Json::obj();
    g.set("root", "extern/skyisle-gen");
    g.set("commit", generator_commit());
    g.set("dirty", ZHOUZHU_GENERATOR_DIRTY!=0);
    g.set("backend", "游戏内");
    return g;
}

Json seconds_json(const std::vector<std::pair<std::string, double>> &p_seconds) {
    Json s = Json::obj();
    for (const auto &kv : p_seconds) {
        s.set(kv.first, pyround(kv.second, 2));
    }
    return s;
}

std::string island_config_str(const GenConfig &p_config, const std::string &p_key, const std::string &p_fallback) {
    return p_config.island.gets(p_key, p_fallback);
}

gen_export::GroupMeta meta_of(const PlanetState &p_planet, int64_t p_node) {
    gen_export::GroupMeta m;
    m.node = p_node;
    m.seed = p_planet.seed;
    m.run = p_planet.run;
    m.cls = int(p_planet.world.islands.cls[size_t(p_node)]);
    m.tilt_deg = p_planet.world.planet.axial_tilt_deg;
    m.lapse_c_per_km = p_planet.config.planet.get("s04.climate.lapse_c_per_km", 6.0);
    m.snow_temp_c = p_planet.config.island.get("weather.snow_temp_c", 0.5);
    m.fog_surface_max_m = p_planet.config.island.get("weather.fog_surface_max_m", 800.0);
    m.core_root_ratio = p_planet.config.island.get("core_root_ratio", 5.0);
    return m;
}

// 本群的 NodeInputs（③④ + ⑨ 的人口与邦都）：与 Python 前端 island/engine.inputs(full=True) 同值
isl::NodeInputs inputs_of(const PlanetState &p_planet, int64_t p_node, bool p_polity) {
    isl::NodeInputs x = pln::node_inputs(p_planet.world.islands, p_planet.world.climate, p_node, p_planet.seed, p_planet.config.planet);
    if (p_polity) {
        pln::apply_polity(p_planet.society.polity, p_node, p_planet.config.planet, x);
        pln::apply_routes(p_planet.world.islands, p_planet.society.routes, p_node, x);
    }
    return x;
}

} // namespace

std::array<double, 3> precip_formula(const GenConfig &p_config) {
    return { 0.0,p_config.planet.get("s04.climate.precip_mm_ref",4000.0),1.0 };
}

isl::Group generate_group(const PlanetState &p_planet, int64_t p_node, gen_export::LodEntry *r_lod, gen_export::WeatherEntry *r_weather) {
    // 与生成器的 island::generate（steps = 5、第 0 年）一步一步同：地形 → 水系 → 资源 → 四季与逐日曲线 → 逐日天气 → 聚落。
    // 拆开来做只为在水系之后取粗版（块降采样读的地表、河湖还没被资源的开采、聚落的开垦改过——同 skyisle island lod 另跑的那一遍），
    // 省掉生存世界里为粗版再跑一遍地形 + 水系；单测 gen_seed42 查这条路与 island::generate、与单跑粗版逐字节相同。
    const GenConfig &cfg = p_planet.config;
    const int threads = cfg.threads();
    const isl::NodeInputs x = inputs_of(p_planet, p_node, true);
    isl::Group g = isl::build_terrain(x, p_planet.view, cfg.island, 0.0, threads);
    isl::finalize_islands(g);
    isl::build_hydro(g, p_planet.view, cfg.island, threads);
    isl::finalize_islands(g);
    if (r_lod) {
        *r_lod = gen_export::lod_entry(p_node, g, isl::block_reduce(g, gen_export::lod_factor(g.res_km * 1000.0)));
    }
    isl::build_resources(g, cfg.island);
    isl::detect_landforms(g, cfg.island);
    g.clim = isl::build_climate(g.inp, p_planet.view, cfg.island);
    g.daily = isl::daily_curves(g.clim, g.inp);
    g.has_climate = true;
    g.wparams = isl::season_params(g.clim, cfg.island);
    skyisle::Rng r = isl::part_rng(g.inp, "weather:0");
    g.weather = isl::simulate_year(r, g.clim, g.daily, g.wparams, g.inp.height_m, cfg.island, g.islands[0].rim_j, g.inp.lapse_c_per_km);
    g.year = 0;
    g.has_weather = true;
    if (r_weather) {
        *r_weather = gen_export::weather_entry(p_node, g, meta_of(p_planet, p_node));
    }
    isl::build_settlements(g, p_planet.view, cfg.island);
    isl::build_rivernet(g, cfg.island);
    return g;
}

// 粗版：同 lod.build_lod（cpp 后端）——原生分辨率的地形 + 水系 → 四季、逐日曲线、一年的逐日天气（岸缘取水系之后主岛的 rim_m）→ 块降采样
void lod_only(const PlanetState &p_planet, int64_t p_node, gen_export::LodEntry &r_lod, gen_export::WeatherEntry &r_weather, double p_lod_res_m) {
    const GenConfig &cfg = p_planet.config;
    const isl::NodeInputs x = inputs_of(p_planet, p_node, false);
    isl::Group g = isl::build_terrain(x, p_planet.view, cfg.island, 0.0, cfg.threads());
    isl::finalize_islands(g);
    isl::build_hydro(g, p_planet.view, cfg.island, cfg.threads());
    isl::finalize_islands(g);
    // weather_year：build_climate → daily_curves → season_params → simulate_year（随机流 weather:0）
    g.clim = isl::build_climate(x, p_planet.view, cfg.island);
    g.daily = isl::daily_curves(g.clim, x);
    g.has_climate = true;
    g.wparams = isl::season_params(g.clim, cfg.island);
    skyisle::Rng r = isl::part_rng(x, "weather:0");
    g.weather = isl::simulate_year(r, g.clim, g.daily, g.wparams, x.height_m, cfg.island, g.islands[0].rim_j, x.lapse_c_per_km);
    g.year = 0;
    g.has_weather = true;
    const int f = gen_export::lod_factor(g.res_km * 1000.0, p_lod_res_m);
    const isl::LodBlock b = isl::block_reduce(g, f);
    r_lod = gen_export::lod_entry(p_node, g, b);
    r_weather = gen_export::weather_entry(p_node, g, meta_of(p_planet, p_node));
}

namespace {

bool write_lod_files(const PlanetState &p_planet, const std::string &p_lod_dir, const gen_export::LodEntry &p_lod, const gen_export::WeatherEntry &p_weather,
        std::string &r_error) {
    std::error_code ec;
    fs::create_directories(upath(p_lod_dir), ec);
    const std::string base = (upath(p_lod_dir) / std::to_string(p_lod.node)).u8string();
    // 天气先写：游戏看到 <节点>.bin 就去读 .wx
    if (!write_file(base + ".wx", gen_export::weather_bin({ p_weather }, precip_formula(p_planet.config), std::string()), r_error)) {
        return false;
    }
    return write_file(base + ".bin", gen_export::lods_bin({ p_lod }), r_error);
}

struct CacheEntry {
    std::string sha1;
    uint64_t seed = 0;
    std::shared_ptr<const PlanetState> state;
    std::shared_ptr<std::mutex> building;
};

std::mutex g_cache_mutex;
std::list<CacheEntry> g_cache; // 最近用的在前

} // namespace

// ---------------------------------------------------------------- 进度

void GenStatus::progress(const std::string &p_stage, double p_value, const std::string &p_text) {
    std::lock_guard<std::mutex> lock(mutex);
    state.stage = p_stage;
    state.progress = p_value < 0.0 ? 0.0 : (p_value > 1.0 ? 1.0 : p_value);
    state.text = p_text;
}

void GenStatus::event(const std::string &p_key, int64_t p_node, const std::string &p_text) {
    std::lock_guard<std::mutex> lock(mutex);
    state.events.push_back(Event{ p_key, p_node, p_text });
    state.text = p_text;
}

void GenStatus::finish(const std::vector<std::pair<std::string, double>> &p_seconds) {
    std::lock_guard<std::mutex> lock(mutex);
    state.done = true;
    state.progress = 1.0;
    state.seconds = p_seconds;
    state.text = "好了";
}

void GenStatus::fail(const std::string &p_error) {
    std::lock_guard<std::mutex> lock(mutex);
    state.failed = true;
    state.error = p_error;
    state.text = "出错：" + p_error;
}

bool GenStatus::settled() {
    std::lock_guard<std::mutex> lock(mutex);
    return state.done || state.failed;
}

GenStatus::Snapshot GenStatus::take() {
    std::lock_guard<std::mutex> lock(mutex);
    Snapshot s = state;
    state.events.clear();
    return s;
}

// ---------------------------------------------------------------- 行星层

std::shared_ptr<const PlanetState> planet_state(const GenConfig &p_config, uint64_t p_seed) {
    std::shared_ptr<std::mutex> building;
    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
            if (it->sha1 == p_config.sha1 && it->seed == p_seed) {
                if (it->state) {
                    g_cache.splice(g_cache.begin(), g_cache, it);
                    return g_cache.front().state;
                }
                building = it->building;
                break;
            }
        }
        if (!building) {
            building = std::make_shared<std::mutex>();
            g_cache.push_front(CacheEntry{ p_config.sha1, p_seed, nullptr, building });
        }
    }
    // 同一份只算一回：先拿到这把锁的算，后来的等它
    std::lock_guard<std::mutex> build_lock(*building);
    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        for (CacheEntry &e : g_cache) {
            if (e.sha1 == p_config.sha1 && e.seed == p_seed && e.state) {
                return e.state;
            }
        }
    }
    const double t0 = now_s();
    auto ps = std::make_shared<PlanetState>();
    ps->config = p_config;
    ps->seed = p_seed;
    ps->run = "seed" + std::to_string(p_seed);
    const int threads = p_config.threads();
    ps->world = pln::run(p_config.planet, p_seed);
    ps->society = pln::run_society(p_config.planet, p_seed, ps->world, 9, /*skip_diffusion=*/true, threads);
    ps->view = pln::planet_view(ps->world.planet, ps->world.islands, ps->world.climate, p_config.planet);
    ps->seconds = now_s() - t0;
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
        if (it->sha1 == p_config.sha1 && it->seed == p_seed) {
            it->state = ps;
            g_cache.splice(g_cache.begin(), g_cache, it);
            break;
        }
    }
    // 只留最近的两份（一份约 60 MB）
    while (g_cache.size() > 2 && g_cache.back().state) {
        g_cache.pop_back();
    }
    return ps;
}

bool has_planet_state(const GenConfig &p_config, uint64_t p_seed) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    for (const CacheEntry &e : g_cache) {
        if (e.sha1 == p_config.sha1 && e.seed == p_seed && e.state) {
            return true;
        }
    }
    return false;
}

void clear_planet_states() {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    g_cache.remove_if([](const CacheEntry &e) { return bool(e.state); });
}

// ---------------------------------------------------------------- 一群

bool make_group(const PlanetState &p_planet, int64_t p_node, const std::string &p_out_dir, const std::string &p_lod_dir, const std::string &p_config_file,
        GenStatus *p_status, double p_p0, double p_p1, GroupTimes &r_times, std::string &r_error) {
    const GenConfig &cfg = p_planet.config;
    const int64_t n_nodes = int64_t(p_planet.world.islands.n());
    if (p_node < 0 || p_node >= n_nodes) {
        r_error = "节点号 " + std::to_string(p_node) + " 超出范围 [0, " + std::to_string(n_nodes) + ")";
        return false;
    }
    const double span = p_p1 - p_p0;
    const double t0 = now_s();
    try {
        if (p_status) {
            p_status->progress("generate", p_p0 + 0.05 * span, "生成岛群 #" + std::to_string(p_node) + "：布局、地形、水系、气候、天气、聚落、资源");
        }
        const bool want_lod = !p_lod_dir.empty();
        gen_export::LodEntry lod;
        gen_export::WeatherEntry wx;
        const isl::Group g = generate_group(p_planet, p_node, want_lod ? &lod : nullptr, want_lod ? &wx : nullptr);
        r_times.generate = now_s() - t0;
        const double t1 = now_s();
        if (p_status) {
            p_status->progress("export", p_p0 + 0.6 * span,
                    "岛群 #" + std::to_string(p_node) + "：导出游戏的包（terrain.bin、world.json、sites.tsv、weather.tsv、resources.tsv）");
        }
        const gen_export::GroupFiles files = gen_export::group_files(g, meta_of(p_planet, p_node));
        const fs::path out = upath(p_out_dir);
        const fs::path tmp = out.parent_path() / (out.filename().u8string() + ".tmp");
        std::error_code ec;
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        if (ec) {
            r_error = "建不了目录 " + tmp.u8string() + "：" + ec.message();
            return false;
        }
        const std::pair<const char *, const std::string *> parts[5] = { { "terrain.bin", &files.terrain_bin }, { "weather.tsv", &files.weather_tsv },
            { "world.json", &files.world_json }, { "sites.tsv", &files.sites_tsv }, { "resources.tsv", &files.resources_tsv } };
        for (const auto &p : parts) {
            std::ofstream f(tmp / p.first, std::ios::binary | std::ios::trunc);
            f.write(p.second->data(), std::streamsize(p.second->size()));
            if (!f) {
                r_error = "写不了 " + (tmp / p.first).u8string();
                return false;
            }
        }
        r_times.write = now_s() - t1;
        if (want_lod) {
            const double t2 = now_s();
            if (p_status) {
                p_status->progress("lod", p_p0 + 0.85 * span, "岛群 #" + std::to_string(p_node) + "：2 km 粗版");
            }
            if (!write_lod_files(p_planet, p_lod_dir, lod, wx, r_error)) {
                return false;
            }
            r_times.lod = now_s() - t2;
        }
        // stamp.json（缓存键，计划 4.7）：生成器提交号与后端、配置哈希、导出版本
        Json stamp = Json::obj();
        stamp.set("node", p_node);
        stamp.set("created", now_iso());
        stamp.set("generator", generator_json());
        Json run = Json::obj();
        run.set("config", p_config_file);
        run.set("seed", int64_t(p_planet.seed));
        run.set("config_sha1", cfg.sha1);
        run.set("island_sha1", cfg.island_sha1);
        stamp.set("run", run);
        Json exp = Json::obj();
        exp.set("terrain_version", 6);
        exp.set("exporter", "src/gen_export.cpp");
        stamp.set("export", exp);
        std::vector<std::pair<std::string, double>> secs = { { "generate", r_times.generate }, { "write", r_times.write } };
        if (!p_lod_dir.empty()) {
            secs.push_back({ "lod", r_times.lod });
        }
        stamp.set("seconds", seconds_json(secs));
        {
            const std::string text = pyfmt::json_dumps(stamp, 1, CRLF);
            std::ofstream f(tmp / "stamp.json", std::ios::binary | std::ios::trunc);
            f.write(text.data(), std::streamsize(text.size()));
            if (!f) {
                r_error = "写不了 stamp.json";
                return false;
            }
        }
        fs::remove_all(out, ec);
        fs::rename(tmp, out, ec);
        if (ec) {
            r_error = "改不了名 " + tmp.u8string() + " → " + out.u8string() + "：" + ec.message();
            return false;
        }
        r_times.total = now_s() - t0;
        return true;
    } catch (const std::exception &e) {
        r_error = std::string("生成岛群 #") + std::to_string(p_node) + " 出错：" + e.what();
        return false;
    }
}

bool make_lod(const PlanetState &p_planet, int64_t p_node, const std::string &p_lod_dir, double &r_seconds, std::string &r_error) {
    const double t0 = now_s();
    const int64_t n_nodes = int64_t(p_planet.world.islands.n());
    if (p_node < 0 || p_node >= n_nodes) {
        r_error = "节点号超出范围";
        return false;
    }
    try {
        gen_export::LodEntry lod;
        gen_export::WeatherEntry wx;
        lod_only(p_planet, p_node, lod, wx);
        if (!write_lod_files(p_planet, p_lod_dir, lod, wx, r_error)) {
            return false;
        }
    } catch (const std::exception &e) {
        r_error = std::string("粗版 #") + std::to_string(p_node) + " 出错：" + e.what();
        return false;
    }
    r_seconds = now_s() - t0;
    return true;
}

// ---------------------------------------------------------------- 新建世界

int64_t pick_spawn(const PlanetState &p_planet, std::string &r_reason) {
    const pln::Polity &P = p_planet.society.polity;
    for (int64_t s : P.open_a) {
        if (s >= 0 && s < int64_t(P.states.size()) && P.states[size_t(s)].capital >= 0) {
            r_reason = "开局候选 A（边陲小邦）邦" + std::string(s < 10 ? "00" : (s < 100 ? "0" : "")) + std::to_string(s) + " 的都城";
            return P.states[size_t(s)].capital;
        }
    }
    // np.where(cls == 1, pop（float32）, −1).argmax()
    const pln::Islands &I = p_planet.world.islands;
    int64_t best = 0;
    float best_v = 0.0f;
    for (size_t k = 0; k < I.n(); k++) {
        const float v = I.cls[k] == 1 ? float(P.pop[k]) : -1.0f;
        if (k == 0 || v > best_v) {
            best_v = v;
            best = int64_t(k);
        }
    }
    r_reason = "人口最多的中疏群";
    return best;
}

bool make_world(const GenConfig &p_config, uint64_t p_seed, const std::string &p_world_dir, GenStatus *p_status, WorldResult &r_result, std::string &r_error) {
    const double t0 = now_s();
    const fs::path world = upath(p_world_dir);
    std::error_code ec;
    fs::create_directories(world, ec);
    try {
        if (p_status) {
            p_status->progress("planet", 0.02, "种子 " + std::to_string(p_seed) + "：行星层 ①–⑨（地形、风、岛群分布、气候、航路、文明、政治）");
        }
        const std::shared_ptr<const PlanetState> ps = planet_state(p_config, p_seed);
        r_result.seconds.push_back({ "planet", now_s() - t0 });
        const double t1 = now_s();
        if (p_status) {
            p_status->progress("export_planet", 0.45, "导出行星包（planet.bin；还没有粗版，远处先画替身）");
        }
        const fs::path planet = world / "planet";
        fs::create_directories(planet, ec);
        const std::string source = ps->run + " " + generator_commit();
        if (!write_file((planet / "config.tsv").u8string(), p_config.text, r_error) ||
                !write_file((planet / "planet.bin").u8string(), gen_export::planet_bin(ps->world, ps->society, p_seed, source), r_error) ||
                !write_file((planet / "lods.bin").u8string(), gen_export::lods_bin({}), r_error) ||
                !write_file((planet / "weather.bin").u8string(), gen_export::weather_bin({}, precip_formula(p_config), source), r_error)) {
            return false;
        }
        r_result.seconds.push_back({ "export_planet", now_s() - t1 });
        r_result.spawn = pick_spawn(*ps, r_result.spawn_reason);
        if (p_status) {
            p_status->progress("spawn", 0.55, "出生的岛群：#" + std::to_string(r_result.spawn) + "（" + r_result.spawn_reason + "）");
        }
        GroupTimes times;
        if (!make_group(*ps, r_result.spawn, (world / "groups" / std::to_string(r_result.spawn)).u8string(), (world / "lods").u8string(),
                    (planet / "config.tsv").u8string(), p_status, 0.55, 0.98, times, r_error)) {
            return false;
        }
        r_result.seconds.push_back({ "spawn_group", times.total });
        Json info = Json::obj();
        info.set("name", world.filename().u8string());
        info.set("mode", "survival");
        info.set("seed", int64_t(p_seed));
        info.set("spawn", r_result.spawn);
        info.set("spawn_reason", r_result.spawn_reason);
        info.set("planet", "planet");
        info.set("config", "planet/config.tsv");
        info.set("generator", generator_json());
        info.set("config_sha1", p_config.sha1);
        info.set("created", now_iso());
        info.set("seconds", seconds_json(r_result.seconds));
        if (!write_file((world / "world.json").u8string(), pyfmt::json_dumps(info, 1, CRLF), r_error)) {
            return false;
        }
        r_result.seconds.push_back({ "total", now_s() - t0 });
        return true;
    } catch (const std::exception &e) {
        r_error = std::string("新建世界出错：") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------- 文件

bool write_file(const std::string &p_path, const std::string &p_data, std::string &r_error) {
    const fs::path path = upath(p_path);
    const fs::path tmp = path.parent_path() / (path.filename().u8string() + ".tmp");
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(p_data.data(), std::streamsize(p_data.size()));
        if (!f) {
            r_error = "写不了 " + tmp.u8string();
            return false;
        }
    }
    fs::rename(tmp, path, ec); // 目标已有就换掉
    if (ec) {
        r_error = "改不了名 " + tmp.u8string() + "：" + ec.message();
        return false;
    }
    return true;
}

bool read_file(const std::string &p_path, std::string &r_data) {
    std::ifstream f(upath(p_path), std::ios::binary);
    if (!f) {
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    r_data = ss.str();
    return true;
}

bool file_exists(const std::string &p_path) {
    std::error_code ec;
    return fs::exists(upath(p_path), ec);
}

} // namespace gen_core
