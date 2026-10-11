#include "far_groups.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "almanac.h"
#include "far_group_mesh.h"
#include "job_pool.h"
#include "planet_jobs.h"
#include "planet_surface.h"
#include "planet_view.h"
#include "world.h"
#include "world_clock.h"

using namespace godot;

namespace {

constexpr const char *SHADER = "res://shaders/far_group.gdshader";
constexpr double DROP_BEYOND_M = 60000.0; // 出了 radius 再远这么多才拆（回滞）
constexpr double PI = 3.14159265358979323846;

// 后台建好的一群的网格（引擎的数组在后台就备好了）
struct FarGroupArrays : JobResult {
    int node = -1;
    int tier = 0; // 0 细（500 m）1 粗（2 km）
    double res_m = 0.0;
    bool stand_in = false; // 行星包里没有它的粗版：画的是替身
    uint64_t lod_gen = 0;
    PackedVector3Array vertices, normals;
    PackedColorArray colors;
    PackedByteArray season;
    PackedVector2Array uvs;
    PackedInt32Array indices;
};

uint64_t job_key(int p_node, int p_tier) {
    return JobPool::key(JobKind::FAR_GROUP, uint64_t(p_node) * 4 + uint64_t(p_tier));
}

double smooth01(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

void FarGroups::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_group_count"), &FarGroups::get_group_count);
    ClassDB::bind_method(D_METHOD("get_chunk_count"), &FarGroups::get_chunk_count);
    ClassDB::bind_method(D_METHOD("get_triangle_count"), &FarGroups::get_triangle_count);
    ClassDB::bind_method(D_METHOD("get_wanted_count"), &FarGroups::get_wanted_count);
    ClassDB::bind_method(D_METHOD("is_idle"), &FarGroups::is_idle);
    ClassDB::bind_method(D_METHOD("get_nodes"), &FarGroups::get_nodes);
    ClassDB::bind_method(D_METHOD("get_group_resolution", "node"), &FarGroups::get_group_resolution);
    ClassDB::bind_method(D_METHOD("get_stand_in_count"), &FarGroups::get_stand_in_count);
    ClassDB::bind_method(D_METHOD("get_replaced_count"), &FarGroups::get_replaced_count);
    ClassDB::bind_method(D_METHOD("is_stand_in", "node"), &FarGroups::is_stand_in);
    ClassDB::bind_method(D_METHOD("set_planet_path", "value"), &FarGroups::set_planet_path);
    ClassDB::bind_method(D_METHOD("get_planet_path"), &FarGroups::get_planet_path);
    ClassDB::bind_method(D_METHOD("set_map_path", "value"), &FarGroups::set_map_path);
    ClassDB::bind_method(D_METHOD("get_map_path"), &FarGroups::get_map_path);
    ClassDB::bind_method(D_METHOD("set_radius", "value"), &FarGroups::set_radius);
    ClassDB::bind_method(D_METHOD("get_radius"), &FarGroups::get_radius);
    ClassDB::bind_method(D_METHOD("set_fine_distance", "value"), &FarGroups::set_fine_distance);
    ClassDB::bind_method(D_METHOD("get_fine_distance"), &FarGroups::get_fine_distance);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "planet_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PlanetView"), "set_planet_path", "get_planet_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "map_path"), "set_map_path", "get_map_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "10000,2000000,1000,suffix:m"), "set_radius", "get_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fine_distance", PROPERTY_HINT_RANGE, "0,1000000,1000,suffix:m"), "set_fine_distance", "get_fine_distance");
}

void FarGroups::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) {
        cancel_all();
    }
}

void FarGroups::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    planet = Object::cast_to<PlanetView>(get_node_or_null(planet_path));
    if (PlanetJobs *jobs = PlanetJobs::find(this)) {
        pool = jobs->share_pool();
    } else if (GroupWorld *w = Object::cast_to<GroupWorld>(get_node_or_null(map_path))) {
        pool = w->share_pool();
    }
    if (!planet || !pool) {
        ERR_PRINT(String(U"FarGroups：planet_path（PlanetView）/ map_path（World）没接上，远处的岛群不画"));
        set_process(false);
        return;
    }
    const Ref<Shader> shader = ResourceLoader::get_singleton()->load(SHADER);
    material.instantiate();
    if (shader.is_valid()) {
        material->set_shader(shader);
    } else {
        ERR_PRINT(String(U"FarGroups：读不了着色器 ") + SHADER);
    }
    set_process_priority(110); // PlanetView（100）算好这一帧的相机之后
}

