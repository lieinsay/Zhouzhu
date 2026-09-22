#include "build_overlay.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/prism_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

namespace {

constexpr double ELEVATION_MIN = 0.15, ELEVATION_MAX = 1.45;
constexpr double DISTANCE_MIN = 8.0, DISTANCE_MAX = 160.0;
constexpr real_t EDGE_T = 0.12f;
constexpr real_t PLINTH_MARGIN = 0.4f;
constexpr real_t ROOF_OVERHANG = 0.5f;

Ref<StandardMaterial3D> make_ghost(const Color &p_color) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_color);
    material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    return material;
}

Ref<StandardMaterial3D> make_edge(const Color &p_color) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_color);
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true); // 描边穿透地形也看得见
    material->set_render_priority(2);
    return material;
}

MeshInstance3D *make_box(Node *p_parent, const String &p_name, const Vector3 &p_size, const Transform3D &p_xf) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(p_size);
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_name(p_name);
    instance->set_mesh(mesh);
    instance->set_transform(p_xf);
    instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    p_parent->add_child(instance);
    return instance;
}

} // namespace

void BuildOverlay::_bind_methods() {}

void BuildOverlay::ensure_built() {
    if (camera) {
        return;
    }
    mat_ok = make_ghost(Color(0.45f, 0.95f, 0.5f, 0.32f));
    mat_bad = make_ghost(Color(1.0f, 0.35f, 0.3f, 0.32f));
    mat_selected = make_ghost(Color(1.0f, 1.0f, 1.0f, 0.18f));
    mat_edge_ok = make_edge(Color(0.55f, 1.0f, 0.6f));
    mat_edge_bad = make_edge(Color(1.0f, 0.4f, 0.35f));
    mat_edge_selected = make_edge(Color(1.0f, 1.0f, 1.0f));

    camera = memnew(Camera3D);
    camera->set_name("BuildCamera");
    camera->set_far(8000.0);
    add_child(camera);

    ghost = memnew(Node3D);
    ghost->set_name("Ghost");
    add_child(ghost);
}

// 幽灵：台基（只画露出来的那截）+ 墙 + 悬山顶，加地坪一圈四条描边
void BuildOverlay::rebuild_ghost(const BuildingDef &p_def) {
    const TypedArray<Node> children = ghost->get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        Node *child = Object::cast_to<Node>(children[k]);
        ghost->remove_child(child);
        child->queue_free();
    }
    ghost_def = &p_def;
    const real_t w = real_t(p_def.width_m), d = real_t(p_def.depth_m), plinth = real_t(p_def.plinth_m), wall = real_t(p_def.wall_m), roof = real_t(p_def.roof_m);
    make_box(ghost, "Plinth", Vector3(w + 2 * PLINTH_MARGIN, plinth, d + 2 * PLINTH_MARGIN), Transform3D(Basis(), Vector3(0, -0.5f * plinth, 0)));
    make_box(ghost, "Walls", Vector3(w, wall, d), Transform3D(Basis(), Vector3(0, 0.5f * wall, 0)));
    Ref<PrismMesh> prism;
    prism.instantiate();
    prism->set_left_to_right(0.5f);
    prism->set_size(Vector3(d + 2 * ROOF_OVERHANG, roof, w + 2 * ROOF_OVERHANG));
    MeshInstance3D *roof_instance = memnew(MeshInstance3D);
    roof_instance->set_name("Roof");
    roof_instance->set_mesh(prism);
    roof_instance->set_transform(Transform3D(Basis(Vector3(0, 1, 0), real_t(Math_PI / 2)), Vector3(0, wall + 0.5f * roof, 0)));
    roof_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    ghost->add_child(roof_instance);
    // 门的位置画一根竖条，转向时看得出正面朝哪
    make_box(ghost, "DoorMark", Vector3(1.1f, 2.0f, EDGE_T), Transform3D(Basis(), Vector3(0, 1.0f, 0.5f * d + 0.1f)));
    // 占地描边：四条棱，贴地坪
    const real_t hw = 0.5f * w + PLINTH_MARGIN, hd = 0.5f * d + PLINTH_MARGIN;
    make_box(ghost, "EdgeFront", Vector3(2 * hw + EDGE_T, EDGE_T, EDGE_T), Transform3D(Basis(), Vector3(0, 0, hd)))->set_name("Edge0");
    make_box(ghost, "EdgeBack", Vector3(2 * hw + EDGE_T, EDGE_T, EDGE_T), Transform3D(Basis(), Vector3(0, 0, -hd)))->set_name("Edge1");
    make_box(ghost, "EdgeLeft", Vector3(EDGE_T, EDGE_T, 2 * hd + EDGE_T), Transform3D(Basis(), Vector3(-hw, 0, 0)))->set_name("Edge2");
    make_box(ghost, "EdgeRight", Vector3(EDGE_T, EDGE_T, 2 * hd + EDGE_T), Transform3D(Basis(), Vector3(hw, 0, 0)))->set_name("Edge3");
}

