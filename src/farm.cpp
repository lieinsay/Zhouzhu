#include "farm.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "curved_material.h"
#include "local_season.h"
#include "world_clock.h"

using namespace godot;

const Vector2i Farm::NO_CELL = Vector2i(-1, -1);

static_assert(int(Farm::PLOT_RIPE) == int(PlotState::RIPE) && int(Farm::ACTION_HARVEST) == int(FarmAction::HARVEST) &&
                int(Farm::LAND_YARD) == int(Arability::YARD),
        "Farm 的枚举要与 farm_model.h 的逐项对齐");

namespace {

const char *GENERATED_META = "farm_generated";

const Color SOIL(0.30f, 0.22f, 0.15f);
const Color SOIL_RIDGE(0.36f, 0.27f, 0.18f);
const Color SPROUT(0.47f, 0.66f, 0.26f);
const Color LEAF(0.30f, 0.53f, 0.19f);

constexpr real_t SOIL_LIFT = 0.04f; // 垦过的土比草皮高一点，免得共面闪烁
constexpr real_t BALK = 0.03f; // 格与格之间留的田埂（占格宽的比例，每侧）
constexpr int RIDGES = 4; // 每格几条垄
constexpr int PLANTS_PER_RIDGE = 4;
constexpr real_t RIDGE_HALF_WIDTH = 0.085f; // 占格宽的比例
constexpr real_t RIDGE_HEIGHT = 0.13f;
constexpr real_t HIGHLIGHT_LIFT = 0.1f;
constexpr real_t HIGHLIGHT_WIDTH = 0.05f; // 占格宽的比例

// 一种作物的画法：各外观阶段的苗高与半宽（米），熟了的秆色，顶上那一截（粟穗 / 豆荚）的颜色与尺寸
struct CropLook {
    real_t height[FarmModel::STAGES];
    real_t half_width[FarmModel::STAGES];
    Color stalk_ripe;
    Color head;
    real_t head_half_width;
    real_t head_height;
};

const CropLook MILLET_LOOK = { { 0.10f, 0.32f, 0.62f, 0.78f }, { 0.025f, 0.04f, 0.05f, 0.05f }, Color(0.62f, 0.58f, 0.27f), Color(0.85f, 0.67f, 0.22f), 0.075f, 0.24f };
// 菽：矮而蓬，熟了秆子发褐，顶上一团淡黄绿的豆荚
const CropLook BEAN_LOOK = { { 0.08f, 0.22f, 0.36f, 0.42f }, { 0.03f, 0.07f, 0.10f, 0.11f }, Color(0.45f, 0.40f, 0.22f), Color(0.72f, 0.74f, 0.38f), 0.13f, 0.12f };

const CropLook &look_of(const Plot &p_plot) {
    return p_plot.crop && std::strcmp(p_plot.crop->id, "bean") == 0 ? BEAN_LOOK : MILLET_LOOK;
}

// 一格地：四个角（相对左上角节点 (i, j) 的坐标，y 是地面高），以及格内任一点 (u 沿 x, v 沿 z ∈ [0, 1]) 的贴地位置——
// 按地形网格同样的两个三角面取，所以画出来的东西严丝合缝地贴在坡上。
struct Patch {
    Vector3 p00, p01, p10, p11; // (i, j) (i, j+1) (i+1, j) (i+1, j+1)

