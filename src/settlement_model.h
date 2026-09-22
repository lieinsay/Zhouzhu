#pragma once

#include <string>
#include <vector>

#include "buildings.h"
#include "ground_query.h"
#include "island_field.h"

// 一座岛上的聚落：一张建筑清单，加选址的规矩。不含 Godot 头。
//
// 建筑记在岛的局部坐标里（x 向东、z 向南、y 向上，与 IslandField 同）：中心 (x, z)、绕 y 轴的朝向 yaw（弧度，0 = 门朝 +z）。
// 建筑自己的坐标系：u 沿面阔（x）、v 沿进深（z）；转 yaw 后 u 轴指向 (cos yaw, −sin yaw)，v 轴指向 (sin yaw, cos yaw)——与 Godot 绕 y 轴的旋转一致。
//
// 选址（survey_site）用地面查询（SiteGround）勘察占地的九个点（四角、四边中点、中心）：都得有地面；不压禁区（岛上是河、停机坪、路、船坞坪）；
// 高差不超过 max_rise_m（台基把地坪架平，太陡就架不平）；与别的建筑之间留 gap_m 的空。
// 通过了就给出地坪高（占地内最高地面 + 台基高）与台基该埋到多深（最低地面再往下一点）；不通过给出中文原因，一次只说最要紧的一条。
//
// 与场景文件 / 脚本之间用 "kind@x,z,yaw°" 文本，一座一行（yaw 以度计，一位小数）。

struct Building {
    std::string kind; // BuildingDef::id
    double x = 0.0, z = 0.0;
    double yaw = 0.0; // 弧度
};

struct SiteRules {
    double max_rise_m = 2.5; // 占地内地面高差上限：再高台基就成了一堵墙
    double dock_margin_m = 3.0; // 停机坪外再让出这么多
    double trail_margin_m = 3.5; // 占地的哪个点离路中线这么近就算压着路（路半宽 2.5）
    double yard_margin_m = 2.0; // 船坞坪外再让出这么多
    double gap_m = 1.0; // 建筑之间至少空这么多
    double bury_m = 0.6; // 台基埋到最低地面之下多少
};

struct SiteReport {
    std::string reason; // 空 = 能盖
    double ground_min = 0.0, ground_max = 0.0; // 占地内的地面高
    double floor_y = 0.0; // 地坪（台基面）
    double plinth_bottom = 0.0; // 台基底
    bool ok() const { return reason.empty(); }
};

class SettlementLayout {
    std::vector<Building> items;

public:
    SettlementLayout() = default;
    explicit SettlementLayout(std::vector<Building> p_items) :
            items(std::move(p_items)) {}

    const std::vector<Building> &get_items() const { return items; }
    int size() const { return int(items.size()); }
    bool empty() const { return items.empty(); }
    const Building &at(int p_index) const { return items[size_t(p_index)]; }

    // 不检查合法性的直接改动（编辑函数在检查后调用）
    void add(const Building &p_building) { items.push_back(p_building); }
    void erase(int p_index) { items.erase(items.begin() + p_index); }

    bool operator==(const SettlementLayout &p_other) const;
    bool operator!=(const SettlementLayout &p_other) const { return !(*this == p_other); }
};

// 建筑自己的两根轴在岛坐标里的方向
void building_axes(double p_yaw, double &r_ux, double &r_uz, double &r_vx, double &r_vz);
// 占地四角（岛坐标，顺序：左前、右前、右后、左后；前 = 门那一面 +v），p_margin 往外扩
void footprint_corners(const BuildingDef &p_def, double p_x, double p_z, double p_yaw, double p_margin, double r_x[4], double r_z[4]);
// 点在不在占地里（p_margin 往外扩）
bool footprint_contains(const BuildingDef &p_def, const Building &p_building, double p_px, double p_pz, double p_margin = 0.0);
// 两座建筑的占地（各往外扩 p_gap / 2）有没有压着
bool footprints_overlap(const Building &p_a, const Building &p_b, double p_gap);

// 一座游戏尺度的岛当作勘察的地面：高 = 高度场双线性；禁区依次是河（最要紧）、停机坪、船坞坪、路。
// p_dock_apron_m 是停机坪半径（IslandParams::dock_apron_m）。只持引用，须比 field 活得短
class IslandSiteGround : public SiteGround {
    const IslandField &field;
    double dock_apron_m;
    SiteRules rules;

public:
    IslandSiteGround(const IslandField &p_field, double p_dock_apron_m, const SiteRules &p_rules) :
            field(p_field), dock_apron_m(p_dock_apron_m), rules(p_rules) {}
    bool site_height(double p_x, double p_z, double &r_y) const override;
    SiteBlock site_block(double p_x, double p_z) const override;
};

// 勘察：p_ignore 是清单里要当作不存在的那一座（挪动自己时用），−1 = 都算
SiteReport survey_site(const SiteGround &p_ground, const BuildingDef &p_def, double p_x, double p_z, double p_yaw,
        const SettlementLayout &p_others, const SiteRules &p_rules, int p_ignore = -1);
// 同上，地面是一座岛
SiteReport survey_site(const IslandField &p_field, double p_dock_apron_m, const BuildingDef &p_def, double p_x, double p_z, double p_yaw,
        const SettlementLayout &p_others, const SiteRules &p_rules, int p_ignore = -1);

// 编辑：返回空串表示做了；否则原因，清单不动
std::string settlement_place(SettlementLayout &p_layout, const IslandField &p_field, double p_dock_apron_m, const std::string &p_kind,
        double p_x, double p_z, double p_yaw, const SiteRules &p_rules);
// 点落在哪一座的占地里；没有返回 −1（重叠时取清单里靠后的，即最近放的）
int settlement_find(const SettlementLayout &p_layout, double p_px, double p_pz);

// 文本："kind@x,z,yaw°"，坐标两位小数、角度一位小数
std::string building_id(const Building &p_building);
bool parse_building_id(const std::string &p_text, Building &r_building);
std::vector<std::string> settlement_ids(const SettlementLayout &p_layout);
// 认不出的条目跳过；r_ok 报告是否全都认识。空行与 # 开头的行不算条目
SettlementLayout settlement_from_ids(const std::vector<std::string> &p_ids, bool *r_ok = nullptr);
