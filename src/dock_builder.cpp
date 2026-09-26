#include "dock_builder.h"

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include "curved_material.h"

using namespace godot;

namespace dock_builder {

namespace {

Ref<Material> flat_material(const Color &p_albedo) {
    return curved_material::solid(p_albedo, 1.0f);
}

} // namespace

Vector3 point(const DockPlan &p_plan, double p_along_out, double p_up) {
    double x = 0.0, y = 0.0, z = 0.0;
    dock_point(p_plan, p_along_out, p_up, x, y, z);
    return Vector3(real_t(x), real_t(y), real_t(z));
}

Transform3D berth_contact(const DockPlan &p_plan) {
    const Vector3 toward_pier(real_t(-p_plan.out_x), 0, real_t(-p_plan.out_z));
    const Vector3 up(0, 1, 0);
    return Transform3D(Basis(toward_pier, up, toward_pier.cross(up)), point(p_plan, p_plan.pier_m + BERTH_GAP, PIER_LIFT));
}

Vector3 pier_end(const DockPlan &p_plan) {
    return point(p_plan, p_plan.pier_m, PIER_LIFT);
}

Transform3D spawn(const DockPlan &p_plan) {
    const Vector3 out(real_t(p_plan.out_x), 0, real_t(p_plan.out_z));
    return Transform3D(Basis::looking_at(out, Vector3(0, 1, 0)), point(p_plan, -SPAWN_INLAND, 0.0));
}

void build_pier(Node3D *p_parent, const DockPlan &p_plan, Adopt p_adopt) {
    // 栈桥：x 轴沿伸出方向
    const Vector3 out(real_t(p_plan.out_x), 0, real_t(p_plan.out_z));
    const Basis pier_basis(out, Vector3(0, 1, 0), out.cross(Vector3(0, 1, 0)));
    const Vector3 pier_size(real_t(p_plan.pier_m + PIER_INLAND), real_t(PIER_THICKNESS), real_t(PIER_WIDTH));
    const Transform3D pier_xf(pier_basis, point(p_plan, 0.5 * (p_plan.pier_m - PIER_INLAND), PIER_LIFT - 0.5 * PIER_THICKNESS));

    Ref<BoxMesh> pier_mesh;
    pier_mesh.instantiate();
    pier_mesh->set_size(pier_size);
    pier_mesh->set_material(flat_material(Color(0.47f, 0.34f, 0.22f)));
    MeshInstance3D *pier = memnew(MeshInstance3D);
    pier->set_mesh(pier_mesh);
    pier->set_transform(pier_xf);
    p_adopt(p_parent, pier, "Pier");

    Ref<BoxShape3D> pier_shape;
    pier_shape.instantiate();
    pier_shape->set_size(pier_size);
    CollisionShape3D *pier_collider = memnew(CollisionShape3D);
    pier_collider->set_shape(pier_shape);
    pier_collider->set_transform(pier_xf);
    p_adopt(p_parent, pier_collider, "PierShape");

    // 栈桥头的两根系缆桩（只是样子）
    Ref<CylinderMesh> post_mesh;
    post_mesh.instantiate();
    post_mesh->set_top_radius(0.12f);
    post_mesh->set_bottom_radius(0.14f);
    post_mesh->set_height(1.1f);
    post_mesh->set_material(flat_material(Color(0.30f, 0.21f, 0.14f)));
    for (int side = -1; side <= 1; side += 2) {
        MeshInstance3D *post = memnew(MeshInstance3D);
        post->set_mesh(post_mesh);
        post->set_position(point(p_plan, p_plan.pier_m - 0.4, PIER_LIFT + 0.55) + pier_basis.get_column(2) * real_t(side * (0.5 * PIER_WIDTH - 0.15)));
        p_adopt(p_parent, post, side < 0 ? "PostL" : "PostR");
    }
}

} // namespace dock_builder
