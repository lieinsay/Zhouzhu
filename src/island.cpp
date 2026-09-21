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

#include "island_mesh.h"

using namespace godot;

namespace {

const char *GENERATED_META = "island_generated";
constexpr double PIER_INLAND = 4.0; // 栈桥根埋进岸里多少
constexpr double PIER_THICKNESS = 0.5;
constexpr double BERTH_GAP = 0.15; // 船舷与栈桥头之间的缝
constexpr double SPAWN_INLAND = 12.0;

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
    ClassDB::bind_method(D_METHOD("get_berth_transform", "half_beam"), &Island::get_berth_transform);
    ClassDB::bind_method(D_METHOD("get_pier_end_position"), &Island::get_pier_end_position);
    ClassDB::bind_method(D_METHOD("get_spawn_transform"), &Island::get_spawn_transform);
    ClassDB::bind_method(D_METHOD("get_surface_height", "global_position"), &Island::get_surface_height);
    ClassDB::bind_method(D_METHOD("get_trail_points"), &Island::get_trail_points);

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

    // 栈桥：x 轴沿伸出方向
    const IslandDock &dock = field.dock;
    const Vector3 out(real_t(dock.out_x), 0, real_t(dock.out_z));
    const Basis pier_basis(out, Vector3(0, 1, 0), out.cross(Vector3(0, 1, 0)));
    const Vector3 pier_size(real_t(dock.pier_m + PIER_INLAND), real_t(PIER_THICKNESS), real_t(PIER_WIDTH));
    const Transform3D pier_xf(pier_basis, dock_local(0.5 * (dock.pier_m - PIER_INLAND), PIER_LIFT - 0.5 * PIER_THICKNESS));

    Ref<BoxMesh> pier_mesh;
    pier_mesh.instantiate();
    pier_mesh->set_size(pier_size);
    pier_mesh->set_material(make_material(Color(0.47f, 0.34f, 0.22f), false));
    MeshInstance3D *pier = memnew(MeshInstance3D);
    pier->set_mesh(pier_mesh);
    pier->set_transform(pier_xf);
    adopt(pier, "Pier");

    Ref<BoxShape3D> pier_shape;
    pier_shape.instantiate();
    pier_shape->set_size(pier_size);
    CollisionShape3D *pier_collider = memnew(CollisionShape3D);
    pier_collider->set_shape(pier_shape);
    pier_collider->set_transform(pier_xf);
    adopt(pier_collider, "PierShape");

    // 栈桥头的两根系缆桩（只是样子）
    Ref<CylinderMesh> post_mesh;
    post_mesh.instantiate();
    post_mesh->set_top_radius(0.12f);
    post_mesh->set_bottom_radius(0.14f);
    post_mesh->set_height(1.1f);
    post_mesh->set_material(make_material(Color(0.30f, 0.21f, 0.14f), false));
    for (int side = -1; side <= 1; side += 2) {
        MeshInstance3D *post = memnew(MeshInstance3D);
        post->set_mesh(post_mesh);
        post->set_position(dock_local(dock.pier_m - 0.4, PIER_LIFT + 0.55) + pier_basis.get_column(2) * real_t(side * (0.5 * PIER_WIDTH - 0.15)));
        adopt(post, side < 0 ? "PostL" : "PostR");
    }

    emit_signal("rebuilt");
}

Vector3 Island::dock_local(double p_along_out, double p_up) const {
    const IslandDock &dock = field.dock;
    return Vector3(real_t(dock.x + p_along_out * dock.out_x), real_t(dock.y + p_up), real_t(dock.z + p_along_out * dock.out_z));
}

Transform3D Island::get_berth_transform(double p_half_beam) const {
    const IslandDock &dock = field.dock;
    const Vector3 along_shore(real_t(-dock.out_z), 0, real_t(dock.out_x));
    const Transform3D local(Basis::looking_at(along_shore, Vector3(0, 1, 0)), dock_local(dock.pier_m + p_half_beam + BERTH_GAP, PIER_LIFT));
    return get_global_transform() * local;
}

Vector3 Island::get_pier_end_position() const {
    return get_global_transform().xform(dock_local(field.dock.pier_m, PIER_LIFT));
}

Transform3D Island::get_spawn_transform() const {
    const IslandDock &dock = field.dock;
    Vector3 pos = dock_local(-SPAWN_INLAND, 0.0);
    double ground = 0.0;
    if (field.sample_height(pos.x, pos.z, ground)) {
        pos.y = real_t(ground);
    }
    const Vector3 out(real_t(dock.out_x), 0, real_t(dock.out_z));
    return get_global_transform() * Transform3D(Basis::looking_at(out, Vector3(0, 1, 0)), pos);
}

PackedVector3Array Island::get_trail_points() const {
    PackedVector3Array points;
    const Transform3D xf = get_global_transform();
    for (const TrailPoint &point : field.trail) {
        points.push_back(xf.xform(Vector3(real_t(point.x), real_t(point.y), real_t(point.z))));
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
