#include "farm.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

const Vector2i Farm::NO_CELL = Vector2i(-1, -1);

static_assert(int(Farm::PLOT_RIPE) == int(PlotState::RIPE) && int(Farm::ACTION_HARVEST) == int(FarmAction::HARVEST) &&
                int(Farm::LAND_TRAIL) == int(Arability::TRAIL),
        "Farm 的枚举要与 farm_model.h 的逐项对齐");

namespace {

const char *GENERATED_META = "farm_generated";

const Color SOIL(0.30f, 0.22f, 0.15f);
const Color SOIL_RIDGE(0.36f, 0.27f, 0.18f);
const Color SPROUT(0.47f, 0.66f, 0.26f);
const Color LEAF(0.30f, 0.53f, 0.19f);
const Color STALK_RIPE(0.62f, 0.58f, 0.27f);
const Color EAR(0.85f, 0.67f, 0.22f); // 粟穗

constexpr real_t SOIL_LIFT = 0.04f; // 垦过的土比草皮高一点，免得共面闪烁
constexpr real_t BALK = 0.03f; // 格与格之间留的田埂（占格宽的比例，每侧）
constexpr int RIDGES = 4; // 每格几条垄
constexpr int PLANTS_PER_RIDGE = 4;
constexpr real_t RIDGE_HALF_WIDTH = 0.085f; // 占格宽的比例
constexpr real_t RIDGE_HEIGHT = 0.13f;
constexpr real_t HIGHLIGHT_LIFT = 0.1f;
constexpr real_t HIGHLIGHT_WIDTH = 0.05f; // 占格宽的比例

// 粟苗各外观阶段的高与半宽（米）
constexpr real_t PLANT_HEIGHT[FarmModel::STAGES] = { 0.10f, 0.32f, 0.62f, 0.78f };
constexpr real_t PLANT_HALF_WIDTH[FarmModel::STAGES] = { 0.025f, 0.04f, 0.05f, 0.05f };

// 一格地：四个角的局部坐标，以及格内任一点 (u 沿 x, v 沿 z ∈ [0, 1]) 的贴地位置——按地形网格同样的两个三角面取，
// 所以画出来的东西严丝合缝地贴在坡上。
struct Patch {
    Vector3 p00, p01, p10, p11; // (i, j) (i, j+1) (i+1, j) (i+1, j+1)

    Patch(const IslandField &f, int i, int j) {
        auto corner = [&f](int a, int b) { return Vector3(real_t(f.coord(b)), f.height[f.index(a, b)], real_t(f.coord(a))); };
        p00 = corner(i, j);
        p01 = corner(i, j + 1);
        p10 = corner(i + 1, j);
        p11 = corner(i + 1, j + 1);
    }

    Vector3 at(real_t u, real_t v, real_t p_lift = 0.0f) const {
        const Vector3 ground = u + v <= 1.0f ? p00 + (p01 - p00) * u + (p10 - p00) * v : p11 + (p10 - p11) * (1.0f - u) + (p01 - p11) * (1.0f - v);
        return ground + Vector3(0, p_lift, 0);
    }
};

struct MeshBuilder {
    PackedVector3Array vertices;
    PackedVector3Array normals;
    PackedColorArray colors;
    PackedInt32Array indices;

    // 四个点绕一圈；朝 p_facing 的那一面是正面
    void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, const Vector3 &p_facing, const Color &p_color) {
        // Godot 的面法线 = (a − c) × (a − b)
        Vector3 normal = (a - c).cross(a - b).normalized();
        if (normal.dot(p_facing) < 0) {
            std::swap(b, d);
            normal = -normal;
        }
        const int v = int(vertices.size());
        for (const Vector3 &p : { a, b, c, d }) {
            vertices.push_back(p);
            normals.push_back(normal);
            colors.push_back(p_color);
        }
        for (int index : { v, v + 1, v + 2, v, v + 2, v + 3 }) {
            indices.push_back(index);
        }
    }

    void triangle(const Vector3 &a, Vector3 b, Vector3 c, const Vector3 &p_facing, const Color &p_color) {
        Vector3 normal = (a - c).cross(a - b).normalized();
        if (normal.dot(p_facing) < 0) {
            std::swap(b, c);
            normal = -normal;
        }
        const int v = int(vertices.size());
        for (const Vector3 &p : { a, b, c }) {
            vertices.push_back(p);
            normals.push_back(normal);
            colors.push_back(p_color);
        }
        for (int index : { v, v + 1, v + 2 }) {
            indices.push_back(index);
        }
    }

