#include "interior_overlay.h"
#include "art_mesh.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

constexpr double ELEVATION_MIN = 0.2, ELEVATION_MAX = 1.5;
constexpr double DISTANCE_MIN = 4.0, DISTANCE_MAX = 60.0;
constexpr real_t CURSOR_SIZE = 0.5f;

Ref<StandardMaterial3D> make_ghost(const Color &p_color, bool p_on_top) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_color);
    material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    if (p_on_top) {
        material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true); // 光标压在家具下面也看得见
        material->set_render_priority(2);
    }
    return material;
}

bool same_boxes(const std::vector<BoxPart> &p_a, const std::vector<BoxPart> &p_b) {
    if (p_a.size() != p_b.size()) {
        return false;
    }
    for (size_t k = 0; k < p_a.size(); k++) {
        const BoxPart &a = p_a[k], &b = p_b[k];
        if (a.sx != b.sx || a.sy != b.sy || a.sz != b.sz || a.x != b.x || a.y != b.y || a.z != b.z || a.yaw != b.yaw || a.pitch != b.pitch || a.shape!=b.shape) {
            return false;
        }
    }
    return true;
}

} // namespace

void InteriorOverlay::_bind_methods() {}

void InteriorOverlay::ensure_built() {
    if (camera) {
        return;
    }
    mat_ok = make_ghost(Color(0.45f, 0.95f, 0.5f, 0.45f), false);
    mat_bad = make_ghost(Color(1.0f, 0.35f, 0.3f, 0.45f), false);
    mat_selected = make_ghost(Color(1.0f, 1.0f, 1.0f, 0.35f), false);
    mat_cursor = make_ghost(Color(1.0f, 0.95f, 0.4f, 0.9f), true);

    camera = memnew(Camera3D);
    camera->set_name("InteriorCamera");
    camera->set_far(4000.0);
    add_child(camera);
    frame = memnew(Node3D);
    frame->set_name("Frame");
    add_child(frame);
    ghost = memnew(Node3D);
    ghost->set_name("Ghost");
    frame->add_child(ghost);
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(Vector3(CURSOR_SIZE, 0.04f, CURSOR_SIZE));
    MeshInstance3D *mark = memnew(MeshInstance3D);
    mark->set_name("Cursor");
    mark->set_mesh(mesh);
    mark->set_material_override(mat_cursor);
    mark->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    frame->add_child(mark);
    cursor = mark;
}

void InteriorOverlay::place_camera() {
    const real_t e = real_t(orbit_elevation), y = real_t(orbit_yaw), d = real_t(orbit_distance);
    const Vector3 offset(std::cos(e) * std::sin(y), std::sin(e), std::cos(e) * std::cos(y));
    camera->set_global_position(focus + offset * d);
    camera->look_at(focus, Vector3(0, 1, 0));
}

void InteriorOverlay::show_at(const Transform3D &p_building, const Vector3 &p_focus) {
    ensure_built();
    set_global_transform(Transform3D());
    frame->set_global_transform(p_building);
    focus = p_focus;
    orbit_yaw = p_building.basis.get_euler().y + 0.4; // 从门那一侧斜着看进去
    orbit_elevation = 1.05;
    orbit_distance = 16.0;
    place_camera();
}

void InteriorOverlay::refresh(const Transform3D &p_building, const Vector3 &p_focus, const Vector3 &p_cursor, const std::vector<BoxPart> &p_ghost, GhostState p_state) {
    ensure_built();
    frame->set_global_transform(p_building);
    focus = p_focus;
    cursor->set_position(p_cursor + Vector3(0, 0.03f, 0));
    if (int(p_state) == shown_state && same_boxes(p_ghost, shown)) {
        place_camera();
        return;
    }
    shown = p_ghost;
    shown_state = int(p_state);
    const TypedArray<Node> old = ghost->get_children();
    for (int64_t k = 0; k < old.size(); k++) {
        Node *child = Object::cast_to<Node>(old[k]);
        ghost->remove_child(child);
        child->queue_free();
    }
    const Ref<StandardMaterial3D> fill = p_state == GHOST_CAN_PLACE ? mat_ok : (p_state == GHOST_CANNOT_PLACE ? mat_bad : mat_selected);
    for(const BoxPart &part:p_ghost){
        if(!part.visual)continue;
        MeshInstance3D *instance=memnew(MeshInstance3D);
        instance->set_mesh(art_mesh::shape(part,fill));instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);ghost->add_child(instance);
    }
    place_camera();
}

void InteriorOverlay::orbit(double p_yaw_delta, double p_elevation_delta) {
    orbit_yaw += p_yaw_delta;
    orbit_elevation = std::clamp(orbit_elevation + p_elevation_delta, ELEVATION_MIN, ELEVATION_MAX);
    if (camera) {
        place_camera();
    }
}

void InteriorOverlay::zoom(double p_factor) {
    orbit_distance = std::clamp(orbit_distance * p_factor, DISTANCE_MIN, DISTANCE_MAX);
    if (camera) {
        place_camera();
    }
}

Vector3 InteriorOverlay::get_forward() const {
    return Vector3(real_t(-std::sin(orbit_yaw)), 0, real_t(-std::cos(orbit_yaw)));
}

Vector3 InteriorOverlay::get_right() const {
    return Vector3(real_t(std::cos(orbit_yaw)), 0, real_t(-std::sin(orbit_yaw)));
}
