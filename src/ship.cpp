#include "ship.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_shape_query_parameters3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/classes/prism_mesh.hpp>
#include <godot_cpp/classes/torus_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>

#include "freight.h"
#include "input_actions.h"

using namespace godot;

namespace {

constexpr double MOOR_MAX_SPEED = 9.0;
constexpr double MOOR_GAIN = 1.2; // 靠泊速度 = 距离 × 此值 + 下限，越近越慢
constexpr double MOOR_MIN_SPEED = 0.6;
constexpr double MOOR_YAW_RATE = 0.7;
constexpr double BANK_PER_YAW_RATE = 0.3; // 侧倾角 / 偏航角速度
constexpr double TRIM_PER_CLIMB = 0.012; // 纵倾角 / 垂直速度
constexpr double CAMERA_PITCH_MIN = -1.2;
constexpr double CAMERA_PITCH_MAX = 0.5;

const char *GENERATED_META = "ship_generated";
const char *CARGO_META = "ship_cargo"; // 货箱：换货时只重画它们
const char *GANGWAY_META = "ship_gangway"; // 野泊的舷梯：收放时只动它
constexpr uint32_t LADDER_LAYER = 8; // Player 的 Sensor 只看这一层

// 几何（米）
constexpr real_t CELL_L = real_t(CELL_LENGTH_M), CELL_W = real_t(CELL_WIDTH_M), CELL_H = real_t(CELL_HEIGHT_M);
constexpr real_t DECK_T = 0.7f; // 第 0 层甲板板厚，甲板面在 y = 0
constexpr real_t HULL_W = 5.2f, HULL_H = 2.8f; // 船体吊在甲板下
constexpr real_t RAIL_T = 0.15f, RAIL_H = 1.15f;
constexpr real_t GATE_HALF = 1.0f; // 舷门半宽
constexpr real_t LADDER_GAP_HALF = 0.7f; // 梯顶处舷栏缺口的半宽
constexpr real_t LADDER_ALONG = -2.8f; // 梯子靠墙的哪一头：沿墙偏这么多（左舷 / 船头那一头），避开舵柱、座位与货箱
constexpr real_t HELM_AFT = 1.7f; // 舵柱在驾驶区中心之后多少
constexpr real_t SEAT_AFT = 2.9f;
constexpr real_t BOW_WEDGE = 4.5f; // 船头楔的长（与 ship_layout 的 BOW_M 配：楔尖再多出一点）
constexpr real_t LADDER_W = 0.9f;
constexpr real_t CRATE_SIZE = 1.4f, CRATE_H = 1.1f; // 货位上的货箱（人抱着时画小一号，见 Player）
constexpr real_t LADDER_ZONE_DEPTH = 1.0f; // 爬梯区域离墙多深
constexpr real_t LADDER_ZONE_OVER = 1.2f; // 爬梯区域超出梯顶甲板多高：人到这儿才松开，正好落在甲板上
constexpr real_t GANGWAY_W = 1.8f, GANGWAY_T = 0.25f; // 野泊的舷梯（舷门缺口 2 m 宽）
constexpr real_t GANGWAY_BURY = 0.4f; // 梯脚往地里多埋一截：地面起伏也不悬着

const int N4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }; // (dx, dz)

// 从 p_from 转到 p_to 的最短有向角（godot-cpp 的 Math 里没有 angle_difference）
double angle_difference(double p_from, double p_to) {
    const double diff = std::fmod(p_to - p_from, Math_TAU);
    return std::fmod(2.0 * diff, Math_TAU) - diff;
}

Ref<StandardMaterial3D> make_material(const Color &p_albedo, float p_roughness) {
    Ref<StandardMaterial3D> material;
    material.instantiate();
    material->set_albedo(p_albedo);
    material->set_roughness(p_roughness);
    return material;
}

void adopt(Node *p_parent, Node *p_node, const String &p_name) {
    p_node->set_name(p_name);
    p_node->set_meta(GENERATED_META, true);
    p_parent->add_child(p_node);
}

// 一只盒子：样子挂在 Visual 下，碰撞（可选）挂在船根下
void add_box(Node3D *p_visual, Node *p_body, const String &p_name, const Vector3 &p_size, const Transform3D &p_xf, const Ref<StandardMaterial3D> &p_material, bool p_collide) {
    Ref<BoxMesh> mesh;
    mesh.instantiate();
    mesh->set_size(p_size);
    mesh->set_material(p_material);
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_mesh(mesh);
    instance->set_transform(p_xf);
    adopt(p_visual, instance, p_name);
    if (!p_collide) {
        return;
    }
    Ref<BoxShape3D> shape;
    shape.instantiate();
    shape->set_size(p_size);
    CollisionShape3D *collider = memnew(CollisionShape3D);
    collider->set_shape(shape);
    collider->set_transform(p_xf);
    adopt(p_body, collider, p_name + String("Shape"));
}

void add_mesh(Node3D *p_visual, const String &p_name, const Ref<Mesh> &p_mesh, const Transform3D &p_xf) {
    MeshInstance3D *instance = memnew(MeshInstance3D);
    instance->set_mesh(p_mesh);
    instance->set_transform(p_xf);
    adopt(p_visual, instance, p_name);
}

Color cargo_color(const String &p_item) {
    float r = 0, g = 0, b = 0;
    freight::crate_color(p_item.utf8().get_data(), r, g, b);
    return Color(r, g, b);
}

Transform3D at(real_t p_x, real_t p_y, real_t p_z) {
    return Transform3D(Basis(), Vector3(p_x, p_y, p_z));
}

// 一段 [a, b] 挖掉若干缺口后剩下的段
std::vector<std::pair<real_t, real_t>> cut_gaps(real_t a, real_t b, const std::vector<std::pair<real_t, real_t>> &p_gaps) {
    std::vector<std::pair<real_t, real_t>> pieces = { { a, b } };
    for (const auto &gap : p_gaps) {
        std::vector<std::pair<real_t, real_t>> next;
        for (const auto &piece : pieces) {
            if (gap.second <= piece.first || gap.first >= piece.second) {
                next.push_back(piece);
                continue;
            }
            if (gap.first > piece.first) {
                next.push_back({ piece.first, gap.first });
            }
            if (gap.second < piece.second) {
                next.push_back({ gap.second, piece.second });
            }
        }
        pieces = next;
    }
    return pieces;
}

} // namespace

