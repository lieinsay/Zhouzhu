// 岛群体检：把一个岛群的全部码头与村落离线铺一遍，报出哪里铺不好。不链 Godot。
//
//   build/Debug/zhouzhu_site_survey.exe game/worlds/2051                  # 摘要
//   build/Debug/zhouzhu_site_survey.exe game/worlds/2051 --report out.tsv # 另写逐条明细（每个码头 / 村一行）
//   build/Release/zhouzhu_site_survey.exe game/worlds/2051 --sim 24      # 另把全部村落开账、在远处结 24 个游戏小时，报花费
//   build/Debug/zhouzhu_site_survey.exe game/worlds/2051 --rivers 河.tsv  # 另把河网逐点写出（河号、x、z、平岸水面、宽、深、汇流）
//   有 weather.tsv 时另报一年的天气（逐月的台面气温、雨量、雪线、河冰线、物候、日出日落定的出工收工），再抽几处村子
//   一年里每两个钟头取一次局地天气，看雨量对不对得上那一年、坏天气歇工多少钟头（局地雨量差出四成就不达标）
//   有 resources.tsv 时另把采场与地标落一遍（采掘面、堆场；采掘面要在 60 m 内够得着开局的船野泊，规划时就量好了，只看地面、不看房子），
//   再抽样生成散布（每隔 --scatter-stride 块取一块，默认 6），报每公顷长了多少、每块花多少时间、泊地上有没有树
//   build/Release/zhouzhu_site_survey.exe --planet game/planet/seed42 [--home 2051] [--overlap]
//                                          # 行星包：摘要、邻群表、坐标自检、每群粗版对陆地；--overlap 另查邻群陆地叠没叠
//   build/Release/zhouzhu_site_survey.exe --planet-weather game/planet/seed42 [--grid 2] [--steps 4] [--worlds game/worlds]
//                                          # 行星天气场（行星计划 P5）：逐日天气包、粗细两版天气对照、一年里全行星的云量与降水按纬度带与 ④ 的气候场对账（±20%）
//   build/Release/zhouzhu_site_survey.exe --island-floor [<群目录>]
//                                          # 本机生成过的岛群逐座查岛底离云带顶一千米（浮高计划 F3；缺省查 %APPDATA%\Godot\app_userdata\州渚\worlds\seed42\groups，不进 ctest）
//   选项：--seed N（默认 2051，与 World 的 terrain_seed 一致）、--max-missing F（摆不下的房子占比上限，默认 0.02）、
//         --sim H（远处结算几个游戏小时；0 = 不结）、--scatter-stride N（散布抽样的间隔块数；0 = 不抽）、
//         --calendar N（开局是一年里的第几天，远处结算的时令按它；默认 3，与 WorldClock 一致）
//
// 游戏里走近了才铺的东西，这里一次全铺：同一套纯算法（world_sites.h），所以这里铺得好，游戏里就铺得好。
// 退出码：0 = 全部达标；1 = 有码头找不到岸缘，或摆不下的房子超过上限，或资源点落不了地 / 野泊不下的超过上限；2 = 读不了资产。
// 换了岛群、改了生成器或铺村的规矩之后跑一遍，不用进游戏一个个去看。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <cmath>
#include <map>
#include <tuple>

#include "almanac.h"
#include "planet_data.h"
#include "planet_frame.h"
#include "planet_weather.h"
#include "freight.h"
#include "local_season.h"
#include "resource_scatter.h"
#include "resource_sites.h"
#include "weather.h"
#include "wild_landing.h"
#include "world_ground.h"
#include "world_grid.h"
#include "world_ledger.h"
#include "world_sites.h"
#include "world_terrain.h"

namespace {

bool read_file(const std::string &p_path, std::string &r_text) {
    std::ifstream in(p_path, std::ios::binary);
    if (!in) {
        return false;
    }
    r_text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

constexpr double SURVEY_PI = 3.14159265358979323846;

double since_ms(const std::chrono::steady_clock::time_point &p_start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_start).count();
}

} // namespace

