#include "placed_props.h"

#include <cmath>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

const Color WOOD(0.55f, 0.39f, 0.23f);
const Color WOOD_DARK(0.4f, 0.28f, 0.17f);
const Color WOOD_LIGHT(0.72f, 0.56f, 0.36f);
const Color CLAY(0.62f, 0.4f, 0.28f);
const Color CLAY_DARK(0.45f, 0.28f, 0.2f);
const Color IRON(0.25f, 0.24f, 0.23f);
const Color STONE(0.5f, 0.49f, 0.47f);
const Color FLAME(1.0f, 0.62f, 0.2f);

Ref<StandardMaterial3D> solid(const Color &p_color, float p_roughness = 0.9f) {
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_albedo(p_color);
    m->set_roughness(p_roughness);
    return m;
}

Ref<StandardMaterial3D> glowing(const Color &p_color, float p_energy) {
    Ref<StandardMaterial3D> m = solid(p_color, 1.0f);
    m->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
    m->set_emission(p_color);
    m->set_emission_energy_multiplier(p_energy);
    return m;
}

// 画一件：p_override 不为空时所有部件都用它（虚影）
class Builder {
    Node3D *root;
    Ref<Material> override_material;

    void add(const Ref<PrimitiveMesh> &p_mesh, const Vector3 &p_at, const Ref<Material> &p_material) {
        p_mesh->set_material(override_material.is_valid() ? override_material : p_material);
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_mesh(p_mesh);
        mi->set_position(p_at);
        mi->set_cast_shadows_setting(override_material.is_valid() ? GeometryInstance3D::SHADOW_CASTING_SETTING_OFF : GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
        root->add_child(mi);
    }

public:
    Builder(Node3D *p_root, const Ref<Material> &p_override) :
            root(p_root), override_material(p_override) {}

    void box(const Vector3 &p_size, const Vector3 &p_at, const Ref<Material> &p_material) {
        Ref<BoxMesh> mesh;
        mesh.instantiate();
        mesh->set_size(p_size);
        add(mesh, p_at, p_material);
    }
    void box(const Vector3 &p_size, const Vector3 &p_at, const Color &p_color) { box(p_size, p_at, solid(p_color)); }
    void cylinder(double p_radius, double p_height, const Vector3 &p_at, const Color &p_color, double p_top = -1.0) {
        Ref<CylinderMesh> mesh;
        mesh.instantiate();
        mesh->set_bottom_radius(real_t(p_radius));
        mesh->set_top_radius(real_t(p_top < 0.0 ? p_radius : p_top));
        mesh->set_height(real_t(p_height));
        mesh->set_radial_segments(12);
        add(mesh, p_at, solid(p_color));
    }
    void sphere(double p_radius, double p_height, const Vector3 &p_at, const Ref<Material> &p_material) {
        Ref<SphereMesh> mesh;
        mesh.instantiate();
        mesh->set_radius(real_t(p_radius));
        mesh->set_height(real_t(p_height));
        mesh->set_radial_segments(16);
        mesh->set_rings(8);
        add(mesh, p_at, p_material);
    }
    void sphere(double p_radius, double p_height, const Vector3 &p_at, const Color &p_color) { sphere(p_radius, p_height, p_at, solid(p_color)); }
    bool is_ghost() const { return override_material.is_valid(); }
};

Transform3D pose_of(double p_x, double p_y, double p_z, int p_quarter, double p_yaw = 0.0) {
    return Transform3D(Basis(Vector3(0, 1, 0), real_t(quarter_yaw(p_quarter) + p_yaw)), Vector3(real_t(p_x), real_t(p_y), real_t(p_z)));
}

} // namespace