void Ship::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_controlled", "controlled"), &Ship::set_controlled);
    ClassDB::bind_method(D_METHOD("is_controlled"), &Ship::is_controlled);
    ClassDB::bind_method(D_METHOD("place_at", "contact", "keep_heading"), &Ship::place_at, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("moor_at", "contact"), &Ship::moor_at);
    ClassDB::bind_method(D_METHOD("is_mooring"), &Ship::is_mooring);
    ClassDB::bind_method(D_METHOD("is_at", "contact", "tolerance"), &Ship::is_at);
    ClassDB::bind_method(D_METHOD("get_speed"), &Ship::get_speed);
    ClassDB::bind_method(D_METHOD("set_gear", "gear"), &Ship::set_gear);
    ClassDB::bind_method(D_METHOD("get_gear"), &Ship::get_gear);
    ClassDB::bind_method(D_METHOD("get_gear_name"), &Ship::get_gear_name);
    ClassDB::bind_method(D_METHOD("get_helm_position"), &Ship::get_helm_position);
    ClassDB::bind_method(D_METHOD("get_seat_transform"), &Ship::get_seat_transform);
    ClassDB::bind_method(D_METHOD("get_exit_transform"), &Ship::get_exit_transform);
    ClassDB::bind_method(D_METHOD("get_camera"), &Ship::get_camera);
    ClassDB::bind_method(D_METHOD("get_deck_position", "x", "z"), &Ship::get_deck_position);
    ClassDB::bind_method(D_METHOD("get_ladder_feet"), &Ship::get_ladder_feet);
    ClassDB::bind_method(D_METHOD("get_ladder_tops"), &Ship::get_ladder_tops);
    ClassDB::bind_method(D_METHOD("get_stats"), &Ship::get_stats);
    ClassDB::bind_method(D_METHOD("is_aboard", "global"), &Ship::is_aboard);
    ClassDB::bind_method(D_METHOD("can_fly"), &Ship::can_fly);
    ClassDB::bind_method(D_METHOD("get_section_count"), &Ship::get_section_count);
    ClassDB::bind_method(D_METHOD("get_bow_z"), &Ship::get_bow_z);
    ClassDB::bind_method(D_METHOD("get_stern_z"), &Ship::get_stern_z);
    ClassDB::bind_method(D_METHOD("get_port_edge_x"), &Ship::get_port_edge_x);
    ClassDB::bind_method(D_METHOD("get_starboard_edge_x"), &Ship::get_starboard_edge_x);
    ClassDB::bind_method(D_METHOD("get_levels"), &Ship::get_levels);
    ClassDB::bind_method(D_METHOD("get_hold_slot_count"), &Ship::get_hold_slot_count);
    ClassDB::bind_method(D_METHOD("get_hold_slot_position", "slot"), &Ship::get_hold_slot_position);
    ClassDB::bind_method(D_METHOD("is_hold_slot_open", "slot"), &Ship::is_hold_slot_open);
    ClassDB::bind_method(D_METHOD("find_hold_slot", "global", "range"), &Ship::find_hold_slot);
    ClassDB::bind_method(D_METHOD("set_cargo", "slot", "item"), &Ship::set_cargo);
    ClassDB::bind_method(D_METHOD("get_cargo", "slot"), &Ship::get_cargo);
    ClassDB::bind_method(D_METHOD("get_cargo_count"), &Ship::get_cargo_count);
    ClassDB::bind_method(D_METHOD("set_gangway", "side", "run", "rise"), &Ship::set_gangway);
    ClassDB::bind_method(D_METHOD("clear_gangway"), &Ship::clear_gangway);
    ClassDB::bind_method(D_METHOD("has_gangway"), &Ship::has_gangway);
    ClassDB::bind_method(D_METHOD("get_gangway_side"), &Ship::get_gangway_side);
    ClassDB::bind_method(D_METHOD("get_gangway_foot"), &Ship::get_gangway_foot);
    ClassDB::bind_method(D_METHOD("get_gangway_top"), &Ship::get_gangway_top);
    ClassDB::bind_method(D_METHOD("get_max_speed"), &Ship::get_max_speed);

    ClassDB::bind_method(D_METHOD("set_layout", "ids"), &Ship::set_layout);
    ClassDB::bind_method(D_METHOD("get_layout"), &Ship::get_layout);
    ClassDB::bind_method(D_METHOD("set_min_altitude", "value"), &Ship::set_min_altitude);
    ClassDB::bind_method(D_METHOD("get_min_altitude"), &Ship::get_min_altitude);

    ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "layout"), "set_layout", "get_layout");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_altitude", PROPERTY_HINT_RANGE, "-100,1000,1,suffix:m"), "set_min_altitude", "get_min_altitude");

    ADD_SIGNAL(MethodInfo("moored"));
    ADD_SIGNAL(MethodInfo("refitted"));
}

void Ship::_ready() {
    visual = get_node<Node3D>("Visual");
    camera_rig = get_node<Node3D>("CameraRig");
    camera = get_node<Camera3D>("CameraRig/Camera3D");
    ERR_FAIL_COND_MSG(!visual || !camera_rig || !camera, "Ship: 场景里缺 Visual / CameraRig/Camera3D");
    rebuild(); // 编辑器里也拼，摆场景时看得见船
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        set_process_unhandled_input(false);
        return;
    }
    state.yaw = get_rotation().y;
}