void BuildOverlay::place_camera() {
    const real_t e = real_t(orbit_elevation), y = real_t(orbit_yaw), d = real_t(orbit_distance);
    const Vector3 offset(std::cos(e) * std::sin(y), std::sin(e), std::cos(e) * std::cos(y));
    camera->set_global_position(focus + offset * d);
    camera->look_at(focus, Vector3(0, 1, 0));
}

void BuildOverlay::show_at(const Vector3 &p_focus) {
    ensure_built();
    set_global_transform(Transform3D());
    focus = p_focus;
    orbit_yaw = 0.6;
    orbit_elevation = 0.7;
    orbit_distance = 30.0;
    place_camera();
}

void BuildOverlay::refresh(const Vector3 &p_focus, const BuildingDef &p_def, const Transform3D &p_ghost, GhostState p_state) {
    ensure_built();
    focus = p_focus;
    if (ghost_def != &p_def) {
        rebuild_ghost(p_def);
    }
    ghost->set_global_transform(p_ghost);
    const Ref<StandardMaterial3D> fill = p_state == GHOST_CAN_PLACE ? mat_ok : (p_state == GHOST_CANNOT_PLACE ? mat_bad : mat_selected);
    const Ref<StandardMaterial3D> edge = p_state == GHOST_CAN_PLACE ? mat_edge_ok : (p_state == GHOST_CANNOT_PLACE ? mat_edge_bad : mat_edge_selected);
    const TypedArray<Node> children = ghost->get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        MeshInstance3D *part = Object::cast_to<MeshInstance3D>(children[k]);
        if (part) {
            part->set_material_override(part->get_name().begins_with("Edge") || part->get_name() == StringName("DoorMark") ? edge : fill);
        }
    }
    place_camera();
}

void BuildOverlay::orbit(double p_yaw_delta, double p_elevation_delta) {
    orbit_yaw += p_yaw_delta;
    orbit_elevation = std::clamp(orbit_elevation + p_elevation_delta, ELEVATION_MIN, ELEVATION_MAX);
    if (camera) {
        place_camera();
    }
}

void BuildOverlay::zoom(double p_factor) {
    orbit_distance = std::clamp(orbit_distance * p_factor, DISTANCE_MIN, DISTANCE_MAX);
    if (camera) {
        place_camera();
    }
}

Vector3 BuildOverlay::get_forward() const {
    // 相机在焦点的 (sin y, ·, cos y) 方向上，看向焦点：水平前方就是反过来
    return Vector3(real_t(-std::sin(orbit_yaw)), 0, real_t(-std::cos(orbit_yaw)));
}

Vector3 BuildOverlay::get_right() const {
    return Vector3(real_t(std::cos(orbit_yaw)), 0, real_t(-std::sin(orbit_yaw)));
}
