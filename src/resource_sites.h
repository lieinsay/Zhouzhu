#pragma once

#include <string>
#include <vector>

#include "deposit_kinds.h"
#include "ship_layout.h"
#include "world_sites.h"
#include "world_terrain.h"

// 资源的落地：生成器的点、片、赋存区、采场（resources.tsv，tools/export_skyisle.py 导出）在岛群里落成采场与地标，和采场的存量账。不含 Godot 头。
//
// 采场（生成器按村挑的采石场、土坑、采砂场、矿坑、淘金点、硫磺坑）= 采掘面（人站在这儿按住 E 采）+ 堆场（在采掘面旁几步远，打包装箱）。
// 船不停在这里：没有船台，玩家自己在附近找块平地野泊（wild_landing.h），走几十米过来——所以采掘面 reach_m 内得有开局的船落得下的地方；
// 规划把找到的那一处记成「泊地」，散布（resource_scatter.h）在泊地上不长树，所以规划说够得着、游戏里就落得下。
// 地标（泉眼、温泉、洞穴）：点旁找一处站得住的地摆个样子，采不了；岛底的洞（生成器的「底面洞」）人到不了，不摆。片（林子、苇荡、浮石露头）与散的品位场不在这里落：地上一株株的见 resource_scatter.h。
// 规划是确定性的纯函数（同一份资产每次铺得一样）：游戏加载时一次规划完（只有位置），走近了才摆实物；体检工具离线铺一遍查毛病。
//   · 离更早的一处（采场号小的）不到 merge_m 的并掉；
//   · 采掘面从采场格心起由近及远找：有地、离岸线（崖边）够远、不是水 / 河滩 / 码头的坪 / 船坞坪、坡缓、不在村子里（按估的半径让开），
//     旁边 pile_m 处还得有一块同样站得住的地放堆场；最近的那处够不着船（resource_reach）就接着往外找，试 reach_tries 处都不行才用最近的那处。
// 存量：每处采场有上限（按那一格的品位），采一份少一份，按全局钟线性回满（从空到满 refill_hours 个钟头）；账是一笔 {存量, 记到的钟点}，由地图持有。

// 点与片（deposit 行）
struct ResourceDeposit {
    int id = 0; // 生成器的号
    std::string kind; // 生成器的键（deposit_kinds.h）
    int island = 0;
    double x = 0.0, z = 0.0; // 代表格的格心（米）
    double area_km2 = 0.0;
    int grade = deposits::GRADE_MID;
    std::string subtype; // UTF-8，可以空
    DepositForm form = DepositForm::POINT;
};

// 散的赋存区（occurrence 行）：品位够的一片连通块
struct ResourceOccurrence {
    int id = 0;
    std::string kind;
    int island = 0;
    double x = 0.0, z = 0.0; // 品位峰值格
    double area_km2 = 0.0;
    int grade = deposits::GRADE_MID;
    std::string subtype;
    double length_km = 0.0;
    double axis_deg = 0.0; // 走向，自东逆时针（生成器的口径：x 东、y 北）
};

// 采场（working 行）
struct ResourceWorking {
    int id = 0;
    std::string kind;
    int island = 0;
    double x = 0.0, z = 0.0; // 采场格心
    int occurrence = -1; // 所在的赋存区号
    double grade = 0.5; // 那一格的品位（0–1）
    std::string subtype;
    std::vector<int> villages; // 用它的村（游戏的村号）
};

struct ResourceData {
    std::vector<ResourceDeposit> deposits;
    std::vector<ResourceOccurrence> occurrences;
    std::vector<ResourceWorking> workings;

    const ResourceDeposit *deposit_by_id(int p_id) const;
    const ResourceOccurrence *occurrence_by_id(int p_id) const;
};

// 读 resources.tsv；失败返回 false，r_error 说哪一行怎么了
bool parse_resources(const std::string &p_text, ResourceData &r_data, std::string &r_error);