void Ship::ensure_materials() {
    if (mat_wood.is_valid()) {
        return;
    }
    mat_wood = make_material(Color(0.52f, 0.37f, 0.23f), 0.95f);
    mat_hull = make_material(Color(0.33f, 0.22f, 0.15f), 0.9f);
    mat_rail = make_material(Color(0.4f, 0.28f, 0.18f), 0.9f);
    mat_sail = make_material(Color(0.9f, 0.86f, 0.74f), 1.0f);
    mat_sail->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
    mat_stone = make_material(Color(0.25f, 0.62f, 0.6f), 0.6f);
    mat_stone->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
    mat_stone->set_emission(Color(0.2f, 0.75f, 0.7f));
    mat_stone->set_emission_energy_multiplier(1.4f);
    mat_crate = make_material(Color(0.58f, 0.44f, 0.26f), 0.95f);
    mat_roof = make_material(Color(0.27f, 0.19f, 0.13f), 0.9f);
    mat_glow = make_material(Color(0.55f, 0.9f, 0.85f), 0.4f);
    mat_glow->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
    mat_glow->set_emission(Color(0.3f, 0.9f, 0.8f));
    mat_glow->set_emission_energy_multiplier(2.0f);
}

void Ship::clear_generated() {
    for (Node *parent : { static_cast<Node *>(this), static_cast<Node *>(visual) }) {
        if (!parent) {
            continue;
        }
        const TypedArray<Node> children = parent->get_children();
        for (int64_t k = 0; k < children.size(); k++) {
            Node *child = Object::cast_to<Node>(children[k]);
            if (child && child->has_meta(GENERATED_META)) {
                parent->remove_child(child);
                child->queue_free();
            }
        }
    }
}

// 一节：顶层 = 开放甲板 + 陈设，被盖住 = 封闭舱体；第 0 层下面吊船体
void Ship::build_module(const Module &p_module) {
    const Cell &c = p_module.at;
    const real_t cx = real_t(cell_center_x(c.x)), cz = real_t(cell_center_z(c.z)), deck = real_t(cell_deck_y(c.y));
    const bool top = !layout.has({ c.x, c.y + 1, c.z });
    const int height = layout.column_height(c.x, c.z);
    const String prefix = vformat("M%d_%d_%d_", c.x, c.y, c.z);

    if (c.y == 0) {
        add_box(visual, this, prefix + String("Hull"), Vector3(HULL_W, HULL_H, CELL_L), at(cx, -DECK_T - 0.5f * HULL_H, cz), mat_hull, true);
        if (p_module.kind == ModuleKind::POWER) {
            add_box(visual, this, prefix + String("Floatstone"), Vector3(2.4f, 1.6f, 5.6f), at(cx, -DECK_T - HULL_H - 0.8f, cz), mat_stone, false);
        }
    }

    if (!top) {
        // 封闭舱体：一整块，露在外面的墙按模块装饰
        add_box(visual, this, prefix + String("Cabin"), Vector3(CELL_W, CELL_H, CELL_L), at(cx, deck + 0.5f * CELL_H, cz), mat_hull, true);
        for (const auto &d : N4) {
            if (layout.has({ c.x + d[0], c.y, c.z + d[1] })) {
                continue; // 这面墙贴着别的节
            }
            const Vector3 n(real_t(d[0]), 0, real_t(d[1]));
            const Vector3 face = Vector3(cx, deck + 0.5f * CELL_H, cz) + n * (0.5f * CELL_W + 0.04f);
            const Vector3 along = Vector3(real_t(d[1]), 0, real_t(-d[0])); // 沿墙
            const Basis wall(along, Vector3(0, 1, 0), n);
            const String face_name = prefix + vformat("F%d%d_", d[0], d[1]);
            if (p_module.kind == ModuleKind::STORAGE) {
                add_box(visual, this, face_name + String("Hatch"), Vector3(1.6f, 1.4f, 0.08f), Transform3D(wall, face + Vector3(0, -0.4f, 0)), mat_crate, false);
                add_box(visual, this, face_name + String("Batten"), Vector3(CELL_W - 0.5f, 0.14f, 0.06f), Transform3D(wall, face + Vector3(0, 1.4f, 0)), mat_rail, false);
            } else if (p_module.kind == ModuleKind::POWER) {
                for (int s = -1; s <= 1; s += 2) {
                    add_box(visual, this, face_name + vformat("Slit%d", s), Vector3(1.8f, 0.32f, 0.08f), Transform3D(wall, face + along * (s * 1.7f) + Vector3(0, 0.5f, 0)), mat_glow, false);
                }
            }
        }
        return;
    }

    // 开放甲板：第 0 层要一块甲板板（更高层踩的是下面舱体的顶）
    if (c.y == 0) {
        add_box(visual, this, prefix + String("Deck"), Vector3(CELL_W, DECK_T, CELL_L), at(cx, -0.5f * DECK_T, cz), mat_wood, true);
    }

    // 舷栏：邻列比这层低（或没有）的那条边才有；舷门与梯顶处留缺口
    const int gate_min = int(std::lround(port_edge_x(layout) / CELL_WIDTH_M + 0.5)), gate_max = int(std::lround(starboard_edge_x(layout) / CELL_WIDTH_M - 0.5));
    for (const auto &d : N4) {
        const int neighbor = layout.column_height(c.x + d[0], c.z + d[1]);
        if (neighbor >= height) {
            continue;
        }
        std::vector<std::pair<real_t, real_t>> gaps;
        const bool along_z = d[1] == 0; // 边沿 z（±x 那两条边）
        if (neighbor > 0) {
            // 低邻列的梯子从这儿上来：梯子沿墙偏在 along = (dz, −dx) 那一头（与 build_ladder 同一套算法）
            const real_t ladder_at = LADDER_ALONG * real_t(along_z ? -d[0] : d[1]);
            gaps.push_back({ ladder_at - LADDER_GAP_HALF, ladder_at + LADDER_GAP_HALF });
        }
        if (c.y == 0 && c.z == 0 && d[1] == 0 && ((d[0] < 0 && c.x == gate_min) || (d[0] > 0 && c.x == gate_max))) {
            gaps.push_back({ -GATE_HALF, GATE_HALF });
        }
        const real_t edge_center = along_z ? cz : cx;
        const real_t offset = 0.5f * CELL_W - 0.5f * RAIL_T;
        int k = 0;
        for (const auto &piece : cut_gaps(-0.5f * CELL_L, 0.5f * CELL_L, gaps)) {
            if (piece.second - piece.first < 0.05f) {
                continue;
            }
            const real_t mid = edge_center + 0.5f * (piece.first + piece.second), len = piece.second - piece.first;
            const Vector3 size = along_z ? Vector3(RAIL_T, RAIL_H, len) : Vector3(len, RAIL_H, RAIL_T);
            const Vector3 pos = along_z ? Vector3(cx + d[0] * offset, deck + 0.5f * RAIL_H, mid) : Vector3(mid, deck + 0.5f * RAIL_H, cz + d[1] * offset);
            add_box(visual, this, prefix + vformat("Rail%d%d_%d", d[0], d[1], k++), size, Transform3D(Basis(), pos), mat_rail, true);
        }
    }

    switch (p_module.kind) {
        case ModuleKind::BRIDGE: {
            const real_t zh = cz + HELM_AFT;
            add_box(visual, this, prefix + String("HelmPost"), Vector3(0.4f, 1.05f, 0.4f), at(cx, deck + 0.525f, zh), mat_hull, true);
            Ref<TorusMesh> wheel;
            wheel.instantiate();
            wheel->set_inner_radius(0.36f);
            wheel->set_outer_radius(0.48f);
            wheel->set_material(mat_rail);
            add_mesh(visual, prefix + String("Wheel"), wheel, Transform3D(Basis(Vector3(1, 0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0)), Vector3(cx, deck + 1.2f, zh + 0.22f)));
            for (int side = -1; side <= 1; side += 2) {
                for (int end = -1; end <= 1; end += 2) {
                    add_box(visual, this, prefix + vformat("Post%d%d", side, end), Vector3(0.14f, 3.0f, 0.14f), at(cx + side * 2.8f, deck + 1.5f, cz + (end < 0 ? -2.0f : 3.0f)), mat_hull, false);
                }
            }
            add_box(visual, this, prefix + String("Roof"), Vector3(CELL_W, 0.14f, 5.8f), at(cx, deck + 3.07f, cz + 0.5f), mat_roof, false);
            helm_local = Vector3(cx, deck + 1.05f, zh);
            seat_local = Vector3(cx, deck, cz + SEAT_AFT);
        } break;

        case ModuleKind::STORAGE: {
            // 货位上的垫木（不挡脚）；货箱另画（build_cargo）
            for (int side = -1; side <= 1; side += 2) {
                for (int end = -1; end <= 1; end += 2) {
                    const real_t x = cx + side * real_t(HOLD_SLOT_SIDE_M), z = cz + end * real_t(HOLD_SLOT_ALONG_M);
                    add_box(visual, this, prefix + vformat("Pallet%d%d", side, end), Vector3(CRATE_SIZE + 0.2f, 0.06f, CRATE_SIZE + 0.2f), at(x, deck + 0.03f, z), mat_rail, false);
                }
            }
        } break;

        case ModuleKind::POWER: {
            Ref<CylinderMesh> mast;
            mast.instantiate();
            mast->set_top_radius(0.11f);
            mast->set_bottom_radius(0.16f);
            mast->set_height(7.2f);
            mast->set_material(mat_hull);
            Ref<CylinderShape3D> mast_shape;
            mast_shape.instantiate();
            mast_shape->set_radius(0.18f);
            mast_shape->set_height(7.2f);
            for (int side = -1; side <= 1; side += 2) {
                const real_t x = cx + side * 2.7f;
                add_mesh(visual, prefix + vformat("Mast%d", side), mast, at(x, deck + 3.6f, cz));
                CollisionShape3D *collider = memnew(CollisionShape3D);
                collider->set_shape(mast_shape);
                collider->set_transform(at(x, deck + 3.6f, cz));
                adopt(this, collider, prefix + vformat("Mast%dShape", side));
                add_box(visual, this, prefix + vformat("Boom%d", side), Vector3(0.14f, 0.14f, 3.4f), at(x, deck + 1.5f, cz), mat_rail, false);
                add_box(visual, this, prefix + vformat("Sail%d", side), Vector3(0.06f, 4.6f, 4.2f), at(x, deck + 4.5f, cz), mat_sail, false);
            }
        } break;

        case ModuleKind::COUNT:
            break;
    }
}

