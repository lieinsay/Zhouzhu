#include "works_view.h"

#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "piece_mesh.h"
#include "placed_props.h"
#include "shared_looks.h"

using namespace godot;

namespace {

PackedVector3Array vec3s(const std::vector<float> &p_flat) {
    PackedVector3Array out;
    out.resize(int64_t(p_flat.size() / 3));
    Vector3 *w = out.ptrw();
    for (int64_t k = 0; k < out.size(); k++) {
        w[k] = Vector3(p_flat[size_t(k) * 3], p_flat[size_t(k) * 3 + 1], p_flat[size_t(k) * 3 + 2]);
    }
    return out;
}

// 一座的签名：原点、朝向、每件（物品、槽、门、台基底）——没变就不重画
uint64_t signature_of(const Structure &p_s) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    auto mixd = [&](double v) { mix(uint64_t(int64_t(std::llround(v * 1e6)))); };
    mixd(p_s.x);
    mixd(p_s.y);
    mixd(p_s.z);
    mixd(p_s.yaw);
    for (const Piece &p : p_s.pieces) {
        mix(uint64_t(p.id));
        for (char c : p.item) {
            mix(uint64_t(uint8_t(c)));
        }
        mix(uint64_t(p.spot.slot));
        mix(uint64_t(int64_t(p.spot.x)));
        mix(uint64_t(int64_t(p.spot.z)));
        mix(uint64_t(p.spot.level * 64 + p.spot.tier * 8 + p.spot.dir));
        mix(uint64_t(int64_t(p.open)));
        mixd(p.bottom);
    }
    return h;
}

} // namespace

void build_structure_nodes(Node3D *p_root, const StructureMesh &p_mesh, SharedLooks &p_looks, const Ref<Material> &p_override) {
    for (int m = 0; m < int(PartMaterial::COUNT); m++) {
        const MeshPart &part = p_mesh.parts[size_t(m)];
        if (part.empty()) {
            continue;
        }
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = vec3s(part.positions);
        arrays[Mesh::ARRAY_NORMAL] = vec3s(part.normals);
        PackedInt32Array indices;
        indices.resize(int64_t(part.indices.size()));
        std::copy(part.indices.begin(), part.indices.end(), indices.ptrw());
        arrays[Mesh::ARRAY_INDEX] = indices;
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_material(0, p_override.is_valid() ? p_override : p_looks.part_material(PartMaterial(m)));
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_mesh(mesh);
        if (p_override.is_valid()) {
            mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        }
        p_root->add_child(mi);
    }
}

void WorksView::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_built_count"), &WorksView::get_built_count);
    ClassDB::bind_method(D_METHOD("get_structure_node", "structure"), &WorksView::get_structure_node);
}

WorksView::~WorksView() {
    if (own_looks) {
        delete own_looks;
        own_looks = nullptr;
    }
}

void WorksView::_notification(int p_what) {
    if (p_what == NOTIFICATION_READY) {
        set_process(true);
    }
}

SharedLooks &WorksView::get_looks() {
    if (looks) {
        return *looks;
    }
    if (!own_looks) {
        own_looks = new SharedLooks();
    }
    return *own_looks;
}

void WorksView::setup(GroundWorks *p_works, SharedLooks *p_looks) {
    works = p_works;
    looks = p_looks;
    if (!props_view) {
        props_view = memnew(PlacedProps);
        props_view->set_name("Props");
        add_child(props_view);
    }
    structures_revision = -1;
    for (auto &kv : built) {
        if (kv.second.root) {
            kv.second.root->queue_free();
        }
    }
    built.clear();
    sync();
}

void WorksView::_process(double /* p_delta */) {
    sync();
}

void WorksView::sync() {
    if (!works) {
        return;
    }
    props_view->sync(works->props);
    if (works->structures.get_revision() == structures_revision) {
        return;
    }
    structures_revision = works->structures.get_revision();
    std::map<int, bool> alive;
    for (const Structure &s : works->structures.get()) {
        alive[s.id] = true;
        Built &b = built[s.id];
        const uint64_t sig = signature_of(s);
        if (b.root && b.signature == sig) {
            continue;
        }
        b.signature = sig;
        rebuild(s, b);
    }
    for (auto it = built.begin(); it != built.end();) {
        if (alive.count(it->first)) {
            ++it;
            continue;
        }
        if (it->second.root) {
            remove_child(it->second.root);
            it->second.root->queue_free();
        }
        it = built.erase(it);
    }
}

