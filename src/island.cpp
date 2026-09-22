#include "island.h"

#include <cmath>
#include <limits>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "dock_builder.h"
#include "island_mesh.h"

using namespace godot;

namespace {

const char *GENERATED_META = "island_generated";

Ref<StandardMaterial3D> make_material(const Color &p_albedo, bool p_vertex_color) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_albedo);
    material->set_roughness(1.0);
    material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, p_vertex_color);
    // 顶点色是按 sRGB 调的；不声明的话引擎当线性值用，整座岛会发白
    material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, p_vertex_color);
    return material;
}

} // namespace

void Island::_bind_methods() {
    ClassDB::bind_method(D_METHOD("rebuild"), &Island::rebuild);
    ClassDB::bind_method(D_METHOD("get_berth_contact"), &Island::get_berth_contact);
    ClassDB::bind_method(D_METHOD("get_pier_end_position"), &Island::get_pier_end_position);
    ClassDB::bind_method(D_METHOD("get_spawn_transform"), &Island::get_spawn_transform);
    ClassDB::bind_method(D_METHOD("get_surface_height", "global_position"), &Island::get_surface_height);
    ClassDB::bind_method(D_METHOD("get_trail_points"), &Island::get_trail_points);
    ClassDB::bind_method(D_METHOD("get_river_points"), &Island::get_river_points);

    ClassDB::bind_method(D_METHOD("set_seed", "value"), &Island::set_seed);
    ClassDB::bind_method(D_METHOD("get_seed"), &Island::get_seed);
    ClassDB::bind_method(D_METHOD("set_area_m2", "value"), &Island::set_area_m2);
    ClassDB::bind_method(D_METHOD("get_area_m2"), &Island::get_area_m2);
    ClassDB::bind_method(D_METHOD("set_age", "value"), &Island::set_age);
    ClassDB::bind_method(D_METHOD("get_age"), &Island::get_age);
    ClassDB::bind_method(D_METHOD("set_relief_m", "value"), &Island::set_relief_m);
    ClassDB::bind_method(D_METHOD("get_relief_m"), &Island::get_relief_m);
    ClassDB::bind_method(D_METHOD("set_elongation", "value"), &Island::set_elongation);
    ClassDB::bind_method(D_METHOD("get_elongation"), &Island::get_elongation);
    ClassDB::bind_method(D_METHOD("set_axis_deg", "value"), &Island::set_axis_deg);
    ClassDB::bind_method(D_METHOD("get_axis_deg"), &Island::get_axis_deg);
    ClassDB::bind_method(D_METHOD("set_dock_bearing_deg", "value"), &Island::set_dock_bearing_deg);
    ClassDB::bind_method(D_METHOD("get_dock_bearing_deg"), &Island::get_dock_bearing_deg);
    ClassDB::bind_method(D_METHOD("set_cliff_m", "value"), &Island::set_cliff_m);
    ClassDB::bind_method(D_METHOD("get_cliff_m"), &Island::get_cliff_m);
    ClassDB::bind_method(D_METHOD("set_keel_m", "value"), &Island::set_keel_m);
    ClassDB::bind_method(D_METHOD("get_keel_m"), &Island::get_keel_m);
    ClassDB::bind_method(D_METHOD("set_has_river", "value"), &Island::set_has_river);
    ClassDB::bind_method(D_METHOD("get_has_river"), &Island::get_has_river);
    ClassDB::bind_method(D_METHOD("set_has_yard", "value"), &Island::set_has_yard);
    ClassDB::bind_method(D_METHOD("get_has_yard"), &Island::get_has_yard);
    ClassDB::bind_method(D_METHOD("has_shipyard"), &Island::has_shipyard);

    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "area_m2", PROPERTY_HINT_RANGE, "5000,1000000,1000,suffix:m²"), "set_area_m2", "get_area_m2");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "age", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_age", "get_age");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "relief_m", PROPERTY_HINT_RANGE, "1,400,0.5,suffix:m"), "set_relief_m", "get_relief_m");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "elongation", PROPERTY_HINT_RANGE, "1,2.5,0.01"), "set_elongation", "get_elongation");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "axis_deg", PROPERTY_HINT_RANGE, "-180,180,0.1,degrees"), "set_axis_deg", "get_axis_deg");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dock_bearing_deg", PROPERTY_HINT_RANGE, "-180,180,0.1,degrees"), "set_dock_bearing_deg", "get_dock_bearing_deg");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cliff_m", PROPERTY_HINT_RANGE, "2,100,0.5,suffix:m"), "set_cliff_m", "get_cliff_m");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "keel_m", PROPERTY_HINT_RANGE, "0,400,1,suffix:m"), "set_keel_m", "get_keel_m");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_river"), "set_has_river", "get_has_river");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "has_yard"), "set_has_yard", "get_has_yard");

    ADD_SIGNAL(MethodInfo("rebuilt"));
}

