// 进程内生成的命令行版（行星计划 P6e）：与游戏里跑的是同一份代码（src/gen_core.*、src/gen_export.*，生成器的 C++ 核心库），
// 不要 Godot、不要 Python。拿来与 Python 那条路（tools/zhouzhu_gen.py）逐字节对照、量速度、离线补包。
//
//   zhouzhu_gen group --config <config.tsv> --seed N --node N --out <岛群包目录> [--lod-dir <粗版目录>]
//   zhouzhu_gen group --config <config.tsv> --seed N --nodes a,b,c --out-root <目录> [--lod-dir <粗版目录>]   （一批：行星层只算一回，包在 <目录>/<节点>/）
//   zhouzhu_gen lods  --config <config.tsv> --seed N --nodes a,b,c --lod-dir <粗版目录>
//   zhouzhu_gen world --config <config.tsv> --seed N --world <世界目录>
//   zhouzhu_gen planet --config <config.tsv> --seed N --out <目录>   （只写 planet.bin / lods.bin / weather.bin，同新建世界时的行星包）
// 输出（stdout）一行一件：节点、各段用时（秒）。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "gen_core.h"
#include "gen_export.h"
#include "py_format.h"
#include "scipy_edt.h"
#include "skyisle/grid.hpp"

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
    std::fprintf(stderr, "zhouzhu_gen group|lods|world|planet --config <config.tsv> --seed N ...（见 tools/zhouzhu_gen.cpp 开头）\n");
    return 2;
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
                        gen_export::weather_bin({}, { cfg.island.get("climate.precip_mm_min", 150.0), cfg.island.get("climate.precip_mm_max", 4000.0),
                                                            cfg.island.get("climate.precip_mm_exp", 1.3) },
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