void build_prop_model(Node3D *p_root, const PropDef &p_def, double p_lift, const Ref<Material> &p_override) {
    Builder b(p_root, p_override);
    const real_t w = real_t(p_def.width), d = real_t(p_def.depth), h = real_t(p_def.height);
    // 地不平：底下垫一块，垫到最低处
    if (p_lift > 0.03) {
        const real_t lift = real_t(p_lift) + 0.05f;
        b.box(Vector3(w, lift, d), Vector3(0, -lift * 0.5f + 0.02f, 0), p_def.look == PropLook::KILN ? STONE : WOOD_DARK);
    }
    switch (p_def.look) {
        case PropLook::WORKBENCH: {
            b.box(Vector3(w, 0.12f, d), Vector3(0, h - 0.06f, 0), WOOD_LIGHT);
            for (int sx : { -1, 1 }) {
                for (int sz : { -1, 1 }) {
                    b.box(Vector3(0.1f, h - 0.12f, 0.1f), Vector3(sx * (w * 0.5f - 0.1f), (h - 0.12f) * 0.5f, sz * (d * 0.5f - 0.1f)), WOOD_DARK);
                }
            }
            b.box(Vector3(w - 0.2f, 0.05f, d - 0.2f), Vector3(0, 0.25f, 0), WOOD);
            b.box(Vector3(0.36f, 0.05f, 0.22f), Vector3(-0.3f, h + 0.025f, 0.05f), WOOD); // 案上一块刨好的板
            b.box(Vector3(0.05f, 0.18f, 0.05f), Vector3(0.4f, h + 0.09f, -0.1f), IRON); // 一把凿子立着
        } break;
        case PropLook::KILN: {
            b.box(Vector3(w, 0.4f, d), Vector3(0, 0.2f, 0), CLAY_DARK);
            b.sphere(w * 0.46f, (h - 0.6f) * 2.0f, Vector3(0, 0.4f, 0), CLAY);
            b.cylinder(0.14, 0.45, Vector3(0, h - 0.2f, -0.15f), CLAY_DARK, 0.1);
            b.box(Vector3(0.5f, 0.45f, 0.12f), Vector3(0, 0.62f, d * 0.5f - 0.02f), IRON); // 火口
            if (!b.is_ghost()) {
                b.box(Vector3(0.36f, 0.3f, 0.04f), Vector3(0, 0.58f, d * 0.5f + 0.05f), glowing(FLAME, 1.5f));
            }
        } break;
        case PropLook::CHEST: {
            b.box(Vector3(w, h * 0.72f, d), Vector3(0, h * 0.36f, 0), WOOD);
            b.box(Vector3(w + 0.03f, h * 0.24f, d + 0.03f), Vector3(0, h * 0.84f, 0), WOOD_LIGHT);
            for (int s : { -1, 1 }) {
                b.box(Vector3(0.06f, h + 0.01f, d + 0.05f), Vector3(s * w * 0.33f, h * 0.5f, 0), IRON);
            }
            b.box(Vector3(0.12f, 0.14f, 0.04f), Vector3(0, h * 0.7f, d * 0.5f + 0.03f), IRON); // 锁扣
        } break;
        case PropLook::TORCH: {
            b.cylinder(0.05, h - 0.2f, Vector3(0, (h - 0.2f) * 0.5f, 0), WOOD_DARK);
            b.cylinder(0.09, 0.22, Vector3(0, h - 0.12f, 0), Color(0.22f, 0.16f, 0.1f), 0.07);
            if (!b.is_ghost()) {
                b.sphere(0.13, 0.34, Vector3(0, h + 0.1f, 0), glowing(FLAME, 3.0f));
            }
        } break;
        case PropLook::FENCE: {
            for (int s : { -1, 1 }) {
                b.box(Vector3(0.14f, h, 0.14f), Vector3(s * (w * 0.5f - 0.1f), h * 0.5f, 0), WOOD_DARK);
            }
            for (real_t y : { 0.42f, 0.85f }) {
                b.box(Vector3(w, 0.08f, 0.06f), Vector3(0, y, 0), WOOD);
            }
        } break;
    }
}

void add_prop_light(Node3D *p_root, const PropDef &p_def) {
    if (p_def.light_m <= 0.0) {
        return;
    }
    OmniLight3D *light = memnew(OmniLight3D);
    light->set_color(Color(1.0f, 0.72f, 0.42f));
    light->set_param(Light3D::PARAM_ENERGY, 2.2f);
    light->set_param(Light3D::PARAM_RANGE, real_t(p_def.light_m));
    light->set_position(Vector3(0, real_t(p_def.height) + 0.15f, 0));
    p_root->add_child(light);
}

void PlacedProps::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ghost_visible"), &PlacedProps::is_ghost_visible);
    ClassDB::bind_method(D_METHOD("get_prop_node", "id"), &PlacedProps::get_prop_node);
}

