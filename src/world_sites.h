#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dock_site.h"
#include "village_plan.h"
#include "world_ground.h"
#include "world_terrain.h"

// 岛群的点位（码头、村、散户）与据此自动生成码头和村落的入口。不含 Godot 头，无状态。
//
// 点位从 sites.tsv 读（tools/export_skyisle.py 与 world.json 一起导出，格式见那个脚本开头）。
// 生成全是确定性的：同一份资产、同一个种子，每次、每台机器上铺出来的码头与村子都一样，
// 所以游戏里走近了才铺（远处不占内存），批量体检工具（tools/site_survey.cpp）也能离线把整个岛群铺一遍看有没有毛病。

struct SiteIsland {
    int id = 0;
    double rim_m = 0.0, cliff_m = 20.0, keel_m = 100.0, max_shore_m = 1000.0;
};

struct SiteDock {
    int id = 0;
    int island = 0;
    double x = 0.0, z = 0.0; // 生成器给的点（100 m 格上）
    bool main = false;
};

struct SiteSettlement {
    int id = 0;
    int island = 0;
    double x = 0.0, z = 0.0; // 村心
    int households = 1;
    bool seat = false; // 邑治
    bool hamlet = false; // 散户
    std::string name; // UTF-8
};

// 村子还没铺时估它多大（外接圆半径，米）：≈ 格距 × √户数 × 0.65。流式加载按它提前铺，资源点按它让开村子
double settlement_guess_radius(const SiteSettlement &p_site);

struct WorldSites {
    std::vector<SiteIsland> islands;
    std::vector<SiteDock> docks;
    std::vector<SiteSettlement> settlements; // 村在前、散户在后，各按 id

    const SiteDock *dock_by_id(int p_id) const;
    const SiteSettlement *village_by_id(int p_id) const; // 只找村（不含散户）
    std::vector<WorldIsland> terrain_islands() const;
};

// 失败返回 false，r_error 说哪一行怎么了
bool parse_world_sites(const std::string &p_text, WorldSites &r_sites, std::string &r_error);

// 所有码头（与 sites.docks 一一对应；找不到岸缘的 ok = false）。在原地形上量，所以在 set_pads 之前之后调都一样
std::vector<DockPlan> plan_world_docks(const WorldTerrain &p_terrain, const WorldSites &p_sites, const DockRules &p_rules);

// 岛群里的「工程」：码头（与 sites.docks 一一对应）与船坞坪（同样一一对应，只有主码头旁的 present）
struct WorldWorks {
    std::vector<DockPlan> docks;
    std::vector<YardPlan> yards;
};
// 规划全部码头与主码头旁的船坞坪，并把停机坪压进、船坞坪凿进地形。游戏、体检工具、单测都走这一个入口，建地面之前调
WorldWorks lay_world_works(WorldTerrain &r_terrain, const WorldSites &p_sites);

struct WorldVillageRules {
    SiteRules site;
    VillageRules village;
    WorldGroundRules ground;
};

// 一个村 / 散户：清单在村子的局部坐标里（原点 = 村心，世界坐标 = 村心 + 局部）。p_terrain 应已 lay_world_works
VillagePlan plan_world_village(const WorldTerrain &p_terrain, const SiteSettlement &p_site, uint64_t p_seed, const WorldVillageRules &p_rules);
