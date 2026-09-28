#include "refit_overlay.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

namespace {

constexpr real_t CELL_L = real_t(CELL_LENGTH_M), CELL_W = real_t(CELL_WIDTH_M), CELL_H = real_t(CELL_HEIGHT_M);
constexpr real_t GLOW_SHRINK = 0.6f; // 亮格的盒子比格子小一圈，看得出是一格一格的
constexpr real_t EDGE_T = 0.12f;
constexpr double ELEVATION_MIN = -0.15, ELEVATION_MAX = 1.35;
constexpr double ZOOM_STEP = 1.15, ZOOM_MIN = 0.45, ZOOM_MAX = 2.2; // 滚轮一格；离船最近 / 最远是默认距离的几倍
constexpr float GLOW_ALPHA_LOW = 0.12f, GLOW_ALPHA_HIGH = 0.34f;

Ref<StandardMaterial3D> make_ghost(const Color &p_color) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_color);
    material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
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

// 一只单位盒子：大小靠变换里的缩放给
MeshInstance3D *make_unit_box(Node *p_parent, const Ref<StandardMaterial3D> &p_material) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(Vector3(1, 1, 1));
    mesh->set_material(p_material);
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_mesh(mesh);
    instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    p_parent->add_child(instance);
    return instance;
}

Transform3D box_xf(const Vector3 &p_center, const Vector3 &p_size) {
    return Transform3D(Basis().scaled(p_size), p_center);
}

} // namespace

void RefitOverlay::_bind_methods() {}

RefitOverlay::Frame RefitOverlay::make_frame(const char *p_name) {
    Frame f;
    f.root = memnew(Node3D);
    f.root->set_name(p_name);
    f.root->set_visible(false);
    add_child(f.root);
    f.fill = make_unit_box(f.root, mat_ok);
    for (MeshInstance3D *&e : f.edges) {
        e = make_unit_box(f.root, mat_edge_ok);
    }
    return f;
}

void RefitOverlay::place_frame(Frame &p_frame, const CellBox &p_box, real_t p_pad, const Ref<StandardMaterial3D> &p_fill, const Ref<StandardMaterial3D> &p_edge) {
    const Vector3 lo(real_t(p_box.x0) - p_pad, real_t(p_box.y0) - p_pad, real_t(p_box.z0) - p_pad);
    const Vector3 hi(real_t(p_box.x1) + p_pad, real_t(p_box.y1) + p_pad, real_t(p_box.z1) + p_pad);
    const Vector3 size = hi - lo, mid = 0.5f * (lo + hi), half = 0.5f * size;
    p_frame.root->set_visible(true);
    p_frame.fill->set_visible(p_fill.is_valid());
    if (p_fill.is_valid()) {
        p_frame.fill->set_transform(box_xf(mid, size));
        p_frame.fill->set_material_override(p_fill);
    }
    int k = 0;
    for (int a = -1; a <= 1; a += 2) {
        for (int b = -1; b <= 1; b += 2) {
            p_frame.edges[k++]->set_transform(box_xf(mid + Vector3(0, a * half.y, b * half.z), Vector3(size.x + EDGE_T, EDGE_T, EDGE_T)));
            p_frame.edges[k++]->set_transform(box_xf(mid + Vector3(a * half.x, b * half.y, 0), Vector3(EDGE_T, EDGE_T, size.z + EDGE_T)));
            p_frame.edges[k++]->set_transform(box_xf(mid + Vector3(a * half.x, 0, b * half.z), Vector3(EDGE_T, size.y + EDGE_T, EDGE_T)));
        }
    }
    for (MeshInstance3D *e : p_frame.edges) {
        e->set_material_override(p_edge);
    }
}