struct ResourceSiteRules {
    double merge_m = 60.0; // 离更早的一处采场不到这么远：并掉（生成器同类 1.5 km 内已合用，剩下的是别类挨得近的）
    double village_margin_m = 25.0; // 村子（估的半径）外再让这么远（生成器的土坑、采砂场常就在村边）
    double search_m = 150.0; // 采掘面离采场格心最远挪多远（格心是 100 m 格的中心）
    double step_m = 6.0; // 找采掘面的步长
    double max_grade = 0.45; // 采掘面、堆场的坡度上限（前后左右 3 m 的高差 / 6 m）
    double min_shore_m = 12.0; // 离岸线（崖边）至少这么远
    double pile_m = 7.0; // 堆场离采掘面多远
    double dock_margin_m = 8.0; // 码头的坪、船坞坪外再让这么远
    double reach_m = 60.0; // 采掘面这么远内要有开局的船野泊得下的地方（人抱着箱子只走几十米）
    int reach_tries = 24; // 最近那处够不着船时，往外再试几处采掘面
    double reach_skip_m = 20.0; // 离试过、够不着的采掘面不到这么远的不再试
    double landmark_search_m = 80.0; // 地标离点最远挪多远
    double landmark_grade = 0.7; // 地标脚下的坡度上限（洞口常在坡上）
};

struct ResourceSite {
    int working = -1; // 在 ResourceData::workings 里的下标
    int id = 0; // 生成器的采场号
    const DepositKind *kind = nullptr;
    std::string subtype;
    std::string item; // 采出来的物品
    std::string name; // 采场的名字（UTF-8）
    int island = 0;
    int grade = deposits::GRADE_MID; // 所在赋存区的品位（上 / 中 / 下）
    double grade_value = 0.5; // 采场那一格的品位（0–1）
    int cap = 0; // 存量上限
    double face_x = 0.0, face_z = 0.0, face_y = 0.0; // 采掘面（地面上）
    double pile_x = 0.0, pile_z = 0.0, pile_y = 0.0; // 堆场
    double reach_m = -1.0; // 泊地在采掘面周围哪一圈（20、30……米）；−1 = reach_m 内落不下船
    double land_x = 0.0, land_z = 0.0, land_yaw = 0.0; // 泊地：开局的船野泊得下的一个位姿（reach_m ≥ 0 才有）
    std::vector<int> villages;
};

struct ResourceLandmark {
    int deposit = -1; // 在 ResourceData::deposits 里的下标
    int id = 0; // 生成器的号
    const DepositKind *kind = nullptr;
    std::string subtype;
    std::string name;
    int island = 0;
    double x = 0.0, y = 0.0, z = 0.0; // 摆样子的那一点（地面上）
};

struct ResourcePlan {
    std::vector<ResourceSite> sites; // 落了地的采场，按采场号
    std::vector<ResourceLandmark> landmarks; // 摆得出样子的地标，按号
    int merged = 0; // 离别处太近并掉的采场
    int in_village = 0; // 采场在村子里、附近又找不到村外的地
    std::vector<int> failed; // 附近找不到站得住的地的采场号
    std::vector<int> unreachable; // 落了地、但 reach_m 内落不下船的采场号
    std::vector<int> landmarks_failed; // 附近找不到站得住的地的地标号
    int underside = 0; // 岛底的洞：人到不了，不摆样子（地图上照标）
};

// p_terrain 应已 lay_world_works（码头的坪、船坞坪压好了）
ResourcePlan plan_resource_sites(const WorldTerrain &p_terrain, const WorldSites &p_sites, const ResourceData &p_data,
        const ResourceSiteRules &p_rules = ResourceSiteRules());
// 量够不够得着船用的那条船：开局的船（储存区 + 动力区 + 驾驶区）
const ShipLayout &resource_reach_ship();
// 泊地：采掘面 (p_x, p_z) 周围 20 m 起每 10 m 一圈、每圈 12 个方位、船头朝北或朝东，找第一个野泊得下的位姿（只看地面；村子估的半径外加 10 m 里不算）：
// 返回那一圈的半径（位姿写进 r_x / r_z / r_yaw），reach_m 内没有返回 −1。体检工具与规划同一份
double resource_reach(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules = ResourceSiteRules());
double resource_reach(const WorldTerrain &p_terrain, const WorldSites &p_sites, double p_x, double p_z, const ResourceSiteRules &p_rules, double &r_x, double &r_z,
        double &r_yaw);
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
