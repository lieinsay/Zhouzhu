#include "flat_map.h"

#include <cmath>
#include <limits>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "dock_builder.h"
#include "farm.h"
#include "settlement.h"
#include "shipyard.h"
#include "village_plan.h"

using namespace godot;

namespace {

Ref<StandardMaterial3D> make_material(const Color &p_albedo) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_albedo);
    material->set_roughness(1.0);
    return material;
}

template <typename T>
T *child_of(const Node *p_node) {
    const TypedArray<Node> children = p_node->get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        if (T *found = Object::cast_to<T>(children[k])) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

void FlatMap::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_label", "value"), &FlatMap::set_label);
    ClassDB::bind_method(D_METHOD("get_label"), &FlatMap::get_label);
    ClassDB::bind_method(D_METHOD("set_seed", "value"), &FlatMap::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &FlatMap::get_seed);
    ClassDB::bind_method(D_METHOD("set_radius_m", "value"), &FlatMap::set_radius_m);
    ClassDB::bind_method(D_METHOD("get_radius_m"), &FlatMap::get_radius_m);
    ClassDB::bind_method(D_METHOD("set_dock_bearing_deg", "value"), &FlatMap::set_dock_bearing_deg);
    ClassDB::bind_method(D_METHOD("get_dock_bearing_deg"), &FlatMap::get_dock_bearing_deg);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "label"), "set_label", "get_label");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius_m", PROPERTY_HINT_RANGE, "60,1000,1,suffix:m"), "set_radius_m", "get_radius_m");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_bearing_deg", PROPERTY_HINT_RANGE, "-180,180,1,suffix:°"), "set_dock_bearing_deg", "get_dock_bearing_deg");
    ClassDB::bind_method(D_METHOD("set_village_x", "value"), &FlatMap::set_village_x);
    ClassDB::bind_method(D_METHOD("get_village_x"), &FlatMap::get_village_x);
    ClassDB::bind_method(D_METHOD("set_village_z", "value"), &FlatMap::set_village_z);
    ClassDB::bind_method(D_METHOD("get_village_z"), &FlatMap::get_village_z);
    ClassDB::bind_method(D_METHOD("set_households", "value"), &FlatMap::set_households);
    ClassDB::bind_method(D_METHOD("get_households"), &FlatMap::get_households);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "village_x", PROPERTY_HINT_NONE, "suffix:m"), "set_village_x", "get_village_x");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "village_z", PROPERTY_HINT_NONE, "suffix:m"), "set_village_z", "get_village_z");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "households", PROPERTY_HINT_RANGE, "0,60,1"), "set_households", "get_households");
    ADD_SIGNAL(MethodInfo("rebuilt"));
}

void FlatMap::_ready() {
    build();
}

void FlatMap::build() {
    FlatIslandSpec spec;
    spec.radius_m = radius_m;
    spec.dock_bearing = Math::deg_to_rad(dock_bearing_deg);
    island = lay_flat_island(spec);

    // 圆盘：顶面在 y = 0
    Ref<CylinderMesh> disc;
    disc.instantiate();
    disc->set_top_radius(real_t(radius_m));
    disc->set_bottom_radius(real_t(radius_m * 0.6));
    disc->set_height(real_t(THICKNESS_M));
    disc->set_radial_segments(96);
    disc->set_material(make_material(Color(0.52f, 0.62f, 0.38f)));
    MeshInstance3D *ground = memnew(MeshInstance3D);
    ground->set_name("Terrain");
    ground->set_mesh(disc);
    ground->set_position(Vector3(0, real_t(-0.5 * THICKNESS_M), 0));
    add_child(ground);

    Ref<CylinderShape3D> shape;
    shape.instantiate();
    shape->set_radius(real_t(radius_m));
    shape->set_height(real_t(THICKNESS_M));
    CollisionShape3D *collider = memnew(CollisionShape3D);
    collider->set_name("TerrainShape");
    collider->set_shape(shape);
    collider->set_position(Vector3(0, real_t(-0.5 * THICKNESS_M), 0));
    add_child(collider);

    // 溪：只是一条贴地的水面，平地上蹚得过去
    const FlatIslandSpec &s = island.spec;
    const double dx = s.stream_x1 - s.stream_x0, dz = s.stream_z1 - s.stream_z0;
    Ref<BoxMesh> strip;
    strip.instantiate();
    strip->set_size(Vector3(real_t(2.0 * s.stream_half_width_m), 0.04f, real_t(std::hypot(dx, dz))));
    Ref<StandardMaterial3D> water = make_material(Color(0.42f, 0.62f, 0.72f));
    water->set_roughness(0.12f);
    strip->set_material(water);
    MeshInstance3D *stream = memnew(MeshInstance3D);
    stream->set_name("Water");
    stream->set_mesh(strip);
    stream->set_position(Vector3(real_t(0.5 * (s.stream_x0 + s.stream_x1)), 0.02f, real_t(0.5 * (s.stream_z0 + s.stream_z1))));
    stream->set_rotation(Vector3(0, real_t(std::atan2(dx, dz)), 0));
    stream->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    add_child(stream);

    if (island.dock.ok) {
        dock_builder::build_pier(this, island.dock, [](Node3D *p_parent, Node *p_node, const char *p_name) {
            p_node->set_name(p_name);
            p_parent->add_child(p_node);
        });
    }
    built = true;
    // 村子：与岛群同一个铺村算法，只是地面换成这里的；清单当 Settlement 的底清单，布置模式的改动叠在上面
    if (Settlement *settlement = child_of<Settlement>(this)) {
        const FlatGround map(island);
        const SiteRules rules;
        const MapSiteGround ground(map, rules);
        VillageRequest request;
        request.x = village_x;
        request.z = village_z;
        request.households = households;
        request.seat = true; // 有堂屋与仓：挑水的往堂屋送、收成进仓
        request.seed = uint64_t(seed);
        settlement->set_base(plan_village(ground, request, rules, VillageRules()).layout);
    }
    emit_signal("rebuilt");
}

std::unique_ptr<MapGround> FlatMap::host_ground() const {
    return std::make_unique<FlatGround>(island);
}

void FlatMap::host_nav_window(const SettlementLayout &, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const {
    r_x0 = r_z0 = -radius_m; // 铺满整座岛
    r_cols = r_rows = int(std::floor(2.0 * radius_m / p_rules.cell_m));
}

Farm *FlatMap::host_farm() const {
    return child_of<Farm>(this);
}

double FlatMap::map_surface(const Vector3 &p_global) const {
    const Vector3 local = p_global - get_global_position();
    double y = 0.0;
    if (!FlatGround(island).site_height(local.x, local.z, y)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return y + get_global_position().y;
}

int FlatMap::map_island_at(const Vector3 &p_global) const {
    return std::isnan(map_surface(p_global)) ? -1 : 0;
}

Transform3D FlatMap::map_dock_berth(int) const {
    return get_global_transform() * dock_builder::berth_contact(island.dock);
}

Transform3D FlatMap::map_dock_spawn(int) const {
    return get_global_transform() * dock_builder::spawn(island.dock);
}

Vector3 FlatMap::map_dock_position(int) const {
    return get_global_transform().xform(Vector3(real_t(island.dock.x), real_t(island.dock.y), real_t(island.dock.z)));
}

std::vector<Shipyard *> FlatMap::map_shipyards() const {
    std::vector<Shipyard *> out;
    if (Shipyard *yard = child_of<Shipyard>(this)) {
        out.push_back(yard);
    }
    return out;
}
