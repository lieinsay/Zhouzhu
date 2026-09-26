#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>

#include "ground_host.h"
#include "job_pool.h"
#include "settlement.h"
#include "village_prep.h"
#include "villagers.h"
#include "world.h"
#include "world_sites.h"

class Hinterland;

// 后台备好的一处村子（VILLAGE 活的结果）：备料 + 各格简模的引擎数组
struct VillagePrepared : JobResult {
    int site_index = -1;
    VillagePrep prep; // 各格的简模网格已经挪进 proxies（prep.cells[k].proxy 清空了）
    struct Proxy {
        godot::PackedVector3Array vertices, normals;
        godot::PackedColorArray colors;
        godot::PackedInt32Array indices;
    };
    std::vector<Proxy> proxies; // 与 prep.cells 一一对应
};
// 在后台线程上把各格的简模转成引擎数组
void convert_proxies(VillagePrepared &r_prepared);

// 后台备好的一格细模（CELL 活的结果）
struct CellPrepared : JobResult {
    int site_index = -1;
    int cell = -1;
    int64_t layout_version = 0; // 照聚落的哪一版清单备的
    std::vector<BuildingArrays> buildings;
};

// 岛群里的一个村 / 一户散户：节点放在村心（y = 0、不转），局部坐标原点就是村心——单精度在几十公里外也不抖。
// 分三层（SiteStream 按距离推）：
//   简模：进树时照后台备好的料（VillagePrepared：铺底清单 + 布置模式的改动 + 地坪 + 各格简模）挂一个 Settlement（deferred：只有数据）
//     和各格的简模网格（房壳 + 悬山顶，一格一个网格、顶点色、共用材质），远远就看得见一村房子；
//   细模：离焦点近的格子（SiteStream 定）交给后台照聚落的清单备细模（CellPrepared），备好了由近及远一座座挂进聚落、
//     一格挂齐了才藏那一格的简模；走远了先亮出简模、再一座座拆。都按上层给的时限做，一帧只做一点；
//   村民（Villagers）：走近了才招（set_folk），导航格可以在后台备好了交过来（没给就现算），只铺村子周围一圈。
// 它是地面宿主：地面、禁区（溪涧、码头的坪、船坞坪）都从 World 的连续地形来（WorldGround，原点挪到村心），村民种的是 World 下面那片农田，
// 材质用 World 那一份共用的（host_looks）。建与拆都由 SiteStream 按距离决定。
// 村子的账（谁住哪、在干什么、仓、水缸、认领的地）在远处账房（Hinterland）：招村民时借来交给 Villagers，散了还回去接着在远处记。
// 村（不是散户）在船台（land_berth.h）的跳板脚边摆一堆货箱、立一块「村仓 · 托运」的牌子：那儿就是村仓（按进树时的清单摆，布置模式挪了船台不跟着挪）。
class WorldVillage : public godot::Node3D, public GroundHost {
    GDCLASS(WorldVillage, godot::Node3D)

public:
    enum CellState {
        CELL_PROXY, // 只有简模
        CELL_PREPARING, // 细模交给后台了
        CELL_MOUNTING, // 细模备好了，一座座往上挂
        CELL_DETAILED, // 挂齐了，简模藏起来
        CELL_UNMOUNTING, // 走远了：简模亮出来，细模一座座拆
    };

private:
    World *world = nullptr;
    SiteSettlement site;
    uint64_t seed = 1;
    godot::String layout_path; // 布置模式的改动存在哪
    std::shared_ptr<VillagePrepared> prepared;
    VillagePlan plan;
    bool planned = false;
    Settlement *settlement = nullptr;
    Villagers *villagers = nullptr;
    Hinterland *hinterland = nullptr; // 可以没有：那就走近了从头招人
    int site_index = -1; // sites.settlements 下标（账房里的账号）
    godot::Node3D *freight_post = nullptr; // 村仓：船台跳板脚边的货堆与牌子
    godot::Node3D *proxy_root = nullptr;

    struct Cell {
        VillageCell data; // 格号、外接框、清单下标（简模网格不留）
        godot::MeshInstance3D *proxy = nullptr;
        CellState state = CELL_PROXY;
        std::vector<BuildingArrays> pending; // 备好了、还没挂的
        int64_t layout_version = 0; // 这一格照哪一版清单分的
    };
    std::vector<Cell> cells;
    int64_t cells_version = -1; // 格子照聚落的哪一版清单分的