// 梯子：挂在高列面向低列的墙上，从低列的甲板到高列的顶
void Ship::build_ladder(const Ladder &p_ladder) {
    const int dx = p_ladder.low_x - p_ladder.high_x, dz = p_ladder.low_z - p_ladder.high_z; // 高列 → 低列 = 离墙的方向
    const Vector3 outward(real_t(dx), 0, real_t(dz));
    const Vector3 along(real_t(dz), 0, real_t(-dx));
    const Basis frame(along, Vector3(0, 1, 0), outward);
    const real_t wall_x = real_t(cell_center_x(p_ladder.high_x)) + real_t(dx) * 0.5f * CELL_W;
    const real_t wall_z = real_t(cell_center_z(p_ladder.high_z)) + real_t(dz) * 0.5f * CELL_L;
    const real_t foot_y = real_t(cell_deck_y(p_ladder.from_level - 1)), top_y = real_t(cell_deck_y(p_ladder.to_level - 1)); // 层数 → 那层顶的甲板高
    const real_t rise = top_y - foot_y;
    const String name = vformat("Ladder_%d_%d_to_%d_%d", p_ladder.low_x, p_ladder.low_z, p_ladder.high_x, p_ladder.high_z);

    // 踏板：一块薄板贴在墙外，上面几根横档
    const Vector3 wall = Vector3(wall_x, 0, wall_z) + along * LADDER_ALONG; // 靠墙的一头，不挡走道
    add_box(visual, this, name, Vector3(LADDER_W, rise + 0.3f, 0.08f), Transform3D(frame, wall + outward * 0.12f + Vector3(0, foot_y + 0.5f * rise + 0.15f, 0)), mat_rail, false);
    const int rungs = int(std::lround(rise / 0.4f));
    for (int r = 0; r < rungs; r++) {
        add_box(visual, this, name + vformat("_R%d", r), Vector3(LADDER_W + 0.1f, 0.06f, 0.16f), Transform3D(frame, wall + outward * 0.2f + Vector3(0, foot_y + 0.3f + r * 0.4f, 0)), mat_rail, false);
    }

    // 爬梯区域：贴墙、从梯脚甲板到梯顶甲板再高一点；+Z 离墙
    Area3D *zone = memnew(Area3D);
    zone->set_collision_layer(LADDER_LAYER);
    zone->set_collision_mask(0);
    zone->set_meta("ladder", true);
    Ref<BoxShape3D> shape;
    shape.instantiate();
    shape->set_size(Vector3(LADDER_W + 0.6f, rise + LADDER_ZONE_OVER, LADDER_ZONE_DEPTH));
    CollisionShape3D *collider = memnew(CollisionShape3D);
    collider->set_shape(shape);
    zone->add_child(collider);
    zone->set_transform(Transform3D(frame, wall + outward * (0.5f * LADDER_ZONE_DEPTH) + Vector3(0, foot_y + 0.5f * (rise + LADDER_ZONE_OVER), 0)));
    adopt(this, zone, name + String("_Zone"));

    ladder_feet_local.push_back(wall + outward * 0.6f + Vector3(0, foot_y, 0));
    ladder_tops_local.push_back(wall - outward * 0.8f + Vector3(0, top_y, 0));
}

