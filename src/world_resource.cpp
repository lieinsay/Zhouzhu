#include "world_resource.h"

#include <cmath>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

using namespace godot;

namespace {

Color kind_color(const DepositKind *p_kind) {
    return p_kind ? Color(p_kind->r, p_kind->g, p_kind->b) : Color(0.5f, 0.5f, 0.5f);
}

Color item_color(const std::string &p_item) {
    float r = 0.5f, g = 0.5f, b = 0.5f;
    deposits::product_color(p_item, r, g, b);
    return Color(r, g, b);
}

// 一个确定的小抖动（0..1）：同一处每次画得一样
double jitter(int p_id, int p_k) {
    uint64_t h = uint64_t(p_id) * 0x9E3779B97F4A7C15ull + uint64_t(p_k) * 0xBF58476D1CE4E5B9ull;
    h ^= h >> 31;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 29;
    return double(h % 10000) / 10000.0;
}

} // namespace

void WorldResource::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_index"), &WorldResource::get_index);
    ClassDB::bind_method(D_METHOD("get_deposit_id"), &WorldResource::get_deposit_id);
    ClassDB::bind_method(D_METHOD("get_face"), &WorldResource::get_face);
    ClassDB::bind_method(D_METHOD("get_pile"), &WorldResource::get_pile);
}

void WorldResource::setup(const ResourceSite &p_site, int p_index) {
    site = p_site;
    index = p_index;
    landmark = false;
    set_name(vformat("Working%d", site.id));
    set_position(Vector3(real_t(site.face_x), real_t(site.face_y), real_t(site.face_z)));
}

void WorldResource::setup_landmark(const ResourceLandmark &p_mark, int p_index) {
    mark = p_mark;
    index = p_index;
    landmark = true;
    set_name(vformat("Landmark%d", mark.id));
    set_position(Vector3(real_t(mark.x), real_t(mark.y), real_t(mark.z)));
}

Vector3 WorldResource::get_face() const {
    return Vector3(real_t(site.face_x), real_t(site.face_y), real_t(site.face_z));
}

Vector3 WorldResource::get_pile() const {
    return Vector3(real_t(site.pile_x), real_t(site.pile_y), real_t(site.pile_z));
}

Ref<StandardMaterial3D> WorldResource::material(const Color &p_color, float p_roughness) {
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_albedo(p_color);
    m->set_roughness(p_roughness);
    return m;
}

void WorldResource::block(const String &p_name, const Vector3 &p_size, const Vector3 &p_at, const Color &p_color, bool p_solid, double p_yaw) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(p_size);
    mesh->set_material(material(p_color));
    const Transform3D xf(Basis(Vector3(0, 1, 0), real_t(p_yaw)), p_at);
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_name(p_name);
    instance->set_mesh(mesh);
    instance->set_transform(xf);
    add_child(instance);
    if (!p_solid) {
        return;
    }
    StaticBody3D *body = memnew(StaticBody3D);
    body->set_name(p_name + String("Body"));
    Ref<BoxShape3D> shape;
    shape.instantiate();
    shape->set_size(p_size);
    CollisionShape3D *collider = memnew(CollisionShape3D);
    collider->set_shape(shape);
    body->add_child(collider);
    body->set_transform(xf);
    add_child(body);
}

void WorldResource::cylinder(const String &p_name, double p_radius, double p_height, const Transform3D &p_xf, const Color &p_color, bool p_solid) {
    Ref<CylinderMesh> mesh;
    mesh.instantiate();
    mesh->set_top_radius(real_t(p_radius));
    mesh->set_bottom_radius(real_t(p_radius * 1.1));
    mesh->set_height(real_t(p_height));
    mesh->set_radial_segments(10);
    mesh->set_material(material(p_color));
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_name(p_name);
    instance->set_mesh(mesh);
    instance->set_transform(p_xf);
    add_child(instance);
    if (!p_solid) {
        return;
    }
    StaticBody3D *body = memnew(StaticBody3D);
    body->set_name(p_name + String("Body"));
    Ref<CylinderShape3D> shape;
    shape.instantiate();
    shape->set_radius(real_t(p_radius));
    shape->set_height(real_t(p_height));
    CollisionShape3D *collider = memnew(CollisionShape3D);
    collider->set_shape(shape);
    body->add_child(collider);
    body->set_transform(p_xf);
    add_child(body);
}

void WorldResource::_ready() {
    if (landmark) {
        build_landmark();
        return;
    }
    // 背对堆场的那一侧摆采掘面的东西，人站的原点空着
    double bx = site.face_x - site.pile_x, bz = site.face_z - site.pile_z;
    const double bl = std::hypot(bx, bz);
    if (bl > 1e-6) {
        bx /= bl, bz /= bl;
    } else {
        bx = 0.0, bz = -1.0;
    }
    build_face(bx, bz);
    build_pile();
}

