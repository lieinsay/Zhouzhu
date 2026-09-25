#pragma once

#include <map>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>

#include <godot_cpp/classes/cylinder_shape3d.hpp>

#include "resource_scatter.h"
#include "scatter_meshes.h"

class World;

// 岛群里地上一株株、一块块的散布物（resource_scatter.h）：走近了才摆、走远了就拆。节点放在场景原点。
// 上层（World 的 map_focus）每帧 set_focus；每隔 check_interval 秒按焦点重算一次：
//   radius 以内的块交给 World 的后台线程生成清单，生成好了在主线程挂上——每种样子一个 MultiMesh（立着的；采空了的树桩另一个）；
//   collision_radius 以内的块给树干加碰撞（一个静态体、每棵一个圆柱），挡人、挡船野泊；
//   已经铺出来的村子按房子、船台的实际占地把压着的藏掉（散布只按估的半径让村子，铺出来的常比估的大）。
// 采空了（World 的账记了倒）调 refresh_chunk 重摆那一块；每隔 regrow_interval 秒看倒着的长回来没有。
// 都带回滞：拆的距离比建的远 hysteresis 倍。瞬移前调 ensure_near：落点周围 collision_radius 内的块同步生成、摆好、带碰撞。
// 这里只持有「摆了哪些」；清单怎么长在 ResourceScatter（World 持有），采空长回的账在 World 的 ScatterLedger。
class ScatterStream : public godot::Node3D {
    GDCLASS(ScatterStream, godot::Node3D)

public:
    // 够得着的一株
    struct Hit {
        uint64_t key = 0;
        ScatterItem item;
        double gap = 0.0; // 离它边多远
    };

private:
    godot::NodePath world_path = "../World";
    double radius = 900.0;
    double collision_radius = 110.0;
    double hysteresis = 1.2;
    double check_interval = 0.25;
    double regrow_interval = 4.0;
    int adopt_budget_usec = 4000; // 每帧最多花这么久挂块

    World *world = nullptr;
    godot::Vector3 focus;
    bool has_focus = false;
    double since_check = 0.0, since_regrow = 0.0;
    int64_t village_version = -1;

    struct Chunk {
        ScatterChunkKey key;
        std::vector<ScatterItem> items;
        std::vector<char> hidden; // 压着铺出来的房子
        std::vector<char> standing; // 上回摆的时候立着没有
        godot::Node3D *node = nullptr;
        godot::StaticBody3D *body = nullptr;
    };
    std::map<uint64_t, Chunk> chunks;
    std::vector<std::pair<ScatterChunkKey, std::vector<ScatterItem>>> waiting; // 生成好了、还没挂的
    ScatterMeshes meshes;
    std::map<int, godot::Ref<godot::CylinderShape3D>> trunk_shapes; // 按半径 5 cm 一档
    godot::Ref<godot::CylinderShape3D> trunk_shape(double p_radius);

    double chunk_distance(const ScatterChunkKey &p_key, const godot::Vector3 &p_point) const;
    void refresh(bool p_all);
    void adopt(const ScatterChunkKey &p_key, std::vector<ScatterItem> &&p_items);
    void filter_houses(Chunk &r_chunk) const;
    void snapshot_standing(Chunk &r_chunk) const;
    void build_visuals(Chunk &r_chunk);
    void build_body(Chunk &r_chunk);
    void drop(Chunk &r_chunk);
    bool item_standing(const ScatterChunkKey &p_key, const ScatterItem &p_item) const;

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _process(double p_delta) override;

    void set_focus(const godot::Vector3 &p_focus);
    // 同步把 p_point 周围 collision_radius 内的块生成、摆好、带碰撞（瞬移、开局用）；更远的照常排队
    void ensure_near(const godot::Vector3 &p_point);
    // 采空 / 长回之后重摆那一块（没摆着就不管）
    void refresh_chunk(const ScatterChunkKey &p_key);
    // 站在 p_point、面朝 p_facing_yaw（Player 的 look_yaw 约定：0 = 朝 −z）够得着（离它边 reach 以内）、立着的一株：
    // 看着什么采什么：面前 ±45° 以内的按偏离视线的角度挑（对着树伐木，脚边的灌丛不来抢），视线里一株都没有才按离边的距离挑身边的；没有返回 false
    bool nearest_item(const godot::Vector3 &p_point, double p_facing_yaw, Hit &r_hit) const;
    // 物理查到的东西是不是树干
    static bool is_trunk(godot::Object *p_collider);

    // 查询（给测试、调试）
    int get_chunk_count() const { return int(chunks.size()); }
    int get_item_count() const; // 摆着的、立着的
    int get_solid_count() const; // 带碰撞的树干
    // 离 p_point 水平 p_range 以内、摆着的、立着的最近一株（p_kind 是散布物的键，空 = 不挑）：
    // { key, kind, name, product, verb, position, reach, cap, stock, standing }；没有返回空字典
    godot::Dictionary find_item_near(const godot::Vector3 &p_point, double p_range, const godot::String &p_kind = godot::String()) const;
    bool is_standing(int64_t p_key) const;
    // 从 p_from 走到 p_to（水平直线）一路上离立着的树干都有 p_clearance 米以上（摆着的块里查；给测试挑走得通的路）
    bool path_clear(const godot::Vector3 &p_from, const godot::Vector3 &p_to, double p_clearance) const;

    void set_world_path(const godot::NodePath &p_value) { world_path = p_value; }
    godot::NodePath get_world_path() const { return world_path; }
    void set_radius(double p_value) { radius = p_value; }
    double get_radius() const { return radius; }
    void set_collision_radius(double p_value) { collision_radius = p_value; }
    double get_collision_radius() const { return collision_radius; }
};