void FarGroups::cancel_all() {
    if (!pool) {
        return;
    }
    for (const auto &it : groups) {
        for (int tier = 0; tier < 2; tier++) {
            pool->cancel(job_key(it.first, tier));
        }
    }
}

void FarGroups::set_hidden(int p_node, bool p_hidden) {
    if (p_hidden) {
        hidden.insert(p_node);
    } else {
        hidden.erase(p_node);
    }
}

void FarGroups::_process(double /* p_delta */) {
    if (!planet || !planet->is_active() || !pool) {
        return;
    }
    const Vector2 ll = planet->get_camera_latlon();
    const double moved = selected ? great_circle_m(sel_lat, sel_lon, ll.x, ll.y, planet->get_radius()) : 1e18;
    if (moved > reselect_m) {
        select();
    }
    refresh_lods();
    adopt_ready();
    place_all();
    update_seasons(false);
}

void FarGroups::select() {
    const PlanetData *pd = planet->get_planet();
    const Vector2 ll = planet->get_camera_latlon();
    selected = true;
    sel_lat = ll.x;
    sel_lon = ll.y;
    const std::vector<int> near = pd->nodes_within(ll.x, ll.y, radius);
    for (int node : near) {
        if (node == planet->get_anchor_node()) {
            continue;
        }
        Group &g = groups[node];
        g.node = node;
    }
    for (auto it = groups.begin(); it != groups.end();) {
        Group &g = it->second;
        g.distance = planet->group_distance(g.node);
        if (g.distance > radius + DROP_BEYOND_M) {
            if (g.mesh) {
                g.mesh->queue_free();
            }
            for (int tier = 0; tier < 2; tier++) {
                pool->cancel(job_key(g.node, tier));
            }
            it = groups.erase(it);
            continue;
        }
        // 档：细的走远一点才换粗、粗的走近一点才换细（回滞）
        const bool fine_now = g.want_res == fine_res ? g.distance < fine_distance + fine_hysteresis
                : (g.want_res == 0.0 ? g.distance < fine_distance : g.distance < fine_distance - fine_hysteresis);
        const double want = fine_now ? fine_res : coarse_res;
        // 这一档已经交出去了（排着、在做、做好了在 ready 里等挂）就不再交：做好取走以后池里不算「忙」，
        // 原来只看池里忙不忙，高空飞得快时每挪 4 km 重挑一回、一帧只挂一份，同一群做好的网格一份份攒在 ready 里，内存涨到十几 GB、任务池也被占满
        if (want != g.want_res || (g.built_res != want && g.queued_res != want)) {
            g.want_res = want;
            submit(g);
        }
        ++it;
    }
}

void FarGroups::refresh_lods() {
    const uint64_t gen = planet->get_planet()->lod_generation();
    if (gen == lod_gen_seen) {
        return;
    }
    lod_gen_seen = gen;
    const PlanetData *pd = planet->get_planet();
    for (auto &it : groups) {
        Group &g = it.second;
        const uint64_t version = pd->lod_generation(g.node);
        if (g.queued_res == g.want_res && g.queued_lod_gen == version) {
            continue; // 已在建当前版本，别因其他群的更新撤掉它。
        }
        if (g.built_lod_gen != version || (g.queued_res > 0.0 && g.queued_lod_gen != version)) {
            g.built_res = 0.0; // 同一档的数据也可能被换掉；新网格挂上以前旧网格照旧画着。
            g.queued_res = 0.0;
            for (int tier = 0; tier < 2; tier++) {
                pool->cancel(job_key(g.node, tier));
            }
            submit(g);
        }
    }
}