    // 立着的方柱：四个侧面 + 顶
    void column(const Vector3 &p_base, real_t p_half_width, real_t p_height, const Color &p_color) {
        const Vector3 up(0, p_height, 0);
        const Vector3 corners[4] = { Vector3(-p_half_width, 0, -p_half_width), Vector3(p_half_width, 0, -p_half_width),
            Vector3(p_half_width, 0, p_half_width), Vector3(-p_half_width, 0, p_half_width) };
        for (int s = 0; s < 4; s++) {
            const Vector3 a = p_base + corners[s], b = p_base + corners[(s + 1) % 4];
            quad(a, b, b + up, a + up, corners[s] + corners[(s + 1) % 4], p_color);
        }
        quad(p_base + corners[0] + up, p_base + corners[1] + up, p_base + corners[2] + up, p_base + corners[3] + up, Vector3(0, 1, 0), p_color);
    }

    Ref<ArrayMesh> commit() const {
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = vertices;
        arrays[Mesh::ARRAY_NORMAL] = normals;
        arrays[Mesh::ARRAY_COLOR] = colors;
        arrays[Mesh::ARRAY_INDEX] = indices;
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        return mesh;
    }
};

// 每格自己的一点抖动，让苗不站成阅兵方阵；只与格子有关，重画时不变
real_t jitter(const Vector2i &p_cell, int p_salt) {
    uint32_t h = uint32_t(p_cell.x) * 73856093u ^ uint32_t(p_cell.y) * 19349663u ^ uint32_t(p_salt) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return real_t(h & 0xFFFF) / 65535.0f - 0.5f;
}

Ref<ArrayMesh> build_plot_mesh(const Patch &p_patch, const Vector2i &p_cell, const Plot &p_plot) {
    MeshBuilder b;
    const Vector3 up(0, 1, 0);
    const real_t lo = BALK, hi = 1.0f - BALK;

    // 土面：与地形同一条对角线分成两个三角面
    b.triangle(p_patch.at(lo, lo, SOIL_LIFT), p_patch.at(hi, lo, SOIL_LIFT), p_patch.at(lo, hi, SOIL_LIFT), up, SOIL);
    b.triangle(p_patch.at(hi, lo, SOIL_LIFT), p_patch.at(hi, hi, SOIL_LIFT), p_patch.at(lo, hi, SOIL_LIFT), up, SOIL);

    for (int r = 0; r < RIDGES; r++) {
        // 垄：沿 x 的三棱柱，在地形的对角线处折一下
        const real_t v = (real_t(r) + 0.5f) / RIDGES;
        const real_t stops[3] = { lo, std::clamp(1.0f - v, lo, hi), hi };
        for (int s = 0; s < 2; s++) {
            const real_t u0 = stops[s], u1 = stops[s + 1];
            if (u1 - u0 < 1e-4f) {
                continue;
            }
            const Vector3 crest0 = p_patch.at(u0, v, SOIL_LIFT + RIDGE_HEIGHT), crest1 = p_patch.at(u1, v, SOIL_LIFT + RIDGE_HEIGHT);
            b.quad(p_patch.at(u0, v - RIDGE_HALF_WIDTH, SOIL_LIFT), p_patch.at(u1, v - RIDGE_HALF_WIDTH, SOIL_LIFT), crest1, crest0, Vector3(0, 1, -1), SOIL_RIDGE);
            b.quad(p_patch.at(u0, v + RIDGE_HALF_WIDTH, SOIL_LIFT), p_patch.at(u1, v + RIDGE_HALF_WIDTH, SOIL_LIFT), crest1, crest0, Vector3(0, 1, 1), SOIL_RIDGE);
        }

        if (p_plot.state != PlotState::GROWING && p_plot.state != PlotState::RIPE) {
            continue;
        }
        const int stage = FarmModel::stage_of(p_plot);
        const bool ripe = p_plot.state == PlotState::RIPE;
        const Color stalk = ripe ? STALK_RIPE : SPROUT.lerp(LEAF, real_t(stage) / (FarmModel::STAGES - 2));
        for (int q = 0; q < PLANTS_PER_RIDGE; q++) {
            const int salt = r * PLANTS_PER_RIDGE + q;
            const real_t u = (real_t(q) + 0.5f) / PLANTS_PER_RIDGE + 0.08f * jitter(p_cell, salt);
            const real_t height = PLANT_HEIGHT[stage] * (1.0f + 0.25f * jitter(p_cell, salt + 100));
            const Vector3 base = p_patch.at(std::clamp(u, lo, hi), v, SOIL_LIFT + RIDGE_HEIGHT - 0.02f);
            b.column(base, PLANT_HALF_WIDTH[stage], height, stalk);
            if (ripe) {
                b.column(base + Vector3(0, height, 0), 0.075f, 0.24f, EAR);
            }
        }
    }
    return b.commit();
}

