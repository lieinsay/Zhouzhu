#pragma once

#include <cstdint>

#include "ground_query.h"
#include "settlement_model.h"

// 铺村：给一个村心和户数，在地面上自动摆出一村房子。不含 Godot 头，无状态，同样的输入给出同样的清单。
//
// 要盖什么：一户一座民居；邑治（seat）另加一座堂屋、一座仓，排在最前面，占村心最好的位置。
// 摆在哪：以村心为原点、按种子转一个角度的方格点（间距 spacing_m，每点再抖一抖），由近及远一个个试；
// 每个格点最多摆一座，摆不下（勘察不过）就留空、换下一个点，直到都摆下或者超出 max_radius_m。
// 朝向：地面有坡就让门朝下坡——面阔顺着等高线，台基最矮；平地门朝村心。第一个朝向不行再试转 90°。
// 选址规矩全走 survey_site（ground 给出禁区：码头、溪涧……），所以不压码头、不在陡坡上、不挨着别的房子。
// 坐标就是 ground 的坐标。

struct VillageRules {
    double spacing_m = 17.0; // 格点间距：民居 11 m 面阔 + 巷子
    double jitter_m = 3.0; // 每个格点前后左右抖多少
    double max_radius_m = 700.0; // 超出这么远就不摆了（摆不下的户数记在 missing 里）
    double flat_grade = 0.03; // 坡度小于这个算平地：门朝村心
};

struct VillageRequest {
    double x = 0.0, z = 0.0; // 村心
    int households = 1;
    bool seat = false; // 邑治：加堂屋与仓
    uint64_t seed = 1;
};

struct VillagePlan {
    SettlementLayout layout;
    int wanted = 0; // 要盖几座
    int missing = 0; // 摆不下的几座
    double radius = 0.0; // 最远那座的占地离村心多远（外接圆）
};

VillagePlan plan_village(const SiteGround &p_ground, const VillageRequest &p_request, const SiteRules &p_site_rules, const VillageRules &p_rules);