    Patch(const MapGround &g, const PlotLattice &l, int i, int j) {
        auto corner = [&](int a, int b) {
            PlotNode node;
            g.plot_node(l, a, b, node);
            return Vector3(real_t(l.node_x(b) - l.node_x(j)), real_t(node.y), real_t(l.node_z(a) - l.node_z(i)));
        };
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
        const CropLook &look = look_of(p_plot);
        const Color stalk = ripe ? look.stalk_ripe : SPROUT.lerp(LEAF, real_t(stage) / (FarmModel::STAGES - 2));
        for (int q = 0; q < PLANTS_PER_RIDGE; q++) {
            const int salt = r * PLANTS_PER_RIDGE + q;
            const real_t u = (real_t(q) + 0.5f) / PLANTS_PER_RIDGE + 0.08f * jitter(p_cell, salt);
            const real_t height = look.height[stage] * (1.0f + 0.25f * jitter(p_cell, salt + 100));
            const Vector3 base = p_patch.at(std::clamp(u, lo, hi), v, SOIL_LIFT + RIDGE_HEIGHT - 0.02f);
            b.column(base, look.half_width[stage], height, stalk);
            if (ripe) {
                b.column(base + Vector3(0, height, 0), look.head_half_width, look.head_height, look.head);
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
    ClassDB::bind_method(D_METHOD("get_season_factor", "cell"), &Farm::get_season_factor);
    ClassDB::bind_method(D_METHOD("get_moisture", "cell"), &Farm::get_moisture);
    ClassDB::bind_method(D_METHOD("get_plot_count", "state"), &Farm::get_plot_count);
    ClassDB::bind_method(D_METHOD("get_crop", "cell"), &Farm::get_crop);
    ClassDB::bind_method(D_METHOD("get_crop_yield", "crop"), &Farm::get_crop_yield);
    ClassDB::bind_method(D_METHOD("available_action", "cell", "has_seed"), &Farm::available_action);
    ClassDB::bind_method(D_METHOD("perform", "cell", "action", "crop"), &Farm::perform);
    ClassDB::bind_method(D_METHOD("set_highlight", "cell", "color"), &Farm::set_highlight);
    ClassDB::bind_method(D_METHOD("on_host_rebuilt"), &Farm::on_host_rebuilt);
    ClassDB::bind_method(D_METHOD("set_search_radius", "value"), &Farm::set_search_radius);
    ClassDB::bind_method(D_METHOD("get_search_radius"), &Farm::get_search_radius);

    ClassDB::bind_method(D_METHOD("set_max_river_distance", "value"), &Farm::set_max_river_distance);
    ClassDB::bind_method(D_METHOD("get_max_river_distance"), &Farm::get_max_river_distance);
    ClassDB::bind_method(D_METHOD("set_grow_hours", "value"), &Farm::set_grow_hours);
    ClassDB::bind_method(D_METHOD("get_grow_hours"), &Farm::get_grow_hours);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_river_distance", PROPERTY_HINT_RANGE, "4,60,0.5,suffix:m"), "set_max_river_distance", "get_max_river_distance");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grow_hours", PROPERTY_HINT_RANGE, "0.01,48,0.01,suffix:h"), "set_grow_hours", "get_grow_hours");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "search_radius", PROPERTY_HINT_RANGE, "10,1000,1,suffix:m"), "set_search_radius", "get_search_radius");

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
    BIND_ENUM_CONSTANT(LAND_YARD);
}

void Farm::_ready() {
    // 编辑器里不种地
    if (Engine::get_singleton()->is_editor_hint()) {
        set_physics_process(false);
        return;
    }
    set_physics_process_priority(-10); // 地先长、村民后动（村子收回 / 展开搬地时，地里的长势已是这一帧的）
    host = GroundHost::of(get_parent());
    ERR_FAIL_NULL_MSG(host, "Farm 要挂在地面宿主（Island / World / FlatMap）下面");

    plot_material = curved_material::vertex_colored(1.0f);

    highlight_material.instantiate();
    highlight_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    highlight_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);

    // 子节点先于父节点 _ready：宿主此刻可能还没生成地面，等它的 rebuilt；已经生成过（运行中才挂上来）就直接开张
    host->host_node()->connect("rebuilt", Callable(this, "on_host_rebuilt"));
    if (host->host_ready()) {
        on_host_rebuilt();
    }
}

void Farm::on_host_rebuilt() {
    // 地形换了，旧地块连同它们的样子一起作废
    for (const KeyValue<Vector2i, MeshInstance3D *> &entry : plot_nodes) {
        entry.value->queue_free();
    }
    plot_nodes.clear();
    set_highlight(NO_CELL, Color());
    ground = host->host_ground();
    model = FarmModel(ground.get(), ground->plot_lattice(), rules);
}

void Farm::apply_rules() {
    model.set_rules(rules);
}

