#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ground_host.h"
#include "map_host.h"
#include "river_mesh.h"
#include "terrain_tile.h"
#include "world_grid.h"
#include "world_sites.h"
#include "world_terrain.h"

class SiteStream;

// 一个真实比例的岛群（skyisle-gen 第三层产物，经 tools/export_skyisle.py 转成 data_dir 下的 terrain.bin + world.json）。
// 地面是流式的：焦点（上层每帧 set_focus，通常是玩家）一动，就按 TileLayout 重新选块，缺的块丢给后台线程建网格，
// 建好的在主线程挂进场景；被取代的旧块要等盖住它的新块都到齐了才拆，所以地面不会闪出窟窿。最细一级（叶块）带碰撞。
// 瞬移前调 prepare_at：同步建好落点周围的叶块，人落下去脚下一定有地。
// 码头与船坞坪：加载时按 sites.tsv 把全部码头规划好（dock_site）、主码头旁找好船坞坪（yard_site），压 / 凿进连续地形里再建第一块地面；
// 泊位接触线、出生位姿由此给出。栈桥与村落这些实物不归这里管（SiteStream 节点，走近了才建）。
// 它是地面宿主（GroundHost）：下面挂的 Farm（整个岛群一片农田，地块格与岛群坐标对齐）与 Shipyard（主码头旁）用它的坐标；
// 也是地图宿主（MapHost）：码头、脚下的聚落（SiteStream 铺好的村）、流式焦点、岛群地图面板都由它回答。
// 河（world_river.h）：栅格有水文就在建地形时建好河网、凿好河道；weather.tsv 给一年的水情。水面按 river_mesh 切成块，
// 焦点 river_radius 内的块挂上、远了拆；共用一份水面材质（game/shaders/river.gdshader），每帧按全局钟算出今天的径流指数设进去，
// 所以水涨水落不用重建网格。第 0 天对上 weather.tsv 的第 calendar_offset 天；runoff_override ≥ 0 时不看钟（测试、调试用）。
// 加载完发 rebuilt（只一次）。节点须放在场景原点、不旋转不缩放：世界坐标即 World 的局部坐标。
class World : public godot::Node3D, public GroundHost, public MapHost {
    GDCLASS(World, godot::Node3D)

    godot::String data_dir = "res://worlds/2051";
    godot::NodePath sites_path = "../SiteStream"; // 可以不接：那就没有码头与村落的实物，也靠不了岸
    int64_t terrain_seed = 2051;
    double split = 1.25; // 焦点到块的距离 < 块边长 × split 就细分
    int build_threads = 3;
    double retarget_distance = 12.0; // 焦点挪了这么远才重新选块

    WorldGrid grid;
    WorldTerrain terrain;
    TileLayout layout;
    godot::Dictionary info; // world.json
    WorldSites sites; // sites.tsv
    WorldWorks works; // 码头与船坞坪，与 sites.docks 一一对应
    std::vector<FreightPort> freight_ports; // 港口村（跑货）
    bool loaded = false;
    SiteStream *site_stream = nullptr;

    struct Tile {
        TileKey key;
        godot::Node3D *node = nullptr;
    };
    std::map<uint64_t, Tile> tiles; // 已挂进场景的
    std::vector<TileKey> target; // 这一刻该有的
    godot::Vector3 focus;
    godot::Vector3 targeted_focus;
    bool has_target = false;
    godot::Ref<godot::StandardMaterial3D> ground_material;

    // 河
    double river_radius = 4000.0; // 焦点这么远以内的水面块挂上（×1.25 以外拆）
    double calendar_offset = 0.0; // 游戏第 0 天是 weather.tsv 的第几天
    double runoff_override = -1.0; // ≥ 0：径流指数钉死在这个值
    RiverRegime regime;
    RiverMeshRules river_mesh_rules;
    std::vector<RiverChunkKey> river_chunk_keys; // 有河的块
    std::map<int64_t, godot::Node3D *> river_nodes; // 已挂上的水面块（空块为 nullptr）
    godot::Ref<godot::ShaderMaterial> river_material;
    double runoff = 1.0; // 这一帧的径流指数
    godot::Vector3 river_focus;
    bool river_targeted = false;
    bool river_more = false; // 上一回预算用完、还有近处的块没建

