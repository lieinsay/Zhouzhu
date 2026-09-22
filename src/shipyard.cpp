#include "shipyard.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

const char *GENERATED_META = "shipyard_generated";

constexpr real_t BEAM_LENGTH = 30.0f; // 坞墩梁：够 5 节的船（船头 −12.4 … 船尾 15）
constexpr real_t BEAM_U = 1.0f; // 梁中心在长轴上的位置（船尾长，往后挪一点）
constexpr real_t BEAM_SPACING = 1.5f; // 两道梁离船中线各这么远（船体半宽 1.8）
constexpr real_t BEAM_W = 0.5f;
constexpr real_t GANTRY_U = 17.0f;
constexpr real_t GANTRY_HALF_SPAN = 4.2f;
constexpr real_t GANTRY_H = 9.5f;
constexpr real_t GANGWAY_W = 1.6f; // 舷门宽 1.8
constexpr real_t GANGWAY_T = 0.25f;
constexpr real_t DECK_HALF_W = 2.5f;

Ref<StandardMaterial3D> make_material(const Color &p_albedo, float p_roughness) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_albedo);
    material->set_roughness(p_roughness);
    return material;
}

} // namespace

void Shipyard::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_present"), &Shipyard::is_present);
    ClassDB::bind_method(D_METHOD("get_cradle_transform"), &Shipyard::get_cradle_transform);
    ClassDB::bind_method(D_METHOD("get_gangway_top"), &Shipyard::get_gangway_top);
    ClassDB::bind_method(D_METHOD("get_gangway_foot"), &Shipyard::get_gangway_foot);
    ClassDB::bind_method(D_METHOD("get_bench_position"), &Shipyard::get_bench_position);
    ClassDB::bind_method(D_METHOD("on_island_rebuilt"), &Shipyard::on_island_rebuilt);
}

void Shipyard::_ready() {
    island = Object::cast_to<Island>(get_parent());
    ERR_FAIL_NULL_MSG(island, "Shipyard 要挂在 Island 下面");
    mat_timber = make_material(Color(0.45f, 0.32f, 0.20f), 0.95f);
    mat_beam = make_material(Color(0.36f, 0.26f, 0.17f), 0.95f);
    mat_paper = make_material(Color(0.88f, 0.84f, 0.72f), 1.0f);
    // 子节点先于父节点 _ready：岛此刻还没生成，等它的 rebuilt；已经生成过就直接搭
    island->connect("rebuilt", Callable(this, "on_island_rebuilt"));
    if (island->get_field().n > 0) {
        on_island_rebuilt();
    }
}

void Shipyard::on_island_rebuilt() {
    clear_generated();
    build();
}

void Shipyard::clear_generated() {
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        Node *child = Object::cast_to<Node>(children[k]);
        if (child && child->has_meta(GENERATED_META)) {
            remove_child(child);
            child->queue_free();
        }
    }
}

bool Shipyard::is_present() const {
    return island && island->get_field().yard.present;
}

Vector3 Shipyard::yard_point(double p_u, double p_v, double p_up) const {
    const IslandYard &yard = island->get_field().yard;
    double x = 0.0, z = 0.0;
    yard.from_local(p_u, p_v, x, z);
    return Vector3(real_t(x), real_t(yard.y + p_up), real_t(z));
}