// ---------------------------------------------------------------- 行星体检（--planet）
// 行星包（game/planet/<seed>）读一遍：摘要、2051 的邻群表、坐标自检、每群粗版解开对陆地；--overlap 另查邻群陆地叠没叠（Release 下几十秒）。
int survey_planet(const std::string &p_dir, int p_home, bool p_overlap) {
    const auto t0 = std::chrono::steady_clock::now();
    PlanetData pd;
    std::string error;
    if (!pd.load_files(p_dir, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    auto file_mb = [&](const char *p_name) {
        std::ifstream in(p_dir + "/" + p_name, std::ios::binary | std::ios::ate);
        return in ? double(in.tellg()) / 1e6 : 0.0;
    };
    int fails = 0;
    double land = 0.0, pop = 0.0;
    int by_cls[4] = { 0, 0, 0, 0 }, constrained = 0, crossing = 0;
    const std::vector<double> *viol = pd.column("lod.violation_km"), *cons = pd.column("lod.constrained");
    for (int k = 0; k < pd.node_count(); k++) {
        const PlanetNode &n = pd.node(k);
        land += n.area_km2;
        pop += n.pop;
        by_cls[std::clamp(n.cls, 0, 3)]++;
        constrained += cons && (*cons)[size_t(k)] > 0.5 ? 1 : 0;
        crossing += viol && (*viol)[size_t(k)] > 1.5 ? 1 : 0; // 越过分界线（半道缝以外）
    }
    std::printf("行星 seed %d（%s）：半径 %.0f km、倾角 %.0f°、一年 %d 天；%d 个岛群（密接 %d、中疏 %d、稀疏 %d、孤悬 %d），陆地 %.4g km²、人口 %.4g\n", pd.seed,
            pd.source.c_str(), pd.radius_km, pd.tilt_deg, pd.calendar.year_days, pd.node_count(), by_cls[0], by_cls[1], by_cls[2], by_cls[3], land, pop);
    std::printf("包：planet.bin %.1f MB、lods.bin %.1f MB（%d 份粗版）；1° 场 %d 张\n", file_mb("planet.bin"), file_mb("lods.bin"), pd.lod_count(), int(pd.all_grids().size()));
    std::printf("势力范围：布局重摆过的 %d 群，陆地仍越过分界线的 %d 群\n", constrained, crossing);
    // 邻群表
    const PlanetNode &home = pd.node(p_home);
    std::printf("%d（南纬 %.2f°、东经 %.2f°）600 km 内的岛群：\n", p_home, -home.lat, home.lon);
    std::printf("  节点   距离 km   方位°   陆地 km²   主峰 m   粗版\n");
    for (int k : pd.nodes_within(home.lat, home.lon, 600000.0)) {
        if (k == p_home) {
            continue;
        }
        const PlanetNode &n = pd.node(k);
        std::string res;
        for (double r : pd.lod_resolutions(k)) {
            res += (res.empty() ? "" : " / ") + std::to_string(int(r)) + " m";
        }
        std::printf("  %5d  %7.1f  %6.1f  %9.0f  %7.0f   %s\n", k, pd.node_distance_m(p_home, k) / 1000.0, bearing_deg(home.lat, home.lon, n.lat, n.lon), n.area_km2,
                n.peak_m, res.c_str());
    }
    // 坐标自检：往返、换锚（与单测同一套）
    {
        const PlanetShape shape;
        const GroupFrame a(home.lat, home.lon, shape);
        const std::vector<int> around = pd.nodes_within(home.lat, home.lon, 2000000.0);
        const int nb = around.size() > 1 ? around[1] : p_home; // 最近的另一个群
        const GroupFrame b(pd.node(nb).lat, pd.node(nb).lon, shape);
        double rt = 0.0;
        for (int k = 0; k < 200; k++) {
            const PlanetVec p(137.0 * k - 13000.0, 40.0 * (k % 50), 11000.0 - 101.0 * k);
            rt = std::max(rt, (a.to_flat(a.to_planet(p)) - p).length());
        }
        const PlanetVec cam = a.latlon_flat(0.5 * (home.lat + pd.node(nb).lat), 0.5 * (home.lon + pd.node(nb).lon)) + PlanetVec(0.0, 2000.0, 0.0);
        const CurveView va = CurveView::at(a, cam), vb = CurveView::at(b, rebase_point(a, b, cam));
        const PlanetBasis q = rebase_rotation(a, b, cam);
        double jump = 0.0;
        for (int k = 0; k < 50; k++) {
            const PlanetVec p = cam + PlanetVec(900.0 * std::cos(k * 0.4) * k, -300.0, 900.0 * std::sin(k * 0.4) * k);
            jump = std::max(jump, ((vb.visual(b, rebase_point(a, b, p)) - vb.cam) - q.apply(va.visual(a, p) - cam)).length());
        }
        const bool ok = rt < 1e-3 && jump < 0.02;
        fails += ok ? 0 : 1;
        std::printf("坐标自检：往返 %.4f mm、与 %d 之间换锚画面差 %.3f mm %s\n", rt * 1000.0, nb, jump * 1000.0, ok ? "" : "✗");
    }
    // 每群粗版解开，对陆地
    {
        const auto t1 = std::chrono::steady_clock::now();
        int missing = 0, off = 0;
        double worst = 0.0;
        int worst_node = -1;
        for (int k = 0; k < pd.node_count(); k++) {
            const PlanetLod l = pd.lod(k);
            if (l.empty()) {
                missing++;
                continue;
            }
            const double e = std::abs(l.land_km2() - pd.node(k).area_km2) / std::max(1.0, pd.node(k).area_km2);
            off += e > 0.1 ? 1 : 0;
            if (e > worst) {
                worst = e;
                worst_node = k;
            }
        }
        std::printf("粗版：%d 群全部解开 %.0f ms；没有粗版的 %d 群；陆地与生成器差一成以上的 %d 群（最大 %.0f%%，#%d）\n", pd.node_count(), since_ms(t1), missing, off,
                worst * 100.0, worst_node);
        fails += missing > 0 ? 1 : 0;
    }
    if (p_overlap) {
        // 邻群陆地：把每群粗版的陆地格（占比 ≥ 一半）放上球面，查不同群的格子相距多近（格距 2 km，格心相距 < 2 km 算「叠上」、< 4 km 算「挨着」）
        const auto t2 = std::chrono::steady_clock::now();
        struct Cell {
            PlanetVec u;
            int node;
        };
        const double R = pd.radius_km * 1000.0;
        const double bucket_m = 4000.0;
        std::map<std::tuple<int, int, int>, std::vector<Cell>> hash;
        auto key = [&](const PlanetVec &p_u) {
            const PlanetVec p = p_u * (R / bucket_m);
            return std::make_tuple(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)));
        };
        size_t cells = 0;
        for (int k = 0; k < pd.node_count(); k++) {
            const PlanetLod l = pd.lod(k);
            const GroupFrame f(pd.node(k).lat, pd.node(k).lon, PlanetShape());
            for (int r = 0; r < l.rows; r++) {
                for (int c = 0; c < l.cols; c++) {
                    if (l.land[l.index(r, c)] < 128) {
                        continue;
                    }
                    double x = 0.0, z = 0.0;
                    l.cell_center(r, c, x, z);
                    const PlanetVec u = f.to_planet(PlanetVec(x, 0.0, z)).normalized();
                    hash[key(u)].push_back({ u, k });
                    cells++;
                }
            }
        }
        std::map<std::pair<int, int>, double> pairs; // (小, 大) → 最近格心距（米）
        for (const auto &kv : hash) {
            const auto [bx, by, bz] = kv.first;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dz = -1; dz <= 1; dz++) {
                        auto it = hash.find(std::make_tuple(bx + dx, by + dy, bz + dz));
                        if (it == hash.end()) {
                            continue;
                        }
                        for (const Cell &a : kv.second) {
                            for (const Cell &b : it->second) {
                                if (a.node >= b.node) {
                                    continue;
                                }
                                const double d = (a.u - b.u).length() * R;
                                if (d < 4000.0) {
                                    auto &best = pairs.try_emplace({ a.node, b.node }, 1e18).first->second;
                                    best = std::min(best, d);
                                }
                            }
                        }
                    }
                }
            }
        }
        int overlap = 0, touch = 0;
        for (const auto &kv : pairs) {
            (kv.second < 2000.0 ? overlap : touch)++;
        }
        std::printf("邻群陆地（2 km 粗版、%zu 个陆地格，%.0f ms）：格心相距 < 2 km（叠上）%d 对、2–4 km（挨着）%d 对\n", cells, since_ms(t2), overlap, touch);
        int shown = 0;
        for (const auto &kv : pairs) {
            if (kv.second < 2000.0 && shown++ < 8) {
                std::printf("   叠上：%d–%d 相距 %.1f km（群心 %.0f km）\n", kv.first.first, kv.first.second, kv.second / 1000.0,
                        pd.node_distance_m(kv.first.first, kv.first.second) / 1000.0);
            }
        }
    }
    std::printf("行星体检 %.0f ms，%s\n", since_ms(t0), fails ? "有不达标" : "全部达标");
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- 行星天气场体检（--planet-weather，行星计划 P5）
// 逐日天气包读一遍、有细版的群与 weather.tsv 对照（类型逐日相同的比例 ≥ 95%）；
// 再按 p_grid 度的格、一天 p_steps 个时刻取一年的「大势」（不含云团：云团按门槛取，面积份额就是云量），
// 按纬度带与全行星（按面积加权）报年均云量、年降水，与 ④ 的气候场对账：
//   降水对 ④ 的相对降水按生成器的式子折成的年降水（hydro.precip_mm）；云量对「气候云量」（④ 的降水、风暴按生成器逐日天气的统计折成的，
//   永暴带、G 圈里是风暴——就是离群远了行星天气场用的那一份）。差出两成就不达标。
int survey_planet_weather(const std::string &p_dir, double p_grid, int p_steps, const std::string &p_worlds) {
    const auto t0 = std::chrono::steady_clock::now();
    auto pd = std::make_shared<PlanetData>();
    std::string error;
    if (!pd->load_files(p_dir, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }
    std::string bytes;
    if (!read_file(p_dir + "/weather.bin", bytes)) {
        std::fprintf(stderr, "读不了 %s/weather.bin\n", p_dir.c_str());
        return 2;
    }
    WeatherPack pack;
    const auto t_parse = std::chrono::steady_clock::now();
    if (!pack.parse(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), error)) {
        std::fprintf(stderr, "weather.bin：%s\n", error.c_str());
        return 2;
    }
    const double parse_ms = since_ms(t_parse);
    auto file_mb = [&](const char *p_name) {
        std::ifstream in(p_dir + "/" + p_name, std::ios::binary | std::ios::ate);
        return in ? double(in.tellg()) / 1e6 : 0.0;
    };
    int fails = 0;
    auto pw = std::make_shared<PlanetWeather>(pd);
    pw->add_pack(pack);
    const double pack_mb = file_mb("planet.bin") + file_mb("lods.bin") + file_mb("weather.bin");
    std::printf("逐日天气包：%zu 群 × %d 天、%.1f MB，解开 %.0f ms；行星包合计 %.1f MB（计划 ≤ 60 MB）\n", pack.entries.size(), pack.year_days, file_mb("weather.bin"), parse_ms,
            pack_mb);
    // 粗细两版：有细版的群
    int fine_groups = 0, same_kind = 0, same_all = 0, days_total = 0;
    for (int k = 0; k < int(pack.entries.size()); k++) {
        const int node = pack.entries[size_t(k)].node;
        std::string text;
        if (!read_file(p_worlds + "/" + std::to_string(node) + "/weather.tsv", text)) {
            continue;
        }
        Climate fine;
        if (!parse_climate(text, fine, error)) {
            continue;
        }
        const Climate coarse = pack.climate(k, pd->node(node).lat, pd->node(node).lon, pd->tilt_deg);
        int sk = 0, sa = 0;
        const size_t n = std::min(fine.days.size(), coarse.days.size());
        for (size_t d = 0; d < n; d++) {
            const ClimateDay &a = fine.days[d], &b = coarse.days[d];
            sk += a.kind == b.kind ? 1 : 0;
            sa += a.kind == b.kind && a.precip_mm == b.precip_mm && a.temp_c == b.temp_c && a.wind_from_deg == b.wind_from_deg && a.wind_ms == b.wind_ms &&
                            a.sailable == b.sailable && a.storm == b.storm
                    ? 1
                    : 0;
        }
        std::printf("  细版 %d：天气类型逐日相同 %d / %zu，整行逐位相同 %d\n", node, sk, n, sa);
        fine_groups++;
        same_kind += sk;
        same_all += sa;
        days_total += int(n);
    }
    if (fine_groups > 0) {
        const double ratio = double(same_kind) / std::max(1, days_total);
        std::printf("粗细两版：%d 群，天气类型逐日相同 %.1f%%（计划 ≥ 95%%），整行逐位相同 %.1f%% %s\n", fine_groups, 100.0 * ratio, 100.0 * same_all / std::max(1, days_total),
                ratio >= 0.95 ? "" : "✗");
        fails += ratio >= 0.95 ? 0 : 1;
    }
    // 一年
    const auto t_year = std::chrono::steady_clock::now();
    struct Band {
        double w = 0.0, cloud = 0.0, cloud_ref = 0.0, mm = 0.0, mm_ref = 0.0, share = 0.0, storm = 0.0;
    };
    const int n_bands = 12; // 每 15°
    std::vector<Band> bands(size_t(n_bands) + 1); // 最后一个 = 全行星
    const int rows = int(std::round(180.0 / p_grid)), cols = int(std::round(360.0 / p_grid));
    const int year = pack.year_days;
    long long samples = 0;
    for (int r = 0; r < rows; r++) {
        const double lat = -90.0 + (r + 0.5) * p_grid;
        const double area = std::cos(lat * SURVEY_PI / 180.0);
        const int bi = std::clamp(int((lat + 90.0) / 15.0), 0, n_bands - 1);
        for (int c = 0; c < cols; c++) {
            const double lon = -180.0 + (c + 0.5) * p_grid;
            double cloud = 0.0, cloud_ref = 0.0, mm = 0.0, share = 0.0, storm = 0.0;
            for (int d = 0; d < year; d++) {
                for (int s = 0; s < p_steps; s++) {
                    const double day = d + (s + 0.5) / p_steps;
                    const PlanetRegional w = pw->sample(day, lat, lon, 0.0, false);
                    cloud += w.state.cloud;
                    mm += w.state.precip;
                    share += w.group_share;
                    storm += w.state.storm;
                    samples++;
                }
                cloud_ref += pw->climate_state(d + 0.5, lat, lon, 0.0).cloud;
            }
            const double n = double(year) * p_steps;
            for (Band *b : { &bands[size_t(bi)], &bands.back() }) {
                b->w += area;
                b->cloud += area * cloud / n;
                b->cloud_ref += area * cloud_ref / year;
                b->mm += area * mm / n * year; // 年降水（mm）：每天的量取一年的平均再乘天数
                b->mm_ref += area * pw->climate_precip_mm(lat, lon);
                b->share += area * share / n;
                b->storm += area * storm / n;
            }
        }
    }
    const double year_ms = since_ms(t_year);
    std::printf("一年（%g° 的格、一天 %d 个时刻，%lld 次取样，%.0f ms、每次 %.2f µs）：\n", p_grid, p_steps, samples, year_ms, year_ms * 1000.0 / std::max(1LL, samples));
    std::printf("  纬度带        面积份   群管的份   年均云量 / 气候云量      年降水 mm / ④ 的         风暴\n");
    for (int k = 0; k <= n_bands; k++) {
        const Band &b = bands[size_t(k)];
        if (b.w <= 0.0) {
            continue;
        }
        const double cr = b.cloud / std::max(1e-9, b.cloud_ref), mr = b.mm / std::max(1e-9, b.mm_ref);
        const double area_share = b.w / bands.back().w;
        // 两极各一小块（面积不到 2%）只报不判：那里群少、按背景
        const bool judged = k == n_bands || area_share >= 0.02;
        const bool ok = !judged || (std::abs(cr - 1.0) <= 0.2 && std::abs(mr - 1.0) <= 0.2);
        fails += ok ? 0 : 1;
        if (k == n_bands) {
            std::printf("  全行星      %5.1f%%   %6.1f%%   %.3f / %.3f（%+5.1f%%）  %6.0f / %6.0f（%+5.1f%%）  %.3f %s\n", 100.0, 100.0 * b.share / b.w, b.cloud / b.w, b.cloud_ref / b.w,
                    100.0 * (cr - 1.0), b.mm / b.w, b.mm_ref / b.w, 100.0 * (mr - 1.0), b.storm / b.w, ok ? "" : "✗");
        } else {
            std::printf("  %+4d…%+4d°  %5.1f%%   %6.1f%%   %.3f / %.3f（%+5.1f%%）  %6.0f / %6.0f（%+5.1f%%）  %.3f %s\n", -90 + 15 * k, -75 + 15 * k, 100.0 * area_share,
                    100.0 * b.share / b.w, b.cloud / b.w, b.cloud_ref / b.w, 100.0 * (cr - 1.0), b.mm / b.w, b.mm_ref / b.w, 100.0 * (mr - 1.0), b.storm / b.w,
                    ok ? "" : (judged ? "✗" : ""));
        }
    }
    std::printf("行星天气场体检 %.0f ms，%s\n", since_ms(t0), fails ? "有不达标" : "全部达标（±20%）");
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- 本机生成过的岛群的岛底（--island-floor，浮高计划 F3）
// 逐座查 <目录>/<群>/sites.tsv 的岛（island 行：岸缘、崖高、龙骨、最深岸距）：夹过龙骨以后岛底连岩棱都不低于云带顶之上 1000 m。
// 结果随本机生成过哪些群而变，所以不进 ctest（ctest 的 island_floor 只查仓库里的包与进程内现场生成的几个大群）。
int survey_island_floor(const std::filesystem::path &p_groups) {
    std::error_code ec;
    if (!std::filesystem::is_directory(p_groups, ec)) {
        std::fprintf(stderr, "没有这个目录：%s\n", p_groups.u8string().c_str());
        return 2;
    }
    const double floor_y = VerticalDatum::ISLAND_BOTTOM_MIN_Y;
    int n_groups = 0, n_islands = 0, n_clamped = 0, bad = 0;
    for (const auto &e : std::filesystem::directory_iterator(p_groups, ec)) {
        std::ifstream in(e.path() / "sites.tsv", std::ios::binary);
        if (!in) {
            continue;
        }
        const std::string tsv((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        WorldSites sites;
        std::string err;
        const std::string name = e.path().filename().u8string();
        if (!parse_world_sites(tsv, sites, err)) {
            std::printf("  群 %s：sites.tsv 解析不了（%s）\n", name.c_str(), err.c_str());
            bad++;
            continue;
        }
        double low_before = 0.0, low_after = 0.0;
        int clamped = 0;
        for (const WorldIsland &isl : sites.terrain_islands()) {
            const IslandBody b = island_body(isl);
            const double full = 25.0 * std::sqrt(std::max(1.0, isl.max_shore_m)); // 同 world_terrain.cpp 的 KEEL_K · R^KEEL_EXP
            low_before = std::min(low_before, isl.rim_m - b.wall_m - full);
            low_after = std::min(low_after, island_lowest(isl, b));
            clamped += b.keel_m < full - 1e-9 ? 1 : 0;
            n_islands++;
        }
        n_groups++;
        n_clamped += clamped;
        const bool ok = low_after >= floor_y - 1e-6;
        bad += ok ? 0 : 1;
        std::printf("  群 %s：%zu 座岛，夹前岛心底最深 %.0f m → 连岩棱最深 %.0f m（收短 %d 座）%s\n", name.c_str(), sites.islands.size(), low_before, low_after, clamped,
                ok ? "" : " ✗");
    }
    std::printf("%s：%d 群、%d 座岛，收短龙骨的 %d 座；岛底都在 %.0f m 之上：%s\n", p_groups.u8string().c_str(), n_groups, n_islands, n_clamped, floor_y,
            bad ? "否" : "是");
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "--island-floor") {
        std::filesystem::path groups;
        if (argc >= 3) {
            groups = std::filesystem::path(argv[2]);
        } else {
#ifdef _WIN32
            // 缺省：游戏存档目录里剧情世界生成过的群（%APPDATA%\Godot\app_userdata\州渚\worlds\seed42\groups）
            wchar_t *appdata = nullptr;
            size_t len = 0;
            if (_wdupenv_s(&appdata, &len, L"APPDATA") == 0 && appdata) {
                groups = std::filesystem::path(appdata) / L"Godot" / L"app_userdata" / L"州渚" / L"worlds" / L"seed42" / L"groups";
                free(appdata);
            }
#endif
        }
        return survey_island_floor(groups);
    }
    if (argc >= 3 && std::string(argv[1]) == "--planet-weather") {
        double grid = 5.0;
        int steps = 2;
        std::string worlds = std::string(argv[2]) + "/../../worlds";
        for (int k = 3; k < argc; k++) {
            const std::string opt = argv[k];
            if (opt == "--grid" && k + 1 < argc) {
                grid = std::atof(argv[++k]);
            } else if (opt == "--steps" && k + 1 < argc) {
                steps = std::max(1, std::atoi(argv[++k]));
            } else if (opt == "--worlds" && k + 1 < argc) {
                worlds = argv[++k];
            }
        }
        return survey_planet_weather(argv[2], grid, steps, worlds);
    }
    if (argc >= 3 && std::string(argv[1]) == "--planet") {
        int home = 2051;
        bool overlap = false;
        for (int k = 3; k < argc; k++) {
            const std::string opt = argv[k];
            if (opt == "--home" && k + 1 < argc) {
                home = std::atoi(argv[++k]);
            } else if (opt == "--overlap") {
                overlap = true;
            }
        }
        return survey_planet(argv[2], home, overlap);
    }
    if (argc < 2) {
        std::fprintf(stderr, "用法：zhouzhu_site_survey <岛群目录> [--seed N] [--report 明细.tsv] [--max-missing 0.02]\n");
        return 2;
    }
    const std::string dir = argv[1];
    uint64_t seed = 2051;
    std::string report_path;
    double max_missing = 0.02;
    double sim_hours = 0.0;
    std::string rivers_path;
    int scatter_stride = 6;
    int calendar_start = 3;
    for (int k = 2; k + 1 < argc; k += 2) {
        const std::string opt = argv[k];
        if (opt == "--seed") {
            seed = std::strtoull(argv[k + 1], nullptr, 10);
        } else if (opt == "--report") {
            report_path = argv[k + 1];
        } else if (opt == "--max-missing") {
            max_missing = std::atof(argv[k + 1]);
        } else if (opt == "--rivers") {
            rivers_path = argv[k + 1];
        } else if (opt == "--sim") {
            sim_hours = std::atof(argv[k + 1]);
        } else if (opt == "--scatter-stride") {
            scatter_stride = std::atoi(argv[k + 1]);
        } else if (opt == "--calendar") {
            calendar_start = std::atoi(argv[k + 1]);
        }
    }

    std::string bin, tsv, error;
    if (!read_file(dir + "/terrain.bin", bin) || !read_file(dir + "/sites.tsv", tsv)) {
        std::fprintf(stderr, "读不到 %s/terrain.bin 或 sites.tsv\n", dir.c_str());
        return 2;
    }
    WorldGrid grid;
    WorldSites sites;
    if (!parse_world_grid(reinterpret_cast<const uint8_t *>(bin.data()), bin.size(), grid, error) || !parse_world_sites(tsv, sites, error)) {
        std::fprintf(stderr, "资产坏了：%s\n", error.c_str());
        return 2;
    }
    // 河：年降水与一年几天取 weather.tsv（与 World 一致）
    RiverRules river_rules;
    RiverRegime regime = RiverRegime::steady();
    std::string weather;
    if (read_file(dir + "/weather.tsv", weather)) {
        if (RiverRegime::parse(weather, regime, error)) {
            river_rules.precip_mm = regime.precip_mm();
            river_rules.year_days = regime.year_days();
        } else {
            std::fprintf(stderr, "weather.tsv 坏了：%s\n", error.c_str());
            return 2;
        }
    }
    // 天气模型：与 World 同一份（海拔分箱从最低的地到最高的峰）
    Climate climate;
    WeatherModel wx;
    if (!weather.empty() && parse_climate(weather, climate, error)) {
        double low = 1e18, high = -1e18;
        for (size_t k = 0; k < grid.height.size(); k++) {
            if (grid.shore[k] > 0.0f) {
                low = std::min(low, double(grid.height[k]));
                high = std::max(high, double(grid.height[k]));
            }
        }
        wx = WeatherModel(climate, low, high);
    }
    auto river_started = std::chrono::steady_clock::now();
    WorldTerrain terrain(grid, sites.terrain_islands(), seed, river_rules);
    const double river_ms = since_ms(river_started);
    if (terrain.has_rivers()) {
        const WorldRivers &rivers = terrain.get_rivers();
        int count = 0, mouths = 0;
        double widest = 0.0, deepest = 0.0;
        for (const RiverStem &stem : rivers.get_stems()) {
            if (stem.v.empty()) {
                continue;
            }
            count++;
            mouths += stem.mouth ? 1 : 0;
            for (const RiverVertex &v : stem.v) {
                widest = std::max(widest, v.width);
                deepest = std::max(deepest, v.depth);
            }
        }
        double lo = 1e9, hi = 0.0;
        for (double r : regime.get_daily()) {
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        std::printf("河网：%d 条（%d 条出岸缘），共 %.0f km，平岸最宽 %.1f m、最深 %.2f m；径流指数一年里 %.2f..%.2f（%.0f ms）\n",
                count, mouths, rivers.total_length() / 1000.0, widest, deepest, lo, hi, river_ms);
        if (!rivers_path.empty()) {
            if (std::FILE *out = std::fopen(rivers_path.c_str(), "wb")) {
                std::fprintf(out, "stem\tparent\tmouth\tx\tz\ts\tsurface\twidth\tdepth\tfloodplain\tacc_km2\tdry\n");
                for (size_t k = 0; k < rivers.get_stems().size(); k++) {
                    const RiverStem &stem = rivers.get_stems()[k];
                    for (const RiverVertex &v : stem.v) {
                        std::fprintf(out, "%zu\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.2f\t%.2f\t%.2f\t%.1f\t%.2f\t%.3f\n", k, stem.parent, stem.mouth ? 1 : 0,
                                v.x, v.z, v.s, v.surface, v.width, v.depth, v.floodplain, v.acc, v.dry);
                    }
                }
                std::fclose(out);
            }
        }
    }

    // 码头
    auto started = std::chrono::steady_clock::now();
    const WorldWorks works = lay_world_works(terrain, sites);
    const std::vector<DockPlan> &docks = works.docks;
    const double dock_ms = since_ms(started);
    int dock_failed = 0;
    double pier_max = 0.0, pier_sum = 0.0, shift_max = 0.0;
    for (size_t k = 0; k < docks.size(); k++) {
        if (!docks[k].ok) {
            dock_failed++;
            std::printf("  码头 #%d：找不到岸缘\n", sites.docks[k].id);
            continue;
        }
        pier_max = std::max(pier_max, docks[k].pier_m);
        pier_sum += docks[k].pier_m;
        shift_max = std::max(shift_max, std::hypot(docks[k].x - sites.docks[k].x, docks[k].z - sites.docks[k].z));
    }
    const int dock_ok = int(docks.size()) - dock_failed;
    std::printf("码头 %zu 个：%d 个铺好、%d 个找不到岸缘；栈桥平均 %.1f m、最长 %.1f m；岸缘点离生成器的点最远 %.0f m（%.0f ms）\n",
            docks.size(), dock_ok, dock_failed, dock_ok ? pier_sum / dock_ok : 0.0, pier_max, shift_max, dock_ms);
    // 船坞坪：主码头旁各一块
    int yard_failed = 0;
    for (size_t k = 0; k < docks.size(); k++) {
        if (!sites.docks[k].main) {
            continue;
        }
        const YardPlan &yard = works.yards[k];
        if (!yard.present) {
            yard_failed++;
            std::printf("  主码头 #%d：旁边凿不出船坞坪\n", sites.docks[k].id);
            continue;
        }
        std::printf("船坞坪：主码头 #%d 旁，中心离岸缘点 %.0f m，坪高 %.1f m\n", sites.docks[k].id, std::hypot(yard.x - docks[k].x, yard.z - docks[k].z), yard.y);
    }

    // 村落
    const WorldVillageRules rules;
    std::FILE *report = report_path.empty() ? nullptr : std::fopen(report_path.c_str(), "wb");
    if (report) {
        std::fprintf(report, "kind\tid\tisland\thouseholds\twanted\tplaced\tmissing\tradius_m\tms\tname\n");
        for (size_t k = 0; k < docks.size(); k++) {
            std::fprintf(report, "dock\t%d\t%d\t0\t1\t%d\t%d\t%.1f\t0\t\n", sites.docks[k].id, sites.docks[k].island, docks[k].ok ? 1 : 0, docks[k].ok ? 0 : 1, docks[k].pier_m);
        }
    }
    int wanted = 0, missing = 0, short_sites = 0;
    double total_ms = 0.0, worst_ms = 0.0;
    std::string worst_name;
    struct Short {
        const SiteSettlement *site;
        int missing, wanted;
    };
    std::vector<Short> shorts;
    int village_count = 0;
    std::vector<const SiteSettlement *> no_berth; // 村里没摆下船台的
    for (const SiteSettlement &s : sites.settlements) {
        started = std::chrono::steady_clock::now();
        const VillagePlan plan = plan_world_village(terrain, s, seed, rules);
        const double ms = since_ms(started);
        total_ms += ms;
        if (ms > worst_ms) {
            worst_ms = ms;
            worst_name = s.name;
        }
        wanted += plan.wanted;
        missing += plan.missing;
        if (!s.hamlet) {
            village_count++;
            if (freight_post_building(plan.layout) < 0) {
                no_berth.push_back(&s);
            }
        }
        if (plan.missing > 0) {
            short_sites++;
            shorts.push_back({ &s, plan.missing, plan.wanted });
        }
        if (report) {
            std::fprintf(report, "%s\t%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t%.1f\t%s\n", s.hamlet ? "hamlet" : "village", s.id, s.island, s.households, plan.wanted,
                    plan.wanted - plan.missing, plan.missing, plan.radius, ms, s.name.c_str());
        }
    }
    if (report) {
        std::fclose(report);
    }
    std::sort(shorts.begin(), shorts.end(), [](const Short &a, const Short &b) { return a.missing > b.missing; });
    for (size_t k = 0; k < std::min<size_t>(shorts.size(), 10); k++) {
        std::printf("  %s（岛 %d，%d 户）：%d 座摆不下\n", shorts[k].site->name.c_str(), shorts[k].site->island, shorts[k].site->households, shorts[k].missing);
    }
    const double missing_ratio = wanted ? double(missing) / wanted : 0.0;
    const double no_berth_ratio = village_count ? double(no_berth.size()) / village_count : 0.0;
    std::printf("船台：%d 个村里 %d 个盖上了", village_count, village_count - int(no_berth.size()));
    for (size_t k = 0; k < no_berth.size() && k < 10; k++) {
        std::printf("%s%s（岛 %d，%d 户）", k == 0 ? "；没摆下的：" : "、", no_berth[k]->name.c_str(), no_berth[k]->island, no_berth[k]->households);
    }
    std::printf("\n");
    std::printf("村落 %zu 处：要盖 %d 座，摆下 %d 座，摆不下 %d 座（%.2f%%，%d 处不全）；共 %.0f ms，最慢 %s %.0f ms\n",
            sites.settlements.size(), wanted, wanted - missing, missing, 100.0 * missing_ratio, short_sites, total_ms, worst_name.c_str(), worst_ms);

    // 资源：与 World 同一份规划（采场含每处采掘面够不够得着船、地标）与同一份散布
    bool resources_ok = true;
    std::string res_text;
    if (read_file(dir + "/resources.tsv", res_text)) {
        ResourceData data;
        if (!parse_resources(res_text, data, error)) {
            std::fprintf(stderr, "resources.tsv 坏了：%s\n", error.c_str());
            return 2;
        }
        started = std::chrono::steady_clock::now();
        const ResourcePlan plan = plan_resource_sites(terrain, sites, data);
        const double plan_ms = since_ms(started);
        std::map<std::string, int> by_kind, marks_by_kind;
        for (const ResourceSite &s : plan.sites) {
            by_kind[s.kind->site]++;
        }
        for (const ResourceLandmark &m : plan.landmarks) {
            marks_by_kind[m.kind->site]++;
        }
        int patches = 0;
        for (const ResourceDeposit &d : data.deposits) {
            patches += d.form == DepositForm::PATCH ? 1 : 0;
        }
        const int workings = int(data.workings.size());
        std::printf("资源：片 %d、赋存区 %zu、点 %zu（摆了样子 %zu、岛底的洞 %d、落不了 %zu）；采场 %d：落地 %zu、并掉 %d、在村里 %d、落不了 %zu（%.0f ms）\n  ", patches,
                data.occurrences.size(), data.deposits.size() - size_t(patches), plan.landmarks.size(), plan.underside, plan.landmarks_failed.size(), workings,
                plan.sites.size(), plan.merged, plan.in_village, plan.failed.size(), plan_ms);
        for (const auto &entry : by_kind) {
            std::printf("%s %d  ", entry.first.c_str(), entry.second);
        }
        std::printf("｜地标 ");
        for (const auto &entry : marks_by_kind) {
            std::printf("%s %d  ", entry.first.c_str(), entry.second);
        }
        std::printf("\n");
        for (size_t k = 0; k < plan.failed.size() && k < 10; k++) {
            std::printf("  采场 %d：附近找不到站得住的地\n", plan.failed[k]);
        }
        // 野泊覆盖：规划时每处采掘面都量过 reach_m 内有没有开局的船落得下的地方（resource_reach，只看地面）；泊地上散布不长树
        int landable = 0;
        double reach_sum = 0.0;
        std::vector<const ResourceSite *> stranded;
        std::FILE *res_report = report_path.empty() ? nullptr : std::fopen(report_path.c_str(), "ab");
        for (const ResourceSite &s : plan.sites) {
            if (s.reach_m >= 0.0) {
                landable++;
                reach_sum += s.reach_m;
            } else {
                stranded.push_back(&s);
            }
            if (res_report) {
                std::fprintf(res_report, "working\t%d\t%d\t%d\t1\t1\t0\t%.0f\t0\t%s\n", s.id, s.island, s.cap, s.reach_m, s.name.c_str());
            }
        }
        if (res_report) {
            std::fclose(res_report);
        }
        const double landable_ratio = plan.sites.empty() ? 1.0 : double(landable) / double(plan.sites.size());
        std::printf("野泊：%zu 处采场里 %d 处（%.1f%%）在采掘面 %.0f m 内有落得下的地方，平均 %.0f m 远", plan.sites.size(), landable, 100.0 * landable_ratio,
                ResourceSiteRules().reach_m, landable ? reach_sum / landable : 0.0);
        for (size_t k = 0; k < stranded.size() && k < 8; k++) {
            std::printf("%s采场 %d %s（岛 %d）", k == 0 ? "；落不下的：" : "、", stranded[k]->id, stranded[k]->name.c_str(), stranded[k]->island);
        }
        std::printf("\n");
        const double failed_ratio = workings ? double(plan.failed.size()) / double(workings) : 0.0;
        resources_ok = failed_ratio <= max_missing && landable_ratio >= 1.0 - 2.0 * max_missing;

        // 散布：每隔 scatter_stride 块抽一块生成，按抽到的陆地块推全岛群
        const ResourceScatter scatter(terrain, sites, data, plan, seed);
        if (scatter_stride > 0 && scatter.valid()) {
            const ScatterRules &sr = scatter.get_rules();
            const ScatterChunkKey lo = scatter.chunk_at(grid.min_x(), grid.min_z()), hi = scatter.chunk_at(grid.max_x(), grid.max_z());
            std::vector<ScatterChunkKey> keys;
            for (int iz = lo.iz; iz <= hi.iz; iz += scatter_stride) {
                for (int ix = lo.ix; ix <= hi.ix; ix += scatter_stride) {
                    double cx = 0.0, cz = 0.0;
                    scatter.chunk_corner(ScatterChunkKey{ ix, iz }, cx, cz);
                    if (terrain.is_land(cx + 0.5 * sr.chunk_m, cz + 0.5 * sr.chunk_m)) {
                        keys.push_back(ScatterChunkKey{ ix, iz });
                    }
                }
            }
            std::vector<long long> count(size_t(deposits::SCATTER_COUNT), 0);
            double worst_ms = 0.0, gen_ms = 0.0;
            size_t most = 0;
            started = std::chrono::steady_clock::now();
            for (const ScatterChunkKey &key : keys) {
                const auto s0 = std::chrono::steady_clock::now();
                const std::vector<ScatterItem> items = scatter.chunk(key);
                const double ms = since_ms(s0);
                worst_ms = std::max(worst_ms, ms);
                most = std::max(most, items.size());
                for (const ScatterItem &item : items) {
                    count[item.look]++;
                }
            }
            gen_ms = since_ms(started);
            const double ha = double(keys.size()) * sr.chunk_m * sr.chunk_m / 10000.0;
            const double land_ha = double(grid.land_count()) * grid.res * grid.res / 10000.0;
            long long total = 0;
            for (long long c : count) {
                total += c;
            }
            std::printf("散布：抽 %zu 块（块心在陆地上，%.0f 公顷），共 %lld 株 / 块，每公顷 %.0f；每块平均 %.2f ms、最慢 %.2f ms、最多 %zu 株；全岛群约 %.1f 万株\n  ",
                    keys.size(), ha, total, ha > 0 ? total / ha : 0.0, keys.empty() ? 0.0 : gen_ms / keys.size(), worst_ms, most, ha > 0 ? total / ha * land_ha / 10000.0 : 0.0);
            for (int k = 0; k < deposits::SCATTER_COUNT; k++) {
                std::printf("%s %.1f/ha  ", deposits::SCATTER[k].name, ha > 0 ? count[size_t(k)] / ha : 0.0);
            }
            std::printf("\n");
            // 泊地上不长树（规划说够得着船，游戏里就落得下）
            int blocked_landings = 0;
            for (const ResourceSite &s : plan.sites) {
                std::vector<ScatterItem> solids;
                if (s.reach_m >= 0.0) {
                    scatter.solids_near(s.land_x, s.land_z, 14.0, solids);
                }
                blocked_landings += solids.empty() ? 0 : 1;
            }
            std::printf("  泊地 14 m 内有树的采场：%d 处\n", blocked_landings);
            resources_ok = resources_ok && blocked_landings == 0 && total > 0;
        }
    }

    // 天气：逐月摘要，再抽几处村子看局地一年
    bool weather_ok = true;
    if (wx.has_data()) {
        const Climate &c = wx.get_climate();
        const CalendarRules &cal = c.calendar;
        const auto weather_started = std::chrono::steady_clock::now();
        std::printf("天气：一年 %d 天，纬度 %.2f°、交角 %.0f°，台面 %.0f m、岸缘 %.0f m，年降水 %.0f mm；海拔分箱 %.0f..%.0f m\n", cal.year_days, cal.latitude_deg,
                cal.tilt_deg, c.ref_m, c.rim_m, c.precip_mm, wx.get_h_lo(), wx.get_h_hi());
        std::printf("  月   季      台面均温  雨量 雨日  雪线(最低)  河冰线   绿(台面) 叶(800m) 作物   日出  日落  出工  收工\n");
        for (int m = 0; m < cal.months(); m++) {
            const int d0 = m * cal.days_per_month, d1 = d0 + cal.days_per_month;
            double temp = 0.0, rain = 0.0, snow_line = 1e9, ice_line = 1e9;
            int rain_days = 0;
            for (int d = d0; d < d1; d++) {
                temp += c.days[size_t(d)].temp_c / cal.days_per_month;
                rain += c.days[size_t(d)].precip_mm;
                rain_days += c.days[size_t(d)].precip_mm > 0.1 ? 1 : 0;
                snow_line = std::min(snow_line, wx.snow_line(d + 0.5));
                ice_line = std::min(ice_line, wx.ice_line(d + 0.5, 20.0));
            }
            const double mid = 0.5 * (d0 + d1);
            const ElevationState e_ref = wx.elevation(mid, c.ref_m), e_800 = wx.elevation(mid, 800.0);
            const SunState sun = sun_at(mid, cal);
            double start = 0.0, end = 0.0;
            daylight_work_hours(mid, cal, DaylightWork(), start, end);
            auto line_text = [](double v) { return v > 1e8 ? std::string("   没有") : std::to_string(int(std::lround(v))) + " m"; };
            const CalendarDate date = calendar_date(mid, cal);
            std::printf("  %2d  %-8s %6.1f °C %5.0f %4d  %9s  %8s   %5.2f   %5.2f   %5.2f  %5.2f %5.2f %5.2f %5.2f\n", m + 1, wx.season_name(date.season).c_str(), temp, rain,
                    rain_days, line_text(snow_line).c_str(), line_text(ice_line).c_str(), e_ref.green, e_800.leaf, e_ref.crop, sun.sunrise_hour, sun.sunset_hour, start, end);
        }
        // 局地：邑治（村001）、最高的村、离邑治最远的村——一年里每两个钟头取一次
        std::vector<const SiteSettlement *> picks;
        const SiteSettlement *seat = sites.village_by_id(1), *highest = nullptr, *farthest = nullptr;
        double best_h = -1e18, best_d = -1.0;
        for (const SiteSettlement &s : sites.settlements) {
            double y = 0.0;
            if (s.hamlet || !terrain.surface(s.x, s.z, y)) {
                continue;
            }
            if (y > best_h) {
                best_h = y;
                highest = &s;
            }
            const double d = seat ? std::hypot(s.x - seat->x, s.z - seat->z) : 0.0;
            if (d > best_d) {
                best_d = d;
                farthest = &s;
            }
        }
        for (const SiteSettlement *s : { seat, highest, farthest }) {
            if (s) {
                picks.push_back(s);
            }
        }
        int samples = 0;
        const DayRules day;
        for (const SiteSettlement *s : picks) {
            double y = 0.0;
            terrain.surface(s->x, s->z, y);
            double mm = 0.0, snow_mm = 0.0;
            int storm_hours = 0, snowy_hours = 0;
            for (int step = 0; step < cal.year_days * 12; step++) {
                const WeatherSample ws = wx.sample(step / 12.0 + 1.0 / 24.0, s->x, s->z, y);
                mm += ws.precip_mm_h * 2.0;
                snow_mm += ws.precip_mm_h * ws.snow_frac * 2.0;
                storm_hours += ws.storm > 0.5 ? 2 : 0;
                snowy_hours += ws.precip_mm_h > 0.05 && ws.snow_frac > 0.5 ? 2 : 0;
                samples++;
            }
            const double oro = 1.0 + wx.get_rules().orographic_per_km * std::clamp((y - c.ref_m) / 1000.0, -0.3, 1.5);
            // 数据里的小雨常只有零点几毫米，画出来按「至少这么多」下：期望 = 每天 max(数据, 这种天气至少) 之和
            double expect = 0.0;
            for (const ClimateDay &d : c.days) {
                const WeatherKindDef &k = weather_kinds::at(d.kind);
                expect += k.coverage > 0.0 ? std::max(d.precip_mm, k.min_mm) : 0.0;
            }
            expect *= oro;
            const LocalSeason season(&wx, day, double(calendar_start), s->x, s->z, y);
            int shelter_ticks = 0;
            for (int tick = 0; tick < cal.year_days * 96; tick++) {
                shelter_ticks += season.sheltering(game_seconds(tick * 0.25, day)) ? 1 : 0;
            }
            const double ratio = mm / std::max(1.0, expect);
            weather_ok = weather_ok && ratio > 0.6 && ratio < 1.4;
            std::printf("  %s（%.0f m）：一年局地雨雪 %.0f mm（按天气类型应有 %.0f，%.0f%%；其中雪 %.0f mm、下雪 %d 个钟头），风暴 %d 个钟头，坏天气歇工 %.0f 个钟头\n",
                    s->name.c_str(), y, mm, expect, 100.0 * ratio, snow_mm, snowy_hours, storm_hours, shelter_ticks * 0.25);
        }
        std::printf("  天气取样 %d 次，%.0f ms（每次 %.1f µs）\n", samples, since_ms(weather_started), 1000.0 * since_ms(weather_started) / std::max(1, samples));
    }

    // 远处结算：与游戏里的远处账房（Hinterland）同一份开账与结算（world_ledger），全部村落从卯时前开账，每处半个钟头一结
    // 有天气时按时令结（开局是一年里的第 calendar_start 天）
    bool sim_ok = true;
    if (sim_hours > 0.0) {
        const DayRules day;
        const SimRules sim_rules;
        const WorldGround land_ground(terrain, 0.0, 0.0);
        const FarmModel land(&land_ground, land_ground.plot_lattice(), FarmRules());
        ClaimRegistry taken;
        std::vector<WorldLedger> ledgers;
        const double t0 = clock_at(0, 5.5, day);
        started = std::chrono::steady_clock::now();
        int people = 0, farmers = 0, carriers = 0;
        for (const SiteSettlement &s : sites.settlements) {
            WorldLedgerSpec spec;
            spec.site = s;
            spec.seed = seed;
            spec.lattice = land.get_lattice();
            spec.farm_rules = land.get_rules();
            spec.day = day;
            spec.now = t0;
            ledgers.push_back(open_world_ledger(terrain, spec, sim_rules));
            for (const PersonState &p : ledgers.back().state->people) {
                people++;
                farmers += p.mind.who.job == Job::FARMER ? 1 : 0;
                carriers += p.mind.who.job == Job::CARRIER ? 1 : 0;
            }
        }
        const double open_ms = since_ms(started);
        std::printf("远处开账 %zu 处：%d 人（农 %d、挑水 %d），%.0f ms（每处 %.2f ms）\n", ledgers.size(), people, farmers, carriers, open_ms, open_ms / ledgers.size());
        const double step = game_seconds(0.5, day), until = t0 + game_seconds(sim_hours, day);
        double worst_ms = 0.0, total_ms = 0.0;
        int settles = 0;
        std::string worst;
        for (double t = t0 + step; t <= until + 1e-6; t += step) {
            for (size_t k = 0; k < ledgers.size(); k++) {
                const auto s0 = std::chrono::steady_clock::now();
                const LocalSeason season(&wx, day, double(calendar_start), ledgers[k].x, ledgers[k].z, world_ledger_height(ledgers[k]));
                settle_world_ledger(ledgers[k], land, &taken, day, sim_rules, t, wx.has_data() ? &season : nullptr);
                const double ms = since_ms(s0);
                total_ms += ms;
                settles++;
                if (ms > worst_ms) {
                    worst_ms = ms;
                    worst = sites.settlements[k].name;
                }
            }
        }
        int harvested = 0, eaten = 0, hungry = 0, grain = 0, plots = 0, grain0 = 0;
        double water_in = 0.0;
        for (const WorldLedger &l : ledgers) {
            const VillageTally &t = l.state->tally;
            harvested += t.harvested;
            eaten += t.eaten;
            hungry += t.hungry;
            water_in += t.water_in;
            grain += village_grain(*l.state);
            grain0 += 12;
            plots += int(l.state->held.size());
            sim_ok = sim_ok && village_grain(*l.state) == 12 + t.harvested - t.sown - t.eaten;
        }
        const double game_s = game_seconds(sim_hours, day);
        std::printf("远处结算 %.1f 个钟头（%d 次半小时一结）：共 %.0f ms，每次平均 %.3f ms、最慢 %s %.2f ms；"
                    "合游戏里 %.0f s：每秒游戏时间花 %.2f ms（60 fps 下每帧约 %.3f ms）\n",
                sim_hours, settles, total_ms, total_ms / std::max(1, settles), worst.c_str(), worst_ms, game_s, total_ms / game_s, total_ms / game_s / 60.0);
        std::printf("  全岛群：收粮 %d、吃 %d（饿 %d 人次）、挑水 %.0f 担；垦了 %d 块地；仓里的粮 %d → %d；账%s\n", harvested, eaten, hungry, water_in, plots, grain0, grain,
                sim_ok ? "都对得上" : "有对不上的");
    }

    const bool pass = dock_failed == 0 && yard_failed == 0 && missing_ratio <= max_missing && no_berth_ratio <= max_missing && sim_ok && resources_ok && weather_ok;
    std::printf("%s\n", pass ? "达标" : "不达标");
    return pass ? 0 : 1;
}
