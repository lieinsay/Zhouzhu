// 岛群体检：把一个岛群的全部码头与村落离线铺一遍，报出哪里铺不好。不链 Godot。
//
//   build/Debug/zhouzhu_site_survey.exe game/worlds/2051                  # 摘要
//   build/Debug/zhouzhu_site_survey.exe game/worlds/2051 --report out.tsv # 另写逐条明细（每个码头 / 村一行）
//   build/Release/zhouzhu_site_survey.exe game/worlds/2051 --sim 24      # 另把全部村落开账、在远处结 24 个游戏小时，报花费
//   build/Debug/zhouzhu_site_survey.exe game/worlds/2051 --rivers 河.tsv  # 另把河网逐点写出（河号、x、z、平岸水面、宽、深、汇流）
//   选项：--seed N（默认 2051，与 World 的 terrain_seed 一致）、--max-missing F（摆不下的房子占比上限，默认 0.02）、
//         --sim H（远处结算几个游戏小时；0 = 不结）
//
// 游戏里走近了才铺的东西，这里一次全铺：同一套纯算法（world_sites.h），所以这里铺得好，游戏里就铺得好。
// 退出码：0 = 全部达标；1 = 有码头找不到岸缘，或摆不下的房子超过上限；2 = 读不了资产。
// 换了岛群、改了生成器或铺村的规矩之后跑一遍，不用进游戏一个个去看。

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

double since_ms(const std::chrono::steady_clock::time_point &p_start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_start).count();
}

} // namespace

int main(int argc, char **argv) {
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
    std::printf("村落 %zu 处：要盖 %d 座，摆下 %d 座，摆不下 %d 座（%.2f%%，%d 处不全）；共 %.0f ms，最慢 %s %.0f ms\n",
            sites.settlements.size(), wanted, wanted - missing, missing, 100.0 * missing_ratio, short_sites, total_ms, worst_name.c_str(), worst_ms);

    // 远处结算：与游戏里的远处账房（Hinterland）同一份开账与结算（world_ledger），全部村落从卯时前开账，每处半个钟头一结
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
                settle_world_ledger(ledgers[k], land, &taken, day, sim_rules, t);
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

    const bool pass = dock_failed == 0 && yard_failed == 0 && missing_ratio <= max_missing && sim_ok;
    std::printf("%s\n", pass ? "达标" : "不达标");
    return pass ? 0 : 1;
}