void PlacedProps::sync(const Placements &p_placed) {
    if (p_placed.get_revision() == revision) {
        return;
    }
    revision = p_placed.get_revision();
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        Node *child = Object::cast_to<Node>(children[k]);
        if (child && child->has_meta(PROP_META)) {
            remove_child(child);
            child->queue_free();
        }
    }
    for (const PlacedProp &p : p_placed.get_props()) {
        const PropDef *def = props::find(p.item);
        if (!def) {
            continue;
        }
        StaticBody3D *body = memnew(StaticBody3D);
        body->set_name(vformat("Prop%d", p.id));
        body->set_meta(PROP_META, p.id);
        Transform3D pose = pose_of(p.x, p.y, p.z, p.quarter);
        pose.basis = Basis(Vector3(0, 1, 0), real_t(p.yaw_extra)) * pose.basis; // 换锚累下来的转角
        body->set_transform(pose);
        build_prop_model(body, *def, p.y - p.low, Ref<Material>());
        CollisionShape3D *shape = memnew(CollisionShape3D);
        Ref<BoxShape3D> box;
        box.instantiate();
        box->set_size(Vector3(real_t(def->width), real_t(def->height), real_t(def->depth)));
        shape->set_shape(box);
        shape->set_position(Vector3(0, real_t(def->height) * 0.5f, 0));
        body->add_child(shape);
        add_prop_light(body, *def);
        add_child(body);
    }
}

void PlacedProps::ensure_ghost_materials() {
    if (ghost_good.is_valid()) {
        return;
    }
    for (Ref<StandardMaterial3D> *m : { &ghost_good, &ghost_bad }) {
        m->instantiate();
        (*m)->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        (*m)->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        (*m)->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
    }
    ghost_good->set_albedo(Color(0.45f, 1.0f, 0.5f, 0.42f));
    ghost_bad->set_albedo(Color(1.0f, 0.35f, 0.3f, 0.42f));
}

void PlacedProps::show_ghost(const std::string &p_item, const PlaceCheck &p_check, double p_yaw) {
    const PropDef *def = props::find(p_item);
    if (!def) {
        hide_ghost();
        return;
    }
    ensure_ghost_materials();
    if (!ghost || ghost_item != p_item || ghost_ok != p_check.ok) {
        if (ghost) {
            remove_child(ghost);
            ghost->queue_free();
        }
        ghost = memnew(Node3D);
        ghost->set_name("Ghost");
        add_child(ghost);
        ghost_item = p_item;
        ghost_ok = p_check.ok;
        build_prop_model(ghost, *def, 0.0, p_check.ok ? ghost_good : ghost_bad);
    }
    ghost->set_transform(pose_of(p_check.x, p_check.y, p_check.z, p_check.quarter, p_yaw));
    ghost->set_visible(true);
}

void PlacedProps::hide_ghost() {
    if (ghost) {
        ghost->set_visible(false);
    }
}

bool PlacedProps::is_ghost_visible() const {
    return ghost && ghost->is_visible();
}

Object *PlacedProps::obstacle(const PropDef &p_def, const PlaceCheck &p_check, const TypedArray<RID> &p_exclude, double p_yaw) const {
    if (!is_inside_tree()) {
        return nullptr;
    }
    constexpr real_t LIFT = 0.12f; // 底面往上抬：地面、草皮不算
    const real_t h = std::max(real_t(0.2), real_t(p_def.height) - LIFT);
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(Vector3(real_t(p_def.width) - 0.04f, h, real_t(p_def.depth) - 0.04f));
    Ref<PhysicsShapeQueryParameters3D> query;
    query.instantiate();
    query->set_shape(box);
    Transform3D xf = pose_of(p_check.x, p_check.y, p_check.z, p_check.quarter, p_yaw);
    xf.origin.y += LIFT + h * 0.5f;
    query->set_transform(xf);
    query->set_collision_mask(1);
    query->set_exclude(p_exclude);
    PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
    const TypedArray<Dictionary> hits = space->intersect_shape(query, 1);
    if (hits.is_empty()) {
        return nullptr;
    }
    const Dictionary hit = hits[0];
    return Object::cast_to<Object>(hit["collider"]);
}

Node3D *PlacedProps::get_prop_node(int p_id) const {
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        Node3D *child = Object::cast_to<Node3D>(children[k]);
        if (child && child->has_meta(PROP_META) && int(child->get_meta(PROP_META)) == p_id) {
            return child;
        }
    }
    return nullptr;
}