Ref<ArrayMesh> build_highlight_mesh(const Patch &p_patch) {
    MeshBuilder b;
    const Vector3 up(0, 1, 0);
    const Color white(1, 1, 1);
    const real_t w = HIGHLIGHT_WIDTH, lift = HIGHLIGHT_LIFT;
    // 四条边各一条带子；横边在对角线处（u + v = 1）折一下
    auto strip = [&](real_t u0, real_t v0, real_t u1, real_t v1, real_t iu, real_t iv) {
        b.quad(p_patch.at(u0, v0, lift), p_patch.at(u1, v1, lift), p_patch.at(u1 + iu, v1 + iv, lift), p_patch.at(u0 + iu, v0 + iv, lift), up, white);
    };
    strip(0, 0, 1 - w, 0, 0, w); // 上边：内缘 v = w，对角线在 u = 1 − w 处
    strip(1 - w, 0, 1, 0, 0, w);
    strip(0, 1 - w, w, 1 - w, 0, w); // 下边：内缘 v = 1 − w
    strip(w, 1 - w, 1, 1 - w, 0, w);
    strip(0, w, 0, 1 - w, w, 0); // 左边
    strip(1 - w, w, 1 - w, 1 - w, w, 0); // 右边
    return b.commit();
}

} // namespace

void Farm::_bind_methods() {
    ClassDB::bind_method(D_METHOD("cell_at", "global_position"), &Farm::cell_at);
    ClassDB::bind_method(D_METHOD("get_cell_center", "cell"), &Farm::get_cell_center);
    ClassDB::bind_method(D_METHOD("find_nearest_arable", "global_position"), &Farm::find_nearest_arable);
    ClassDB::bind_method(D_METHOD("get_land", "cell"), &Farm::get_land);
    ClassDB::bind_method(D_METHOD("get_plot", "cell"), &Farm::get_plot);
    ClassDB::bind_method(D_METHOD("get_growth", "cell"), &Farm::get_growth);
    ClassDB::bind_method(D_METHOD("get_moisture", "cell"), &Farm::get_moisture);
    ClassDB::bind_method(D_METHOD("get_plot_count", "state"), &Farm::get_plot_count);
    ClassDB::bind_method(D_METHOD("available_action", "cell", "grain"), &Farm::available_action);
    ClassDB::bind_method(D_METHOD("perform", "cell", "action", "grain"), &Farm::perform);
    ClassDB::bind_method(D_METHOD("set_highlight", "cell", "color"), &Farm::set_highlight);
    ClassDB::bind_method(D_METHOD("on_island_rebuilt"), &Farm::on_island_rebuilt);

    ClassDB::bind_method(D_METHOD("set_max_river_distance", "value"), &Farm::set_max_river_distance);
    ClassDB::bind_method(D_METHOD("get_max_river_distance"), &Farm::get_max_river_distance);
    ClassDB::bind_method(D_METHOD("set_grow_seconds", "value"), &Farm::set_grow_seconds);
    ClassDB::bind_method(D_METHOD("get_grow_seconds"), &Farm::get_grow_seconds);
    ClassDB::bind_method(D_METHOD("set_harvest_yield", "value"), &Farm::set_harvest_yield);
    ClassDB::bind_method(D_METHOD("get_harvest_yield"), &Farm::get_harvest_yield);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_river_distance", PROPERTY_HINT_RANGE, "4,60,0.5,suffix:m"), "set_max_river_distance", "get_max_river_distance");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grow_seconds", PROPERTY_HINT_RANGE, "1,1800,1,suffix:s"), "set_grow_seconds", "get_grow_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "harvest_yield", PROPERTY_HINT_RANGE, "1,20,1"), "set_harvest_yield", "get_harvest_yield");

    ADD_SIGNAL(MethodInfo("plot_changed", PropertyInfo(Variant::VECTOR2I, "cell")));

    BIND_ENUM_CONSTANT(PLOT_WILD);
    BIND_ENUM_CONSTANT(PLOT_TILLED);
    BIND_ENUM_CONSTANT(PLOT_GROWING);
    BIND_ENUM_CONSTANT(PLOT_RIPE);
    BIND_ENUM_CONSTANT(ACTION_NONE);
    BIND_ENUM_CONSTANT(ACTION_TILL);
    BIND_ENUM_CONSTANT(ACTION_SOW);
    BIND_ENUM_CONSTANT(ACTION_HARVEST);
    BIND_ENUM_CONSTANT(LAND_ARABLE);
    BIND_ENUM_CONSTANT(LAND_NO_GROUND);
    BIND_ENUM_CONSTANT(LAND_WATER);
    BIND_ENUM_CONSTANT(LAND_TOO_DRY);
    BIND_ENUM_CONSTANT(LAND_TOO_STEEP);
    BIND_ENUM_CONSTANT(LAND_DOCK);
    BIND_ENUM_CONSTANT(LAND_TRAIL);
}

