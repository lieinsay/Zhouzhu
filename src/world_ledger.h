#pragma once

#include <memory>
#include <string>
#include <vector>

#include "village_sim.h"
#include "world_ground.h"
#include "world_sites.h"

// 岛群里一处村落 / 散户的账：开账与远处结算。不含 Godot 头。
// 游戏里由远处账房（Hinterland 节点）持有、后台线程开账、分帧结算；岛群体检工具（site_survey --sim）离线把整个岛群开账、结一整天，量出花费。
// 两边调的是这同一份：开账 = 铺村（与 WorldVillage 同一个 plan_world_village）+ 叠布置模式的改动 + 找水、看附近有没有田 + found_village；
// 开账时的仓 = 粮种 + 这村的土产（freight::specialty_of，按户数）；
// 结算 = village_sim 的远处模式（没有导航格），地块记在账本里（HeldFields），勘察问岛群的农田（p_land）。

struct WorldLedgerSpec {
    SiteSettlement site;
    uint64_t seed = 2051; // SiteStream 的种子（铺村与起名都从它来）
    int per_house = 2, max_folk = 40;
    std::vector<std::string> edits; // 布置模式改动的文本行（village_<号>.txt / hamlet_<号>.txt）
    PlotLattice lattice; // 岛群农田的地块格（岛群坐标）
    FarmRules farm_rules;
    DayRules day;
    double now = 0.0; // 账从这一秒记起
};

struct WorldLedger {
    std::shared_ptr<VillageState> state;
    double x = 0.0, z = 0.0; // 村心（岛群坐标）
    SettlementLayout layout;
    std::vector<WaterLine> water;
    std::unique_ptr<WorldGround> ground; // 村心为原点
    PlotLattice plots; // 岛群农田的地块格，换到村子的坐标
    bool has_farm = false;
};

// 与 WorldVillage::host_seed、它给 Villagers 的种子同一算法：近处按新清单重招人时名字、随机流照旧
uint64_t world_village_seed(uint64_t p_stream_seed, const SiteSettlement &p_site);

// 开账（只读地形，可以在后台线程里做）
WorldLedger open_world_ledger(const WorldTerrain &p_terrain, const WorldLedgerSpec &p_spec, const SimRules &p_rules);

// 远处结算到 p_until。p_land 是岛群的农田（勘察、谁已经垦了），p_taken 是全图认领表（可以为 nullptr）；
// p_season 是这处的时令（local_season.h：按村心 site.x / site.z 与 world_ledger_height 造，与近处的 Villagers 同一处），可以为 nullptr
void settle_world_ledger(WorldLedger &r_ledger, const FarmModel &p_land, ClaimRegistry *p_taken, const DayRules &p_day, const SimRules &p_rules, double p_until,
        const SeasonView *p_season = nullptr);
// 村心的地面高（时令按它取气温）
double world_ledger_height(const WorldLedger &p_ledger);