void Ship::rebuild() {
    ERR_FAIL_NULL(visual);
    ensure_materials();
    clear_generated();
    stats = layout_stats(layout);
    params.max_speed = stats.max_speed;
    params.reverse_speed = 0.25 * stats.max_speed;
    params.max_climb = stats.max_climb;
    params.max_yaw_rate = stats.max_yaw_rate;
    helm_local = Vector3(0, 1, 0);
    seat_local = Vector3(0, 0, 0);
    exit_local = Vector3(0, real_t(cell_deck_y(std::max(0, layout.column_height(0, 0) - 1))), 0); // 出口：舷门那一列的顶层甲板
    ladder_feet_local.clear();
    ladder_tops_local.clear();

    for (const Module &m : layout.get_modules()) {
        build_module(m);
    }
    for (const Ladder &ladder : layout_ladders(layout)) {
        build_ladder(ladder);
    }
    slots = hold_slots(layout);
    cargo.resize(slots.size());
    build_cargo();
    build_gangway();

    // 船头楔：最前一排第 0 层的每一节前面各一只
    const Basis lay_flat(Vector3(1, 0, 0), Vector3(0, 0, 1), Vector3(0, -1, 0)); // 三棱柱躺倒：高 → z
    const int front = layout.min_z();
    for (const Module &m : layout.get_modules()) {
        if (m.at.y != 0 || m.at.z != front) {
            continue;
        }
        const real_t cx = real_t(cell_center_x(m.at.x)), zf = real_t(cell_center_z(front)) - 0.5f * CELL_L;
        Ref<PrismMesh> bow;
        bow.instantiate();
        bow->set_size(Vector3(CELL_W, BOW_WEDGE, DECK_T));
        bow->set_material(mat_wood);
        add_mesh(visual, vformat("Bow%d", m.at.x), bow, Transform3D(lay_flat, Vector3(cx, -0.5f * DECK_T, zf - 0.5f * BOW_WEDGE)));
        Ref<PrismMesh> bow_hull;
        bow_hull.instantiate();
        bow_hull->set_size(Vector3(HULL_W, BOW_WEDGE, HULL_H));
        bow_hull->set_material(mat_hull);
        add_mesh(visual, vformat("BowHull%d", m.at.x), bow_hull, Transform3D(lay_flat, Vector3(cx, -DECK_T - 0.5f * HULL_H, zf - 0.5f * BOW_WEDGE + 1.5f)));
    }

    // 追尾相机：船越长退得越远，越高抬得越高
    if (camera_rig && camera) {
        camera_rig->set_position(Vector3(0, 3.0f + real_t(cell_deck_y(std::max(0, layout.levels() - 1))), 0));
        camera->set_position(Vector3(0, 0, std::max(26.0f, real_t(stern_z(layout)) + 16.0f)));
    }

    emit_signal("refitted");
}

// 货箱：每个放着货的货位一只（带碰撞），箱顶一块按货物着色的盖板
void Ship::build_cargo() {
    if (!visual) {
        return;
    }
    for (Node *parent : { static_cast<Node *>(this), static_cast<Node *>(visual) }) {
        const TypedArray<Node> children = parent->get_children();
        for (int64_t k = 0; k < children.size(); k++) {
            Node *child = Object::cast_to<Node>(children[k]);
            if (child && child->has_meta(CARGO_META)) {
                parent->remove_child(child);
                child->queue_free();
            }
        }
    }
    ensure_materials();
    for (size_t k = 0; k < slots.size() && k < cargo.size(); k++) {
        if (cargo[k].is_empty() || !slots[k].open) {
            continue;
        }
        const HoldSlot &s = slots[k];
        const real_t x = real_t(s.x), y = real_t(s.y), z = real_t(s.z);
        const String name = vformat("Cargo%d", int(k));
        add_box(visual, this, name, Vector3(CRATE_SIZE, CRATE_H, CRATE_SIZE), at(x, y + 0.06f + 0.5f * CRATE_H, z), mat_crate, true);
        Ref<StandardMaterial3D> lid = make_material(cargo_color(cargo[k]), 0.9f);
        add_box(visual, this, name + String("Lid"), Vector3(CRATE_SIZE - 0.2f, 0.05f, CRATE_SIZE - 0.2f), at(x, y + 0.06f + CRATE_H + 0.025f, z), lid, false);
        for (const String &part : { name, name + String("Shape"), name + String("Lid") }) {
            Node *node = visual->get_node_or_null(NodePath(part));
            node = node ? node : get_node_or_null(NodePath(part));
            if (node) {
                node->set_meta(CARGO_META, true);
            }
        }
    }
}