void Farm::_ready() {
    // 编辑器里不种地
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        return;
    }
    island = Object::cast_to<Island>(get_parent());
    ERR_FAIL_NULL_MSG(island, "Farm 要挂在 Island 下面");

    plot_material.instantiate();
    plot_material->set_roughness(1.0);
    plot_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    plot_material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);

    highlight_material.instantiate();
    highlight_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    highlight_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);

    // 子节点先于父节点 _ready：岛此刻还没生成，等它的 rebuilt；已经生成过（运行中才挂上来）就直接勘察
    island->connect("rebuilt", Callable(this, "on_island_rebuilt"));
    if (island->get_field().n > 0) {
        on_island_rebuilt();
    }
}

void Farm::on_island_rebuilt() {
    // 地形换了，旧地块连同它们的样子一起作废
    for (const KeyValue<Vector2i, MeshInstance3D *> &entry : plot_nodes) {
        entry.value->queue_free();
    }
    plot_nodes.clear();
    set_highlight(NO_CELL, Color());
    model = FarmModel(survey_farmland(island->get_field(), island->get_params().dock_apron_m, rules), rules);
}

void Farm::apply_rules() {
    model.set_rules(rules);
}

void Farm::_physics_process(double p_delta) {
    const std::vector<int> changed = model.advance(p_delta);
    for (size_t k = 0; k + 1 < changed.size(); k += 2) {
        refresh_plot(Vector2i(changed[k], changed[k + 1]));
    }
}

bool Farm::has_ground(const Vector2i &p_cell) const {
    return island && model.get_survey().at(p_cell.x, p_cell.y) != Arability::NO_GROUND;
}

Vector2i Farm::cell_at(const Vector3 &p_global) const {
    if (!island) {
        return NO_CELL;
    }
    const IslandField &field = island->get_field();
    const Vector3 local = island->get_global_transform().affine_inverse().xform(p_global);
    const Vector2i cell(int(std::floor((local.z + field.half_extent()) / field.cell_m)), int(std::floor((local.x + field.half_extent()) / field.cell_m)));
    return has_ground(cell) ? cell : NO_CELL;
}

Vector3 Farm::get_cell_center(const Vector2i &p_cell) const {
    ERR_FAIL_COND_V(!has_ground(p_cell), Vector3());
    return island->get_global_transform().xform(Patch(island->get_field(), p_cell.x, p_cell.y).at(0.5f, 0.5f));
}