void RefitOverlay::ensure_built() {
    if (camera) {
        return;
    }
    mat_glow = make_ghost(Color(0.55f, 0.95f, 1.0f, GLOW_ALPHA_LOW));
    mat_ok = make_ghost(Color(0.85f, 1.0f, 0.45f, 0.34f));
    mat_bad = make_ghost(Color(1.0f, 0.35f, 0.3f, 0.3f));
    mat_selected = make_ghost(Color(1.0f, 1.0f, 1.0f, 0.1f));
    mat_edge_ok = make_edge(Color(0.9f, 1.0f, 0.5f));
    mat_edge_bad = make_edge(Color(1.0f, 0.4f, 0.35f));
    mat_edge_selected = make_edge(Color(1.0f, 1.0f, 1.0f));
    mat_edge_hover = make_edge(Color(0.75f, 0.78f, 0.8f, 0.8f));

    camera = memnew(Camera3D);
    camera->set_name("AssemblyCamera");
    camera->set_far(8000.0);
    add_child(camera);

    ghost = make_frame("Ghost");
    selection = make_frame("Selection");
    hovered = make_frame("Hovered");

    ghost_label = memnew(Label3D);
    ghost_label->set_name("GhostLabel");
    ghost_label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    ghost_label->set_font_size(96);
    ghost_label->set_pixel_size(0.012);
    ghost_label->set_outline_size(26);
    ghost_label->set_outline_modulate(Color(0.06f, 0.05f, 0.04f));
    ghost_label->set_draw_flag(Label3D::FLAG_DISABLE_DEPTH_TEST, true);
    ghost_label->set_visible(false);
    add_child(ghost_label);

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
    // 横移：焦点处一像素多少米 × 要挪的像素（取屏幕射线、投影都算上了这一移）
    if (camera->is_inside_tree()) {
        const real_t height = camera->get_viewport()->get_visible_rect().size.y;
        const real_t per_pixel = height > 0 ? 2.0f * d * std::tan(real_t(Math::deg_to_rad(camera->get_fov())) * 0.5f) / height : 0.0f;
        camera->set_h_offset(-real_t(screen_shift) * per_pixel);
    }
}

void RefitOverlay::set_screen_shift(double p_pixels) {
    if (std::abs(p_pixels - screen_shift) < 0.5) {
        return;
    }
    screen_shift = p_pixels;
    if (camera) {
        place_camera();
    }
}

void RefitOverlay::show_for(const ShipLayout &p_layout, const Transform3D &p_ship) {
    ensure_built();
    set_global_transform(p_ship);
    orbit_yaw = 0.7;
    orbit_elevation = 0.45;
    // 按船能长到的最大尺寸取距离：改船时相机不跟着跳
    const real_t length = real_t((2 * MAX_Z + 1) * CELL_LENGTH_M), width = real_t((2 * MAX_X + 1) * CELL_WIDTH_M), height = real_t(MAX_LEVELS * CELL_HEIGHT_M);
    base_distance = std::max(30.0, 0.8 * std::sqrt(double(length * length + width * width + height * height)));
    orbit_distance = base_distance;
    focus = Vector3(0.5f * real_t(port_edge_x(p_layout) + starboard_edge_x(p_layout)), 0.5f * CELL_H, 0.5f * real_t(bow_z(p_layout) + stern_z(p_layout)));
    place_camera();
}

