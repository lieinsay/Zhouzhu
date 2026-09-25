#pragma once

#include <string>
#include <vector>

#include "deposit_kinds.h"
#include "ship_layout.h"
#include "world_sites.h"
#include "world_terrain.h"

// 资源点：生成器的矿点（resources.tsv，tools/export_skyisle.py 导出）在岛群里落成一处处采集点，和它们的存量账。不含 Godot 头。
//
// 一处采集点 = 采掘面（人站在这儿按住 E 采）+ 堆场（在采掘面旁几步远，打包装箱）。
// 船不停在这里：没有船台，玩家自己在附近找块平地野泊（wild_landing.h），走几十米过来——所以采掘面 reach_m 内得有开局的船落得下的地方。
// 规划是确定性的纯函数（同一份资产每次铺得一样）：游戏加载时一次规划完（只有位置），走近了才摆实物；体检工具离线铺一遍查毛病。
//   · 采不了的（泉眼、温泉、洞穴）不落地，只在地图上标；
//   · 离更早的一处（矿点号小的）不到 merge_m 的并掉；
//   · 采掘面从矿点（100 m 格心）起由近及远找：有地、离岸线（崖边）够远、不是水 / 河滩 / 码头的坪 / 船坞坪、坡缓、不在村子里（按估的半径让开），
//     旁边 pile_m 处还得有一块同样站得住的地放堆场；最近的那处够不着船（resource_reach）就接着往外找，试 reach_tries 处都不行才用最近的那处。
// 存量：每处有上限（按品位与面积），采一份少一份，按全局钟线性回满（从空到满 refill_hours 个钟头）；账是一笔 {存量, 记到的钟点}，由地图持有。

struct ResourceDeposit {
    int id = 0; // 生成器的矿点号
    std::string kind; // 生成器的键（deposit_kinds.h）
    int island = 0;
    double x = 0.0, z = 0.0; // 矿点（格心，米）
    double area_km2 = 0.0;
    int grade = deposits::GRADE_MID;
    std::string subtype; // UTF-8，可以空
};

// 读 resources.tsv；失败返回 false，r_error 说哪一行怎么了
bool parse_resource_deposits(const std::string &p_text, std::vector<ResourceDeposit> &r_deposits, std::string &r_error);

struct ResourceSiteRules {
    double merge_m = 150.0; // 离更早的一处不到这么远：并掉
    double village_margin_m = 60.0; // 村子（估的半径）外再让这么远
    double search_m = 150.0; // 采掘面离矿点最远挪多远（矿点是 100 m 格心；斑块半径中位约 110 m，山里的小斑块常在陡坡上）
    double step_m = 6.0; // 找采掘面的步长
    double max_grade = 0.45; // 采掘面、堆场的坡度上限（前后左右 3 m 的高差 / 6 m）
    double min_shore_m = 12.0; // 离岸线（崖边）至少这么远
    double pile_m = 7.0; // 堆场离采掘面多远
    double dock_margin_m = 8.0; // 码头的坪、船坞坪外再让这么远
    double reach_m = 60.0; // 采掘面这么远内要有开局的船野泊得下的地方（人抱着箱子只走几十米）
    int reach_tries = 24; // 最近那处够不着船时，往外再试几处采掘面
    double reach_skip_m = 20.0; // 离试过、够不着的采掘面不到这么远的不再试
};

struct ResourceSite {
    int deposit = -1; // 在 deposits 里的下标
    int id = 0; // 生成器的矿点号
    const DepositKind *kind = nullptr;
    std::string subtype;
    std::string item; // 采出来的物品
    std::string name; // 采集点的名字（UTF-8）
    int island = 0;
    int grade = deposits::GRADE_MID;
    double area_km2 = 0.0;
    int cap = 0; // 存量上限
    double face_x = 0.0, face_z = 0.0, face_y = 0.0; // 采掘面（地面上）
    double pile_x = 0.0, pile_z = 0.0, pile_y = 0.0; // 堆场
    double reach_m = -1.0; // 最近的野泊处在采掘面周围哪一圈（20、30……米）；−1 = reach_m 内落不下船
};

struct ResourcePlan {
    std::vector<ResourceSite> sites; // 落了地的，按矿点号
    int landmarks = 0; // 采不了的（泉眼、温泉、洞穴）
    int merged = 0; // 离别处太近并掉的
    int in_village = 0; // 矿点在村子里、附近又找不到村外的地
    std::vector<int> failed; // 附近找不到站得住的地的矿点号
    std::vector<int> unreachable; // 落了地、但 reach_m 内落不下船的矿点号
};

// p_terrain 应已 lay_world_works（码头的坪、船坞坪压好了）
ResourcePlan plan_resource_sites(const WorldTerrain &p_terrain, const WorldSites &p_sites, const std::vector<ResourceDeposit> &p_deposits,
        const ResourceSiteRules &p_rules = ResourceSiteRules());
// 量够不够得着船用的那条船：开局的船（储存区 + 动力区 + 驾驶区）
const ShipLayout &resource_reach_ship();
// 采掘面 (p_x, p_z) 周围 20 m 起每 10 m 一圈、每圈 12 个方位、船头朝北或朝东，找第一个野泊得下的位姿（只看地面；村子估的半径外加 10 m 里不算）：
// 返回那一圈的半径，reach_m 内没有返回 −1。体检工具与规划同一份
double resource_reach(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules = ResourceSiteRules());
// 这处能不能当采掘面 / 堆场（规划用的同一条规矩；原因写进 r_why，给体检工具）
bool resource_spot_ok(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules, const char **r_why = nullptr);

// 存量账：amount < 0 表示从没采过（满的）
struct ResourceStock {
    double amount = -1.0;
    double hour = 0.0; // 记到的钟点（从第 0 天子夜起的游戏钟头）
};

// 此刻存多少份（带小数：回满是连续的）
double stock_amount(const ResourceStock &p_stock, int p_cap, double p_refill_hours, double p_now_hours);
// 取 p_count 份：不够就不动、返回 false
bool stock_take(ResourceStock &r_stock, int p_cap, double p_refill_hours, double p_now_hours, int p_count);
// 回到 p_amount 份还要几个钟头（已经够了为 0）
double stock_hours_until(const ResourceStock &p_stock, int p_cap, double p_refill_hours, double p_now_hours, double p_amount);