    void build_freight_post();
    void build_cells(const std::vector<VillageCell> &p_cells, const std::vector<VillagePrepared::Proxy> *p_proxies);
    void on_layout_changed();
    godot::MeshInstance3D *make_proxy(const VillagePrepared::Proxy &p_proxy, const VillageCell &p_cell);

protected:
    static void _bind_methods();

public:
    static constexpr double NAV_MARGIN_M = 30.0; // 导航格在房子外接框外再铺这么宽
    static constexpr double CELL_M = 96.0; // 格子多大

    // 进树之前调；p_layout_path 是布置模式改动的文件（可以还不存在）；p_hinterland 是远处账房（可以为 nullptr），p_index 是账号；
    // p_prepared 是后台备好的料（nullptr 就在进树时当场备）
    void setup(World *p_world, const SiteSettlement &p_site, uint64_t p_seed, const godot::String &p_layout_path, Hinterland *p_hinterland = nullptr, int p_index = -1,
            std::shared_ptr<VillagePrepared> p_prepared = nullptr);
    void _ready() override;

    // 招 / 散村民（p_max_folk 封顶）；p_nav 是后台备好的导航格（照当下的清单；nullptr 就现算）；p_progressive：展开分几帧做（流式加载）
    void set_folk(bool p_on, int p_per_house, int p_max_folk, std::shared_ptr<VillageNav> p_nav = nullptr, bool p_progressive = false);
    bool has_folk() const { return villagers != nullptr; }

    // 细模（SiteStream 推）：格子的状态、离焦点（全局）多远；后台备好的一格交过来；按时限挂 / 拆（p_deadline 是 Time 的微秒）
    int get_cell_count() const { return int(cells.size()); }
    CellState get_cell_state(int p_cell) const { return cells[size_t(p_cell)].state; }
    double cell_distance_to(int p_cell, const godot::Vector3 &p_global) const;
    const VillageCell &get_cell(int p_cell) const { return cells[size_t(p_cell)].data; }
    int64_t get_cells_version() const { return cells_version; }
    void mark_preparing(int p_cell) { cells[size_t(p_cell)].state = CELL_PREPARING; }
    void cancel_preparing(int p_cell); // 备的时候走远了
    void accept_cell(CellPrepared &&p_cell); // 后台备好的一格：版本对得上、还在备就收下
    void release_cell(int p_cell); // 走远了：亮出简模，细模等 step_mount 一座座拆
    // 按时限挂 / 拆；还有活没做完返回 true
    bool step_mount(uint64_t p_deadline_usec, const godot::Vector3 &p_focus);
    // 这一格的细模现在就备好、挂齐（瞬移、开局用；按库里此刻的模板）
    void mount_cell_now(int p_cell);
    int get_detailed_cell_count() const;

    const SiteSettlement &get_site() const { return site; }
    int get_site_id() const { return site.id; }
    bool is_hamlet() const { return site.hamlet; }
    godot::String get_site_name() const { return godot::String::utf8(site.name.c_str()); }
    double get_radius() const { return plan.radius; }
    int get_missing() const { return plan.missing; }
    Settlement *get_settlement() const { return settlement; }
    Villagers *get_villagers() const { return villagers; }
    int get_site_index() const { return site_index; }
    Hinterland *get_hinterland() const { return hinterland; }
    // 村仓那一点（船台跳板脚前，全局）；没有船台或还没盖好返回 false
    bool get_freight_post(godot::Vector3 &r_global) const;
    bool is_edited() const { return settlement && settlement->is_dirty(); } // 布置模式改过、还没存

    // GroundHost
    godot::Node3D *host_node() override { return this; }
    const godot::Node3D *host_node() const override { return this; }
    bool host_ready() const override { return planned; }
    std::unique_ptr<MapGround> host_ground() const override;
    void host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const override;
    uint64_t host_seed() const override { return seed * 2654435761ull + uint64_t(site.id) * 2ull + (site.hamlet ? 1ull : 0ull); }
    Farm *host_farm() const override { return world ? world->host_farm() : nullptr; }
    SharedLooks *host_looks() const override { return world ? &world->get_looks() : nullptr; }
    void host_origin(double &r_x, double &r_z) const override {
        r_x = site.x;
        r_z = site.z;
    }
};

VARIANT_ENUM_CAST(WorldVillage::CellState);

// 一个码头的栈桥与碰撞：节点放在岸缘点（y = 0），地面已由 World 的地形压平。
class WorldDock : public godot::StaticBody3D {
    GDCLASS(WorldDock, godot::StaticBody3D)

    int index = -1; // World 的码头下标
    int dock_id = -1;

protected:
    static void _bind_methods() {}

public:
    // 进树之前调
    void setup(const DockPlan &p_plan, int p_index, int p_id);
    int get_index() const { return index; }
    int get_dock_id() const { return dock_id; }
};
