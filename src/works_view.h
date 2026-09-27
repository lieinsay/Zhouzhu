#pragma once

#include <cstdint>
#include <map>

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>

#include "ground_works.h"
#include "piece_mesh.h"

class PlacedProps;
class SharedLooks;

// 营造的实物（切片十一）：照一处地面的营造账（GroundWorks）画搭建件与摆在地上的东西。只画、只答，账在宿主（岛群）或编排（回退的那一份）手里。
// 挂在宿主节点下、用宿主的局部坐标（岛群的 World：邻群跟着它的摆放一起挪，换锚不用管）；没有宿主的回退账挂在编排下、用全局坐标。
// 每帧看账的版本：搭建件按座重画（一座一组网格 + 一份两面都挡的碰撞 + 梯子的爬梯区域，座的签名没变就不动），摆件交给 PlacedProps（它自己看版本）。
// 碰撞层 1（人、船、相机的弹簧臂都碰得到），梯子区域第 8 层（Player 的 Sensor 看这一层，与房子、船上的梯子同一套）。
// 物理：邻群撤出物理（set_physics_on），同地面块。
class WorksView : public godot::Node3D {
    GDCLASS(WorksView, godot::Node3D)

public:
    static constexpr const char *STRUCTURE_META = "works_structure"; // 碰撞体上记着座 id
    static constexpr uint32_t LADDER_LAYER = 8;

private:
    GroundWorks *works = nullptr;
    SharedLooks *looks = nullptr; // 宿主给的共用材质；没给就自己建一份
    SharedLooks *own_looks = nullptr;
    PlacedProps *props_view = nullptr;
    bool physics_on = true;
    int structures_revision = -1;
    struct Built {
        godot::Node3D *root = nullptr;
        godot::StaticBody3D *body = nullptr;
        uint64_t signature = 0;
    };
    std::map<int, Built> built;

    SharedLooks &get_looks();
    void rebuild(const Structure &p_structure, Built &r_built);

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    ~WorksView();
    void _process(double p_delta) override;

    void setup(GroundWorks *p_works, SharedLooks *p_looks);
    GroundWorks *get_works() const { return works; }
    // 账变了立刻照画（改完马上要物理查、测试查的时候；平常每帧自己看）
    void sync();
    void set_physics_on(bool p_on);

    // 物理查到的碰撞体是不是这里的一座：是就给座 id 与这一点在那一座局部坐标里的位置
    bool structure_hit(godot::Object *p_collider, const godot::Vector3 &p_global, int &r_structure, godot::Vector3 &r_local) const;
    // 某座的节点（原点与朝向就是那一座的；测试查、虚影摆位）
    godot::Node3D *get_structure_node(int p_structure) const;
    godot::StaticBody3D *get_structure_body(int p_structure) const;
    PlacedProps *get_props_view() const { return props_view; }
    int get_built_count() const { return int(built.size()); }
};

// 一座（或一件）的网格照着建成 Godot 的节点：按材质的网格挂在 p_root 下；p_override 不为空时全用它（虚影、不投影）
void build_structure_nodes(godot::Node3D *p_root, const StructureMesh &p_mesh, SharedLooks &p_looks, const godot::Ref<godot::Material> &p_override);
