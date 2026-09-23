#pragma once

#include <cstdint>
#include <memory>

#include <godot_cpp/classes/node3d.hpp>

#include "map_ground.h"
#include "settlement_model.h"
#include "villager_model.h"
#include "yard_site.h"

class Farm;

// 地面宿主：Farm、Shipyard、Settlement、Villagers 挂在它下面、变换留在原点，用它的局部坐标，地面只问它。
// 游戏尺度的岛（Island）、岛群（World，Farm 与 Shipyard 挂它）、岛群里的一个村（WorldVillage，Settlement 与 Villagers 挂它）、
// 测试用的平板岛（FlatMap）各自实现；挂在下面的东西只认这个接口，不认具体是谁。
// 宿主的节点每次地面重建完都发 "rebuilt" 信号；host_ready 之前别问地面。宿主都不转不缩放（只平移）。
class GroundHost {
public:
    virtual ~GroundHost() = default;

    virtual godot::Node3D *host_node() = 0;
    virtual const godot::Node3D *host_node() const = 0;
    virtual bool host_ready() const = 0;
    // 局部坐标的地面。调用方持有，别比宿主活得久；宿主 rebuilt 后重取
    virtual std::unique_ptr<MapGround> host_ground() const = 0;

    // 聚落（Settlement / Villagers）才用：导航格铺多大（局部坐标；岛上铺满整座岛，岛群里只铺村子周围）、种子
    virtual void host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const {
        r_x0 = r_z0 = 0.0;
        r_cols = r_rows = 0;
    }
    virtual uint64_t host_seed() const { return 1; }
    // 这里的村民种哪一片农田（可以挂在别的宿主下，比如岛群里的村种 World 下的那片）；没有农田返回 nullptr
    virtual Farm *host_farm() const { return nullptr; }
    // 船坞（Shipyard）才用：船坞坪（局部坐标）；没有 present = false
    virtual YardPlan host_yard() const { return YardPlan(); }

    // 局部坐标原点在地图坐标里的 xz（双精度：岛群里的村离原点几十公里，单精度的节点位置不够）
    virtual void host_origin(double &r_x, double &r_z) const {
        const godot::Vector3 at = host_node()->get_global_position();
        r_x = at.x;
        r_z = at.z;
    }

    static GroundHost *of(godot::Node *p_node) { return dynamic_cast<GroundHost *>(p_node); }
};