// 野泊的舷梯：一块斜板（带碰撞）+ 两侧扶栏（只画）；收着时什么也不挂
void Ship::build_gangway() {
    if (!visual) {
        return;
    }
    for (Node *parent : { static_cast<Node *>(this), static_cast<Node *>(visual) }) {
        const TypedArray<Node> children = parent->get_children();
        for (int64_t k = 0; k < children.size(); k++) {
            Node *child = Object::cast_to<Node>(children[k]);
            if (child && child->has_meta(GANGWAY_META)) {
                parent->remove_child(child);
                child->queue_free();
            }
        }
    }
    if (gangway_side == 0) {
        return;
    }
    ensure_materials();
    const real_t edge = real_t(gangway_side > 0 ? starboard_edge_x(layout) : port_edge_x(layout));
    const real_t run = real_t(gangway_run), rise = real_t(gangway_rise), len = std::sqrt(run * run + rise * rise);
    const Vector3 along = Vector3(real_t(gangway_side) * run, -rise, 0) / len; // 顺着梯子往下
    Vector3 up(-along.y, along.x, 0);
    if (up.y < 0) {
        up = -up;
    }
    const Basis frame(along, up, along.cross(up));
    const Vector3 top(edge, 0, 0), foot = top + along * len;
    const real_t full = len + GANGWAY_BURY; // 梯顶齐着甲板边（再往里伸就顺着坡翘出一道坎）
    const Vector3 mid = top + along * (0.5f * full) - up * (0.5f * GANGWAY_T);
    const String name("Gangway");
    add_box(visual, this, name, Vector3(full, GANGWAY_T, GANGWAY_W), Transform3D(frame, mid), mat_wood, true);
    for (int side = -1; side <= 1; side += 2) {
        add_box(visual, this, name + vformat("Rail%d", side), Vector3(len, 0.08f, 0.08f), Transform3D(frame, 0.5f * (top + foot) + up * 0.9f + Vector3(0, 0, side * 0.5f * GANGWAY_W)), mat_rail, false);
    }
    for (const String &part : { name, name + String("Shape"), name + String("Rail-1"), name + String("Rail1") }) {
        Node *node = visual->get_node_or_null(NodePath(part));
        node = node ? node : get_node_or_null(NodePath(part));
        if (node) {
            node->set_meta(GANGWAY_META, true);
        }
    }
}

void Ship::set_gangway(int p_side, double p_run, double p_rise) {
    gangway_side = p_side > 0 ? 1 : (p_side < 0 ? -1 : 0);
    gangway_run = std::max(0.5, p_run);
    gangway_rise = std::max(0.0, p_rise);
    build_gangway();
}

void Ship::clear_gangway() {
    if (gangway_side == 0) {
        return;
    }
    gangway_side = 0;
    build_gangway();
}

Vector3 Ship::get_gangway_top() const {
    if (gangway_side == 0) {
        return get_global_position();
    }
    return to_global(Vector3(real_t(gangway_side > 0 ? starboard_edge_x(layout) : port_edge_x(layout)), 0, 0));
}

Vector3 Ship::get_gangway_foot() const {
    if (gangway_side == 0) {
        return get_global_position();
    }
    const real_t edge = real_t(gangway_side > 0 ? starboard_edge_x(layout) : port_edge_x(layout));
    return to_global(Vector3(edge + real_t(gangway_side * gangway_run), real_t(-gangway_rise), 0));
}

Object *Ship::landing_obstacle(const Transform3D &p_pose, double p_floor_y, const TypedArray<RID> &p_exclude, int p_gangway_side, double p_gangway_run,
        double p_gangway_rise) const {
    if (!is_inside_tree()) {
        return nullptr;
    }
    PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
    if (!space) {
        return nullptr;
    }
    const real_t bottom = real_t(p_floor_y + 0.5) - p_pose.origin.y; // 船的局部 y
    const real_t top = real_t(cell_deck_y(std::max(1, layout.levels())));
    if (top <= bottom) {
        return nullptr;
    }
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(Vector3(CELL_W, top - bottom, CELL_L));
    Ref<PhysicsShapeQueryParameters3D> query;
    query.instantiate();
    query->set_shape(box);
    query->set_collision_mask(1);
    query->set_exclude(p_exclude);
    const int front = layout.min_z();
    for (const Module &m : layout.get_modules()) {
        if (m.at.y != 0) {
            continue;
        }
        const real_t cx = real_t(cell_center_x(m.at.x)), cz = real_t(cell_center_z(m.at.z));
        // 船头楔那一截并进最前一排：盒子往前加长
        const real_t extra = m.at.z == front ? BOW_WEDGE : 0.0f;
        box->set_size(Vector3(CELL_W, top - bottom, CELL_L + extra));
        query->set_transform(p_pose * at(cx, 0.5f * (top + bottom), cz - 0.5f * extra));
        const TypedArray<Dictionary> hits = space->intersect_shape(query, 1);
        if (!hits.is_empty()) {
            const Dictionary hit = hits[0];
            Object *collider = hit.get("collider", Variant());
            return collider;
        }
    }
    if (p_gangway_side == 0) {
        return nullptr;
    }
    // 舷梯上方：人走下去要过的那一条
    const real_t edge = real_t(p_gangway_side > 0 ? starboard_edge_x(layout) : port_edge_x(layout));
    const real_t run = real_t(p_gangway_run), rise = real_t(p_gangway_rise), len = std::sqrt(run * run + rise * rise);
    if (len < 1.5f) {
        return nullptr;
    }
    const Vector3 along = Vector3(real_t(p_gangway_side) * run, -rise, 0) / len;
    Vector3 up(-along.y, along.x, 0);
    if (up.y < 0) {
        up = -up;
    }
    const real_t checked = len - 1.2f;
    box->set_size(Vector3(checked, 2.0f, GANGWAY_W));
    query->set_transform(p_pose * Transform3D(Basis(along, up, along.cross(up)), Vector3(edge, 0, 0) + along * (0.5f * checked) + up * 1.3f));
    const TypedArray<Dictionary> hits = space->intersect_shape(query, 1);
    if (!hits.is_empty()) {
        const Dictionary hit = hits[0];
        Object *collider = hit.get("collider", Variant());
        return collider;
    }
    return nullptr;
}