void Farm::_physics_process(double p_delta) {
    // 按世界钟走过的游戏小时长（快进时一起快）；没有钟（不该发生）就按帧时与默认的一天长度
    const WorldClock *clock = WorldClock::get_singleton();
    double hours = game_hours(p_delta, DayRules());
    double day = 0.0;
    if (clock) {
        const double now = clock->get_seconds();
        hours = clock_seen < 0.0 ? 0.0 : game_hours(std::max(0.0, now - clock_seen), clock->get_rules());
        day = clock->calendar_day_at(clock_seen < 0.0 ? now : 0.5 * (now + clock_seen));
        clock_seen = now;
    }
    // 有气候的地图：逐格按海拔问时令（冷了不长、雪盖着不长）；没有就照常
    const WeatherModel *wx = host ? host->host_weather() : nullptr;
    const FieldClimate climate(wx, day);
    const std::vector<int> changed = model.advance(hours, wx ? &climate : nullptr);
    for (size_t k = 0; k + 1 < changed.size(); k += 2) {
        refresh_plot(Vector2i(changed[k], changed[k + 1]));
    }
}

void Farm::restore_plots(const std::vector<std::pair<uint64_t, Plot>> &p_entries, double p_saved_at) {
    for (const auto &entry : p_entries) {
        model.put_entry(entry.first, entry.second);
    }
    const std::vector<int> cells = model.plot_cells();
    for (size_t k = 0; k + 1 < cells.size(); k += 2) {
        refresh_plot(Vector2i(cells[k], cells[k + 1]));
    }
    // 下一个物理帧按「存下时 → 此刻」长（Farm::_physics_process 按 clock_seen 算走过的钟头）
    const WorldClock *clock = WorldClock::get_singleton();
    if (clock && p_saved_at >= 0.0) {
        clock_seen = p_saved_at;
    }
}

bool Farm::has_ground(const Vector2i &p_cell) const {
    return ground && p_cell != NO_CELL && model.land_at(p_cell.x, p_cell.y) != Arability::NO_GROUND;
}

Vector3 Farm::cell_corner(const Vector2i &p_cell) const {
    const PlotLattice &l = model.get_lattice();
    return Vector3(real_t(l.node_x(p_cell.y)), 0, real_t(l.node_z(p_cell.x)));
}

Vector2i Farm::cell_at(const Vector3 &p_global) const {
    if (!ground) {
        return NO_CELL;
    }
    const Vector3 local = host_transform().affine_inverse().xform(p_global);
    int i = 0, j = 0;
    if (!model.get_lattice().cell_of(local.x, local.z, i, j)) {
        return NO_CELL;
    }
    const Vector2i cell(i, j);
    return has_ground(cell) ? cell : NO_CELL;
}

Vector3 Farm::get_cell_center(const Vector2i &p_cell) const {
    ERR_FAIL_COND_V(!has_ground(p_cell), Vector3());
    return host_transform().xform(cell_corner(p_cell) + Patch(*ground, model.get_lattice(), p_cell.x, p_cell.y).at(0.5f, 0.5f));
}

Vector2i Farm::find_nearest_arable(const Vector3 &p_global) const {
    if (!ground) {
        return NO_CELL;
    }
    const PlotLattice &l = model.get_lattice();
    const Vector3 local = host_transform().affine_inverse().xform(p_global);
    int i0 = l.i_min, i1 = l.i_max - 1, j0 = l.j_min, j1 = l.j_max - 1;
    if (!l.bounded) {
        int ci = 0, cj = 0;
        l.cell_of(local.x, local.z, ci, cj);
        const int reach = int(std::ceil(search_radius / l.cell_m));
        i0 = ci - reach;
        i1 = ci + reach;
        j0 = cj - reach;
        j1 = cj + reach;
    }
    Vector2i best = NO_CELL;
    double best_d2 = 1e18;
    for (int i = i0; i <= i1; i++) {
        for (int j = j0; j <= j1; j++) {
            const double dx = l.center_x(j) - local.x, dz = l.center_z(i) - local.z;
            if (dx * dx + dz * dz >= best_d2 || (!l.bounded && dx * dx + dz * dz > search_radius * search_radius)) {
                continue;
            }
            if (model.available_action(i, j, false) != FarmAction::TILL) {
                continue;
            }
            if (dx * dx + dz * dz < best_d2) {
                best_d2 = dx * dx + dz * dz;
                best = Vector2i(i, j);
            }
        }
    }
    return best;
}