void RefitOverlay::refresh(const View &p_view) {
    ensure_built();
    set_global_transform(p_view.ship);

    // 亮格：一格一只盒子，一呼一吸
    while (glows.size() < p_view.glow.size()) {
        glows.push_back(make_unit_box(this, mat_glow));
    }
    for (size_t k = 0; k < glows.size(); k++) {
        const bool used = k < p_view.glow.size();
        glows[k]->set_visible(used);
        if (used) {
            const CellBox b = cell_box(p_view.glow[k]);
            const Vector3 mid(real_t(0.5 * (b.x0 + b.x1)), real_t(0.5 * (b.y0 + b.y1)), real_t(0.5 * (b.z0 + b.z1)));
            glows[k]->set_transform(box_xf(mid, Vector3(CELL_W - GLOW_SHRINK, CELL_H - GLOW_SHRINK, CELL_L - GLOW_SHRINK)));
        }
    }
    Color glow = mat_glow->get_albedo();
    glow.a = GLOW_ALPHA_LOW + (GLOW_ALPHA_HIGH - GLOW_ALPHA_LOW) * float(p_view.pulse);
    mat_glow->set_albedo(glow);

    // 虚影
    ghost.root->set_visible(p_view.ghost);
    ghost_label->set_visible(p_view.ghost && !p_view.ghost_text.is_empty());
    if (p_view.ghost) {
        place_frame(ghost, p_view.ghost_box, 0.05f, p_view.ghost_legal ? mat_ok : mat_bad, p_view.ghost_legal ? mat_edge_ok : mat_edge_bad);
        ghost_label->set_text(p_view.ghost_text);
        ghost_label->set_modulate(p_view.ghost_legal ? Color(0.95f, 1.0f, 0.8f) : Color(1.0f, 0.62f, 0.55f));
        ghost_label->set_position(Vector3(real_t(0.5 * (p_view.ghost_box.x0 + p_view.ghost_box.x1)), real_t(p_view.ghost_box.y1) + 1.6f,
                real_t(0.5 * (p_view.ghost_box.z0 + p_view.ghost_box.z1))));
    }

    // 选中框、指着的节点
    selection.root->set_visible(p_view.selected);
    if (p_view.selected) {
        place_frame(selection, p_view.selected_box, 0.15f, mat_selected, mat_edge_selected);
    }
    hovered.root->set_visible(p_view.hovered);
    if (p_view.hovered) {
        place_frame(hovered, p_view.hovered_box, 0.1f, Ref<StandardMaterial3D>(), mat_edge_hover);
    }

    // 标签：贴着船的外廓
    if (p_view.layout) {
        const ShipLayout &layout = *p_view.layout;
        const real_t bow = real_t(bow_z(layout)), stern = real_t(stern_z(layout));
        const real_t port = real_t(port_edge_x(layout)), starboard = real_t(starboard_edge_x(layout));
        const real_t mid_x = 0.5f * (port + starboard), mid_z = 0.5f * (bow + stern);
        labels[0]->set_position(Vector3(mid_x, 1.8f, bow - 3.0f));
        labels[1]->set_position(Vector3(mid_x, 1.8f, stern + 3.0f));
        labels[2]->set_position(Vector3(port - 3.0f, 1.8f, mid_z));
        labels[3]->set_position(Vector3(starboard + 3.0f, 1.8f, mid_z));
    }
    place_camera();
}

void RefitOverlay::orbit(double p_yaw_delta, double p_elevation_delta) {
    orbit_yaw += p_yaw_delta;
    orbit_elevation = std::clamp(orbit_elevation + p_elevation_delta, ELEVATION_MIN, ELEVATION_MAX);
    if (camera) {
        place_camera();
    }
}

void RefitOverlay::zoom(bool p_in) {
    orbit_distance = std::clamp(p_in ? orbit_distance / ZOOM_STEP : orbit_distance * ZOOM_STEP, ZOOM_MIN * base_distance, ZOOM_MAX * base_distance);
    if (camera) {
        place_camera();
    }
}

PickRay RefitOverlay::ray_at(const Vector2 &p_screen) const {
    PickRay ray;
    if (!camera) {
        return ray;
    }
    const Vector3 origin = to_local(camera->project_ray_origin(p_screen));
    const Vector3 dir = get_global_transform().basis.xform_inv(camera->project_ray_normal(p_screen)).normalized();
    ray.ox = origin.x;
    ray.oy = origin.y;
    ray.oz = origin.z;
    ray.dx = dir.x;
    ray.dy = dir.y;
    ray.dz = dir.z;
    return ray;
}

bool RefitOverlay::screen_point(const Vector3 &p_local, Vector2 &r_screen) const {
    if (!camera) {
        return false;
    }
    const Vector3 global = to_global(p_local);
    if (camera->is_position_behind(global)) {
        return false;
    }
    r_screen = camera->unproject_position(global);
    return true;
}