Vector3 Ship::get_hold_slot_position(int p_slot) const {
    ERR_FAIL_INDEX_V(p_slot, int(slots.size()), get_global_position());
    const HoldSlot &s = slots[size_t(p_slot)];
    return to_global(Vector3(real_t(s.x), real_t(s.y), real_t(s.z)));
}

bool Ship::is_hold_slot_open(int p_slot) const {
    return p_slot >= 0 && p_slot < int(slots.size()) && slots[size_t(p_slot)].open;
}

int Ship::find_hold_slot(const Vector3 &p_global, double p_range) const {
    int best = -1;
    double best_d = p_range;
    for (int k = 0; k < int(slots.size()); k++) {
        if (!slots[size_t(k)].open) {
            continue;
        }
        const Vector3 at_slot = get_hold_slot_position(k);
        const double d = Vector2(p_global.x - at_slot.x, p_global.z - at_slot.z).length();
        if (d <= best_d && std::abs(p_global.y - at_slot.y) <= 1.5) {
            best_d = d;
            best = k;
        }
    }
    return best;
}

void Ship::set_cargo(int p_slot, const String &p_item) {
    ERR_FAIL_INDEX(p_slot, int(cargo.size()));
    cargo[size_t(p_slot)] = p_item;
    build_cargo();
}

String Ship::get_cargo(int p_slot) const {
    return p_slot >= 0 && p_slot < int(cargo.size()) ? cargo[size_t(p_slot)] : String();
}

bool Ship::is_aboard(const Vector3 &p_global) const {
    const Vector3 local = get_global_transform().affine_inverse().xform(p_global);
    return layout_contains(layout, local.x, local.y, local.z);
}

int Ship::get_cargo_count() const {
    int n = 0;
    for (const String &item : cargo) {
        n += item.is_empty() ? 0 : 1;
    }
    return n;
}

void Ship::set_layout(const PackedStringArray &p_ids) {
    std::vector<std::string> ids;
    for (int64_t k = 0; k < p_ids.size(); k++) {
        ids.emplace_back(String(p_ids[k]).utf8().get_data());
    }
    bool ok = false;
    const ShipLayout next = layout_from_ids(ids, &ok);
    if (!ok) {
        ERR_PRINT("Ship: layout 里有认不出的条目");
    }
    const std::string problem = layout_problem(next);
    if (!problem.empty()) {
        ERR_PRINT(String("Ship: layout 不合法——") + String::utf8(problem.c_str()));
        return;
    }
    layout = next;
    if (visual) {
        rebuild();
    }
}

PackedStringArray Ship::get_layout() const {
    PackedStringArray ids;
    for (const std::string &id : layout_ids(layout)) {
        ids.push_back(String::utf8(id.c_str()));
    }
    return ids;
}

bool Ship::apply_layout(const ShipLayout &p_layout) {
    if (!layout_problem(p_layout).empty()) {
        return false;
    }
    layout = p_layout;
    if (visual) {
        rebuild();
    }
    return true;
}

Dictionary Ship::get_stats() const {
    Dictionary d;
    d["sections"] = stats.sections;
    d["mass"] = stats.mass;
    d["thrust"] = stats.thrust;
    d["hold"] = stats.hold;
    d["bay"] = stats.bay;
    d["max_speed"] = stats.max_speed;
    d["max_climb"] = stats.max_climb;
    d["max_yaw_rate"] = stats.max_yaw_rate;
    d["can_fly"] = stats.can_fly();
    return d;
}

void Ship::_physics_process(double p_delta) {
    if (mooring) {
        step_mooring(p_delta);
    } else {
        step_flight(p_delta);
    }
    if (visual) {
        const real_t blend = real_t(1.0 - std::exp(-4.0 * p_delta));
        Vector3 tilt = visual->get_rotation();
        tilt.z += (real_t(state.yaw_rate * BANK_PER_YAW_RATE) - tilt.z) * blend; // 左转（角速度为正）向左倾
        tilt.x += (real_t(state.vy * TRIM_PER_CLIMB) - tilt.x) * blend;
        visual->set_rotation(tilt);
    }
}

void Ship::step_flight(double p_delta) {
    FlightInput in;
    if (controlled) {
        Input *input = Input::get_singleton();
        in.throttle = input->get_axis(input_actions::MOVE_BACK, input_actions::MOVE_FORWARD);
        in.rudder = input->get_axis(input_actions::MOVE_LEFT, input_actions::MOVE_RIGHT);
        in.lift = input->get_axis(input_actions::DESCEND, input_actions::ASCEND);
    }
    state = flight_step(state, in, apply_gear(params, gear), p_delta);

    // 别沉进云海
    const double altitude = get_global_position().y;
    if (altitude < min_altitude) {
        state.vy = std::max(state.vy, 2.0 * (min_altitude - altitude));
    }

    // 没人掌舵又已停稳：彻底不动，甲板上的人脚下才是死的
    if (!controlled && get_speed() < 0.01 && std::abs(state.yaw_rate) < 0.001) {
        state.vx = state.vy = state.vz = state.yaw_rate = 0.0;
        return;
    }

    set_rotation(Vector3(0, real_t(state.yaw), 0));
    set_velocity(Vector3(real_t(state.vx), real_t(state.vy), real_t(state.vz)));
    move_and_slide();
    // 撞上东西后的实际速度写回模型，否则它会顶着岛一直「加速」
    const Vector3 actual = get_velocity();
    state.vx = actual.x;
    state.vy = actual.y;
    state.vz = actual.z;
}