    // 后台建网格：主线程排 jobs、取 results；线程只读 terrain / layout（建好后不再变）
    std::vector<std::thread> workers;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<TileKey> jobs;
    std::vector<std::unique_ptr<TileMesh>> results;
    std::map<uint64_t, bool> in_flight; // 排了或正在建的
    bool stopping = false;

    bool load();
    void start_workers();
    void stop_workers();
    void worker_loop();
    void retarget();
    void adopt(std::unique_ptr<TileMesh> p_mesh);
    void prune();
    bool target_has(const TileKey &p_key) const;
    void update_runoff();
    void stream_rivers(bool p_all);
    godot::Node3D *make_river_node(const RiverMesh &p_mesh);

protected:
    static void _bind_methods();

public:
    ~World();
    void _ready() override;
    void _process(double p_delta) override;

    void set_focus(const godot::Vector3 &p_focus) { focus = p_focus; }
    godot::Vector3 get_focus() const { return focus; }
    // 焦点挪到该处，并同步建好它周围 p_radius 以内的叶块（瞬移用）
    void prepare_at(const godot::Vector3 &p_point, double p_radius = 160.0);
    // 该处（世界 xz）脚下的叶块已经挂上（有碰撞）了
    bool is_ready_at(const godot::Vector3 &p_point) const;
    // 地面高度；没有地返回 NAN
    double get_surface_height(double p_x, double p_z) const;
    // 该处最近的岛号（没有地也给最近的那座）；未加载返回 -1
    int get_island_at(double p_x, double p_z) const;
    godot::Dictionary get_info() const { return info; }
    bool is_loaded() const { return loaded; }
    int get_tile_count() const { return int(tiles.size()); }
    // 河：此刻的径流指数（1 = 年均）；该处（世界 xz）的河况——没有河返回空字典，
    // 有则 { distance, bankfull_width, bankfull_depth, floodplain, level, depth, width, flow, wet, bed, acc_km2 }
    double get_runoff() const { return runoff; }
    godot::Dictionary get_river_at(double p_x, double p_z) const;
    int get_river_chunk_count() const;
    // 该处附近（p_radius 以内）汇流至少 p_min_acc_km2 的河中线上最近一点：
    // { x, z, dir_x, dir_z（顺水）, width, floodplain, acc_km2, mouth（这条河出不出岸缘）, mouth_x, mouth_z, mouth_dir_x, mouth_dir_z（河口的流向）}；没有返回空字典
    godot::Dictionary find_river_near(double p_x, double p_z, double p_radius, double p_min_acc_km2) const;
    int get_pending_count() const;

    // 码头（下标与 sites.docks 一致）
    int get_dock_count() const { return int(works.docks.size()); }
    int find_dock(int p_id) const; // 按生成器的码头号找下标；没有 −1
    int get_dock_id(int p_index) const;
    bool is_dock_berthable(int p_index) const; // 找到了岸缘，能靠泊
    godot::Transform3D get_dock_berth(int p_index) const; // 泊位接触线（世界坐标）
    godot::Transform3D get_dock_spawn(int p_index) const; // 坪上面朝栈桥的出生位姿（世界坐标）
    godot::Vector3 get_dock_position(int p_index) const; // 岸缘点（栈桥根）

