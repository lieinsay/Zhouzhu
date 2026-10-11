#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "cover_blend.h"
#include "dock_site.h"
#include "terrain_edits.h"
#include "value_noise.h"
#include "world_grid.h"
#include "world_river.h"
#include "yard_site.h"

// 岛群的连续地形：在任意 (x, z) 上给出有没有地、顶面多高、岛底多深、是什么地表。
// 大形全听栅格（Catmull-Rom 双三次），栅格量不到的 100 m 以下细节由确定性噪声补上：
//   陆地 ⇔ 岸距 + 岸线噪声 > 0（岸线不再是 100 m 的锯齿）
//   顶面 = 栅格高程 + 细节噪声（按地表调幅：岩多草少、田最平；栅格有地形区时再按区加减，山地另加几百米一道的岩梁）
//   岛底 = 岸缘 − 崖壁 − 龙骨 × √向内程度 + 岩棱起伏（倒锥的浮山，见 island_body）
//   河（栅格有水文时，见 world_river.h）：构造时建好河网；河道 + 河漫滩内顶面全按横断面（挖河床、填 / 削滩），
//     再外 blend_m 渐回原地形；河边的细节噪声同样渐隐（河谷是平顺的）。源河同一主干的邻段连续合成，几条河重叠时取最低
//   地表（cover）：各地表类在这一点的份额，交界处几十米的过渡带（见 cover_blend.h）；地皮与散布都从这儿取，只管画，不动高度
//   停机坪（set_pads）：顶面按 dock_pad_weight 拉向坪高——码头后面那一片是平的；河从坪上过时坪让给河（按河的影响权重）
//   船坞坪（set_yards）：坪内全平、坪外按坡度挖 / 填方接回（yard_carve），在停机坪之后
// 同一点无论哪一级网格来取，结果都一样，所以不同精度的地块拼得上。
// 停机坪与船坞坪在建第一块地面之前设好、之后不再变；除此之外没有状态，取样线程安全。

struct WorldIsland {
    int id = 0;
    double rim_m = 0.0; // 岸缘高
    double cliff_m = 20.0; // 生成器的崖高（岸缘到岛底边）
    double keel_m = 100.0; // 生成器的 keel_m：崖脚（岛底边）的高程 = 岸缘 − 崖高，随浮高平移；游戏的岛体不用它（浮高计划 Z2 起，见 island_body）
    double max_shore_m = 1000.0; // 岛上最深的岸距
};

// 游戏里的岛体：生成器把低矮的群当薄片（拓展「按新版生成器优化地形」时量的 2051：主岛崖高 27 m、42 km 宽的岛最厚才 380 m，像一张饼），
// 游戏按「浮山」画——岸缘下先是一段竖直的崖壁，再往下收成倒锥，龙骨深随岛的尺度长（最深岸距 R 米：25·√R，
// 主岛 R 17 km → 约 3.3 km；15 km² 的岛 R 1.9 km → 约 1.1 km；0.3 km² 的小礁 R 250 m → 约 400 m，小岛看着像一块倒悬的山石）。
// 生成器的崖高只作崖壁的下限；生成器的 keel_m 是崖脚的高程、不是深度，不用（浮高计划 Z2：岛体厚度与浮高无关）。
// 岛底最深处落到 y = 0 以下几千米；浮高计划 F3：岛底（连岩棱与起伏的最深处，island_lowest）至少高出云带顶 1000 m
// （VerticalDatum::ISLAND_BOTTOM_MIN_Y = −4000 m），超了就把龙骨收短、崖壁不动——大岛（最深岸距三四十公里）的龙骨照 25·√R 要垂到云带里。
// 远处岛群的低模（far_group_mesh）照抄这几条式子。
struct IslandBody {
    double wall_m = 0.0; // 岸缘下竖直崖壁的高
    double keel_m = 0.0; // 崖脚之下倒锥还垂多深（岛心 = 岸缘 − wall − keel，岩棱再垂 7%）
};
IslandBody island_body(const WorldIsland &p_island);
// 这座岛的岛底最深能到哪（岩棱、起伏都取最坏的一面）：夹过龙骨之后不低于 VerticalDatum::ISLAND_BOTTOM_MIN_Y
double island_lowest(const WorldIsland &p_island, const IslandBody &p_body);

struct TerrainSample {
    uint8_t lith = 0;
    float rockwall = 0;
    bool land = false;
    double top = 0.0; // 顶面 y
    double bottom = 0.0; // 岛底 y
    double shore = 0.0; // 含岸线噪声的岸距（米），陆地为正
    int island = 0; // 最近的岛
    int landcover = 0; // 最近格的地表类
    bool stream = false;
    float bed = 0.0f; // 在河床里（1）→ 出了平岸（0），地表色用
    float bank = 0.0f; // 在河漫滩上（1）→ 渐回原地形（0）
};

