#include "world_village.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "dock_builder.h"
#include "freight.h"
#include "hinterland.h"

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
    ClassDB::bind_method(D_METHOD("is_edited"), &WorldVillage::is_edited);
    ADD_SIGNAL(MethodInfo("rebuilt"));
}

void WorldVillage::setup(World *p_world, const SiteSettlement &p_site, uint64_t p_seed, const String &p_layout_path, Hinterland *p_hinterland, int p_index) {
    world = p_world;
    hinterland = p_hinterland;
    site_index = p_index;
    site = p_site;
    seed = p_seed;
    layout_path = p_layout_path;
    set_name(vformat("%s%d", site.hamlet ? "Hamlet" : "Village", site.id));
    set_position(Vector3(real_t(site.x), 0, real_t(site.z)));
}

void WorldVillage::_ready() {
    ERR_FAIL_NULL_MSG(world, "WorldVillage 要先 setup 再进树");
    plan = plan_world_village(world->get_terrain(), site, seed, WorldVillageRules());
    planned = true;
    settlement = memnew(Settlement);
    settlement->set_name("Settlement");
    settlement->set_layout_path(layout_path);
    settlement->set_base(plan.layout);
    add_child(settlement); // 宿主已经好了：Settlement 进树就叠上改动、盖起来
    if (world->find_freight_port(site_index) >= 0) {
        build_freight_post();
    }
}

namespace {

Ref<StandardMaterial3D> plain(const Color &p_color) {
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_albedo(p_color);
    m->set_roughness(0.95f);
    return m;
}

void add_block(Node3D *p_parent, const String &p_name, const Vector3 &p_size, const Vector3 &p_at, const Ref<StandardMaterial3D> &p_material) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(p_size);
    mesh->set_material(p_material);
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_name(p_name);
    instance->set_mesh(mesh);
    instance->set_position(p_at);
    p_parent->add_child(instance);
}

} // namespace

// 村仓门边：三只货箱（箱盖是这村土产的颜色）+ 一根木杆挂牌子。只是摆设，不挡人
void WorldVillage::build_freight_post() {
    const SettlementLayout &layout = settlement->get_model();
    const int k = freight_post_building(layout, 0.0, 0.0);
    if (k < 0) {
        return;
    }
    double x = 0, z = 0, px = 0, pz = 0, yaw = 0;
    freight_post_spot(layout.get_items()[size_t(k)], x, z, px, pz, yaw);
    const double y = world->get_surface_height(site.x + px, site.z + pz);
    if (std::isnan(y)) {
        return;
    }
    freight_post = memnew(Node3D);
    freight_post->set_name("FreightPost");
    freight_post->set_position(Vector3(real_t(px), real_t(y), real_t(pz)));
    freight_post->set_rotation(Vector3(0, real_t(yaw), 0));
    add_child(freight_post);
    float r = 0, g = 0, b = 0;
    freight::crate_color(freight::specialty_of(seed, site.id, site.hamlet), r, g, b);
    const Ref<StandardMaterial3D> wood = plain(Color(0.58f, 0.44f, 0.26f)), lid = plain(Color(r, g, b)), pole = plain(Color(0.33f, 0.22f, 0.15f));
    const Vector3 crates[3] = { Vector3(-0.8f, 0.0f, 0.0f), Vector3(0.8f, 0.0f, 0.1f), Vector3(-0.1f, 1.1f, 0.05f) };
    for (int c = 0; c < 3; c++) {
        add_block(freight_post, vformat("Crate%d", c), Vector3(1.4f, 1.1f, 1.4f), crates[c] + Vector3(0, 0.55f, 0), wood);
        add_block(freight_post, vformat("Lid%d", c), Vector3(1.2f, 0.05f, 1.2f), crates[c] + Vector3(0, 1.125f, 0), lid);
    }
    add_block(freight_post, "Pole", Vector3(0.14f, 3.2f, 0.14f), Vector3(1.9f, 1.6f, 0.0f), pole);
    add_block(freight_post, "Board", Vector3(1.3f, 0.5f, 0.06f), Vector3(1.9f, 2.7f, 0.1f), plain(Color(0.8f, 0.7f, 0.5f)));
    Label3D *label = memnew(Label3D);
    label->set_name("Sign");
    label->set_text(String(U"村仓 · 托运"));
    label->set_position(Vector3(1.9f, 3.45f, 0.0f));
    label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    label->set_font_size(48);
    label->set_outline_size(12);
    label->set_pixel_size(0.008f);
    label->set_visibility_range_end(80.0f);
    freight_post->add_child(label);
}

bool WorldVillage::get_freight_post(Vector3 &r_global) const {
    if (!freight_post || !settlement) {
        return false;
    }
    const SettlementLayout &layout = settlement->get_model();
    const int k = freight_post_building(layout, 0.0, 0.0);
    if (k < 0) {
        return false;
    }
    double x = 0, z = 0, px = 0, pz = 0, yaw = 0;
    freight_post_spot(layout.get_items()[size_t(k)], x, z, px, pz, yaw);
    const double gx = site.x + x, gz = site.z + z;
    const double y = world->get_surface_height(gx, gz);
    r_global = Vector3(real_t(gx), real_t(std::isnan(y) ? 0.0 : y), real_t(gz));
    return true;
}

void WorldVillage::set_folk(bool p_on, int p_per_house, int p_max_folk) {
    if (p_on == has_folk() || !settlement) {
        return;
    }
    if (!p_on) {
        if (hinterland) {
            hinterland->set_layout(site_index, settlement->get_model()); // 布置模式改过：远处照新清单记
        }
        villagers->queue_free(); // 离树时把账收回去（地块从农田里拿出来记进账本）
        villagers = nullptr;
        return;
    }
    villagers = memnew(Villagers);
    villagers->set_name("Villagers");
    villagers->set_settlement_path(NodePath("../Settlement"));
    villagers->set_per_house(p_per_house);
    villagers->set_max_folk(p_max_folk);
    villagers->set_seed(int64_t(host_seed() & 0x7FFFFFFF));
    if (hinterland && site_index >= 0) {
        villagers->set_ledger(hinterland->checkout(site_index, settlement->get_model()), hinterland->get_registry());
    }
    add_child(villagers); // 排在 Settlement 后面
}

std::unique_ptr<MapGround> WorldVillage::host_ground() const {
    return std::make_unique<WorldGround>(world->get_terrain(), site.x, site.z);
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
