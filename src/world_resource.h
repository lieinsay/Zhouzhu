#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "resource_sites.h"

// 岛群里的一处资源点的实物：节点放在采掘面（地面上那一点，不转），挂着
//   · 采掘面的样子（按种类：凿开的石壁、土坑、浮石露头、伐倒的原木、砂砾滩、淘金溜槽、矿坑、苇丛、结壳），大石头、原木带碰撞；
//     人站的那一点（原点）空着，东西都摆在背对堆场的那一侧；
//   · 堆场：两只货箱（箱盖是产物的颜色）+ 一根木杆挂牌子「采石场（安山岩 / 凝灰岩）· 下品」。货箱只是摆设，不挡人。
// 只画：位置、种类由规划给（ResourceSite），采多少、存多少是地图宿主的账。建与拆由 SiteStream 按距离决定。
class WorldResource : public godot::Node3D {
    GDCLASS(WorldResource, godot::Node3D)

    ResourceSite site;
    int index = -1; // World 的资源点下标

    godot::Ref<godot::StandardMaterial3D> material(const godot::Color &p_color, float p_roughness = 0.95f);
    // 一只盒子（p_solid：带碰撞）；位置是节点的局部坐标，p_yaw 绕竖轴转
    void block(const godot::String &p_name, const godot::Vector3 &p_size, const godot::Vector3 &p_at, const godot::Color &p_color, bool p_solid, double p_yaw = 0.0);
    void cylinder(const godot::String &p_name, double p_radius, double p_height, const godot::Transform3D &p_xf, const godot::Color &p_color, bool p_solid);
    void build_face(double p_back_x, double p_back_z);
    void build_pile();

protected:
    static void _bind_methods();

public:
    // 进树之前调
    void setup(const ResourceSite &p_site, int p_index);
    void _ready() override;

    const ResourceSite &get_site() const { return site; }
    int get_index() const { return index; }
    int get_deposit_id() const { return site.id; }
    godot::Vector3 get_face() const; // 采掘面（全局）
    godot::Vector3 get_pile() const; // 堆场（全局）
};