// 采掘面：b = 背对堆场的方向，s = 它的左手；东西离原点 1.5 m 以外。高度按原点的地面（采掘面选在坡缓处，几米内起伏不大），略往地里埋一点
void WorldResource::build_face(double p_back_x, double p_back_z) {
    const Vector3 b(real_t(p_back_x), 0, real_t(p_back_z)), s(real_t(p_back_z), 0, real_t(-p_back_x));
    const double yaw = std::atan2(p_back_x, p_back_z); // 让盒子的 +z 朝 b
    auto at = [&](double p_along, double p_side, double p_y) { return b * real_t(p_along) + s * real_t(p_side) + Vector3(0, real_t(p_y), 0); };
    const Color base = kind_color(site.kind);
    const Color product = item_color(site.item);
    const int id = site.id;
    const DepositLook look = site.kind ? site.kind->look : DepositLook::NONE;
    switch (look) {
        case DepositLook::QUARRY: {
            // 凿开的石壁：三大块，面朝原点；地上几块凿好的方料
            const Vector3 sizes[3] = { Vector3(3.6f, 3.2f, 2.2f), Vector3(2.8f, 2.4f, 2.0f), Vector3(2.6f, 3.8f, 1.8f) };
            const double sides[3] = { -2.6, 0.2, 2.8 };
            for (int k = 0; k < 3; k++) {
                const Color c = base.darkened(float(0.1 * jitter(id, k)));
                block(vformat("Wall%d", k), sizes[k], at(3.4 + 0.4 * jitter(id, 10 + k), sides[k], 0.5 * sizes[k].y - 0.3), c, true, yaw + 0.15 * (jitter(id, 20 + k) - 0.5));
            }
            for (int k = 0; k < 3; k++) {
                block(vformat("Ashlar%d", k), Vector3(0.9f, 0.6f, 0.9f), at(1.7 + 0.3 * k, -1.8 + 1.7 * k, 0.3), product, false, yaw + 0.4 * k);
            }
        } break;
        case DepositLook::PIT: {
            // 挖开的土坑：一片翻出来的红土，边上一堆土、一只筐
            block("Dug", Vector3(4.5f, 0.12f, 3.6f), at(2.8, 0.0, 0.03), base.darkened(0.25f), false, yaw);
            block("Spoil", Vector3(2.2f, 1.0f, 1.4f), at(4.6, 2.2, 0.4), base, true, yaw + 0.3);
            cylinder("Basket", 0.35, 0.45, Transform3D(Basis(), at(1.6, -1.4, 0.22)), Color(0.6f, 0.47f, 0.28f), false);
        } break;
        case DepositLook::BAR: {
            // 砂砾滩：一片淡色的砂石，一只筛子
            block("Gravel", Vector3(6.0f, 0.1f, 4.0f), at(3.0, 0.0, 0.03), product, false, yaw);
            block("Heap", Vector3(1.6f, 0.7f, 1.6f), at(4.2, -2.0, 0.3), product.darkened(0.1f), false, yaw + 0.8);
            block("Sieve", Vector3(1.2f, 0.08f, 0.8f), at(1.8, 1.4, 0.6), Color(0.55f, 0.42f, 0.26f), false, yaw);
        } break;
        case DepositLook::SLUICE: {
            // 淘金滩：斜架着的木溜槽、一只淘盘
            const Basis tilt = Basis(Vector3(0, 1, 0), real_t(yaw)) * Basis(Vector3(1, 0, 0), 0.12f);
            block("Bar", Vector3(5.0f, 0.1f, 3.5f), at(3.0, 0.0, 0.03), base.darkened(0.3f), false, yaw);
            Ref<BoxMesh> trough;
            trough.instantiate();
            trough->set_size(Vector3(0.6f, 0.35f, 4.0f));
            trough->set_material(material(Color(0.55f, 0.4f, 0.24f)));
            MeshInstance3D *t = memnew(MeshInstance3D);
            t->set_name("Sluice");
            t->set_mesh(trough);
            t->set_transform(Transform3D(tilt, at(2.6, 0.0, 0.7)));
            add_child(t);
            cylinder("Pan", 0.4, 0.08, Transform3D(Basis(), at(1.6, -1.5, 0.05)), Color(0.35f, 0.3f, 0.26f), false);
        } break;
        case DepositLook::MINE: {
            // 矿坑：黑黢黢的坑口、一架辘轳、一堆矿石
            block("Shaft", Vector3(2.4f, 0.1f, 2.4f), at(3.2, 0.0, 0.03), Color(0.12f, 0.1f, 0.09f), false, yaw);
            const Color wood(0.45f, 0.32f, 0.2f);
            block("PostL", Vector3(0.18f, 2.2f, 0.18f), at(3.2, -1.4, 1.1), wood, true, yaw);
            block("PostR", Vector3(0.18f, 2.2f, 0.18f), at(3.2, 1.4, 1.1), wood, true, yaw);
            block("Beam", Vector3(3.0f, 0.2f, 0.2f), at(3.2, 0.0, 2.1), wood, false, yaw);
            for (int k = 0; k < 4; k++) {
                block(vformat("Ore%d", k), Vector3(0.7f, 0.5f, 0.7f), at(1.8 + 0.3 * jitter(id, k), 2.0 + 0.5 * k, 0.25 + (k == 3 ? 0.45 : 0.0)), product, false, 0.9 * k);
            }
        } break;
        case DepositLook::CRUST: {
            // 硫磺坑：地上一片片结壳
            for (int k = 0; k < 4; k++) {
                const Vector3 size(real_t(1.5 + 1.5 * jitter(id, k)), 0.06f, real_t(1.0 + 1.2 * jitter(id, 10 + k)));
                block(vformat("Crust%d", k), size, at(2.2 + 1.2 * k * jitter(id, 20 + k), -2.0 + 1.4 * k, 0.03 + 0.01 * k), base.lightened(float(0.2 * jitter(id, 30 + k))), false, 1.3 * k);
            }
        } break;
        case DepositLook::NONE:
        case DepositLook::SPRING:
        case DepositLook::HOTSPRING:
        case DepositLook::CAVE:
            break;
    }
}

