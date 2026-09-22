#include "refit_overlay.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

constexpr real_t CELL_L = real_t(CELL_LENGTH_M), CELL_W = real_t(CELL_WIDTH_M), CELL_H = real_t(CELL_HEIGHT_M);
constexpr real_t CANDIDATE_SHRINK = 0.5f; // 候选格的盒子比格子小一圈，看得出是一格一格的
constexpr real_t EDGE_T = 0.1f;
constexpr double ELEVATION_MIN = -0.15, ELEVATION_MAX = 1.35;

Ref<StandardMaterial3D> make_ghost(const Color &p_color, bool p_unshaded) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_color);
    material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    if (p_unshaded) {
        material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    }
    material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
    return material;
}

Ref<StandardMaterial3D> make_edge(const Color &p_color) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_color);
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true); // 描边穿透船身也看得见
    material->set_render_priority(2);
    return material;
}

MeshInstance3D *make_box(Node *p_parent, const Vector3 &p_size, const Ref<StandardMaterial3D> &p_material) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(p_size);
    mesh->set_material(p_material);
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_mesh(mesh);
    instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    p_parent->add_child(instance);
    return instance;
}

Vector3 cell_center(const Cell &c) {
    return Vector3(real_t(cell_center_x(c.x)), real_t(cell_deck_y(c.y)) + 0.5f * CELL_H, real_t(cell_center_z(c.z)));
}

} // namespace

void RefitOverlay::_bind_methods() {}

void RefitOverlay::ensure_built() {
    if (camera) {
        return;
    }
    mat_candidate = make_ghost(Color(0.45f, 0.75f, 1.0f, 0.16f), true);
    mat_ok = make_ghost(Color(1.0f, 0.9f, 0.4f, 0.28f), true);
    mat_bad = make_ghost(Color(1.0f, 0.35f, 0.3f, 0.28f), true);
    mat_selected = make_ghost(Color(1.0f, 1.0f, 1.0f, 0.12f), true);
    mat_edge_ok = make_edge(Color(1.0f, 0.92f, 0.45f));
    mat_edge_bad = make_edge(Color(1.0f, 0.4f, 0.35f));
    mat_edge_selected = make_edge(Color(1.0f, 1.0f, 1.0f));

    camera = memnew(Camera3D);
    camera->set_name("AssemblyCamera");
    camera->set_far(8000.0);
    add_child(camera);

    cursor = memnew(Node3D);
    cursor->set_name("Cursor");
    add_child(cursor);
    cursor_fill = make_box(cursor, Vector3(CELL_W, CELL_H, CELL_L), mat_ok);
    // 十二条棱
    const real_t hx = 0.5f * CELL_W, hy = 0.5f * CELL_H, hz = 0.5f * CELL_L;
    for (int sy = -1; sy <= 1; sy += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            MeshInstance3D *e = make_box(cursor, Vector3(CELL_W + EDGE_T, EDGE_T, EDGE_T), mat_edge_ok);
            e->set_position(Vector3(0, sy * hy, sz * hz));
            cursor_edges.push_back(e);
        }
        for (int sx = -1; sx <= 1; sx += 2) {
            MeshInstance3D *e = make_box(cursor, Vector3(EDGE_T, EDGE_T, CELL_L + EDGE_T), mat_edge_ok);
            e->set_position(Vector3(sx * hx, sy * hy, 0));
            cursor_edges.push_back(e);
        }
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            MeshInstance3D *e = make_box(cursor, Vector3(EDGE_T, CELL_H + EDGE_T, EDGE_T), mat_edge_ok);
            e->set_position(Vector3(sx * hx, 0, sz * hz));
            cursor_edges.push_back(e);
        }
    }

    const char32_t *texts[4] = { U"船头", U"船尾", U"左舷", U"右舷" };
    for (int k = 0; k < 4; k++) {
        Label3D *label = memnew(Label3D);
        label->set_text(String(texts[k]));
        label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
        label->set_font_size(110);
        label->set_pixel_size(0.012);
        label->set_outline_size(28);
        label->set_modulate(Color(0.95f, 0.92f, 0.8f));
        label->set_outline_modulate(Color(0.08f, 0.06f, 0.04f));
        label->set_draw_flag(Label3D::FLAG_DISABLE_DEPTH_TEST, true);
        add_child(label);
        labels[k] = label;
    }
}

