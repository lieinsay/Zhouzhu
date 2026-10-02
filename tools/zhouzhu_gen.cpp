// 进程内生成的命令行版（行星计划 P6e）：与游戏里跑的是同一份代码（src/gen_core.*、src/gen_export.*，生成器的 C++ 核心库），
// 不要 Godot、不要 Python。拿来与 Python 那条路（tools/zhouzhu_gen.py）逐字节对照、量速度、离线补包。
//
//   zhouzhu_gen group --config <config.tsv> --seed N --node N --out <岛群包目录> [--lod-dir <粗版目录>]
//   zhouzhu_gen group --config <config.tsv> --seed N --nodes a,b,c --out-root <目录> [--lod-dir <粗版目录>]   （一批：行星层只算一回，包在 <目录>/<节点>/）
//   zhouzhu_gen lods  --config <config.tsv> --seed N --nodes a,b,c --lod-dir <粗版目录>
//   zhouzhu_gen world --config <config.tsv> --seed N --world <世界目录>
//   zhouzhu_gen planet --config <config.tsv> --seed N --out <目录>   （只写 planet.bin / lods.bin / weather.bin，同新建世界时的行星包）
//   zhouzhu_gen near-lods --config <config.tsv> --seed N --lods <lods.bin> --near N [--near-res 500] [--near-radius 600] [--out <lods.bin>]
//   zhouzhu_gen refresh-lods --config <config.tsv> --seed N --lods <lods.bin> --nodes a,b --out <lods.bin>
//     （按原有分辨率更新指定岛群，保留其余条目的原始数据。）
//   zhouzhu_gen refresh-weather --config <config.tsv> --seed N --lods <lods.bin> --out <weather.bin> [--fine-root <groups目录>]
//     （换行星包里「开局周围那一圈」的细一档粗版（D11）：留下每群的第一条（全行星那一档），丢掉后面的（旧的一圈），
//       再给 --near 那一群 --near-radius km 内（群心之间的大圆距离）的每一群生成 --near-res 米一档、按节点排在后面——
//       同 tools/export_planet.py --near N --near-res 500 --near-radius 600 写出的 lods.bin；--out 缺省就地改写）
// 输出（stdout）一行一件：节点、各段用时（秒）。
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gen_core.h"
#include "gen_export.h"
#include "py_format.h"
#include "scipy_edt.h"
#include "skyisle/grid.hpp"
#include "skyisle/island/climate.hpp"
#include "skyisle/planet/view.hpp"