class WorldTerrain {
    const WorldGrid *grid = nullptr;
    std::vector<WorldIsland> islands; // 按 id 下标
    std::vector<IslandBody> bodies; // 同上
    uint64_t seed = 0;
    std::shared_ptr<const WorldRivers> rivers; // 建好就不再变，可以在拷贝之间共用
    CoverBlend cover_blend;
    std::vector<DockPlan> pads;
    std::unordered_map<int64_t, std::vector<int>> pad_buckets; // 桶 → 压着这个桶（含坪外 PAD_REACH_M）的坪
    std::vector<YardPlan> yards;
    YardRules yard_rules;
    // 改地形（切片十一，terrain_edits.h）：主线程换一份新的、后台线程（建地面块、量高、导航格）照读；锁只护着指针，
    // 外包框放在原子量里——框外的取样（绝大多数）不上锁。拷贝之间共用同一份
    struct EditSlot {
        std::mutex mutex;
        std::shared_ptr<const TerrainEdits> edits;
        std::atomic<double> min_x{ 1.0 }, min_z{ 1.0 }, max_x{ -1.0 }, max_z{ -1.0 };
    };
    std::shared_ptr<EditSlot> edit_slot = std::make_shared<EditSlot>();

public:
    static constexpr double PAD_BUCKET_M = 256.0;
    static constexpr double PAD_REACH_M = 30.0; // near_pad 的让出最多问到坪外这么远
    static constexpr double YARD_REACH_M = 200.0; // 船坞坪的挖 / 填方最远接到坪外这么远

    static constexpr double COAST_AMP_M = 28.0; // 岸线噪声幅度：岸线最多进退这么多
    static constexpr double COAST_FEATURE_M = 90.0;

    WorldTerrain() = default;
    // p_grid 须比本对象活得久。栅格有水文就按 p_river_rules 建河网
    WorldTerrain(const WorldGrid &p_grid, const std::vector<WorldIsland> &p_islands, uint64_t p_seed, const RiverRules &p_river_rules = RiverRules());

    const WorldGrid &get_grid() const { return *grid; }
    bool valid() const { return grid && !grid->empty(); }
    const WorldIsland &island_info(int p_id) const;
    const IslandBody &island_body_of(int p_id) const;
    // 所有岛底能垂到的最低处（含岩棱起伏的上限），云海要在它之下
    double lowest_bottom() const;
    const WorldRivers &get_rivers() const { return *rivers; }
    bool has_rivers() const { return rivers && !rivers->empty(); }
    const CoverBlend &get_cover() const { return cover_blend; }
    // 这一点各地表类的份额；p_footprint = 取样间距（远处的粗网格传格距，过渡带跟着放宽）
    CoverMix cover(double p_x, double p_z, double p_footprint = 0.0) const { return cover_blend.mix(p_x, p_z, p_footprint); }

    double coast_noise(double p_x, double p_z) const; // 约 [-1, 1]
    double detail(double p_x, double p_z) const; // 米
    TerrainSample sample(double p_x, double p_z) const;
    // 不算停机坪的原地形（码头选址就在它上面量）
    TerrainSample sample_raw(double p_x, double p_z) const;

    // 停机坪：只收 ok 的；建地面之前调
    void set_pads(const std::vector<DockPlan> &p_pads);
    const std::vector<DockPlan> &get_pads() const { return pads; }
    // 该点离哪个停机坪的圆心不到「坪半径 + p_margin」（p_margin ≤ PAD_REACH_M）
    bool near_pad(double p_x, double p_z, double p_margin) const;
    // 船坞坪：只收 present 的；在 set_pads 之后、建地面之前调
    void set_yards(const std::vector<YardPlan> &p_yards, const YardRules &p_rules);
    const std::vector<YardPlan> &get_yards() const { return yards; }
    // 只问有没有地（比 sample 省：不算高度）
    bool is_land(double p_x, double p_z) const;
    // 只要顶面（没有地返回 false）：玩家改过的地方加上改动（切片十一），玩法都问它
    bool surface(double p_x, double p_z, double &r_y) const;
    // 原地形的顶面（不算玩家的改动）：散布、采场规划要按坐标哈希逐位确定，不许随改动变
    bool surface_natural(double p_x, double p_z, double &r_y) const;

    // 改地形：换一份（nullptr / 空的 = 没有改动）；此刻的那份；一点比原地形高多少（线程安全）
    void set_edits(const std::shared_ptr<const TerrainEdits> &p_edits);
    std::shared_ptr<const TerrainEdits> get_edits() const;
    double edit_delta(double p_x, double p_z) const;
};