Vector2i Farm::find_nearest_arable(const Vector3 &p_global) const {
    if (!island) {
        return NO_CELL;
    }
    const IslandField &field = island->get_field();
    const Vector3 local = island->get_global_transform().affine_inverse().xform(p_global);
    Vector2i best = NO_CELL;
    double best_d2 = 1e18;
    for (int i = 0; i < field.n - 1; i++) {
        for (int j = 0; j < field.n - 1; j++) {
            if (model.available_action(i, j, 0) != FarmAction::TILL) {
                continue;
            }
            const double dx = field.coord(j) + 0.5 * field.cell_m - local.x, dz = field.coord(i) + 0.5 * field.cell_m - local.z;
            if (dx * dx + dz * dz < best_d2) {
                best_d2 = dx * dx + dz * dz;
                best = Vector2i(i, j);
            }
        }
    }
    return best;
}

Farm::Land Farm::get_land(const Vector2i &p_cell) const {
    return Land(model.get_survey().at(p_cell.x, p_cell.y));
}

Farm::State Farm::get_plot(const Vector2i &p_cell) const {
    return State(model.plot_at(p_cell.x, p_cell.y).state);
}

double Farm::get_growth(const Vector2i &p_cell) const {
    const Plot plot = model.plot_at(p_cell.x, p_cell.y);
    return plot.state == PlotState::RIPE ? 1.0 : plot.growth;
}

double Farm::get_moisture(const Vector2i &p_cell) const {
    const FarmSurvey &survey = model.get_survey();
    return survey.in_range(p_cell.x, p_cell.y) ? survey.moisture[survey.index(p_cell.x, p_cell.y)] : 0.0;
}

int Farm::get_plot_count(State p_state) const {
    return model.count(PlotState(p_state));
}

Farm::Action Farm::available_action(const Vector2i &p_cell, int p_grain) const {
    return Action(model.available_action(p_cell.x, p_cell.y, p_grain));
}

int Farm::perform(const Vector2i &p_cell, Action p_action, int p_grain) {
    if (p_action == ACTION_NONE || available_action(p_cell, p_grain) != p_action) {
        return 0;
    }
    const int delta = model.perform(p_cell.x, p_cell.y, FarmAction(p_action), p_grain);
    refresh_plot(p_cell);
    return delta;
}

void Farm::refresh_plot(const Vector2i &p_cell) {
    const Plot plot = model.plot_at(p_cell.x, p_cell.y);
    MeshInstance3D *node = plot_nodes.has(p_cell) ? plot_nodes[p_cell] : nullptr;
    if (plot.state == PlotState::WILD) {
        if (node) {
            node->queue_free();
            plot_nodes.erase(p_cell);
        }
    } else {
        if (!node) {
            node = memnew(MeshInstance3D);
            node->set_name(vformat("Plot_%d_%d", p_cell.x, p_cell.y));
            node->set_meta(GENERATED_META, true);
            node->set_material_override(plot_material);
            add_child(node);
            plot_nodes[p_cell] = node;
        }
        node->set_mesh(build_plot_mesh(Patch(island->get_field(), p_cell.x, p_cell.y), p_cell, plot));
    }
    emit_signal("plot_changed", p_cell);
}

void Farm::set_highlight(const Vector2i &p_cell, const Color &p_color) {
    if (!has_ground(p_cell)) {
        if (highlight) {
            highlight->set_visible(false);
        }
        highlighted = NO_CELL;
        return;
    }
    if (!highlight) {
        highlight = memnew(MeshInstance3D);
        highlight->set_name("Highlight");
        highlight->set_meta(GENERATED_META, true);
        highlight->set_material_override(highlight_material);
        highlight->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        add_child(highlight);
    }
    if (p_cell != highlighted) {
        highlight->set_mesh(build_highlight_mesh(Patch(island->get_field(), p_cell.x, p_cell.y)));
        highlighted = p_cell;
    }
    highlight_material->set_albedo(p_color);
    highlight->set_visible(true);
}

void Farm::set_max_river_distance(double p_value) {
    rules.max_river_m = p_value;
    // 勘察结果跟着变，连已垦的地一起作废：这是摆场景时调的参数，不是玩到一半改的
    if (island && island->get_field().n > 0) {
        on_island_rebuilt();
    }
}

void Farm::set_grow_seconds(double p_value) {
    rules.grow_seconds = p_value;
    apply_rules();
}

void Farm::set_harvest_yield(int p_value) {
    rules.harvest_yield = p_value;
    apply_rules();
}