namespace {

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::vector<int64_t> parse_nodes(const std::string &p_s) {
    std::vector<int64_t> out;
    size_t a = 0;
    while (a < p_s.size()) {
        size_t b = p_s.find(',', a);
        if (b == std::string::npos) {
            b = p_s.size();
        }
        if (b > a) {
            out.push_back(std::atoll(p_s.substr(a, b - a).c_str()));
        }
        a = b + 1;
    }
    return out;
}

int usage() {
    std::fprintf(stderr, "zhouzhu_gen group|lods|world|planet|near-lods|refresh-lods|refresh-weather --config <config.tsv> --seed N ...（见 tools/zhouzhu_gen.cpp 开头）\n");
    return 2;
}

// lods.bin（export_planet.write_lods / gen_export::lods_bin 的格式）读回成一条条；格距、左上角是 f32，读回再写逐位不变
bool parse_lods(const std::string &p_bytes, std::vector<gen_export::LodEntry> &r_entries, std::string &r_error) {
    auto get = [&](size_t p_at, auto &r_value) {
        std::memcpy(&r_value, p_bytes.data() + p_at, sizeof(r_value));
    };
    if (p_bytes.size() < 12 || p_bytes.compare(0, 4, "ZZLD") != 0) {
        r_error = "不是 lods.bin";
        return false;
    }
    uint32_t version = 0, count = 0;
    get(4, version);
    get(8, count);
    constexpr size_t ROW = 4 + 4 * 3 + 2 * 4 + 8 + 4;
    if (version != 1 || p_bytes.size() < 12 + size_t(count) * ROW) {
        r_error = "lods.bin 版本不对或索引不全";
        return false;
    }
    r_entries.clear();
    for (uint32_t k = 0; k < count; k++) {
        const size_t at = 12 + size_t(k) * ROW;
        uint32_t node = 0, bytes = 0;
        float res = 0.0f, ox = 0.0f, oy = 0.0f;
        uint16_t rows = 0, cols = 0, n_islands = 0;
        uint64_t off = 0;
        get(at, node);
        get(at + 4, res);
        get(at + 8, ox);
        get(at + 12, oy);
        get(at + 16, rows);
        get(at + 18, cols);
        get(at + 20, n_islands);
        get(at + 24, off);
        get(at + 32, bytes);
        if (off + bytes > p_bytes.size()) {
            r_error = "lods.bin 第 " + std::to_string(k) + " 条的数据越界";
            return false;
        }
        gen_export::LodEntry e;
        e.node = node;
        e.res_m = res;
        e.origin_x = ox;
        e.origin_y = oy;
        e.rows = rows;
        e.cols = cols;
        e.n_islands = n_islands;
        e.blob = p_bytes.substr(size_t(off), bytes);
        r_entries.push_back(std::move(e));
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage();
    }
    const std::string cmd = argv[1];
    if (cmd == "repr" && argc == 4) {
        // 对照 Python 的写法用：输入一行一个 double 的 64 位十六进制，输出 repr / format(x, "g") / repr(round(x, 1)) / repr(round(x, 3))
        std::string in, out;
        if (!gen_core::read_file(argv[2], in)) {
            return 2;
        }
        size_t a = 0;
        while (a < in.size()) {
            size_t b = in.find('\n', a);
            if (b == std::string::npos) {
                b = in.size();
            }
            const std::string line = in.substr(a, b - a);
            a = b + 1;
            if (line.empty()) {
                continue;
            }
            const uint64_t bits = std::strtoull(line.c_str(), nullptr, 16);
            double x;
            std::memcpy(&x, &bits, 8);
            out += pyfmt::repr(x) + "\t" + pyfmt::fmt_g(x) + "\t" + pyfmt::repr(skyisle::pyround(x, 1)) + "\t" + pyfmt::repr(skyisle::pyround(x, 3)) + "\n";
        }
        std::string error;
        return gen_core::write_file(argv[3], out, error) ? 0 : 1;
    }
    if (cmd == "edt" && argc == 4) {
        // 对照 scipy 用：输入 u32 H、u32 W、H·W 个字节（非 0 = 不是特征），输出最近特征的行、列（int32 各 H·W 个）
        std::string in;
        if (!gen_core::read_file(argv[2], in) || in.size() < 8) {
            return 2;
        }
        uint32_t h = 0, w = 0;
        std::memcpy(&h, in.data(), 4);
        std::memcpy(&w, in.data() + 4, 4);
        std::vector<int32_t> fr, fc;
        scipy_nd::feature_transform(reinterpret_cast<const uint8_t *>(in.data() + 8), int(h), int(w), fr, fc);
        std::string out(reinterpret_cast<const char *>(fr.data()), fr.size() * 4);
        out.append(reinterpret_cast<const char *>(fc.data()), fc.size() * 4);
        std::string error;
        return gen_core::write_file(argv[3], out, error) ? 0 : 1;
    }
    std::map<std::string, std::string> a;
    for (int k = 2; k + 1 < argc; k += 2) {
        a[argv[k]] = argv[k + 1];
    }
    std::string text;
    if (!a.count("--config") || !gen_core::read_file(a["--config"], text)) {
        std::fprintf(stderr, "读不了配置 %s\n", a.count("--config") ? a["--config"].c_str() : "（没给 --config）");
        return 2;
    }
    GenConfig cfg;
    std::string error;
    if (!parse_gen_config(text, cfg, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    const uint64_t seed = uint64_t(std::atoll(a.count("--seed") ? a["--seed"].c_str() : "42"));
    const double t0 = now_s();
    if (cmd == "world") {
        gen_core::WorldResult r;
        if (!gen_core::make_world(cfg, seed, a["--world"], nullptr, r, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::printf("world seed %llu spawn %lld", static_cast<unsigned long long>(seed), static_cast<long long>(r.spawn));
        for (const auto &kv : r.seconds) {
            std::printf(" %s %.2f", kv.first.c_str(), kv.second);
        }
        std::printf("\n");
        return 0;
    }
    const std::shared_ptr<const gen_core::PlanetState> ps = gen_core::planet_state(cfg, seed);
    std::printf("planet seed %llu %.2f s\n", static_cast<unsigned long long>(seed), ps->seconds);
    if (cmd == "planet") {
        const std::string out = a["--out"];
        const std::string source = ps->run + " " + gen_core::generator_commit();
        if (!gen_core::write_file(out + "/planet.bin", gen_export::planet_bin(ps->world, ps->society, seed, source), error) ||
                !gen_core::write_file(out + "/lods.bin", gen_export::lods_bin({}), error) ||
                !gen_core::write_file(out + "/weather.bin",
                        gen_export::weather_bin({}, {0.0,cfg.planet.get("s04.climate.precip_mm_ref",4000.0),1.0},
                                source),
                        error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    if (cmd == "group") {
        std::vector<int64_t> nodes = a.count("--nodes") ? parse_nodes(a["--nodes"]) : std::vector<int64_t>{ std::atoll(a["--node"].c_str()) };
        int failed = 0;
        for (int64_t node : nodes) {
            const std::string out = a.count("--out-root") ? a["--out-root"] + "/" + std::to_string(node) : a["--out"];
            gen_core::GroupTimes t;
            if (!gen_core::make_group(*ps, node, out, a.count("--lod-dir") ? a["--lod-dir"] : std::string(), a["--config"], nullptr, 0.0, 1.0, t, error)) {
                std::fprintf(stderr, "#%lld: %s\n", static_cast<long long>(node), error.c_str());
                failed++;
                continue;
            }
            std::printf("group %lld generate %.3f write %.3f lod %.3f total %.3f\n", static_cast<long long>(node), t.generate, t.write, t.lod, t.total);
            std::fflush(stdout);
        }
        std::printf("done %.2f s\n", now_s() - t0);
        return failed ? 1 : 0;
    }
    if (cmd=="refresh-weather") {
        std::string bytes; std::vector<gen_export::LodEntry> lods;
        if (!gen_core::read_file(a["--lods"],bytes) || !parse_lods(bytes,lods,error)) { std::fprintf(stderr,"%s\n",error.c_str());return 1; }
        std::map<int64_t,double> rims, resolutions;
        for(const auto &l:lods) if(l.n_islands && (!rims.count(l.node) || l.res_m<resolutions[l.node])) {
            float rim=0; std::memcpy(&rim,l.blob.data()+8,4); rims[l.node]=rim; resolutions[l.node]=l.res_m;
        }
        if (a.count("--fine-root")) for (auto &kv:rims) {
            std::string sites;
            if (!gen_core::read_file(a["--fine-root"]+"/"+std::to_string(kv.first)+"/sites.tsv",sites)) continue;
            std::istringstream rows(sites); std::string line;
            while (std::getline(rows,line)) {
                std::istringstream row(line); std::string kind; int id; double rim;
                if ((row>>kind>>id>>rim) && kind=="island" && id==0) { kv.second=rim; break; }
            }
        }
        std::vector<gen_export::WeatherEntry> entries;
        for (const auto &kv:rims) {
            skyisle::island::Group g;
            g.inp=skyisle::planet::node_inputs(ps->world.islands,ps->world.climate,kv.first,seed,cfg.planet);
            g.P_mm=g.inp.precip_mm_ref*std::clamp(g.inp.precip,0.0,1.0);
            g.islands.resize(1); g.islands[0].rim_j=kv.second;
            g.clim=skyisle::island::build_climate(g.inp,ps->view,cfg.island);
            g.daily=skyisle::island::daily_curves(g.clim,g.inp);
            g.wparams=skyisle::island::season_params(g.clim,cfg.island);
            auto rng=skyisle::island::part_rng(g.inp,"weather:0");
            g.weather=skyisle::island::simulate_year(rng,g.clim,g.daily,g.wparams,g.inp.height_m,cfg.island,kv.second,g.inp.lapse_c_per_km);
            gen_export::GroupMeta m; m.node=kv.first; m.seed=seed;
            m.snow_temp_c=cfg.island.get("weather.snow_temp_c",0.5);
            m.fog_surface_max_m=cfg.island.get("weather.fog_surface_max_m",800.0);
            entries.push_back(gen_export::weather_entry(kv.first,g,m));
        }
        if(!gen_core::write_file(a["--out"],gen_export::weather_bin(entries,{0,cfg.planet.get("s04.climate.precip_mm_ref",4000),1},ps->run+" "+gen_core::generator_commit()),error))return 1;
        std::printf("weather %zu groups %.2f s\n",entries.size(),now_s()-t0);return 0;
    }
    if (cmd == "refresh-lods") {
        if (!a.count("--lods") || !a.count("--out") || !a.count("--nodes")) return usage();
        std::string bytes;
        std::vector<gen_export::LodEntry> entries;
        if (!gen_core::read_file(a["--lods"], bytes) || !parse_lods(bytes, entries, error)) {
            std::fprintf(stderr, "LOD read failed: %s\n", error.c_str());
            return 1;
        }
        const auto nodes = parse_nodes(a["--nodes"]);
        for (const auto node : nodes) {
            bool found = false;
            for (auto &entry : entries) {
                if (entry.node != node) continue;
                gen_export::LodEntry fresh;
                gen_export::WeatherEntry weather;
                gen_core::lod_only(*ps, node, fresh, weather, entry.res_m);
                entry = std::move(fresh);
                found = true;
                std::printf("refresh-lods %lld %.0f m\n", static_cast<long long>(node), entry.res_m);
                std::fflush(stdout);
            }
            if (!found) {
                std::fprintf(stderr, "No existing LOD for node %lld\n", static_cast<long long>(node));
                return 1;
            }
        }
        if (!gen_core::write_file(a["--out"], gen_export::lods_bin(entries), error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }
    if (cmd == "near-lods") {
        const std::string in_path = a["--lods"];
        const std::string out_path = a.count("--out") ? a["--out"] : in_path;
        const int64_t near = std::atoll(a["--near"].c_str());
        const double near_res = a.count("--near-res") ? std::atof(a["--near-res"].c_str()) : 500.0;
        const double radius_km = a.count("--near-radius") ? std::atof(a["--near-radius"].c_str()) : 600.0;
        std::string bytes;
        std::vector<gen_export::LodEntry> old_entries;
        if (!gen_core::read_file(in_path, bytes) || !parse_lods(bytes, old_entries, error)) {
            std::fprintf(stderr, "读不了 %s：%s\n", in_path.c_str(), error.c_str());
            return 2;
        }
        const auto &I = ps->world.islands;
        if (near < 0 || size_t(near) >= I.n()) {
            std::fprintf(stderr, "没有岛群 #%lld\n", static_cast<long long>(near));
            return 2;
        }
        // 每群的第一条是全行星那一档（export_planet 先写全行星、再写近邻），后面的是旧的一圈
        std::vector<gen_export::LodEntry> entries;
        std::vector<char> seen(I.n(), 0);
        size_t dropped = 0, dropped_bytes = 0;
        for (gen_export::LodEntry &e : old_entries) {
            if (e.node >= 0 && size_t(e.node) < seen.size() && !seen[size_t(e.node)]) {
                seen[size_t(e.node)] = 1;
                entries.push_back(std::move(e));
            } else {
                dropped++;
                dropped_bytes += e.blob.size();
            }
        }
        // 近邻：群心之间的大圆距离（同 export_planet：R · arccos(clip(sin φ0 sin φ + cos φ0 cos φ cos(λ − λ0))）
        const double deg = 3.14159265358979323846 / 180.0;
        const double R = ps->world.planet.radius_km;
        const double la0 = I.lat[size_t(near)] * deg, lo0 = I.lon[size_t(near)] * deg;
        std::vector<int64_t> nodes;
        for (size_t k = 0; k < I.n(); k++) {
            const double la = I.lat[k] * deg, lo = I.lon[k] * deg;
            double c = std::sin(la0) * std::sin(la) + std::cos(la0) * std::cos(la) * std::cos(lo - lo0);
            c = c < -1.0 ? -1.0 : (c > 1.0 ? 1.0 : c);
            const double d = R * std::acos(c);
            if (d <= radius_km) {
                nodes.push_back(int64_t(k));
                if (radius_km - d < 1.0) {
                    std::printf("注意：#%zu 离 #%lld %.3f km，贴着 %.0f km 的线\n", k, static_cast<long long>(near), d, radius_km);
                }
            }
        }
        size_t added_bytes = 0;
        for (int64_t node : nodes) {
            const double t = now_s();
            gen_export::LodEntry lod;
            gen_export::WeatherEntry wx;
            gen_core::lod_only(*ps, node, lod, wx, near_res);
            added_bytes += lod.blob.size();
            std::printf("near %lld res %.0f %dx%d %zu B %.2f s\n", static_cast<long long>(node), lod.res_m, lod.rows, lod.cols, lod.blob.size(), now_s() - t);
            std::fflush(stdout);
            entries.push_back(std::move(lod));
        }
        if (!gen_core::write_file(out_path, gen_export::lods_bin(entries), error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::printf("near-lods #%lld：丢掉旧的一圈 %zu 条 %.2f MB，加 %zu 条 %.2f MB（%.0f km 内、%.0f m）；共 %zu 条 → %s，%.2f s\n",
                static_cast<long long>(near), dropped, dropped_bytes / 1e6, nodes.size(), added_bytes / 1e6, radius_km, near_res, entries.size(),
                out_path.c_str(), now_s() - t0);
        return 0;
    }
    if (cmd == "lods") {
        int failed = 0;
        for (int64_t node : parse_nodes(a["--nodes"])) {
            double s = 0.0;
            if (!gen_core::make_lod(*ps, node, a["--lod-dir"], s, error)) {
                std::fprintf(stderr, "#%lld: %s\n", static_cast<long long>(node), error.c_str());
                failed++;
                continue;
            }
            std::printf("lod %lld %.3f\n", static_cast<long long>(node), s);
            std::fflush(stdout);
        }
        return failed ? 1 : 0;
    }
    return usage();
}