void Island::_ready() {
    rebuild();
}

void Island::queue_rebuild() {
    if (!is_inside_tree() || rebuild_queued) {
        return;
    }
    rebuild_queued = true;
    call_deferred("rebuild");
}

void Island::clear_generated() {
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        Node *child = Object::cast_to<Node>(children[k]);
        if (child && child->has_meta(GENERATED_META)) {
            remove_child(child);
            child->queue_free();
        }
    }
}

void Island::rebuild() {
    rebuild_queued = false;
    clear_generated();

    params = IslandParams();
    params.seed = uint64_t(seed);
    params.area_m2 = area_m2;
    params.age = age;
    params.rim_m = 0.0;
    params.peak_m = relief_m;
    params.elongation = elongation;
    params.theta = Math::deg_to_rad(axis_deg);
    params.dock_bearing = Math::deg_to_rad(dock_bearing_deg);
    params.cliff_m = cliff_m;
    params.keel_m = keel_m;
    params.river = has_river;
    params.trail = has_river;
    params.yard = has_yard;
    field = generate_island_field(params);

    auto adopt = [this](Node *p_node, const char *p_name) {
        p_node->set_name(p_name);
        p_node->set_meta(GENERATED_META, true);
        add_child(p_node);
    };

    // 岛体
    const IslandMeshData data = build_island_mesh(field);
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = data.vertices;
    arrays[Mesh::ARRAY_NORMAL] = data.normals;
    arrays[Mesh::ARRAY_COLOR] = data.colors;
    arrays[Mesh::ARRAY_INDEX] = data.indices;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    mesh->surface_set_material(0, make_material(Color(1, 1, 1), true));
    MeshInstance3D *terrain = memnew(MeshInstance3D);
    terrain->set_mesh(mesh);
    adopt(terrain, "Terrain");

    Ref<ConcavePolygonShape3D> terrain_shape;
    terrain_shape.instantiate();
    terrain_shape->set_faces(data.collision_faces);
    CollisionShape3D *terrain_collider = memnew(CollisionShape3D);
    terrain_collider->set_shape(terrain_shape);
    adopt(terrain_collider, "TerrainShape");

    // 河水
    const RiverMeshData water = build_river_mesh(field);
    if (!water.vertices.is_empty()) {
        Array water_arrays;
        water_arrays.resize(Mesh::ARRAY_MAX);
        water_arrays[Mesh::ARRAY_VERTEX] = water.vertices;
        water_arrays[Mesh::ARRAY_NORMAL] = water.normals;
        water_arrays[Mesh::ARRAY_COLOR] = water.colors;
        water_arrays[Mesh::ARRAY_INDEX] = water.indices;
        Ref<ArrayMesh> water_mesh;
        water_mesh.instantiate();
        water_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, water_arrays);
        Ref<StandardMaterial3D> water_material = make_material(Color(0.42f, 0.62f, 0.72f), true);
        water_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        water_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        water_material->set_roughness(0.12f);
        water_mesh->surface_set_material(0, water_material);
        MeshInstance3D *water_instance = memnew(MeshInstance3D);
        water_instance->set_mesh(water_mesh);
        water_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        adopt(water_instance, "Water");
    }

    // 栈桥、碰撞、系缆桩（与岛群的码头同一套搭法）
    dock_builder::build_pier(this, dock_plan(), [](Node3D *p_parent, Node *p_node, const char *p_name) {
        p_node->set_name(p_name);
        p_node->set_meta(GENERATED_META, true);
        p_parent->add_child(p_node);
    });

    emit_signal("rebuilt");
}