void RefitOverlay::place_camera() {
    const real_t e = real_t(orbit_elevation), y = real_t(orbit_yaw), d = real_t(orbit_distance);
    const Vector3 offset(std::cos(e) * std::sin(y), std::sin(e), std::cos(e) * std::cos(y));
    camera->set_position(focus + offset * d);
    camera->look_at(to_global(focus), Vector3(0, 1, 0));
}

void RefitOverlay::show_for(const ShipLayout &p_layout, const Transform3D &p_ship) {
    ensure_built();
    set_global_transform(p_ship);
    orbit_yaw = 0.7;
    orbit_elevation = 0.45;
    const real_t length = real_t(stern_z(p_layout) - bow_z(p_layout)) + 2.0f * CELL_L;
    const real_t width = real_t(starboard_edge_x(p_layout) - port_edge_x(p_layout)) + 2.0f * CELL_W;
    const real_t height = real_t(cell_deck_y(p_layout.levels())) + CELL_H;
    orbit_distance = std::max(24.0, 1.15 * std::sqrt(double(length * length + width * width + height * height)));
    focus = Vector3(0.5f * real_t(port_edge_x(p_layout) + starboard_edge_x(p_layout)), 0.5f * real_t(cell_deck_y(p_layout.levels())), 0.5f * real_t(bow_z(p_layout) + stern_z(p_layout)));
    place_camera();
}

void RefitOverlay::refresh(const ShipLayout &p_layout, const Transform3D &p_ship, const Cell &p_cursor, CursorState p_state, const std::vector<Cell> &p_candidates) {
    ensure_built();
    set_global_transform(p_ship);

    while (candidates.size() < p_candidates.size()) {
        candidates.push_back(make_box(this, Vector3(CELL_W - CANDIDATE_SHRINK, CELL_H - CANDIDATE_SHRINK, CELL_L - CANDIDATE_SHRINK), mat_candidate));
    }
    for (size_t k = 0; k < candidates.size(); k++) {
        const bool used = k < p_candidates.size();
        candidates[k]->set_visible(used);
        if (used) {
            candidates[k]->set_position(cell_center(p_candidates[k]));
        }
    }

    cursor->set_position(cell_center(p_cursor));
    const Ref<StandardMaterial3D> fill = p_state == CURSOR_CAN_PLACE ? mat_ok : (p_state == CURSOR_CANNOT_PLACE ? mat_bad : mat_selected);
    const Ref<StandardMaterial3D> edge = p_state == CURSOR_CAN_PLACE ? mat_edge_ok : (p_state == CURSOR_CANNOT_PLACE ? mat_edge_bad : mat_edge_selected);
    cursor_fill->set_material_override(fill);
    for (MeshInstance3D *e : cursor_edges) {
        e->set_material_override(edge);
    }

    // 标签：贴着船的外廓
    const real_t bow = real_t(bow_z(p_layout)), stern = real_t(stern_z(p_layout));
    const real_t port = real_t(port_edge_x(p_layout)), starboard = real_t(starboard_edge_x(p_layout));
    const real_t mid_x = 0.5f * (port + starboard), mid_z = 0.5f * (bow + stern);
    labels[0]->set_position(Vector3(mid_x, 1.5f, bow - 2.5f));
    labels[1]->set_position(Vector3(mid_x, 1.5f, stern + 2.5f));
    labels[2]->set_position(Vector3(port - 2.5f, 1.5f, mid_z));
    labels[3]->set_position(Vector3(starboard + 2.5f, 1.5f, mid_z));
    place_camera();
}

void RefitOverlay::orbit(double p_yaw_delta, double p_elevation_delta) {
    orbit_yaw += p_yaw_delta;
    orbit_elevation = std::clamp(orbit_elevation + p_elevation_delta, ELEVATION_MIN, ELEVATION_MAX);
    if (camera) {
        place_camera();
    }
}
