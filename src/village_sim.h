#pragma once

#include <cstdint>
#include <vector>

#include "day_clock.h"
#include "field_book.h"
#include "map_ground.h"
#include "village_state.h"
#include "villager_model.h"

// 村落的推进：把一本账（VillageState）从它记到的那一秒推进到某一秒。不含 Godot 头。
// 远近同一套：在屋里 / 走路 / 干活三个阶段，干完了落账（finish）、问 decide_task 下一件，作息的分界点一到就回家，午饭晚饭吃粮用水。
// 唯一的分叉是走路——近处（有导航格）A* 找路、沿路点一米一米地走；远处（没有导航格）不寻路，按直线距离 × 绕路系数估出要走几秒。
// 推进是按事件走的：两次事件之间一步跨过去，所以近处逐帧调（dt 很小）和远处半小时结一次（dt 很大）走的是同一段代码。
//
// 村外的东西（清单、地块格、水、导航格、田、全图认领表）由调用方借给它（VillageWorld），它只改账本和借来的田。

// 远近共用的数字
struct SimRules {
    WorkRules work;
    NavRules nav;
    double walk_speed = 1.3; // 开账时每人的步速 = 这个 × [0.85, 1.15]
    double walk_detour = 1.25; // 远处估走路：直线距离 × 这个（绕房子、绕坡）；由「逐帧 vs 结算」的单测校过
    int meal_grain = 1; // 每人每顿吃几份粮
    double meal_water = 0.25; // 每人每顿用几担水
    double vat_per_person = 1.0; // 水缸每口人一担
    double vat_min = 6.0;
    int seed_reserve_per_farmer = 2; // 吃饭时仓里给每个农夫留几份种子不动
};

// 推进时借来的村外的东西（都是本宿主坐标）
struct VillageWorld {
    const SettlementLayout *layout = nullptr;
    const PlotLattice *plots = nullptr; // 农田的地块格；没有农田为 nullptr
    const std::vector<WaterLine> *water = nullptr;
    const NavGrid *nav = nullptr; // 有 = 近处（沿路点走），没有 = 远处（估着走）
    const MapGround *ground = nullptr; // 远处判断站不站得住
    FieldBook *fields = nullptr; // 没有农田为 nullptr
    ClaimRegistry *taken = nullptr; // 全图认领表；可以为 nullptr
    const DayRules *day = nullptr;
    const SimRules *rules = nullptr;
};

// 开账时村里的仓（粟 8、菽 4）
Inventory village_starting_store();

// 开账：按清单招人（assign_residents，p_max_folk > 0 时封顶），每人站在自家门前、在屋里，出门错开几秒；
// 仓 = p_store，水缸半满；账从 p_now 记起。有导航格时门前站位吸到最近的干地
void found_village(VillageState &r_state, const VillageWorld &p_world, bool p_has_farm, bool p_has_water, int p_per_house, int p_max_folk, uint64_t p_seed,
        const Inventory &p_store, double p_now);

// 推进到 p_until（钟的秒）。p_until 不晚于账上的钟就什么都不做
void advance_village(VillageState &r_state, const VillageWorld &p_world, double p_until);

// 展开（远 → 近）：p_world 带着导航格。账本里拿着的地放回 p_farm（放不下的——已经有人垦了——认领作废），
// 站着的吸到最近的可走格，走着的从此刻的位置重新找路（找不到就原地歇一阵，回家的直接进门）。
// p_repath 为假时走着的先不找路、接着估着走（流式加载：展开那一帧不给几十个人一起找路），上层之后用 repath_walker 一个个找。
void deploy_village(VillageState &r_state, const VillageWorld &p_world, FarmModel *p_farm, bool p_repath = true);
// 估着走的第 p_index 个人从此刻站的地方按导航格重新找路；不是估着走的（或没有导航格）返回 false
bool repath_walker(VillageState &r_state, const VillageWorld &p_world, int p_index);
// 收回（近 → 远）：认领的地连同长势从 p_farm 里拿出来记进账本，走着的改成估着走（还剩的路长照记）
void collapse_village(VillageState &r_state, FarmModel *p_farm);

// 村子附近（离村心「外接圆半径 + field_radius_m」以内）有没有能垦的地：有才招农夫。近处远处开账都问它。
// p_land 是地图的农田（它自己的坐标），(p_to_land_x, p_to_land_z) 把宿主坐标换过去
bool village_has_farm(const FarmModel &p_land, double p_to_land_x, double p_to_land_z, const SettlementLayout &p_layout, const WorkRules &p_work);
// 村心与外接圆半径（房子位置的平均、最远一座离村心多远）
void village_extent(const SettlementLayout &p_layout, double &r_cx, double &r_cz, double &r_reach);
// 挑水去的水：村子 water_reach_m 以内的水中线（宿主坐标）
std::vector<WaterLine> village_water(const MapGround &p_ground, const SettlementLayout &p_layout, const WorkRules &p_work);