DockPlan Island::dock_plan() const {
    const IslandDock &dock = field.dock;
    DockPlan plan;
    plan.ok = true;
    plan.x = dock.x;
    plan.z = dock.z;
    plan.y = dock.y;
    plan.out_x = dock.out_x;
    plan.out_z = dock.out_z;
    plan.pier_m = dock.pier_m;
    plan.apron_m = params.dock_apron_m;
    return plan;
}

Transform3D Island::get_berth_contact() const {
    return get_global_transform() * dock_builder::berth_contact(dock_plan());
}

Vector3 Island::get_pier_end_position() const {
    return get_global_transform().xform(dock_builder::pier_end(dock_plan()));
}

Transform3D Island::get_spawn_transform() const {
    Transform3D local = dock_builder::spawn(dock_plan());
    double ground = 0.0;
    if (field.sample_height(local.origin.x, local.origin.z, ground)) {
        local.origin.y = real_t(ground);
    }
    return get_global_transform() * local;
}

PackedVector3Array Island::get_trail_points() const {
    PackedVector3Array points;
    const Transform3D xf = get_global_transform();
    for (const TrailPoint &point : field.trail) {
        points.push_back(xf.xform(Vector3(real_t(point.x), real_t(point.y), real_t(point.z))));
    }
    return points;
}

PackedVector3Array Island::get_river_points() const {
    PackedVector3Array points;
    const Transform3D xf = get_global_transform();
    for (const RiverPoint &point : field.river) {
        points.push_back(xf.xform(Vector3(real_t(point.x), real_t(point.water), real_t(point.z))));
    }
    return points;
}

double Island::get_surface_height(const Vector3 &p_global) const {
    const Vector3 local = get_global_transform().affine_inverse().xform(p_global);
    double ground = 0.0;
    if (!field.sample_height(local.x, local.z, ground)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return get_global_transform().xform(Vector3(local.x, real_t(ground), local.z)).y;
}

void Island::set_seed(int64_t p_value) {
    seed = p_value;
    queue_rebuild();
}

void Island::set_area_m2(double p_value) {
    area_m2 = p_value;
    queue_rebuild();
}

void Island::set_age(double p_value) {
    age = p_value;
    queue_rebuild();
}

void Island::set_relief_m(double p_value) {
    relief_m = p_value;
    queue_rebuild();
}

void Island::set_elongation(double p_value) {
    elongation = p_value;
    queue_rebuild();
}

void Island::set_axis_deg(double p_value) {
    axis_deg = p_value;
    queue_rebuild();
}

void Island::set_dock_bearing_deg(double p_value) {
    dock_bearing_deg = p_value;
    queue_rebuild();
}

void Island::set_cliff_m(double p_value) {
    cliff_m = p_value;
    queue_rebuild();
}

void Island::set_keel_m(double p_value) {
    keel_m = p_value;
    queue_rebuild();
}

void Island::set_has_river(bool p_value) {
    has_river = p_value;
    queue_rebuild();
}

void Island::set_has_yard(bool p_value) {
    has_yard = p_value;
    queue_rebuild();
}

std::unique_ptr<SiteGround> Island::host_site_ground(const SiteRules &p_rules) const {
    return std::make_unique<IslandSiteGround>(field, params.dock_apron_m, p_rules);
}

std::unique_ptr<NavGround> Island::host_nav_ground() const {
    return std::make_unique<IslandNavGround>(field);
}

void Island::host_nav_window(const SettlementLayout &, const NavRules &p_rules, double &r_x0, double &r_z0, int &r_cols, int &r_rows) const {
    // 铺满整座岛（与 build_nav_grid(IslandField) 一致）
    const double ext = field.half_extent();
    r_x0 = -ext;
    r_z0 = -ext;
    r_cols = r_rows = field.n < 2 ? 0 : int(std::floor(2.0 * ext / p_rules.cell_m));
}