    // 给同在 C++ 里的上层（WorldMap 画底图）
    const WorldGrid &get_grid() const { return grid; }
    const WorldTerrain &get_terrain() const { return terrain; }
    const WorldSites &get_sites() const { return sites; }
    const std::vector<DockPlan> &get_docks() const { return works.docks; }
    int find_freight_port(int p_site_index) const { return find_port(freight_ports, p_site_index); } // sites.settlements 下标 → 托运用的下标；散户 −1
    // 测试与调试：某村（村号）村仓那一点（船台跳板脚前）的全局位置；村子还没铺或没有船台返回 (0,0,0)
    godot::Vector3 get_freight_post(int p_village_id) const;
    godot::PackedInt32Array get_freight_port_ids() const; // 能托运的村的村号
    SiteStream *get_site_stream() const { return site_stream; }

    // GroundHost（Farm、Shipyard 挂在它下面）
    godot::Node3D *host_node() override { return this; }
    const godot::Node3D *host_node() const override { return this; }
    bool host_ready() const override { return loaded; }
    std::unique_ptr<MapGround> host_ground() const override;
    Farm *host_farm() const override; // 下面的 Farm 子节点
    YardPlan host_yard() const override; // 主码头旁的船坞坪
    void host_origin(double &r_x, double &r_z) const override { r_x = r_z = 0.0; }

    // MapHost
    godot::Node3D *map_node() override { return this; }
    bool map_ready() const override { return loaded; }
    double map_surface(const godot::Vector3 &p_global) const override { return get_surface_height(p_global.x, p_global.z); }
    int map_island_at(const godot::Vector3 &p_global) const override;
    bool map_footing(const godot::Vector3 &p_global) const override;
    int map_dock_count() const override { return get_dock_count(); }
    int map_dock_id(int p_index) const override { return get_dock_id(p_index); }
    bool map_dock_open(int p_index) const override;
    godot::Transform3D map_dock_berth(int p_index) const override { return get_dock_berth(p_index); }
    godot::Transform3D map_dock_spawn(int p_index) const override { return get_dock_spawn(p_index); }
    godot::Vector3 map_dock_position(int p_index) const override { return get_dock_position(p_index); }
    int map_dock_island(int p_index) const override;
    godot::String map_dock_label(int p_index) const override;
    std::vector<Shipyard *> map_shipyards() const override;
    Farm *map_farm_at(const godot::Vector3 &p_global) const override { return host_farm(); }
    GroundHost *map_settlement_at(const godot::Vector3 &p_global) const override;
    void map_focus(const godot::Vector3 &p_global) override;
    void map_prepare(const godot::Vector3 &p_global) override;
    int map_pending() const override;
    godot::String map_location_text(const godot::Vector3 &p_global) const override;
    WorldMap *map_make_panel() override;
    void map_refresh_panel(WorldMap *p_panel) const override;
    double map_view_distance() const override { return 60000.0; } // 岛群近百公里宽，远处的岛也要看得见
    const std::vector<FreightPort> &map_freight_ports() const override { return freight_ports; }
    FreightPost map_freight_post_near(const godot::Vector3 &p_global, double p_range) override;
    bool map_freight_target(int p_port, godot::Transform3D &r_target) const override;

    void set_sites_path(const godot::NodePath &p_value) { sites_path = p_value; }
    godot::NodePath get_sites_path() const { return sites_path; }

    void set_data_dir(const godot::String &p_value) { data_dir = p_value; }
    godot::String get_data_dir() const { return data_dir; }
    void set_terrain_seed(int64_t p_value) { terrain_seed = p_value; }
    int64_t get_terrain_seed() const { return terrain_seed; }
    void set_split(double p_value) { split = p_value; }
    double get_split() const { return split; }
    void set_build_threads(int p_value) { build_threads = p_value; }
    int get_build_threads() const { return build_threads; }
    void set_river_radius(double p_value) { river_radius = p_value; }
    double get_river_radius() const { return river_radius; }
    void set_calendar_offset(double p_value) { calendar_offset = p_value; }
    double get_calendar_offset() const { return calendar_offset; }
    void set_runoff_override(double p_value);
    double get_runoff_override() const { return runoff_override; }
};