void WorksView::rebuild(const Structure &p_s, Built &r_built) {
    if (r_built.root) {
        remove_child(r_built.root);
        r_built.root->queue_free();
        r_built.root = nullptr;
        r_built.body = nullptr;
    }
    const StructureMesh mesh = build_structure_mesh(p_s);
    Node3D *root = memnew(Node3D);
    root->set_name(vformat("Structure%d", p_s.id));
    root->set_transform(Transform3D(Basis(Vector3(0, 1, 0), real_t(p_s.yaw)), Vector3(real_t(p_s.x), real_t(p_s.y), real_t(p_s.z))));
    build_structure_nodes(root, mesh, get_looks(), Ref<Material>());
    // 碰撞：整座一个凹多边形，两面都挡（墙薄，人与相机从哪边碰都算）；碰撞形状得是 StaticBody3D 的直接子节点
    StaticBody3D *body = memnew(StaticBody3D);
    body->set_name("Body");
    body->set_meta(STRUCTURE_META, p_s.id);
    body->set_process_mode(physics_on ? PROCESS_MODE_INHERIT : PROCESS_MODE_DISABLED);
    if (!mesh.collision.empty()) {
        Ref<ConcavePolygonShape3D> shape;
        shape.instantiate();
        shape->set_backface_collision_enabled(true);
        shape->set_faces(vec3s(mesh.collision));
        CollisionShape3D *collider = memnew(CollisionShape3D);
        collider->set_shape(shape);
        body->add_child(collider);
    }
    root->add_child(body);
    // 梯子：与房子、船上的同一套（区域的 +Z 指向人站的那侧）
    for (size_t z = 0; z < mesh.ladders.size(); z++) {
        const LadderZone &l = mesh.ladders[z];
        Area3D *zone = memnew(Area3D);
        zone->set_name(vformat("Ladder%d", int(z)));
        zone->set_collision_layer(LADDER_LAYER);
        zone->set_collision_mask(0);
        zone->set_monitoring(false);
        zone->set_meta("ladder", true);
        Ref<BoxShape3D> box;
        box.instantiate();
        box->set_size(Vector3(real_t(l.sx), real_t(l.sy), real_t(l.sz)));
        CollisionShape3D *zone_shape = memnew(CollisionShape3D);
        zone_shape->set_shape(box);
        zone->add_child(zone_shape);
        zone->set_transform(Transform3D(Basis(Vector3(0, 1, 0), real_t(l.yaw)), Vector3(real_t(l.x), real_t(l.y), real_t(l.z))));
        root->add_child(zone);
    }
    add_child(root);
    r_built.root = root;
    r_built.body = body;
}

void WorksView::set_physics_on(bool p_on) {
    physics_on = p_on;
    const ProcessMode mode = physics_on ? PROCESS_MODE_INHERIT : PROCESS_MODE_DISABLED;
    for (auto &kv : built) {
        if (kv.second.body) {
            kv.second.body->set_process_mode(mode);
        }
    }
    if (props_view) {
        props_view->set_process_mode(mode);
    }
}

bool WorksView::structure_hit(Object *p_collider, const Vector3 &p_global, int &r_structure, Vector3 &r_local) const {
    StaticBody3D *body = Object::cast_to<StaticBody3D>(p_collider);
    if (!body || !body->has_meta(STRUCTURE_META)) {
        return false;
    }
    const int id = int(body->get_meta(STRUCTURE_META));
    const auto found = built.find(id);
    if (found == built.end() || found->second.body != body) {
        return false;
    }
    r_structure = id;
    r_local = found->second.root->to_local(p_global);
    return true;
}

Node3D *WorksView::get_structure_node(int p_structure) const {
    const auto found = built.find(p_structure);
    return found == built.end() ? nullptr : found->second.root;
}

StaticBody3D *WorksView::get_structure_body(int p_structure) const {
    const auto found = built.find(p_structure);
    return found == built.end() ? nullptr : found->second.body;
}
