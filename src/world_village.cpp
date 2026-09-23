#include "world_village.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/core/class_db.hpp>

#include "dock_builder.h"

using namespace godot;

void WorldVillage::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_site_id"), &WorldVillage::get_site_id);
    ClassDB::bind_method(D_METHOD("is_hamlet"), &WorldVillage::is_hamlet);
    ClassDB::bind_method(D_METHOD("get_site_name"), &WorldVillage::get_site_name);
    ClassDB::bind_method(D_METHOD("get_radius"), &WorldVillage::get_radius);
    ClassDB::bind_method(D_METHOD("get_missing"), &WorldVillage::get_missing);
    ClassDB::bind_method(D_METHOD("has_folk"), &WorldVillage::has_folk);
    ClassDB::bind_method(D_METHOD("get_settlement"), &WorldVillage::get_settlement);
    ClassDB::bind_method(D_METHOD("get_villagers"), &WorldVillage::get_villagers);
    ADD_SIGNAL(MethodInfo("rebuilt"));
}

void WorldVillage::setup(World *p_world, const SiteSettlement &p_site, uint64_t p_seed) {
    world = p_world;
    site = p_site;
    seed = p_seed;
    set_name(vformat("%s%d", site.hamlet ? "Hamlet" : "Village", site.id));
    set_position(Vector3(real_t(site.x), 0, real_t(site.z)));
}

void WorldVillage::_ready() {
    ERR_FAIL_NULL_MSG(world, "WorldVillage 要先 setup 再进树");
    plan = plan_world_village(world->get_terrain(), site, seed, WorldVillageRules());
    planned = true;
    settlement = memnew(Settlement);
    settlement->set_name("Settlement");
    settlement->set_model(plan.layout);
    add_child(settlement); // 宿主已经好了：Settlement 进树就盖
}

void WorldVillage::set_folk(bool p_on, int p_per_house, int p_max_folk) {
    if (p_on == has_folk() || !settlement) {
        return;
    }
    if (!p_on) {
        villagers->queue_free();
        villagers = nullptr;
        return;
    }
    villagers = memnew(Villagers);
    villagers->set_name("Villagers");
    villagers->set_settlement_path(NodePath("../Settlement"));
    villagers->set_farm_path(NodePath());
    villagers->set_per_house(p_per_house);
    villagers->set_max_folk(p_max_folk);
    villagers->set_seed(int64_t(host_seed() & 0x7FFFFFFF));
    add_child(villagers); // 排在 Settlement 后面
}

std::unique_ptr<SiteGround> WorldVillage::host_site_ground(const SiteRules &) const {
    return std::make_unique<WorldSiteGround>(world->get_terrain(), site.x, site.z, PAD_SEARCH_M);
}

std::unique_ptr<NavGround> WorldVillage::host_nav_ground() const {
    return std::make_unique<WorldNavGround>(world->get_terrain(), site.x, site.z);
}

void WorldVillage::host_nav_window(const SettlementLayout &p_layout, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const {
    // 房子外接框外扩一圈
    double lo_x = 0.0, lo_z = 0.0, hi_x = 0.0, hi_z = 0.0;
    for (const Building &b : p_layout.get_items()) {
        const BuildingDef *def = buildings::by_id(b.kind);
        const double r = def ? 0.5 * std::hypot(def->width_m, def->depth_m) : 0.0;
        lo_x = std::min(lo_x, b.x - r);
        lo_z = std::min(lo_z, b.z - r);
        hi_x = std::max(hi_x, b.x + r);
        hi_z = std::max(hi_z, b.z + r);
    }
    r_x0 = lo_x - NAV_MARGIN_M;
    r_z0 = lo_z - NAV_MARGIN_M;
    r_cols = int(std::ceil((hi_x - lo_x + 2.0 * NAV_MARGIN_M) / p_rules.cell_m));
    r_rows = int(std::ceil((hi_z - lo_z + 2.0 * NAV_MARGIN_M) / p_rules.cell_m));
}

void WorldDock::setup(const DockPlan &p_plan, int p_index, int p_id) {
    index = p_index;
    dock_id = p_id;
    set_name(vformat("Dock%d", p_id));
    set_position(Vector3(real_t(p_plan.x), 0, real_t(p_plan.z)));
    DockPlan local = p_plan; // 以岸缘点为原点
    local.x = 0.0;
    local.z = 0.0;
    dock_builder::build_pier(this, local, [](Node3D *p_parent, Node *p_node, const char *p_name) {
        p_node->set_name(p_name);
        p_parent->add_child(p_node);
    });
}