// 堆场：两只货箱（箱盖是产物的颜色）+ 一根杆子挂牌子；只是摆设
void WorldResource::build_pile() {
    Node3D *pile = memnew(Node3D);
    pile->set_name("Pile");
    pile->set_position(Vector3(real_t(site.pile_x - site.face_x), real_t(site.pile_y - site.face_y), real_t(site.pile_z - site.face_z)));
    add_child(pile);
    const Ref<StandardMaterial3D> wood = material(Color(0.58f, 0.44f, 0.26f)), lid = material(item_color(site.item)), pole = material(Color(0.33f, 0.22f, 0.15f));
    auto add = [&](const String &p_name, const Vector3 &p_size, const Vector3 &p_at, const Ref<StandardMaterial3D> &p_material) {
        Ref<BoxMesh> mesh;
        mesh.instantiate();
        mesh->set_size(p_size);
        mesh->set_material(p_material);
        MeshInstance3D *instance = memnew(MeshInstance3D);
        instance->set_name(p_name);
        instance->set_mesh(mesh);
        instance->set_position(p_at);
        pile->add_child(instance);
    };
    // 摆在堆场那一点的两边，人站的那一点空着
    const Vector3 crates[2] = { Vector3(-1.6f, 0.0f, 0.6f), Vector3(-1.4f, 0.0f, -0.9f) };
    for (int c = 0; c < 2; c++) {
        add(vformat("Crate%d", c), Vector3(1.4f, 1.1f, 1.4f), crates[c] + Vector3(0, 0.5f, 0), wood);
        add(vformat("Lid%d", c), Vector3(1.2f, 0.05f, 1.2f), crates[c] + Vector3(0, 1.075f, 0), lid);
    }
    add("Pole", Vector3(0.14f, 3.2f, 0.14f), Vector3(1.6f, 1.5f, 0.0f), pole);
    add("Board", Vector3(1.3f, 0.5f, 0.06f), Vector3(1.6f, 2.6f, 0.1f), material(Color(0.8f, 0.7f, 0.5f)));
    sign(pile, String::utf8(site.name.c_str()) + String(U" · ") + String::utf8(deposits::grade_name(site.grade)) + String(U"品"), Vector3(1.6f, 3.35f, 0.0f));
}

void WorldResource::sign(Node3D *p_parent, const String &p_text, const Vector3 &p_at) {
    Label3D *label = memnew(Label3D);
    label->set_name("Sign");
    label->set_text(p_text);
    label->set_position(p_at);
    label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    label->set_font_size(48);
    label->set_outline_size(12);
    label->set_pixel_size(0.008f);
    label->set_visibility_range_end(90.0f);
    p_parent->add_child(label);
}

