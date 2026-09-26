#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "deposit_kinds.h"
#include "resource_sites.h"
#include "world_sites.h"
#include "world_terrain.h"

// 散布：地上一株株、一块块能采的东西，按生成器给的地表、片号、赋存场长出来。不含 Godot 头。
//
//   树（阔叶 / 针叶）：林地里密（片号是林木的片，按片的子类定树种；高处多针叶），灌丛、草坡、崖缘零星几棵；
//   灌丛：灌丛地里一蓬挨一蓬，林下、草坡、崖缘稀稀拉拉；
//   苇丛：泥炭 / 芦苇的片里（湿地）；
//   浮石块：浮石露头的片里（崖缘、峡谷壁）；石块：石料品位够的地方（裸岩、陡坡），越富越密越大；矿石：金属矿化带里；
//   黏土坡、砂砾堆、淘金沙、硫磺 / 鸟粪结壳：各自品位够的地方（不上田、不上林）。
// 按 chunk_m 见方的块生成：每一层按自己的步长铺一张全岛群共用的抖动网格（格点抖动后落进哪块归哪块），每个格点掷一次（概率看那一点的地表 / 片号 / 品位），
// 中了才去量高、查让不让。
// 过渡：机会按这一点的地表份额（WorldTerrain::cover，与地皮同一份）加权，片（泥炭、浮石、鸟粪）也按片的份额，交界处几十米里渐变、边上犬牙交错；
// 林缘（林地份额一半处）多一圈灌丛带、树矮一截；针叶的比例随片与高度平滑着变；赋存的品位在门槛上下渐入；陡坡从上限的六成起渐渐长不住。
// 一切由坐标哈希决定：同一块无论什么时候、哪条线程来生成都一样，所以实例键（块 + 块内序号）稳定，存量账可以按键记。
// 让开：没有地、离崖边不到 min_shore_m、水里、村子里（估的半径外加 village_margin_m）、码头的坪、船坞坪、采场的采掘面与堆场、
// 采场的泊地（规划时量好的野泊处，树长在那儿就落不下船了）、地标；树与石块不上河漫滩，太陡的坡不长树。

struct ScatterItem {
    uint8_t look = 0; // ScatterLook
    uint8_t sub = 0; // 子类（deposit_kinds.h：矿种下标、泥炭 / 芦苇、硫磺 / 鸟粪）
    uint16_t index = 0; // 块内序号（生成顺序）
    float x = 0.0f, y = 0.0f, z = 0.0f; // 世界坐标；y = 摆放高度（地面略往下埋）
    float yaw = 0.0f;
    float scale = 1.0f;

    ScatterLook kind() const { return ScatterLook(look); }
};

struct ScatterChunkKey {
    int ix = 0, iz = 0;
    bool operator==(const ScatterChunkKey &p_other) const { return ix == p_other.ix && iz == p_other.iz; }
    uint64_t packed() const { return (uint64_t(uint32_t(ix) & 0xFFFFFF) << 24) | uint64_t(uint32_t(iz) & 0xFFFFFF); }
    static ScatterChunkKey unpack(uint64_t p_packed);
};

// 一株的键：块 + 块内序号（存量账、采集按它认）
uint64_t scatter_key(const ScatterChunkKey &p_chunk, int p_index);
ScatterChunkKey scatter_key_chunk(uint64_t p_key);
int scatter_key_index(uint64_t p_key);

struct ScatterRules {
    double chunk_m = 128.0;
    double tree_step_m = 6.5; // 各层抖动网格的步长
    double shrub_step_m = 4.5;
    double reed_step_m = 3.0;
    double rock_step_m = 7.0;
    double bank_step_m = 9.0;
    double min_shore_m = 3.0;
    double village_margin_m = 12.0;
    double pad_margin_m = 6.0;
    double site_clear_m = 12.0; // 采掘面、堆场周围这么远不长
    double landing_clear_m = 17.0; // 采场的泊地周围这么远不长（开局的船 7 × 22 m、舷梯 9.5 m）
    double landmark_clear_m = 8.0;
    double max_tree_grade = 1.0; // 坡（高差 / 水平）陡过这个不长树（45°；六成起渐稀）
    double max_bank_grade = 0.4; // 土坡、砂堆、结壳只在缓坡
    double reach_m = 1.6; // 离一株的边这么近够得着（采集）
};