Farm::Land Farm::get_land(const Vector2i &p_cell) const {
    return p_cell == NO_CELL ? LAND_NO_GROUND : Land(model.land_at(p_cell.x, p_cell.y));
}

Farm::State Farm::get_plot(const Vector2i &p_cell) const {
    return State(model.plot_at(p_cell.x, p_cell.y).state);
}

double Farm::get_season_factor(const Vector2i &p_cell) const {
    const WeatherModel *wx = host ? host->host_weather() : nullptr;
    const WorldClock *clock = WorldClock::get_singleton();
    if (!wx || !clock || !ground) {
        return 1.0;
    }
    return FieldClimate(wx, clock->get_calendar_day()).growth_factor(model.survey(p_cell.x, p_cell.y).height);
}

double Farm::get_growth(const Vector2i &p_cell) const {
    const Plot plot = model.plot_at(p_cell.x, p_cell.y);
    return plot.state == PlotState::RIPE ? 1.0 : plot.growth;
}

double Farm::get_moisture(const Vector2i &p_cell) const {
    return has_ground(p_cell) ? double(model.survey(p_cell.x, p_cell.y).moisture) : 0.0;
}

int Farm::get_plot_count(State p_state) const {
    return model.count(PlotState(p_state));
}

String Farm::get_crop(const Vector2i &p_cell) const {
    const Plot plot = model.plot_at(p_cell.x, p_cell.y);
    return plot.crop ? String(plot.crop->id) : String();
}

int Farm::get_crop_yield(const String &p_crop) const {
    const CropDef *crop = catalog::crop(p_crop.utf8().get_data());
    return crop ? crop->yield : 0;
}

Farm::Action Farm::available_action(const Vector2i &p_cell, bool p_has_seed) const {
    return p_cell == NO_CELL ? ACTION_NONE : Action(model.available_action(p_cell.x, p_cell.y, p_has_seed));
}

int Farm::perform(const Vector2i &p_cell, Action p_action, const String &p_crop) {
    int gained = 0;
    bool done = false;
    switch (p_action) {
        case ACTION_TILL:
            done = model.till(p_cell.x, p_cell.y);
            break;
        case ACTION_SOW:
            done = model.sow(p_cell.x, p_cell.y, catalog::crop(p_crop.utf8().get_data()));
            break;
        case ACTION_HARVEST: {
            const Harvest got = model.harvest(p_cell.x, p_cell.y);
            done = got.count > 0;
            gained = got.count;
        } break;
        case ACTION_NONE:
            break;
    }
    if (done) {
        refresh_plot(p_cell);
    }
    return gained;
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
            node->set_position(cell_corner(p_cell));
            add_child(node);
            plot_nodes[p_cell] = node;
        }
        node->set_mesh(build_plot_mesh(Patch(*ground, model.get_lattice(), p_cell.x, p_cell.y), p_cell, plot));
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
        highlight->set_mesh(build_highlight_mesh(Patch(*ground, model.get_lattice(), p_cell.x, p_cell.y)));
        highlight->set_position(cell_corner(p_cell));
        highlighted = p_cell;
    }
    highlight_material->set_albedo(p_color);
    highlight->set_visible(true);
}

void Farm::set_max_river_distance(double p_value) {
    rules.max_river_m = p_value;
    // 勘察结果跟着变，连已垦的地一起作废：这是摆场景时调的参数，不是玩到一半改的
    if (host && host->host_ready()) {
        on_host_rebuilt();
    }
}

void Farm::refresh_cells(const std::vector<Vector2i> &p_cells) {
    for (const Vector2i &cell : p_cells) {
        refresh_plot(cell);
    }
}

void Farm::set_grow_hours(double p_value) {
    rules.grow_hours = p_value;
    apply_rules();
}