// 地标：泉眼一圈石头围一汪水；温泉一池水冒白汽；洞穴是岩壁上一个黑洞口
void WorldResource::build_landmark() {
    const int id = mark.id;
    const DepositLook look = mark.kind ? mark.kind->look : DepositLook::NONE;
    const Color rock(0.5f, 0.49f, 0.46f);
    switch (look) {
        case DepositLook::SPRING:
        case DepositLook::HOTSPRING: {
            const bool hot = look == DepositLook::HOTSPRING;
            const double r = hot ? 3.2 : 1.6;
            const Color water = hot ? Color(0.45f, 0.7f, 0.72f) : Color(0.3f, 0.52f, 0.7f);
            cylinder("Pool", r, 0.12, Transform3D(Basis(), Vector3(0, 0.02f, 0)), water, false);
            const int stones = hot ? 14 : 9;
            for (int k = 0; k < stones; k++) {
                const double a = 6.2831853 * k / stones + 0.3 * jitter(id, k);
                const double s = 0.5 + 0.35 * jitter(id, 20 + k);
                block(vformat("Stone%d", k), Vector3(real_t(s), real_t(0.45 * s + 0.1), real_t(s * 0.8)),
                        Vector3(real_t((r + 0.3) * std::cos(a)), real_t(0.15 * s), real_t((r + 0.3) * std::sin(a))), rock.lightened(float(0.15 * jitter(id, 40 + k))),
                        false, a);
            }
            if (hot) {
                // 白汽：几团半透明的白块叠在池子上
                Ref<StandardMaterial3D> steam = material(Color(0.95f, 0.95f, 0.95f, 0.35f));
                steam->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                for (int k = 0; k < 4; k++) {
                    Ref<BoxMesh> puff;
                    puff.instantiate();
                    const real_t s = real_t(1.2 + 0.8 * jitter(id, 60 + k));
                    puff->set_size(Vector3(s, s * 0.7f, s));
                    puff->set_material(steam);
                    MeshInstance3D *m = memnew(MeshInstance3D);
                    m->set_name(vformat("Steam%d", k));
                    m->set_mesh(puff);
                    m->set_position(Vector3(real_t(1.4 * (jitter(id, 70 + k) - 0.5)), real_t(0.8 + 0.9 * k), real_t(1.4 * (jitter(id, 80 + k) - 0.5))));
                    m->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
                    add_child(m);
                }
            }
        } break;
        case DepositLook::CAVE: {
            // 一座半埋的岩丘（带碰撞），朝 +z 那面一个黑洞口，洞口前几块落石
            auto knob = [&](const String &p_name, const Vector3 &p_at, const Vector3 &p_size, const Color &p_color, double p_yaw) {
                Ref<SphereMesh> sphere;
                sphere.instantiate();
                sphere->set_radius(0.5f);
                sphere->set_height(1.0f);
                sphere->set_radial_segments(7);
                sphere->set_rings(4);
                sphere->set_material(material(p_color));
                MeshInstance3D *m = memnew(MeshInstance3D);
                m->set_name(p_name);
                m->set_mesh(sphere);
                m->set_transform(Transform3D(Basis(Vector3(0, 1, 0), real_t(p_yaw)).scaled(p_size), p_at));
                add_child(m);
            };
            knob("Mound", Vector3(0, 0.4f, -3.2f), Vector3(9.0f, 7.0f, 7.5f), rock, 0.5 * jitter(id, 1));
            knob("Shoulder", Vector3(3.2f, 0.2f, -2.4f), Vector3(4.5f, 4.2f, 4.0f), rock.darkened(0.08f), 1.3 + jitter(id, 2));
            knob("Mouth", Vector3(0, 1.1f, 0.25f), Vector3(2.4f, 2.6f, 0.9f), Color(0.04f, 0.04f, 0.05f), 0.0);
            knob("RubbleL", Vector3(-2.0f, 0.15f, 1.2f), Vector3(1.0f, 0.7f, 0.9f), rock.lightened(0.1f), 0.4);
            knob("RubbleR", Vector3(1.8f, 0.1f, 1.5f), Vector3(0.8f, 0.55f, 0.8f), rock.lightened(0.05f), 1.1);
            StaticBody3D *body = memnew(StaticBody3D);
            body->set_name("MoundBody");
            Ref<CylinderShape3D> shape;
            shape.instantiate();
            shape->set_radius(3.4f);
            shape->set_height(4.0f);
            CollisionShape3D *collider = memnew(CollisionShape3D);
            collider->set_shape(shape);
            collider->set_position(Vector3(0, 1.8f, -3.4f));
            body->add_child(collider);
            add_child(body);
        } break;
        default:
            break;
    }
    Node3D *post = memnew(Node3D);
    post->set_name("Post");
    add_child(post);
    sign(post, String::utf8(mark.name.c_str()), Vector3(0, look == DepositLook::CAVE ? 5.4f : 2.4f, 0));
}
