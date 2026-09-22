#pragma once

#include <cstdint>
#include <memory>

#include <godot_cpp/classes/node3d.hpp>

#include "ground_query.h"
#include "island_field.h"
#include "settlement_model.h"
#include "villager_model.h"

// 聚落的宿主：Settlement 与 Villagers 挂在它下面、变换留在原点，用它的局部坐标。
// 游戏尺度的岛（Island）与岛群里的一个村（WorldVillage）各自实现；Settlement 与 Villagers 只认这个接口，不认具体是谁。
// 宿主的节点每次地面重建完都发 "rebuilt" 信号；host_ready 之前别问地面。
class SettlementHost {
public:
    virtual ~SettlementHost() = default;

    virtual godot::Node3D *host_node() = 0;
    virtual const godot::Node3D *host_node() const = 0;
    virtual bool host_ready() const = 0;
    // 勘察与导航的地面（局部坐标）。调用方持有，别比宿主活得久
    virtual std::unique_ptr<SiteGround> host_site_ground(const SiteRules &p_rules) const = 0;
    virtual std::unique_ptr<NavGround> host_nav_ground() const = 0;
    // 导航格铺多大（局部坐标）：岛上铺满整座岛，岛群里只铺村子周围
    virtual void host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const = 0;
    virtual uint64_t host_seed() const = 0;
    // 有高度场的岛才给（村民据此种地、挑水）；岛群里的村没有，返回 nullptr
    virtual const IslandField *host_field() const { return nullptr; }

    static SettlementHost *of(godot::Node *p_node) { return dynamic_cast<SettlementHost *>(p_node); }
};