void FarGroups::submit(Group &p_group) {
    const int tier = p_group.want_res == fine_res ? 0 : 1;
    const uint64_t version = planet->get_planet()->lod_generation(p_group.node);
    pool->cancel(job_key(p_group.node, 1 - tier));
    if (p_group.built_res == p_group.want_res && p_group.built_lod_gen == version) {
        p_group.queued_res = 0.0;
        return;
    }
    if (p_group.queued_res == p_group.want_res && p_group.queued_lod_gen == version) {
        return; // 这一档已经交出去了
    }
    const std::shared_ptr<const PlanetData> data = planet->share_planet();
    const int node = p_group.node;
    const double max_res = tier == 0 ? fine_res * 1.2 : 1e9;
    const double radius_m = planet->get_radius();
    const bool submitted = pool->submit(job_key(node, tier), p_group.distance, [data, node, tier, max_res, radius_m]() -> std::unique_ptr<JobResult> {
        std::unique_ptr<FarGroupArrays> out = std::make_unique<FarGroupArrays>();
        out->node = node;
        out->tier = tier;
        PlanetLod lod = data->lod(node, max_res, &out->lod_gen);
        if (lod.empty()) {
            // 还没有粗版（生存模式）：替身，岸缘取每块陆地最低的格
            lod = surface_stand_in(*data, node, 2000.0);
            out->stand_in = true;
            int n_isl = 0;
            for (size_t k = 0; k < lod.land.size(); k++) {
                n_isl = lod.land[k] ? std::max(n_isl, int(lod.island[k]) + 1) : n_isl;
            }
            lod.islands.assign(size_t(n_isl), PlanetLodIsland());
            std::vector<double> rim(size_t(n_isl), 1e9);
            for (size_t k = 0; k < lod.land.size(); k++) {
                if (lod.land[k]) {
                    rim[lod.island[k]] = std::min(rim[lod.island[k]], double(lod.height[k]));
                }
            }
            for (int i = 0; i < n_isl; i++) {
                lod.islands[size_t(i)].rim_m = rim[size_t(i)] < 1e8 ? rim[size_t(i)] : 0.0;
            }
        }
        const FarGroupMesh m = build_far_group_mesh(lod, radius_m);
        out->res_m = m.res_m;
        if (m.empty()) {
            return out;
        }
        const int64_t n = int64_t(m.vertex_count());
        out->vertices.resize(n);
        out->normals.resize(n);
        out->colors.resize(n);
        out->uvs.resize(n);
        Vector3 *v = out->vertices.ptrw();
        Vector3 *nn = out->normals.ptrw();
        Color *c = out->colors.ptrw();
        Vector2 *uv = out->uvs.ptrw();
        for (int64_t k = 0; k < n; k++) {
            const size_t i = size_t(k);
            v[k] = Vector3(m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]);
            nn[k] = Vector3(m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]);
            c[k] = Color(m.colors[i * 4], m.colors[i * 4 + 1], m.colors[i * 4 + 2], m.colors[i * 4 + 3]);
            uv[k] = Vector2(m.uvs[i * 2], m.uvs[i * 2 + 1]);
        }
        out->season.resize(int64_t(m.season.size()));
        std::copy(m.season.begin(), m.season.end(), out->season.ptrw());
        out->indices.resize(int64_t(m.indices.size()));
        std::copy(m.indices.begin(), m.indices.end(), out->indices.ptrw());
        return out;
    });
    if (submitted) {
        p_group.queued_res = p_group.want_res;
        p_group.queued_lod_gen = version;
    }
}

