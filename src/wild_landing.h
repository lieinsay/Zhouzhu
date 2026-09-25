#pragma once

#include <limits>
#include <string>

#include "ship_layout.h"

// 野泊：空艇不找码头、船台，在一块平地上方落下来悬停（浮石托着，不着地），从舷门放下自带的舷梯，人顺着走下地。不含 Godot 头，无状态。
//
// 给地面（高、是不是水）、船的布局、船此刻的位置与朝向（原点 = 舷门格甲板面中心，船头朝 −z）：
//   · 船底下（第 0 层每节的船体 + 船头楔，按 sample_m 一格取样）处处要有地面——悬在崖边上不行；
//     地面最高处与最低处差不过 max_relief_m（太不平船身会一头悬得老高）；
//   · 甲板面落到「地面最高处 + deck_lift_m」：动力区吊的浮石底离地还有一拃，别的地方离得更远；
//   · 两舷各试放一道舷梯：从舷门的甲板边往外，水平长 run 从短往长试，找第一处落脚——
//     脚下有地、不是水，梯板不陡过 max_slope（约 32°），梯板下的地面处处低于梯板（落脚前那一段除外）；
//     两舷都放得下时挑朝着 prefer 那一侧（给了的话），否则挑梯子短的那一侧，一样短挑右舷。
// 船此刻离落点太高（高过 max_descent_m）也不让野泊：先降下来。放不下都有原因（给玩家看的话）。
// 船下的房子、栈桥这类实物这里不管（它只问地面），由场景用碰撞另查（Ship::landing_obstacle）。

class LandingGround {
public:
    virtual ~LandingGround() = default;
    // 地面高；没有地面（崖外、虚空）返回 false
    virtual bool height(double p_x, double p_z, double &r_y) const = 0;
    // 这一点是不是水（河槽、湖）：舷梯不落在水里
    virtual bool water(double p_x, double p_z) const { return false; }
};

struct LandingRules {
    double deck_lift_m = 5.6; // 甲板面在船底下地面最高处之上多高（浮石底离地 0.5 m）
    double max_relief_m = 4.0; // 船底下地面的高低差上限
    double sample_m = 1.75; // 船底下取样的格距
    double max_descent_m = 40.0; // 船此刻最多比落点高这么多（再高先降）
    double max_slope = 0.62; // 舷梯最陡（落差 / 水平长，约 32°）
    double min_run_m = 3.0, max_run_m = 16.0; // 舷梯水平长的范围
    double run_step_m = 0.5;
    double plank_clear_m = 0.15; // 梯板下的地面至少低这么多
    double foot_free_m = 1.0; // 落脚前这一段不查（梯脚本来就贴着地）
};

// 舷门的甲板边离船中线多远（右舷 +、左舷 −），与 ship_layout 的 port_edge_x / starboard_edge_x 同
struct WildLanding {
    bool ok = false;
    std::string why; // 不行的原因（UTF-8）
    double deck_y = 0.0; // 落下后船原点（甲板面）的高
    double ground_hi = 0.0, ground_lo = 0.0; // 船底下地面的最高、最低
    int side = 0; // 舷梯放在哪一舷：+1 右舷、−1 左舷
    double run = 0.0, rise = 0.0; // 舷梯的水平长、落差
    double door_x = 0.0, door_z = 0.0; // 舷门的甲板边（世界 xz；y = deck_y）
    double out_x = 0.0, out_z = 0.0; // 舷梯往外的方向（单位向量）
    double foot_x = 0.0, foot_z = 0.0, foot_y = 0.0; // 梯脚落在哪
};

// p_x / p_y / p_z：船原点此刻的位置；p_yaw：船的偏航（Godot 的 rotation.y）；p_prefer_*：想让舷梯朝着哪儿（NAN = 不挑）
WildLanding plan_wild_landing(const LandingGround &p_ground, const ShipLayout &p_layout, double p_x, double p_y, double p_z, double p_yaw,
        const LandingRules &p_rules = LandingRules(), double p_prefer_x = std::numeric_limits<double>::quiet_NaN(), double p_prefer_z = std::numeric_limits<double>::quiet_NaN());
