#pragma once

#include "planet_frame.h"

// 云带之下（行星计划 P7，docs/PLAN-PLANET.md D13、D14）：垂直剖面上的雾、光、颠簸，与船的最低高度。不含 Godot 头。
// 云带在 −3800…−5000 m（VerticalDatum），厚 1.2 km；云底以下 600 m 净空，再往下是海（−5600 m）。
// 不做体积云：
//   - 云带里是一层浓雾（引擎的指数雾，能见度几十米），光随走进的深度变暗——直射按云带的光学厚度与太阳高度（太阳越低走的路越长），
//     漫射光从云带顶上的全亮渐渐暗到云底下的「阴天」那一份；
//   - 云底是一张压着的灰面（SeaSurface 画），云底下是阴天式的漫射光：看不见日面，晨昏只有云底染色（天与光按这里的数给）；
//   - 船在云带里颠簸（flight_model 的 flight_bump），云底下只剩一点（风暴另加，天与光按天气给）。
// 相机（天与光）与船（颠簸）按同一个剖面取：云带顶、云底两处各是几十米的软边，升降一趟读数连续（sea_descent_e2e 录着查）。
struct CloudBandRules {
    double top_y = VerticalDatum::CLOUD_TOP_Y;
    double base_y = VerticalDatum::CLOUD_BASE_Y;
    double sea_y = VerticalDatum::SEA_Y;
    double top_soft_above_m = 40.0; // 云带顶是软的：顶上 40 m 起有雾
    double top_soft_below_m = 50.0; // 顶下 50 m 雾到最浓
    double base_soft_above_m = 50.0; // 云底：底上 50 m 起雾渐淡
    double base_soft_below_m = 70.0; // 底下 70 m 出了云
    double sky_switch_from_m = 10.0, sky_switch_to_m = 40.0; // 云带顶下这一段（雾已经很浓）天空换成从下往上看
    double optical_depth = 5.0; // 整层云带的光学厚度：直射剩 exp(−τ · 走进的份 / sin 太阳高)，太阳高时到云底只剩 0.4%
    double min_sun_sin = 0.2; // 太阳再低也按这么斜算路径
    double under_diffuse = 0.4; // 云底下的漫射光：占云带顶上的份（云底从下往上看的亮度按它）
    double diffuse_k = 0.9; // 漫射光随深度衰减的快慢（小 = 近乎按深度线性：厚云里的扩散光）
    double low_sun_darken = 0.8; // 太阳低时漫射光暗得更快（走进的份按 1 − (1 − d)^(1 + 它 · (1 − sin))）
    double turbulence_in = 1.0; // 颠簸：云带里
    double turbulence_under = 0.2; // 云底下（风暴另加）
};

struct CloudBandState {
    double inside = 0.0; // 在云里：雾有多浓（0..1）；云带顶上、云底下都是 0
    double depth = 0.0; // 走进云带多深（0 = 顶 … 1 = 底；云底以下是 1）
    double under = 0.0; // 出了云底多少（0..1）
    double below = 0.0; // 天空要不要换成从下往上看（0..1）：云带顶下 10–40 m 换过去（那里雾已经很浓，看不出换）
    double sun = 1.0; // 直射还剩多少（0..1）
    double diffuse = 1.0; // 漫射光（天光、环境光）还剩多少
    double turbulence = 0.0; // 颠簸 0..1（不含风暴）
    double sea_height = 0.0; // 离海面多高（米）
};

// 高 p_y（游戏 y）、太阳高度的正弦 p_sun_sin（夜里是负的，按 min_sun_sin 算路径）
CloudBandState cloud_band_at(double p_y, double p_sun_sin, const CloudBandRules &p_rules = CloudBandRules());

// 船的最低高度（D14）：海面之上 15 m——下面是海（有行星数据的地图）时 −5585 m；没有行星数据的地图照旧 p_flat_floor（世界 y = 15 m）。
// 岛上下面是地面：船与地面、崖壁、岛底本来就有碰撞（move_and_slide），不另设
constexpr double SHIP_SEA_CLEARANCE_M = 15.0;
inline double ship_floor_y(bool p_over_sea, double p_flat_floor) {
    return p_over_sea ? VerticalDatum::SEA_Y + SHIP_SEA_CLEARANCE_M : p_flat_floor;
}
