#pragma once

#include <map>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

#include <godot_cpp/classes/cylinder_shape3d.hpp>

#include "job_pool.h"
#include "resource_scatter.h"
#include "scatter_meshes.h"

class GroupWorld;
class WorldVillage;

// 后台生成好的一块散布：清单 + 每一株的 MultiMesh 数据（16 个数：变换 3×4、块的西北角为原点，实例色）
struct ScatterChunkJob : JobResult {
    ScatterChunkKey key;
    std::vector<ScatterItem> items;
    std::vector<float> instances;
};

// 后台生成好的一块远处的林子：阔叶、针叶两份 MultiMesh 数据（远块的西北角为原点）
struct FarTreesJob : JobResult {
    int ix = 0, iz = 0;
    godot::PackedFloat32Array broadleaf, conifer;
    int broadleaf_count = 0, conifer_count = 0;
};

// 岛群里地上一株株、一块块的散布物（resource_scatter.h）：走近了才摆、走远了就拆。节点放在场景原点。
// 上层（World 的 map_focus）每帧 set_focus；每隔 check_interval 秒按焦点重算一次：
//   radius 以内的块交给 World 的任务池生成清单（连每一株的 MultiMesh 数据一起备好），生成好了在主线程挂上——每种样子一个 MultiMesh
//   （立着的；采空了的树桩另一个），一块一次 set_buffer；
//   collision_radius 以内的块给树干加碰撞（一个静态体、每棵一个圆柱），挡人、挡船野泊；
//   已经铺出来的村子按房子、船台的实际占地把压着的藏掉（散布只按估的半径让村子，铺出来的常比估的大）；村子铺了 / 拆了只重查它周围的块。
// 远近分级（共用的着色器按实例离相机多远淡入淡出，见 game/shaders/scatter.gdshader）：
//   树 NEAR_LOD_M 以内是细模（投影），往外是低模，FAR_LOD_M 处交给远处的林子；灌丛、石块、苇这些到各自的距离淡掉；
//   远处的林子：far_radius 以内按 FAR_CHUNK_M 见方的远块在后台生成（ResourceScatter::far_trees，按同一份地表份额在粗格上长一团团树冠），不投影。
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

    static constexpr double NEAR_LOD_M = 200.0; // 树：细模 → 低模
    static constexpr double FAR_LOD_M = 800.0; // 树：低模 → 远处的林子
    static constexpr double FAR_CHUNK_M = 512.0;
    static constexpr double FAR_STEP_M = 19.5; // 远处的林子一团树冠占这么宽（近处树层步长的 3 倍）

private:
    godot::NodePath world_path = "../World";
    double radius = 900.0;
    double collision_radius = 110.0;
    double far_radius = 4000.0;
    double hysteresis = 1.2;
    double check_interval = 0.25;
    double regrow_interval = 4.0;
    int adopt_budget_usec = 4000; // 每帧最多花这么久挂块

    GroupWorld *world = nullptr;
    godot::ObjectID world_id; // 拆的时候撤掉自己在它通道上排着的活（它可能先走）
    godot::Vector3 focus;
    bool has_focus = false;
    double since_check = 0.0, since_regrow = 0.0;
    int64_t village_version = -1;
    int64_t works_revision = -1; // 玩家营造的账（切片十一）上回按哪一版藏的
    double works_x0 = 0.0, works_z0 = 0.0, works_x1 = -1.0, works_z1 = -1.0; // 上回营造账的外包框（撤掉的东西底下要重新露出来）
    struct VillageMark {
        const WorldVillage *village = nullptr;
        double x = 0.0, z = 0.0, reach = 0.0;
    };
    std::vector<VillageMark> filtered; // 上回按哪几处村子藏的

    struct Chunk {
        ScatterChunkKey key;
        std::vector<ScatterItem> items;
        std::vector<float> instances; // 每株 16 个数（ScatterChunkJob::instances）
        std::vector<char> hidden; // 藏着的：压着铺出来的房子，或压着玩家的营造（改过的格、地基、摆件）
        std::vector<char> house_hidden; // 其中压着房子的（改地形时只重查营造那一半）
        std::vector<char> standing; // 上回摆的时候立着没有
        godot::Node3D *node = nullptr;
        godot::StaticBody3D *body = nullptr;
    };
    std::map<uint64_t, Chunk> chunks;
    std::vector<std::unique_ptr<ScatterChunkJob>> waiting; // 生成好了、还没挂的
    std::map<uint64_t, godot::Node3D *> far_chunks; // 远处的林子：远块 → 节点（空块为 nullptr）
    std::vector<std::unique_ptr<FarTreesJob>> far_waiting;
    ScatterMeshes meshes;
    std::map<int, godot::Ref<godot::CylinderShape3D>> trunk_shapes; // 按半径 5 cm 一档
    godot::Ref<godot::CylinderShape3D> trunk_shape(double p_radius);

    double chunk_distance(const ScatterChunkKey &p_key, const godot::Vector3 &p_point) const;
    double far_distance(int p_ix, int p_iz, double &r_farthest) const; // 远块离焦点：最近处、最远处
    void refresh(bool p_all);
    void refresh_far();
    void refilter_villages();
    JobPool::Work chunk_job(const ScatterChunkKey &p_key) const;
    void adopt(std::unique_ptr<ScatterChunkJob> p_job);
    void adopt_far(std::unique_ptr<FarTreesJob> p_job);
    void filter_houses(Chunk &r_chunk) const;
    // 玩家改过的格（级数不是 0）、地基底下、摆件底下的也藏掉（清单照旧确定，存量账的键不变）
    void filter_works(Chunk &r_chunk) const;
    void refilter_works();
    bool works_bounds(double &r_x0, double &r_z0, double &r_x1, double &r_z1) const;
    void snapshot_standing(Chunk &r_chunk) const;
    void build_visuals(Chunk &r_chunk);
    void build_body(Chunk &r_chunk);
    void drop(Chunk &r_chunk);
    bool item_standing(const ScatterChunkKey &p_key, const ScatterItem &p_item) const;
    void update_eye();

protected:
    static void _bind_methods();
    void _notification(int p_what);

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
    // (x, z) 周围 p_radius 以内（到树干边）有没有立着的树干（摆着的块里查；改地形时树根底下挖不得）
    bool solid_standing_near(double p_x, double p_z, double p_radius) const;

    // 查询（给测试、调试）
    int get_chunk_count() const { return int(chunks.size()); }
    int get_far_chunk_count() const; // 挂着的远处林子的远块（有树的）
    int get_far_tree_count() const; // 远处的林子一共几团
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
    void set_far_radius(double p_value) { far_radius = p_value; }
    double get_far_radius() const { return far_radius; }
};