void Shipyard::build() {
    if (!is_present()) {
        return;
    }
    const IslandYard &yard = island->get_field().yard;
    const Vector3 u_axis(real_t(yard.ux), 0, real_t(yard.uz));
    const Vector3 v_axis(real_t(yard.vx), 0, real_t(yard.vz));
    const Vector3 up(0, 1, 0);
    // 盒子的朝向：x 沿长轴 u；z 取 u × y，与短轴 v 同向或反向都行（盒子对称），但不能是左手系，否则面会翻
    const Basis frame(u_axis, up, u_axis.cross(up));

    auto add_box = [&](const String &p_name, const Vector3 &p_size, const Transform3D &p_xf, const Ref<StandardMaterial3D> &p_material, bool p_collide) {
        Ref<BoxMesh> mesh;
        mesh.instantiate();
        mesh->set_size(p_size);
        mesh->set_material(p_material);
        MeshInstance3D *instance = memnew(MeshInstance3D);
        instance->set_mesh(mesh);
        instance->set_transform(p_xf);
        instance->set_name(p_name);
        instance->set_meta(GENERATED_META, true);
        add_child(instance);
        if (!p_collide) {
            return;
        }
        Ref<BoxShape3D> shape;
        shape.instantiate();
        shape->set_size(p_size);
        CollisionShape3D *collider = memnew(CollisionShape3D);
        collider->set_shape(shape);
        collider->set_transform(p_xf);
        collider->set_name(p_name + String("Shape"));
        collider->set_meta(GENERATED_META, true);
        add_child(collider);
    };
    auto place = [&](double p_u, double p_v, double p_up) { return Transform3D(frame, yard_point(p_u, p_v, p_up)); };

    // 坞墩梁：托着船体两侧的底（船体底在甲板面之下 2.6 m）
    const real_t beam_h = real_t(CRADLE_LIFT - 2.6);
    for (int side = -1; side <= 1; side += 2) {
        add_box(side < 0 ? "KeelBeamA" : "KeelBeamB", Vector3(BEAM_LENGTH, beam_h, BEAM_W), place(BEAM_U, SHIP_V + side * BEAM_SPACING, 0.5 * beam_h), mat_beam, true);
    }
    // 门架：船首尾两端各一座，跨过船身
    for (int end = -1; end <= 1; end += 2) {
        for (int side = -1; side <= 1; side += 2) {
            add_box(vformat("GantryPost%d%d", end, side), Vector3(0.4f, GANTRY_H, 0.4f), place(end * GANTRY_U, SHIP_V + side * GANTRY_HALF_SPAN, 0.5 * GANTRY_H), mat_timber, true);
        }
        add_box(vformat("GantryBeam%d", end), Vector3(0.4f, 0.4f, 2 * GANTRY_HALF_SPAN + 0.4f), place(end * GANTRY_U, SHIP_V, GANTRY_H + 0.2), mat_timber, false);
    }

    // 跳板：从坪上斜到舷门外的甲板边缘
    const Vector3 top = yard_point(0.0, SHIP_V + DECK_HALF_W, CRADLE_LIFT);
    const Vector3 foot = yard_point(0.0, SHIP_V + DECK_HALF_W + GANGWAY_RUN, 0.0);
    const Vector3 downhill = (foot - top).normalized();
    const Vector3 plank_up = downhill.cross(u_axis).normalized();
    const Vector3 plank_up_fixed = plank_up.y >= 0 ? plank_up : -plank_up;
    const Basis plank_frame(u_axis, plank_up_fixed, u_axis.cross(plank_up_fixed));
    add_box("Gangway", Vector3(GANGWAY_W, GANGWAY_T, real_t((foot - top).length())), Transform3D(plank_frame, 0.5f * (top + foot) - plank_up_fixed * (0.5f * GANGWAY_T)), mat_timber, true);

    // 画样台：一张桌子，上面摊着样图
    add_box("Bench", Vector3(1.6f, 0.9f, 0.8f), place(BENCH_U, BENCH_V + 0.9, 0.45), mat_beam, true);
    add_box("BenchPaper", Vector3(1.2f, 0.03f, 0.6f), place(BENCH_U, BENCH_V + 0.9, 0.915), mat_paper, false);
}

Transform3D Shipyard::get_cradle_transform() const {
    ERR_FAIL_COND_V(!is_present(), Transform3D());
    const IslandYard &yard = island->get_field().yard;
    const Vector3 along(real_t(yard.ux), 0, real_t(yard.uz));
    return get_global_transform() * Transform3D(Basis::looking_at(along, Vector3(0, 1, 0)), yard_point(0.0, SHIP_V, CRADLE_LIFT));
}

Vector3 Shipyard::get_gangway_top() const {
    ERR_FAIL_COND_V(!is_present(), Vector3());
    return get_global_transform().xform(yard_point(0.0, SHIP_V + DECK_HALF_W - 0.3, CRADLE_LIFT));
}

Vector3 Shipyard::get_gangway_foot() const {
    ERR_FAIL_COND_V(!is_present(), Vector3());
    return get_global_transform().xform(yard_point(0.0, SHIP_V + DECK_HALF_W + GANGWAY_RUN + 0.8, 0.0));
}

Vector3 Shipyard::get_bench_position() const {
    ERR_FAIL_COND_V(!is_present(), Vector3());
    return get_global_transform().xform(yard_point(BENCH_U, BENCH_V, 0.0));
}
