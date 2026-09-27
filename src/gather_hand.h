#pragma once

#include <godot_cpp/variant/string.hpp>

#include "map_host.h"
#include "pack_hand.h"
#include "play_notice.h"
#include "player.h"

// 采集：站到采场的采掘面前（face_range 以内）、或地上一株散布物跟前（够得着它，见 resource_scatter.h）按下 E 起工，
// 按住才算工时，满一份（种类表的 gather_s 秒；手上拿着对口的工具减半）就进背包、存量少一份（告诉地图：散布物采空了要倒）；
// 背包放不下就不采；
// 一直按着就接着采下一份，采空了停下、说还要几个钟头才回来一份。存量账在地图宿主手里（按全局钟回满）。
// 与场景无关的玩法部件（不是节点），编排持有、步行时每帧推一下；打包装箱是跑货部件（FreightHand）在堆场做的事。
class GatherHand {
    ResourceSpot spot; // 这一帧够得着采掘面的资源点（site = −1：没有）
    bool working = false;
    double work_time = 0.0; // 这一份干了多久
    int gathered = 0; // 这一回按住 E 采了几份
    double face_range = 2.6; // 离采掘面多近够得着

    double stock_now() const;
    double work_seconds(const PackHand &p_pack) const; // 一份要按住几秒（看手上的工具）

public:
    // 每个物理帧（步行、没开面板时）。p_hands_full：抱着货箱（腾不出手）；p_interact：这一帧刚按下 E。
    // 这一下 E 归它（起工，或说为什么不能采）返回 true，编排就别再拿它干别的
    bool step(MapHost *p_map, Player *p_player, PackHand &p_pack, bool p_hands_full, bool p_interact, double p_delta, const Say &p_say);
    // 放下手上的活（开面板、上舵时）
    void stand_down();
    // 手上拿着挖垒的家伙（锹镐、土）时，这一下 E 让不让给采集：采场的采掘面前，或面前那一株正对手上的家伙（石镐对石块、木锹对黏土坡）才让
    bool prefers(MapHost *p_map, Player *p_player, const PackHand &p_pack) const;

    bool is_working() const { return working; }
    int get_site() const { return spot.site; } // 够得着的资源点（地图的下标）；没有 −1
    // 底部提示：采掘面前能干什么、干到哪了；不在采掘面前返回空
    godot::String prompt(const PackHand &p_pack) const;
};