void Ship::step_mooring(double p_delta) {
    Vector3 position = get_global_position();
    const Vector3 to_berth = berth_position - position;
    const double distance = to_berth.length();
    const double step = std::min(MOOR_MAX_SPEED, MOOR_GAIN * distance + MOOR_MIN_SPEED) * p_delta;
    const bool there = distance <= step;
    position = there ? berth_position : position + to_berth * real_t(step / distance);

    const double yaw_error = angle_difference(state.yaw, berth_yaw);
    const double yaw_step = MOOR_YAW_RATE * p_delta;
    const bool aligned = std::abs(yaw_error) <= yaw_step;
    state.yaw = aligned ? berth_yaw : state.yaw + (yaw_error > 0 ? yaw_step : -yaw_step);
    state.yaw_rate = aligned ? 0.0 : (yaw_error > 0 ? MOOR_YAW_RATE : -MOOR_YAW_RATE);

    const Vector3 moved = position - get_global_position();
    state.vx = moved.x / p_delta;
    state.vy = moved.y / p_delta;
    state.vz = moved.z / p_delta;
    set_global_position(position);
    set_rotation(Vector3(0, real_t(state.yaw), 0));

    if (there && aligned) {
        mooring = false;
        state.vx = state.vy = state.vz = state.yaw_rate = 0.0;
        emit_signal("moored");
    }
}

void Ship::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!controlled || !camera_rig || Input::get_singleton()->get_mouse_mode() != Input::MOUSE_MODE_CAPTURED) {
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        const Vector2 rel = motion->get_relative();
        camera_yaw -= rel.x * mouse_sensitivity;
        camera_pitch = std::clamp(camera_pitch - rel.y * mouse_sensitivity, CAMERA_PITCH_MIN, CAMERA_PITCH_MAX);
        camera_rig->set_rotation(Vector3(real_t(camera_pitch), real_t(camera_yaw), 0));
    }
}

void Ship::set_controlled(bool p_controlled) {
    controlled = p_controlled;
    if (controlled && camera_rig) {
        // 上舵时相机回到正后方
        camera_yaw = 0.0;
        camera_pitch = -0.3;
        camera_rig->set_rotation(Vector3(real_t(camera_pitch), 0, 0));
    }
}

// 船头朝 p_yaw 时，把面向接触线那一侧的船边贴上去，原点该在哪：
// 船的 +x（右舷）与接触线的 X 同向就是右舷贴上，反向就是左舷贴上
Vector3 Ship::origin_for(const Transform3D &p_contact, double p_yaw) const {
    const Vector3 toward = p_contact.basis.get_column(0);
    const Vector3 starboard = Basis(Vector3(0, 1, 0), real_t(p_yaw)).xform(Vector3(1, 0, 0));
    const double edge = starboard.dot(toward) >= 0 ? starboard_edge_x(layout) : -port_edge_x(layout);
    return p_contact.origin - toward * real_t(edge);
}

void Ship::set_gear(int p_gear) {
    gear = Math::clamp(p_gear, 0, flight_gears::count() - 1);
}

String Ship::get_gear_name() const {
    return String::utf8(flight_gears::at(gear).name);
}

void Ship::place_at(const Transform3D &p_contact, bool p_keep_heading) {
    clear_gangway();
    mooring = false;
    gear = 0;
    const double contact_yaw = p_contact.basis.get_euler().y;
    const double yaw = p_keep_heading && std::abs(angle_difference(state.yaw, contact_yaw)) > Math_PI / 2 ? contact_yaw + Math_PI : contact_yaw;
    state = FlightState();
    state.yaw = yaw;
    set_global_position(origin_for(p_contact, yaw));
    set_rotation(Vector3(0, real_t(yaw), 0));
    set_velocity(Vector3());
}

void Ship::moor_at(const Transform3D &p_contact) {
    clear_gangway();
    // 两舷都有舷门，船头朝哪边都能靠：取转得少的那个
    const double yaw = p_contact.basis.get_euler().y;
    berth_yaw = std::abs(angle_difference(state.yaw, yaw)) <= Math_PI / 2 ? yaw : yaw + Math_PI;
    berth_position = origin_for(p_contact, berth_yaw);
    mooring = true;
    gear = 0;
}

bool Ship::is_at(const Transform3D &p_contact, double p_tolerance) const {
    if (mooring) {
        return false;
    }
    const double yaw = p_contact.basis.get_euler().y;
    const Vector3 here = get_global_position();
    return here.distance_to(origin_for(p_contact, yaw)) <= p_tolerance || here.distance_to(origin_for(p_contact, yaw + Math_PI)) <= p_tolerance;
}

double Ship::get_speed() const {
    return std::sqrt(state.vx * state.vx + state.vy * state.vy + state.vz * state.vz);
}

Vector3 Ship::get_helm_position() const {
    return to_global(helm_local);
}

Transform3D Ship::get_seat_transform() const {
    return get_global_transform() * Transform3D(Basis(), seat_local);
}

Transform3D Ship::get_exit_transform() const {
    return get_global_transform() * Transform3D(Basis(), exit_local);
}

Vector3 Ship::get_deck_position(int p_x, int p_z) const {
    const int height = layout.column_height(p_x, p_z);
    if (height == 0) {
        return get_global_position();
    }
    return to_global(Vector3(real_t(cell_center_x(p_x)), real_t(cell_deck_y(height - 1)), real_t(cell_center_z(p_z))));
}

PackedVector3Array Ship::get_ladder_feet() const {
    PackedVector3Array out;
    for (int64_t k = 0; k < ladder_feet_local.size(); k++) {
        out.push_back(to_global(ladder_feet_local[k]));
    }
    return out;
}

PackedVector3Array Ship::get_ladder_tops() const {
    PackedVector3Array out;
    for (int64_t k = 0; k < ladder_tops_local.size(); k++) {
        out.push_back(to_global(ladder_tops_local[k]));
    }
    return out;
}
