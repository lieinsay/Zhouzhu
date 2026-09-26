#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "resource_sites.h"

// 岛群里的一处采场或地标的实物：节点放在采掘面 / 地标那一点（地面上，不转）。
// 采场挂着
//   · 采掘面的样子（按种类：凿开的石壁、土坑、砂砾滩、淘金溜槽、矿坑、硫磺结壳），大石头、土堆带碰撞；
//     人站的那一点（原点）空着，东西都摆在背对堆场的那一侧；
//   · 堆场：两只货箱（箱盖是产物的颜色）+ 一根木杆挂牌子「采石场（安山岩 / 凝灰岩）· 下品」。货箱只是摆设，不挡人。
// 地标（泉眼、温泉、洞穴）只摆个样子、挂块牌子，采不了。
// 只画：位置、种类由规划给（ResourceSite / ResourceLandmark），采多少、存多少是地图宿主的账。建与拆由 SiteStream 按距离决定。
class WorldResource : public godot::Node3D {
    GDCLASS(WorldResource, godot::Node3D)

    ResourceSite site;
    int index = -1; // World 的采场下标（地标：地标下标）
    bool landmark = false;
    ResourceLandmark mark;

    godot::Ref<godot::Material> material(const godot::Color &p_color, float p_roughness = 0.95f); // 弯曲标准材质（curved_material）
    // 一只盒子（p_solid：带碰撞）；位置是节点的局部坐标，p_yaw 绕竖轴转
    void block(const godot::String &p_name, const godot::Vector3 &p_size, const godot::Vector3 &p_at, const godot::Color &p_color, bool p_solid, double p_yaw = 0.0);
    void cylinder(const godot::String &p_name, double p_radius, double p_height, const godot::Transform3D &p_xf, const godot::Color &p_color, bool p_solid);
    void build_face(double p_back_x, double p_back_z);
    void build_pile();
    void build_landmark();
    void sign(godot::Node3D *p_parent, const godot::String &p_text, const godot::Vector3 &p_at);

protected:
    static void _bind_methods();

public:
    // 进树之前调：采场、地标二选一
    void setup(const ResourceSite &p_site, int p_index);
    void setup_landmark(const ResourceLandmark &p_mark, int p_index);
    void _ready() override;

    const ResourceSite &get_site() const { return site; }
    int get_index() const { return index; }
    int get_deposit_id() const { return landmark ? mark.id : site.id; } // 采场号；地标是生成器的点号
    bool is_landmark() const { return landmark; }
    godot::Vector3 get_face() const; // 采掘面（全局）
    godot::Vector3 get_pile() const; // 堆场（全局）
};