void FarGroups::adopt_ready() {
    std::vector<JobPool::Done> done;
    pool->collect(JobKind::FAR_GROUP, done);
    for (JobPool::Done &d : done) {
        ready.push_back(std::move(d.result));
    }
    // 走远了、档又换了的这一帧全扔（原来一帧只扔一份：高空飞得快时扔的赶不上攒的，该挂的一份也挂不上）
    ready.erase(std::remove_if(ready.begin(), ready.end(),
                        [this](const std::unique_ptr<JobResult> &p_r) {
                            const FarGroupArrays *a = static_cast<const FarGroupArrays *>(p_r.get());
                            const auto found = groups.find(a->node);
                            if (found == groups.end()) {
                                return true;
                            }
                            const double res = a->tier == 0 ? fine_res : coarse_res;
                            Group &g = found->second;
                            if (res != g.want_res || a->lod_gen != planet->get_planet()->lod_generation(a->node)) {
                                if (g.queued_res == res && g.queued_lod_gen == a->lod_gen) {
                                    g.queued_res = 0.0;
                                }
                                submit(g); // 同键的旧活做完、从池里取走后，才能交新的。
                                return true;
                            }
                            return false;
                        }),
            ready.end());
    // 一帧挂一份，近的先
    int pick = -1;
    double best = 1e18;
    for (size_t k = 0; k < ready.size(); k++) {
        const FarGroupArrays *a = static_cast<const FarGroupArrays *>(ready[k].get());
        const double d = groups.find(a->node)->second.distance;
        if (d < best) {
            best = d;
            pick = int(k);
        }
    }
    if (pick < 0) {
        return;
    }
    std::unique_ptr<JobResult> taken = std::move(ready[size_t(pick)]);
    ready.erase(ready.begin() + pick);
    {
        FarGroupArrays *a = static_cast<FarGroupArrays *>(taken.get());
        auto found = groups.find(a->node);
        if (found == groups.end()) {
            return; // 走远了
        }
        Group &g = found->second;
        const double res = a->tier == 0 ? fine_res : coarse_res;
        if (res != g.want_res || a->lod_gen != planet->get_planet()->lod_generation(a->node)) {
            if (g.queued_res == res && g.queued_lod_gen == a->lod_gen) {
                g.queued_res = 0.0;
            }
            submit(g);
            return; // 档或数据又换了。
        }
        MeshInstance3D *mesh = nullptr;
        AABB true_box;
        if (!a->indices.is_empty()) {
            Array arrays;
            arrays.resize(Mesh::ARRAY_MAX);
            arrays[Mesh::ARRAY_VERTEX] = a->vertices;
            arrays[Mesh::ARRAY_NORMAL] = a->normals;
            arrays[Mesh::ARRAY_COLOR] = a->colors;
            arrays[Mesh::ARRAY_TEX_UV] = a->uvs;
            arrays[Mesh::ARRAY_CUSTOM0] = a->season;
            arrays[Mesh::ARRAY_INDEX] = a->indices;
            Ref<ArrayMesh> am;
            am.instantiate();
            const uint64_t format = uint64_t(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
            am->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), BitField<Mesh::ArrayFormat>(format));
            am->surface_set_material(0, material);
            // 着色器把远处沿视线压到相机 26 km 以内（planet_squeeze），按真位置的包围盒剔除会被远平面剪掉：包围盒放到把相机也包进去，永远画（一共十几群）
            true_box = am->get_aabb();
            am->set_custom_aabb(AABB(Vector3(-2.5e6f, -2.5e6f, -2.5e6f), Vector3(5e6f, 5e6f, 5e6f)));
            mesh = memnew(MeshInstance3D);
            mesh->set_name(vformat("Group%d", a->node));
            mesh->set_mesh(am);
            mesh->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            mesh->set_as_top_level(true);
            add_child(mesh);
            mesh->set_global_transform(planet->group_transform(a->node));
        }
        if (g.mesh) {
            g.mesh->queue_free();
        }
        g.mesh = mesh;
        g.shown = true;
        if (mesh) {
            g.center = true_box.get_center(); // 按顶点算的（自定的包围盒是极大的那个）
            g.sphere = 0.5 * true_box.size.length();
        }
        replaced += g.stand_in && !a->stand_in && g.mesh ? 1 : 0;
        g.built_res = res;
        g.built_lod_gen = a->lod_gen;
        if (g.queued_res == res) {
            g.queued_res = 0.0;
        }
        g.stand_in = a->stand_in;
        g.triangles = int(a->indices.size() / 3);
        if (g.stand_in && planet->get_planet()->has_lod(g.node)) {
            // 这一份在后台建的时候粗版刚好来了：再按粗版建一回
            g.built_res = 0.0;
            submit(g);
        }
        adopted++;
        season_hour = -1e9; // 新挂的这一份也要设时令
    }
}

void FarGroups::place_all() {
    const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
    Plane sides[4];
    bool cull = false;
    if (camera) {
        const TypedArray<Plane> planes = camera->get_frustum(); // 近、远、左、上、右、下（法线朝外）
        if (planes.size() >= 6) {
            for (int k = 0; k < 4; k++) {
                sides[k] = planes[k + 2];
            }
            cull = true;
        }
    }
    for (auto &it : groups) {
        Group &g = it.second;
        if (!g.mesh) {
            continue;
        }
        const Transform3D xf = planet->group_transform(it.first);
        bool inside = true;
        if (cull) {
            const Vector3 c = xf.xform(g.center);
            for (const Plane &p : sides) {
                if (p.distance_to(c) > g.sphere) {
                    inside = false;
                    break;
                }
            }
        }
        // 锚群（换锚之后它的粗版可能还在）、GroupStream 载入了的群：不画粗版
        inside = inside && !hidden.count(it.first) && it.first != planet->get_anchor_node();
        if (inside) {
            g.mesh->set_global_transform(xf);
        }
        if (inside != g.shown) {
            g.mesh->set_visible(inside);
            g.shown = inside;
        }
    }
}