// 一片圆（按 x 排好，查一点在不在任何一个圆里）
class DiskIndex {
    struct Disk {
        double x, z, r;
    };
    std::vector<Disk> disks;
    double max_r = 0.0;

public:
    void add(double p_x, double p_z, double p_r);
    void finish(); // 加完调一次（排序）
    bool hit(double p_x, double p_z) const;
    bool empty() const { return disks.empty(); }
};

class ResourceScatter {
    const WorldTerrain *terrain = nullptr;
    ScatterRules rules;
    uint64_t seed = 0;
    DiskIndex villages, works; // works：采掘面、堆场、泊地、地标
    // 片号（deposit id）→ 这片长什么：林木的树种比例、泥炭还是芦苇、浮石、鸟粪
    struct Patch {
        uint8_t kind = 0; // 0 不是片 1 林木 2 泥炭芦苇 3 浮石 4 鸟粪
        uint8_t sub = 0;
        float conifer = 0.15f; // 林木：针叶树占几成
    };
    std::vector<Patch> patches;
    // 金属矿化带（按岛）：矿石的矿种取同岛最近那条
    struct Ore {
        int island;
        double x, z;
        uint8_t sub;
    };
    std::vector<Ore> ores;

    uint8_t ore_sub(int p_island, double p_x, double p_z) const;
    uint8_t patch_kind(size_t p_cell) const;
    const Patch *patch_near(const CoverStencil &p_stencil, uint8_t p_kind) const; // 4×4 格里这种片份额最大的那格的片
    bool blocked(double p_x, double p_z) const;

public:
    ResourceScatter() = default;
    // p_terrain 须比本对象活得久、应已 lay_world_works；p_plan 是 p_data 的规划（采掘面、泊地、地标都让开）
    ResourceScatter(const WorldTerrain &p_terrain, const WorldSites &p_sites, const ResourceData &p_data, const ResourcePlan &p_plan, uint64_t p_seed,
            const ScatterRules &p_rules = ScatterRules());

    bool valid() const { return terrain && terrain->valid() && terrain->get_grid().has_resources(); }
    const ScatterRules &get_rules() const { return rules; }
    ScatterChunkKey chunk_at(double p_x, double p_z) const;
    void chunk_corner(const ScatterChunkKey &p_key, double &r_x, double &r_z) const;
    // 一块里的全部（按序号）；线程安全
    std::vector<ScatterItem> chunk(const ScatterChunkKey &p_key) const;
    // 该点周围 p_radius 以内挡人挡船的（树干）：圆心与半径；给野泊的规划与测试
    void solids_near(double p_x, double p_z, double p_radius, std::vector<ScatterItem> &r_items) const;
};

// 一株的大小：树干（挡）的半径；够得着的距离（离中心）
double scatter_solid_radius(const ScatterItem &p_item);
double scatter_reach(const ScatterItem &p_item, const ScatterRules &p_rules);

// 散布物的存量账：按键记，没记的是满的、立着的。采空了就倒（树桩 / 坑），按全局钟回满了才立回来
class ScatterLedger {
    struct Entry {
        ResourceStock stock;
        bool felled = false;
    };
    std::unordered_map<uint64_t, Entry> entries;

public:
    // 这一株此刻立着没有（倒了的回满就算立回来）
    bool standing(uint64_t p_key, int p_cap, double p_refill_hours, double p_now_hours) const;
    // 这一株的账（没记过就记一笔满的）；unordered_map 的元素不搬家，指针一直有效
    ResourceStock *stock(uint64_t p_key);
    const ResourceStock *find(uint64_t p_key) const; // 没记过返回 nullptr（满的）
    // 采走 p_count 份：不够或倒着的不给；采空了就倒
    bool take(uint64_t p_key, int p_cap, double p_refill_hours, double p_now_hours, int p_count);
    // 采了一份后调：采空了记成倒了，返回倒没倒
    bool settle(uint64_t p_key, int p_cap, double p_refill_hours, double p_now_hours);
    // 某块里有没有倒着的（给画的那一头：要不要定时看它们长回来）
    bool any_felled_in(const ScatterChunkKey &p_chunk) const;
    size_t size() const { return entries.size(); }
};
