#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "terrain_tile.h"
#include "world_grid.h"
#include "world_terrain.h"

// 一个真实比例的岛群（skyisle-gen 第三层产物，经 tools/export_skyisle.py 转成 data_dir 下的 terrain.bin + world.json）。
// 地面是流式的：焦点（上层每帧 set_focus，通常是玩家）一动，就按 TileLayout 重新选块，缺的块丢给后台线程建网格，
// 建好的在主线程挂进场景；被取代的旧块要等盖住它的新块都到齐了才拆，所以地面不会闪出窟窿。最细一级（叶块）带碰撞。
// 瞬移前调 prepare_at：同步建好落点周围的叶块，人落下去脚下一定有地。
// 节点须放在场景原点、不旋转不缩放：世界坐标即 World 的局部坐标。
class World : public godot::Node3D {
    GDCLASS(World, godot::Node3D)

    godot::String data_dir = "res://worlds/2051";
    int64_t terrain_seed = 2051;
    double split = 1.25; // 焦点到块的距离 < 块边长 × split 就细分
    int build_threads = 3;
    double retarget_distance = 12.0; // 焦点挪了这么远才重新选块

    WorldGrid grid;
    WorldTerrain terrain;
    TileLayout layout;
    godot::Dictionary info; // world.json
    bool loaded = false;

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
    int get_pending_count();

    // 给同在 C++ 里的上层（WorldMap 画底图）
    const WorldGrid &get_grid() const { return grid; }
    const WorldTerrain &get_terrain() const { return terrain; }

    void set_data_dir(const godot::String &p_value) { data_dir = p_value; }
    godot::String get_data_dir() const { return data_dir; }
    void set_terrain_seed(int64_t p_value) { terrain_seed = p_value; }
    int64_t get_terrain_seed() const { return terrain_seed; }
    void set_split(double p_value) { split = p_value; }
    double get_split() const { return split; }
    void set_build_threads(int p_value) { build_threads = p_value; }
    int get_build_threads() const { return build_threads; }
};