void FarGroups::update_seasons(bool p_force) {
    const WorldClock *clock = WorldClock::get_singleton();
    const double day = clock ? clock->get_calendar_day() : 0.0;
    const double hour = day * 24.0;
    if (!p_force && std::abs(hour - season_hour) < 1.0) {
        return;
    }
    season_hour = hour;
    const PlanetData *pd = planet->get_planet();
    const double year = double(std::max(1, pd->calendar.year_days));
    CalendarRules rules;
    rules.year_days = pd->calendar.year_days;
    const double doy = day_of_year(day, rules);
    for (auto &it : groups) {
        if (!it.second.mesh) {
            continue;
        }
        // 简化版海拔表：气温 = 海面（高度 0）的年均温 + 半个温差 × cos（离最暖那天多远），按各群自己的直减率往上减；
        // 最暖那天 = 该半球的夏至 + 45 天（逐日表里南半球一月最暖、七月最冷，北半球的开局那群 #6329 七月最暖、一月最冷）
        const PlanetNode &n = pd->node(it.first);
        const double lapse = n.height_m > 50.0 ? std::clamp((n.temp_sea_c - n.temp_c) / n.height_m * 1000.0, 3.5, 9.0) : 6.0; // °C/km
        const double amp = 0.5 * n.season_range_c;
        const double warm = (n.lat >= 0.0 ? pd->calendar.solstice_n : pd->calendar.solstice_n + 0.5 * year) + 45.0;
        auto t_sea = [&](double p_doy) { return n.temp_sea_c + amp * std::cos(2.0 * PI * (p_doy - warm) / year); };
        const double y_snow = (t_sea(doy - 20.0) + 1.0) / lapse * 1000.0; // 二十天的滞后：−1 °C 以上的高处积雪
        const double y_dormant = (t_sea(doy - 10.0) - 5.0) / lapse * 1000.0; // 十天的滞后：5 °C 以上的高处草枯
        const double t700 = t_sea(doy - 10.0) - lapse * 0.7;
        const double cooling = std::max(0.0, std::sin(2.0 * PI * (doy - warm) / year));
        const double autumn = cooling * std::clamp(1.0 - std::abs(t700 - 8.0) / 6.0, 0.0, 1.0);
        const double leaf = smooth01(1.0, 7.0, t700);
        it.second.mesh->set_instance_shader_parameter("season_lines", Vector4(real_t(y_snow), real_t(y_dormant), real_t(autumn), real_t(leaf)));
    }
}

int FarGroups::get_group_count() const {
    int n = 0;
    for (const auto &it : groups) {
        n += it.second.mesh ? 1 : 0;
    }
    return n;
}

int FarGroups::get_triangle_count() const {
    int n = 0;
    for (const auto &it : groups) {
        n += it.second.mesh ? it.second.triangles : 0;
    }
    return n;
}

bool FarGroups::is_idle() const {
    if (!selected) {
        return false;
    }
    for (const auto &it : groups) {
        if (it.second.built_res != it.second.want_res || it.second.built_lod_gen != planet->get_planet()->lod_generation(it.first)) {
            return false;
        }
    }
    return true;
}

PackedInt32Array FarGroups::get_nodes() const {
    PackedInt32Array out;
    for (const auto &it : groups) {
        if (it.second.mesh) {
            out.push_back(it.first);
        }
    }
    return out;
}

int FarGroups::get_stand_in_count() const {
    int n = 0;
    for (const auto &it : groups) {
        n += it.second.mesh && it.second.stand_in ? 1 : 0;
    }
    return n;
}

bool FarGroups::is_stand_in(int p_node) const {
    const auto found = groups.find(p_node);
    return found != groups.end() && found->second.stand_in;
}

double FarGroups::get_group_resolution(int p_node) const {
    const auto found = groups.find(p_node);
    return found == groups.end() ? 0.0 : found->second.built_res;
}
