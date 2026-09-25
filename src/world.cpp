#include "world.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "dock_builder.h"
#include "farm.h"
#include "shipyard.h"
#include "hinterland.h"
#include "scatter_stream.h"
#include "site_stream.h"
#include "world_clock.h"
#include "world_ground.h"
#include "world_map.h"

using namespace godot;

namespace {

constexpr uint64_t ADOPT_BUDGET_USEC = 6000; // 每帧最多花这么久往场景里挂块
constexpr uint64_t RIVER_BUDGET_USEC = 3000; // 每帧最多花这么久建水面块
constexpr double RIVER_RETARGET_M = 64.0; // 焦点挪了这么远才重新挑水面块
constexpr const char *RIVER_SHADER = "res://shaders/river.gdshader";

double tile_distance(const TileLayout &p_layout, const TileKey &p_key, double p_x, double p_z) {
    double x, z;
    p_layout.corner(p_key, x, z);
    const double s = p_layout.size(p_key.level);
    const double dx = std::max({ x - p_x, 0.0, p_x - (x + s) });
    const double dz = std::max({ z - p_z, 0.0, p_z - (z + s) });
    return std::hypot(dx, dz);
}

} // namespace

void World::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_focus", "focus"), &World::set_focus);
    ClassDB::bind_method(D_METHOD("get_focus"), &World::get_focus);
    ClassDB::bind_method(D_METHOD("prepare_at", "point", "radius"), &World::prepare_at, DEFVAL(160.0));
    ClassDB::bind_method(D_METHOD("is_ready_at", "point"), &World::is_ready_at);
    ClassDB::bind_method(D_METHOD("get_surface_height", "x", "z"), &World::get_surface_height);
    ClassDB::bind_method(D_METHOD("get_island_at", "x", "z"), &World::get_island_at);
    ClassDB::bind_method(D_METHOD("get_info"), &World::get_info);
    ClassDB::bind_method(D_METHOD("is_loaded"), &World::is_loaded);
    ClassDB::bind_method(D_METHOD("get_tile_count"), &World::get_tile_count);
    ClassDB::bind_method(D_METHOD("get_pending_count"), &World::get_pending_count);
    ClassDB::bind_method(D_METHOD("get_dock_count"), &World::get_dock_count);
    ClassDB::bind_method(D_METHOD("find_dock", "id"), &World::find_dock);
    ClassDB::bind_method(D_METHOD("get_freight_post", "village_id"), &World::get_freight_post);
    ClassDB::bind_method(D_METHOD("get_freight_port_ids"), &World::get_freight_port_ids);
    ClassDB::bind_method(D_METHOD("get_resource_site", "working_id"), &World::get_resource_site);
    ClassDB::bind_method(D_METHOD("get_resource_ids"), &World::get_resource_ids);
    ClassDB::bind_method(D_METHOD("find_resource_near", "x", "z", "radius", "kind"), &World::find_resource_near, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_landmark", "id"), &World::get_landmark);
    ClassDB::bind_method(D_METHOD("find_landmark_near", "x", "z", "radius", "kind"), &World::find_landmark_near, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("find_scatter_near", "x", "z", "radius", "kind"), &World::find_scatter_near, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_scatter_count_near", "x", "z", "radius", "solid_only"), &World::get_scatter_count_near);
    ClassDB::bind_method(D_METHOD("get_dock_id", "index"), &World::get_dock_id);
    ClassDB::bind_method(D_METHOD("is_dock_berthable", "index"), &World::is_dock_berthable);
    ClassDB::bind_method(D_METHOD("get_dock_berth", "index"), &World::get_dock_berth);
    ClassDB::bind_method(D_METHOD("get_dock_spawn", "index"), &World::get_dock_spawn);
    ClassDB::bind_method(D_METHOD("get_dock_position", "index"), &World::get_dock_position);
    ClassDB::bind_method(D_METHOD("get_runoff"), &World::get_runoff);
    ClassDB::bind_method(D_METHOD("get_river_at", "x", "z"), &World::get_river_at);
    ClassDB::bind_method(D_METHOD("get_river_chunk_count"), &World::get_river_chunk_count);
    ClassDB::bind_method(D_METHOD("find_river_near", "x", "z", "radius", "min_acc_km2"), &World::find_river_near);
    ClassDB::bind_method(D_METHOD("set_river_radius", "value"), &World::set_river_radius);
    ClassDB::bind_method(D_METHOD("get_river_radius"), &World::get_river_radius);
    ClassDB::bind_method(D_METHOD("set_calendar_offset", "value"), &World::set_calendar_offset);
    ClassDB::bind_method(D_METHOD("get_calendar_offset"), &World::get_calendar_offset);
    ClassDB::bind_method(D_METHOD("set_runoff_override", "value"), &World::set_runoff_override);
    ClassDB::bind_method(D_METHOD("get_runoff_override"), &World::get_runoff_override);

    ClassDB::bind_method(D_METHOD("set_data_dir", "value"), &World::set_data_dir);
    ClassDB::bind_method(D_METHOD("get_data_dir"), &World::get_data_dir);
    ClassDB::bind_method(D_METHOD("set_terrain_seed", "value"), &World::set_terrain_seed);
    ClassDB::bind_method(D_METHOD("get_terrain_seed"), &World::get_terrain_seed);
    ClassDB::bind_method(D_METHOD("set_split", "value"), &World::set_split);
    ClassDB::bind_method(D_METHOD("get_split"), &World::get_split);
    ClassDB::bind_method(D_METHOD("set_build_threads", "value"), &World::set_build_threads);
    ClassDB::bind_method(D_METHOD("get_build_threads"), &World::get_build_threads);

    ClassDB::bind_method(D_METHOD("set_sites_path", "value"), &World::set_sites_path);
    ClassDB::bind_method(D_METHOD("get_sites_path"), &World::get_sites_path);
    ClassDB::bind_method(D_METHOD("set_scatter_path", "value"), &World::set_scatter_path);
    ClassDB::bind_method(D_METHOD("get_scatter_path"), &World::get_scatter_path);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "data_dir", PROPERTY_HINT_DIR), "set_data_dir", "get_data_dir");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sites_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "SiteStream"), "set_sites_path", "get_sites_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "scatter_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "ScatterStream"), "set_scatter_path", "get_scatter_path");
    ADD_SIGNAL(MethodInfo("rebuilt"));
    ADD_PROPERTY(PropertyInfo(Variant::INT, "terrain_seed"), "set_terrain_seed", "get_terrain_seed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "split", PROPERTY_HINT_RANGE, "0.5,4,0.05"), "set_split", "get_split");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "build_threads", PROPERTY_HINT_RANGE, "1,16,1"), "set_build_threads", "get_build_threads");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "river_radius", PROPERTY_HINT_RANGE, "500,20000,100"), "set_river_radius", "get_river_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "calendar_offset", PROPERTY_HINT_RANGE, "0,400,1"), "set_calendar_offset", "get_calendar_offset");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "runoff_override", PROPERTY_HINT_RANGE, "-1,8,0.05"), "set_runoff_override", "get_runoff_override");
}

World::~World() {
    stop_workers();
}

void World::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    site_stream = Object::cast_to<SiteStream>(get_node_or_null(sites_path));
    scatter_stream = Object::cast_to<ScatterStream>(get_node_or_null(scatter_path));
    loaded = load();
    if (!loaded) {
        set_process(false);
        return;
    }
    start_workers();
    set_process(true);
    emit_signal("rebuilt"); // 挂在下面的 Farm、Shipyard 先于这里 _ready，此刻才等到地面
}

bool World::load() {
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(data_dir.path_join("terrain.bin"));
    std::string error;
    if (bytes.is_empty() || !parse_world_grid(bytes.ptr(), size_t(bytes.size()), grid, error)) {
        ERR_PRINT(String("World: 读不了 ") + data_dir.path_join("terrain.bin") + "：" + String::utf8(error.c_str()));
        return false;
    }
    const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(data_dir.path_join("world.json")));
    if (parsed.get_type() != Variant::DICTIONARY) {
        ERR_PRINT(String("World: 读不了 ") + data_dir.path_join("world.json"));
        return false;
    }
    info = parsed;
    std::vector<WorldIsland> islands;
    const Array rows = info.get("islands", Array());
    for (int64_t k = 0; k < rows.size(); k++) {
        const Dictionary row = rows[k];
        WorldIsland isl;
        isl.id = int(row.get("id", 0));
        isl.rim_m = double(row.get("rim_m", 0.0));
        isl.cliff_m = double(row.get("cliff_m", 20.0));
        isl.keel_m = double(row.get("keel_m", 100.0));
        isl.max_shore_m = double(row.get("max_shore_m", 1000.0));
        islands.push_back(isl);
    }
    // 河的水情：weather.tsv（没有就常年一个样）；年降水与一年几天也给河网定尺度
    RiverRules river_rules;
    const String weather_path = data_dir.path_join("weather.tsv");
    if (FileAccess::file_exists(weather_path)) {
        const PackedByteArray weather = FileAccess::get_file_as_bytes(weather_path);
        if (RiverRegime::parse(std::string(reinterpret_cast<const char *>(weather.ptr()), size_t(weather.size())), regime, error)) {
            river_rules.precip_mm = regime.precip_mm();
            river_rules.year_days = regime.year_days();
        } else {
            ERR_PRINT(String("World: 读不了 ") + weather_path + "：" + String::utf8(error.c_str()));
            regime = RiverRegime::steady();
        }
    } else {
        regime = RiverRegime::steady();
    }
    terrain = WorldTerrain(grid, islands, uint64_t(terrain_seed), river_rules);
    layout = TileLayout(grid);

    // 码头：在原地形上量好，再把停机坪压进地形里——都在第一块地面开建之前
    const String sites_path = data_dir.path_join("sites.tsv");
    const PackedByteArray sites_bytes = FileAccess::get_file_as_bytes(sites_path);
    const std::string sites_text(reinterpret_cast<const char *>(sites_bytes.ptr()), size_t(sites_bytes.size()));
    if (sites_bytes.is_empty() || !parse_world_sites(sites_text, sites, error)) {
        ERR_PRINT(String("World: 读不了 ") + sites_path + "：" + String::utf8(error.c_str()) + String(U"（用 tools/export_skyisle.py --sites 导出）"));
        sites = WorldSites();
    }
    works = lay_world_works(terrain, sites);
    freight_ports = plan_freight_ports(sites);

    // 资源：码头的坪压好之后规划采场与地标（采掘面让开坪）；散布让开采场与泊地。没有 resources.tsv 就没有
    const String resources_path = data_dir.path_join("resources.tsv");
    if (FileAccess::file_exists(resources_path)) {
        const PackedByteArray res_bytes = FileAccess::get_file_as_bytes(resources_path);
        const std::string res_text(reinterpret_cast<const char *>(res_bytes.ptr()), size_t(res_bytes.size()));
        if (parse_resources(res_text, resource_data, error)) {
            resource_plan = plan_resource_sites(terrain, sites, resource_data);
            resource_stocks.assign(resource_plan.sites.size(), ResourceStock());
            scatter = ResourceScatter(terrain, sites, resource_data, resource_plan, uint64_t(terrain_seed));
        } else {
            ERR_PRINT(String("World: 读不了 ") + resources_path + "：" + String::utf8(error.c_str()));
            resource_data = ResourceData();
        }
    }

    ground_material.instantiate();
    ground_material->set_roughness(1.0);
    ground_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    // 顶点色是按 sRGB 调的；不声明的话引擎当线性值用，整片地会发白
    ground_material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);

    if (terrain.has_rivers()) {
        river_chunk_keys = river_chunks(terrain, river_mesh_rules);
        const Ref<Shader> shader = ResourceLoader::get_singleton()->load(RIVER_SHADER);
        if (shader.is_valid()) {
            const RiverRules &rr = terrain.get_rivers().get_rules();
            river_material.instantiate();
            river_material->set_shader(shader);
            river_material->set_shader_parameter("bankfull_ratio", rr.bankfull_ratio);
            river_material->set_shader_parameter("stage_exp", rr.stage_exp);
            river_material->set_shader_parameter("flood_max", rr.flood_max);
            river_material->set_shader_parameter("floodplain_slope", rr.floodplain_slope);
            river_material->set_shader_parameter("bed_flat", rr.bed_flat);
            river_material->set_shader_parameter("wet_depth", rr.wet_depth_m);
            river_material->set_shader_parameter("fall_length", river_mesh_rules.fall_m);
        } else {
            ERR_PRINT(String("World: 读不了水面着色器 ") + RIVER_SHADER + String(U"，河只凿河道、不画水"));
        }
    }
    update_runoff();
    return true;
}

void World::set_runoff_override(double p_value) {
    runoff_override = p_value;
    if (loaded) {
        update_runoff();
    }
}

void World::update_runoff() {
    if (runoff_override >= 0.0) {
        runoff = runoff_override;
    } else {
        const WorldClock *clock = WorldClock::get_singleton();
        const double day = clock ? clock->get_seconds() / clock->get_rules().day_seconds : 0.0;
        runoff = regime.runoff(day + calendar_offset);
    }
    if (river_material.is_valid()) {
        river_material->set_shader_parameter("runoff", runoff);
    }
}

Node3D *World::make_river_node(const RiverMesh &m) {
    if (m.empty() || river_material.is_null()) {
        return nullptr;
    }
    const int64_t n = int64_t(m.positions.size() / 3);
    PackedVector3Array vertices, normals;
    PackedVector2Array uvs;
    PackedFloat32Array custom0, custom1;
    PackedInt32Array indices;
    vertices.resize(n);
    normals.resize(n);
    uvs.resize(n);
    for (int64_t k = 0; k < n; k++) {
        vertices.set(k, Vector3(m.positions[k * 3], m.positions[k * 3 + 1], m.positions[k * 3 + 2]));
        normals.set(k, Vector3(m.normals[k * 3], m.normals[k * 3 + 1], m.normals[k * 3 + 2]));
        uvs.set(k, Vector2(m.uvs[k * 2], m.uvs[k * 2 + 1]));
    }
    custom0.resize(int64_t(m.custom0.size()));
    std::copy(m.custom0.begin(), m.custom0.end(), custom0.ptrw());
    custom1.resize(int64_t(m.custom1.size()));
    std::copy(m.custom1.begin(), m.custom1.end(), custom1.ptrw());
    indices.resize(int64_t(m.indices.size()));
    std::copy(m.indices.begin(), m.indices.end(), indices.ptrw());
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_TEX_UV] = uvs;
    arrays[Mesh::ARRAY_CUSTOM0] = custom0;
    arrays[Mesh::ARRAY_CUSTOM1] = custom1;
    arrays[Mesh::ARRAY_INDEX] = indices;
    const uint64_t format = (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
            (uint64_t(Mesh::ARRAY_CUSTOM_RGBA_FLOAT) << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT);
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), BitField<Mesh::ArrayFormat>(format));
    mesh->surface_set_material(0, river_material);
    // 顶点的 y 是河床；着色器往上抬到水位（远处再抬一点），跌水帘往下垂：包围盒两头放宽
    const double chunk = river_mesh_rules.chunk_m;
    mesh->set_custom_aabb(AABB(Vector3(-40.0f, real_t(m.min_y - river_mesh_rules.fall_m - 5.0), -40.0f),
            Vector3(real_t(chunk + 80.0), real_t(m.max_y - m.min_y + river_mesh_rules.fall_m + 30.0), real_t(chunk + 80.0))));
    MeshInstance3D *view = memnew(MeshInstance3D);
    view->set_name(vformat("River_%d_%d", m.key.ix, m.key.iz));
    view->set_mesh(mesh);
    view->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    view->set_position(Vector3(real_t(m.origin_x), 0, real_t(m.origin_z)));
    add_child(view);
    return view;
}

void World::stream_rivers(bool p_all) {
    if (river_chunk_keys.empty()) {
        return;
    }
    const double chunk = river_mesh_rules.chunk_m;
    auto distance = [&](const RiverChunkKey &k) {
        const double x0 = k.ix * chunk, z0 = k.iz * chunk;
        const double dx = std::max({ x0 - focus.x, 0.0, double(focus.x) - (x0 + chunk) });
        const double dz = std::max({ z0 - focus.z, 0.0, double(focus.z) - (z0 + chunk) });
        return std::hypot(dx, dz);
    };
    // 远了拆
    for (auto it = river_nodes.begin(); it != river_nodes.end();) {
        const RiverChunkKey key{ int(it->first >> 32), int(int32_t(uint32_t(it->first))) };
        if (distance(key) > river_radius * 1.25) {
            if (it->second) {
                it->second->queue_free();
            }
            it = river_nodes.erase(it);
        } else {
            ++it;
        }
    }
    // 近的先建，每帧有预算（瞬移时 p_all 一次建完）
    std::vector<std::pair<double, RiverChunkKey>> wanted;
    for (const RiverChunkKey &key : river_chunk_keys) {
        const double d = distance(key);
        if (d <= river_radius && river_nodes.find(key.packed()) == river_nodes.end()) {
            wanted.push_back({ d, key });
        }
    }
    std::sort(wanted.begin(), wanted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    const uint64_t started = Time::get_singleton()->get_ticks_usec();
    river_more = false;
    for (const auto &[d, key] : wanted) {
        if (!p_all && Time::get_singleton()->get_ticks_usec() - started > RIVER_BUDGET_USEC) {
            river_more = true;
            break;
        }
        river_nodes[key.packed()] = make_river_node(build_river_mesh(terrain, key, river_mesh_rules));
    }
}

int World::get_river_chunk_count() const {
    int n = 0;
    for (const auto &[key, node] : river_nodes) {
        n += node != nullptr;
    }
    return n;
}

Dictionary World::get_river_at(double p_x, double p_z) const {
    Dictionary out;
    RiverHit hit;
    if (!loaded || !terrain.has_rivers() || !terrain.get_rivers().nearest(p_x, p_z, 1e9, hit) || hit.distance > hit.at.core()) {
        return out;
    }
    const RiverStage st = river_stage(hit.at, runoff, terrain.get_rivers().get_rules());
    out["distance"] = hit.distance;
    out["bankfull_width"] = hit.at.width;
    out["bankfull_depth"] = hit.at.depth;
    out["floodplain"] = hit.at.floodplain;
    out["bed"] = hit.at.bed();
    out["level"] = st.level;
    out["depth"] = st.depth;
    out["width"] = st.wet ? 2.0 * st.half_width : 0.0;
    out["flow"] = st.flow;
    out["wet"] = st.wet;
    out["acc_km2"] = hit.at.acc;
    return out;
}

Dictionary World::find_river_near(double p_x, double p_z, double p_radius, double p_min_acc_km2) const {
    Dictionary out;
    if (!loaded || !terrain.has_rivers()) {
        return out;
    }
    const WorldRivers &rivers = terrain.get_rivers();
    double best = p_radius;
    rivers.segments_in(p_x - p_radius, p_z - p_radius, p_x + p_radius, p_z + p_radius, [&](int s, int i) {
        const RiverStem &stem = rivers.get_stems()[size_t(s)];
        const RiverVertex &v = stem.v[size_t(i)], &w = stem.v[size_t(i) + 1];
        const double d = std::hypot(v.x - p_x, v.z - p_z);
        if (v.acc >= p_min_acc_km2 && d < best && !terrain.near_pad(v.x, v.z, WorldTerrain::PAD_REACH_M)) {
            best = d;
            const double len = std::max(1e-6, std::hypot(w.x - v.x, w.z - v.z));
            out["x"] = v.x;
            out["z"] = v.z;
            out["dir_x"] = (w.x - v.x) / len; // 顺水方向
            out["dir_z"] = (w.z - v.z) / len;
            out["width"] = v.width;
            out["floodplain"] = v.floodplain;
            out["acc_km2"] = v.acc;
            out["mouth"] = stem.mouth;
            const RiverVertex &last = stem.v.back(), &before = stem.v[stem.v.size() - 2];
            const double mouth_len = std::max(1e-6, std::hypot(last.x - before.x, last.z - before.z));
            out["mouth_x"] = last.x;
            out["mouth_z"] = last.z;
            out["mouth_dir_x"] = (last.x - before.x) / mouth_len;
            out["mouth_dir_z"] = (last.z - before.z) / mouth_len;
        }
    });
    return out;
}

void World::start_workers() {
    stopping = false;
    for (int k = 0; k < std::max(1, build_threads); k++) {
        workers.emplace_back([this]() { worker_loop(); });
    }
    if (scatter.valid()) {
        for (int k = 0; k < std::max(1, scatter_threads); k++) {
            scatter_workers.emplace_back([this]() { scatter_loop(); });
        }
    }
}

void World::stop_workers() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stopping = true;
        jobs.clear();
        scatter_jobs.clear();
    }
    queue_cv.notify_all();
    for (std::thread &t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers.clear();
    for (std::thread &t : scatter_workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    scatter_workers.clear();
}

void World::scatter_loop() {
    for (;;) {
        ScatterChunkKey key;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this]() { return stopping || !scatter_jobs.empty(); });
            if (stopping) {
                return;
            }
            key = scatter_jobs.front();
            scatter_jobs.pop_front();
            scatter_in_flight[key.packed()] = true;
        }
        std::vector<ScatterItem> items = scatter.chunk(key);
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            scatter_in_flight.erase(key.packed());
            scatter_done.emplace_back(key, std::move(items));
        }
    }
}

void World::scatter_request(const std::vector<ScatterChunkKey> &p_keys) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        // 还没开工的旧活全作废（正在生成的做完照样交回来，用不用由上层定）
        for (const ScatterChunkKey &key : scatter_jobs) {
            scatter_in_flight.erase(key.packed());
        }
        scatter_jobs.clear();
        for (const ScatterChunkKey &key : p_keys) {
            if (scatter_in_flight.find(key.packed()) == scatter_in_flight.end()) {
                scatter_jobs.push_back(key);
                scatter_in_flight[key.packed()] = false;
            }
        }
    }
    queue_cv.notify_all();
}

void World::scatter_collect(std::vector<std::pair<ScatterChunkKey, std::vector<ScatterItem>>> &r_done) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    for (auto &done : scatter_done) {
        r_done.push_back(std::move(done));
    }
    scatter_done.clear();
}

void World::worker_loop() {
    for (;;) {
        TileKey key;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this]() { return stopping || !jobs.empty(); });
            if (stopping) {
                return;
            }
            key = jobs.front();
            jobs.pop_front();
            in_flight[key.packed()] = true;
        }
        std::unique_ptr<TileMesh> mesh = std::make_unique<TileMesh>(build_tile_mesh(terrain, layout, key));
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            in_flight.erase(key.packed());
            results.push_back(std::move(mesh));
        }
    }
}

bool World::target_has(const TileKey &p_key) const {
    return std::binary_search(target.begin(), target.end(), p_key);
}

void World::retarget() {
    targeted_focus = focus;
    has_target = true;
    target = layout.select(focus.x, focus.z, split);
    std::sort(target.begin(), target.end());

    std::vector<TileKey> wanted;
    for (const TileKey &key : target) {
        if (tiles.find(key.packed()) == tiles.end()) {
            wanted.push_back(key);
        }
    }
    // 近的先建；一样近时细的先建
    const double fx = focus.x, fz = focus.z;
    std::sort(wanted.begin(), wanted.end(), [&](const TileKey &a, const TileKey &b) {
        const double da = tile_distance(layout, a, fx, fz), db = tile_distance(layout, b, fx, fz);
        return da != db ? da < db : a.level > b.level;
    });
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        // 还没开建的旧活全作废（正在建的建完再说：adopt 时不在目标里就丢）
        for (const TileKey &key : jobs) {
            in_flight.erase(key.packed());
        }
        jobs.clear();
        for (const TileKey &key : wanted) {
            if (in_flight.find(key.packed()) == in_flight.end()) {
                jobs.push_back(key);
                in_flight[key.packed()] = false;
            }
        }
    }
    queue_cv.notify_all();
}

void World::adopt(std::unique_ptr<TileMesh> p_mesh) {
    const TileKey key = p_mesh->key;
    if (!target_has(key) || tiles.find(key.packed()) != tiles.end()) {
        return;
    }
    Tile tile;
    tile.key = key;
    if (!p_mesh->empty()) {
        const TileMesh &m = *p_mesh;
        const int64_t n = int64_t(m.positions.size() / 3);
        PackedVector3Array vertices, normals;
        PackedColorArray colors;
        PackedInt32Array indices;
        vertices.resize(n);
        normals.resize(n);
        colors.resize(n);
        for (int64_t k = 0; k < n; k++) {
            vertices.set(k, Vector3(m.positions[k * 3], m.positions[k * 3 + 1], m.positions[k * 3 + 2]));
            normals.set(k, Vector3(m.normals[k * 3], m.normals[k * 3 + 1], m.normals[k * 3 + 2]));
            colors.set(k, Color(m.colors[k * 4], m.colors[k * 4 + 1], m.colors[k * 4 + 2], m.colors[k * 4 + 3]));
        }
        indices.resize(int64_t(m.indices.size()));
        std::copy(m.indices.begin(), m.indices.end(), indices.ptrw());
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = vertices;
        arrays[Mesh::ARRAY_NORMAL] = normals;
        arrays[Mesh::ARRAY_COLOR] = colors;
        arrays[Mesh::ARRAY_INDEX] = indices;
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_material(0, ground_material);

        Node3D *root = memnew(Node3D);
        root->set_name(vformat("Tile_%d_%d_%d", key.level, key.ix, key.iz));
        root->set_position(Vector3(real_t(m.origin_x), 0, real_t(m.origin_z)));
        MeshInstance3D *view = memnew(MeshInstance3D);
        view->set_mesh(mesh);
        root->add_child(view);
        if (!m.collision.empty()) {
            PackedVector3Array faces;
            faces.resize(int64_t(m.collision.size() / 3));
            for (int64_t k = 0; k < faces.size(); k++) {
                faces.set(k, Vector3(m.collision[k * 3], m.collision[k * 3 + 1], m.collision[k * 3 + 2]));
            }
            Ref<ConcavePolygonShape3D> shape;
            shape.instantiate();
            shape->set_faces(faces);
            StaticBody3D *body = memnew(StaticBody3D);
            CollisionShape3D *collider = memnew(CollisionShape3D);
            collider->set_shape(shape);
            body->add_child(collider);
            root->add_child(body);
        }
        add_child(root);
        tile.node = root;
    }
    tiles[key.packed()] = tile;
}

void World::prune() {
    for (auto it = tiles.begin(); it != tiles.end();) {
        const TileKey key = it->second.key;
        if (target_has(key)) {
            ++it;
            continue;
        }
        // 盖住它那片的新块都到齐了才拆
        bool covered = true;
        for (const TileKey &t : target) {
            if (layout.overlaps(t, key) && tiles.find(t.packed()) == tiles.end()) {
                covered = false;
                break;
            }
        }
        if (!covered) {
            ++it;
            continue;
        }
        if (it->second.node) {
            it->second.node->queue_free();
        }
        it = tiles.erase(it);
    }
}

void World::_process(double /* p_delta */) {
    if (!has_target || Vector2(focus.x - targeted_focus.x, focus.z - targeted_focus.z).length() > retarget_distance) {
        retarget();
    }
    update_runoff();
    if (!river_targeted || Vector2(focus.x - river_focus.x, focus.z - river_focus.z).length() > RIVER_RETARGET_M ||
            river_more) {
        river_focus = focus;
        river_targeted = true;
        stream_rivers(false);
    }
    std::vector<std::unique_ptr<TileMesh>> ready;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        ready.swap(results);
    }
    // 近的先挂；时间用完了剩下的放回去下帧再挂
    const double fx = focus.x, fz = focus.z;
    std::sort(ready.begin(), ready.end(), [&](const std::unique_ptr<TileMesh> &a, const std::unique_ptr<TileMesh> &b) {
        return tile_distance(layout, a->key, fx, fz) < tile_distance(layout, b->key, fx, fz);
    });
    const uint64_t started = Time::get_singleton()->get_ticks_usec();
    size_t k = 0;
    for (; k < ready.size() && Time::get_singleton()->get_ticks_usec() - started < ADOPT_BUDGET_USEC; k++) {
        adopt(std::move(ready[k]));
    }
    if (k < ready.size()) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (; k < ready.size(); k++) {
            results.push_back(std::move(ready[k]));
        }
    }
    prune();
}

void World::prepare_at(const Vector3 &p_point, double p_radius) {
    if (!loaded) {
        return;
    }
    focus = p_point;
    retarget();
    river_focus = focus;
    river_targeted = true;
    stream_rivers(true);
    for (const TileKey &key : target) {
        if (key.level == layout.get_max_level() && tiles.find(key.packed()) == tiles.end() &&
                tile_distance(layout, key, p_point.x, p_point.z) <= p_radius) {
            adopt(std::make_unique<TileMesh>(build_tile_mesh(terrain, layout, key)));
        }
    }
}

bool World::is_ready_at(const Vector3 &p_point) const {
    return loaded && tiles.find(layout.leaf_at(p_point.x, p_point.z).packed()) != tiles.end();
}

double World::get_surface_height(double p_x, double p_z) const {
    double y = 0.0;
    if (!loaded || !terrain.surface(p_x, p_z, y)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return y;
}

int World::get_island_at(double p_x, double p_z) const {
    return loaded ? int(grid.island[grid.nearest_cell(p_x, p_z)]) : -1;
}

int World::get_pending_count() const {
    int missing = 0;
    for (const TileKey &key : target) {
        missing += tiles.find(key.packed()) == tiles.end() ? 1 : 0;
    }
    return missing;
}

int World::find_dock(int p_id) const {
    for (size_t k = 0; k < sites.docks.size(); k++) {
        if (sites.docks[k].id == p_id) {
            return int(k);
        }
    }
    return -1;
}

int World::get_dock_id(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(sites.docks.size()), -1);
    return sites.docks[size_t(p_index)].id;
}

bool World::is_dock_berthable(int p_index) const {
    return p_index >= 0 && p_index < int(works.docks.size()) && works.docks[size_t(p_index)].ok;
}

Transform3D World::get_dock_berth(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(works.docks.size()), Transform3D());
    return dock_builder::berth_contact(works.docks[size_t(p_index)]);
}

Transform3D World::get_dock_spawn(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(works.docks.size()), Transform3D());
    Transform3D xf = dock_builder::spawn(works.docks[size_t(p_index)]);
    double y = 0.0;
    if (terrain.surface(xf.origin.x, xf.origin.z, y)) {
        xf.origin.y = real_t(y);
    }
    return xf;
}

Vector3 World::get_dock_position(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(works.docks.size()), Vector3());
    return dock_builder::point(works.docks[size_t(p_index)], 0.0, 0.0);
}

std::unique_ptr<MapGround> World::host_ground() const {
    return std::make_unique<WorldGround>(terrain, 0.0, 0.0);
}

Farm *World::host_farm() const {
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        if (Farm *farm = Object::cast_to<Farm>(children[k])) {
            return farm;
        }
    }
    return nullptr;
}

YardPlan World::host_yard() const {
    for (const YardPlan &yard : works.yards) {
        if (yard.present) {
            return yard;
        }
    }
    return YardPlan();
}

int World::map_island_at(const Vector3 &p_global) const {
    return std::isnan(get_surface_height(p_global.x, p_global.z)) ? -1 : get_island_at(p_global.x, p_global.z);
}

bool World::map_footing(const Vector3 &p_global) const {
    return is_ready_at(p_global) && !std::isnan(get_surface_height(p_global.x, p_global.z));
}

bool World::map_dock_open(int p_index) const {
    return is_dock_berthable(p_index) && site_stream && site_stream->get_dock(get_dock_id(p_index));
}

int World::map_dock_island(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(sites.docks.size()), -1);
    return sites.docks[size_t(p_index)].island;
}

String World::map_dock_label(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(sites.docks.size()), String());
    const SiteDock &dock = sites.docks[size_t(p_index)];
    return vformat(String(U"岛 #%d 码头 #%d"), dock.island, dock.id);
}

std::vector<Shipyard *> World::map_shipyards() const {
    std::vector<Shipyard *> out;
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        if (Shipyard *yard = Object::cast_to<Shipyard>(children[k])) {
            out.push_back(yard);
        }
    }
    return out;
}

FreightPost World::map_freight_post_near(const Vector3 &p_global, double p_range) {
    FreightPost post;
    WorldVillage *village = site_stream ? site_stream->village_at(p_global) : nullptr;
    const int port = village ? find_freight_port(village->get_site_index()) : -1;
    Vector3 at;
    if (port < 0 || !village->get_freight_post(at) || Vector2(at.x - p_global.x, at.z - p_global.z).length() > p_range) {
        return post;
    }
    post.port = port;
    post.position = at;
    post.name = village->get_site_name();
    const SiteSettlement &site = village->get_site();
    post.specialty = freight::specialty_of(uint64_t(site_stream->get_seed()), site.id, site.hamlet);
    Hinterland *hinterland = village->get_hinterland();
    VillageState *state = hinterland ? hinterland->get_state(village->get_site_index()) : nullptr;
    post.store = state ? &state->store : nullptr;
    return post;
}

bool World::map_freight_target(int p_port, Transform3D &r_target) const {
    if (p_port < 0 || p_port >= int(freight_ports.size())) {
        return false;
    }
    const FreightPort &port = freight_ports[size_t(p_port)];
    // 铺出来了：指着它的船台；还远：指着村心
    WorldVillage *village = site_stream ? site_stream->get_village(port.site_id) : nullptr;
    Settlement *settlement = village ? village->get_settlement() : nullptr;
    const int k = settlement ? freight_post_building(settlement->get_model()) : -1;
    if (k >= 0 && !settlement->find_berths().empty()) {
        r_target = settlement->get_berth_contact(k);
        return true;
    }
    const double y = get_surface_height(port.x, port.z);
    r_target = Transform3D(Basis(), Vector3(real_t(port.x), real_t(std::isnan(y) ? 0.0 : y), real_t(port.z)));
    return true;
}

ResourceStock *World::resource_stock(int p_index) {
    return p_index >= 0 && p_index < int(resource_stocks.size()) ? &resource_stocks[size_t(p_index)] : nullptr;
}

double World::clock_hours() {
    const WorldClock *clock = WorldClock::get_singleton();
    return clock ? clock->get_total_hours() : 0.0;
}

ResourceSpot World::map_resource_near(const Vector3 &p_global, double p_range) {
    ResourceSpot spot;
    WorldResource *node = site_stream ? site_stream->resource_near(p_global, p_range) : nullptr;
    if (!node) {
        return spot;
    }
    const ResourceSite &site = node->get_site();
    spot.site = node->get_index();
    spot.face = node->get_face();
    spot.pile = node->get_pile();
    spot.item = site.item;
    spot.name = String::utf8(site.name.c_str());
    spot.verb = String::utf8(deposits::verb_of(*site.kind).c_str());
    spot.grade = site.grade;
    spot.gather_s = site.kind->gather_s;
    spot.cap = site.cap;
    spot.refill_hours = site.kind->refill_hours;
    spot.stock = resource_stock(spot.site);
    return spot;
}

ResourceSpot World::map_gather_near(const Vector3 &p_global, double p_face_range, double p_facing_yaw) {
    ResourceSpot spot = MapHost::map_gather_near(p_global, p_face_range, p_facing_yaw);
    ScatterStream::Hit hit;
    if (!scatter_stream || !scatter_stream->nearest_item(p_global, p_facing_yaw, hit)) {
        return spot;
    }
    // 采掘面与一株都够得着：取近的（一株按离它边量）
    if (spot.site >= 0 && Vector2(p_global.x - spot.face.x, p_global.z - spot.face.z).length() <= hit.gap) {
        return spot;
    }
    const ScatterLook look = hit.item.kind();
    const ScatterKind &kind = deposits::scatter(look);
    ResourceSpot out;
    out.site = hit.item.index;
    out.scatter = true;
    out.scatter_key = hit.key;
    out.face = Vector3(hit.item.x, hit.item.y, hit.item.z);
    out.pile = out.face;
    out.item = deposits::scatter_product(look, hit.item.sub);
    out.name = String::utf8(deposits::scatter_name(look, hit.item.sub).c_str());
    out.verb = String::utf8(deposits::scatter_verb(look, hit.item.sub).c_str());
    out.grade = deposits::GRADE_MID;
    out.gather_s = kind.gather_s;
    out.cap = deposits::scatter_yield(look, hit.item.scale);
    out.refill_hours = kind.refill_hours;
    out.stock = scatter_ledger.stock(hit.key);
    return out;
}

void World::map_resource_taken(const ResourceSpot &p_spot) {
    if (!p_spot.scatter) {
        return;
    }
    if (scatter_ledger.settle(p_spot.scatter_key, p_spot.cap, p_spot.refill_hours, clock_hours()) && scatter_stream) {
        scatter_stream->refresh_chunk(scatter_key_chunk(p_spot.scatter_key)); // 倒了：换成树桩、拆掉树干的碰撞
    }
}

bool World::map_water_at(double p_x, double p_z) const {
    return WorldLandingGround(terrain).water(p_x, p_z);
}

Dictionary World::get_resource_site(int p_working_id) const {
    Dictionary d;
    for (size_t k = 0; k < resource_plan.sites.size(); k++) {
        const ResourceSite &s = resource_plan.sites[k];
        if (s.id != p_working_id) {
            continue;
        }
        d["index"] = int(k);
        d["kind"] = String(s.kind->key);
        d["face"] = Vector3(real_t(s.face_x), real_t(s.face_y), real_t(s.face_z));
        d["pile"] = Vector3(real_t(s.pile_x), real_t(s.pile_y), real_t(s.pile_z));
        d["item"] = String::utf8(s.item.c_str());
        d["name"] = String::utf8(s.name.c_str());
        d["grade"] = s.grade;
        d["cap"] = s.cap;
        d["island"] = s.island;
        d["stock"] = stock_amount(resource_stocks[k], s.cap, s.kind->refill_hours, clock_hours());
        d["reach"] = s.reach_m;
        d["landing"] = Vector3(real_t(s.land_x), 0.0f, real_t(s.land_z));
        d["landing_yaw"] = s.land_yaw;
        d["built"] = site_stream && site_stream->get_resource(s.id) != nullptr;
        return d;
    }
    return d;
}

Dictionary World::get_landmark(int p_id) const {
    Dictionary d;
    for (const ResourceLandmark &m : resource_plan.landmarks) {
        if (m.id != p_id) {
            continue;
        }
        d["kind"] = String(m.kind->key);
        d["name"] = String::utf8(m.name.c_str());
        d["position"] = Vector3(real_t(m.x), real_t(m.y), real_t(m.z));
        d["island"] = m.island;
        bool built = false;
        if (site_stream) {
            const PackedInt32Array ids = site_stream->get_built_landmarks();
            built = ids.has(m.id);
        }
        d["built"] = built;
        return d;
    }
    return d;
}

int World::find_landmark_near(double p_x, double p_z, double p_radius, const String &p_kind) const {
    int best = -1;
    double best_d = p_radius;
    const std::string kind = p_kind.utf8().get_data();
    for (const ResourceLandmark &m : resource_plan.landmarks) {
        const double d = std::hypot(m.x - p_x, m.z - p_z);
        if (d <= best_d && (kind.empty() || kind == m.kind->key)) {
            best_d = d;
            best = m.id;
        }
    }
    return best;
}

Dictionary World::find_scatter_near(double p_x, double p_z, double p_radius, const String &p_kind) const {
    Dictionary out;
    if (!scatter.valid()) {
        return out;
    }
    const std::string kind = p_kind.utf8().get_data();
    const ScatterChunkKey lo = scatter.chunk_at(p_x - p_radius, p_z - p_radius), hi = scatter.chunk_at(p_x + p_radius, p_z + p_radius);
    double best = p_radius;
    for (int iz = lo.iz; iz <= hi.iz; iz++) {
        for (int ix = lo.ix; ix <= hi.ix; ix++) {
            const ScatterChunkKey key{ ix, iz };
            for (const ScatterItem &item : scatter.chunk(key)) {
                const ScatterKind &sk = deposits::scatter(item.kind());
                const double d = std::hypot(item.x - p_x, item.z - p_z);
                if (d > best || (!kind.empty() && kind != sk.key)) {
                    continue;
                }
                best = d;
                out["key"] = int64_t(scatter_key(key, item.index));
                out["kind"] = String(sk.key);
                out["name"] = String::utf8(deposits::scatter_name(item.kind(), item.sub).c_str());
                out["product"] = String::utf8(deposits::scatter_product(item.kind(), item.sub).c_str());
                out["position"] = Vector3(item.x, item.y, item.z);
                out["radius"] = sk.radius * item.scale;
                out["solid"] = sk.solid;
            }
        }
    }
    return out;
}

int World::get_scatter_count_near(double p_x, double p_z, double p_radius, bool p_solid_only) const {
    if (!scatter.valid()) {
        return 0;
    }
    std::vector<ScatterItem> items;
    if (p_solid_only) {
        scatter.solids_near(p_x, p_z, p_radius, items);
        return int(items.size());
    }
    const ScatterChunkKey lo = scatter.chunk_at(p_x - p_radius, p_z - p_radius), hi = scatter.chunk_at(p_x + p_radius, p_z + p_radius);
    int n = 0;
    for (int iz = lo.iz; iz <= hi.iz; iz++) {
        for (int ix = lo.ix; ix <= hi.ix; ix++) {
            for (const ScatterItem &item : scatter.chunk(ScatterChunkKey{ ix, iz })) {
                n += std::hypot(item.x - p_x, item.z - p_z) <= p_radius ? 1 : 0;
            }
        }
    }
    return n;
}

PackedInt32Array World::get_resource_ids() const {
    PackedInt32Array ids;
    for (const ResourceSite &s : resource_plan.sites) {
        ids.push_back(s.id);
    }
    return ids;
}

int World::find_resource_near(double p_x, double p_z, double p_radius, const String &p_kind) const {
    int best = -1;
    double best_d = p_radius;
    const std::string kind = p_kind.utf8().get_data();
    for (const ResourceSite &s : resource_plan.sites) {
        const double d = std::hypot(s.face_x - p_x, s.face_z - p_z);
        if (d <= best_d && (kind.empty() || kind == s.kind->key)) {
            best_d = d;
            best = s.id;
        }
    }
    return best;
}

Vector3 World::get_freight_post(int p_village_id) const {
    WorldVillage *village = site_stream ? site_stream->get_village(p_village_id) : nullptr;
    Vector3 at;
    return village && village->get_freight_post(at) ? at : Vector3();
}

PackedInt32Array World::get_freight_port_ids() const {
    PackedInt32Array ids;
    for (const FreightPort &p : freight_ports) {
        ids.push_back(p.site_id);
    }
    return ids;
}

GroundHost *World::map_settlement_at(const Vector3 &p_global) const {
    return site_stream ? site_stream->village_at(p_global) : nullptr;
}

void World::map_focus(const Vector3 &p_global) {
    set_focus(p_global);
    if (site_stream) {
        site_stream->set_focus(p_global);
    }
    if (scatter_stream) {
        scatter_stream->set_focus(p_global);
    }
}

void World::map_prepare(const Vector3 &p_global) {
    prepare_at(p_global);
    if (site_stream) {
        site_stream->ensure_near(p_global); // 码头的栈桥、近处的村子与村民，人落地前就在
    }
    if (scatter_stream) {
        scatter_stream->ensure_near(p_global); // 近处的树与树干的碰撞（村子先铺：压着房子的要藏）
    }
}

int World::map_pending() const {
    return get_pending_count();
}

String World::map_location_text(const Vector3 &p_global) const {
    String text = vformat(String(U"岛 #%d · 东 %s km  南 %s km · 高 %d m"), get_island_at(p_global.x, p_global.z),
            String::num(p_global.x / 1000.0, 2), String::num(p_global.z / 1000.0, 2), int(std::lround(p_global.y)));
    // 走到采场跟前：报它的名字与存量
    WorldResource *resource = site_stream ? site_stream->resource_near(p_global, 60.0) : nullptr;
    if (resource) {
        const ResourceSite &s = resource->get_site();
        const double stock = stock_amount(resource_stocks[size_t(resource->get_index())], s.cap, s.kind->refill_hours, clock_hours());
        text += vformat(String(U" · %s（%s品）存 %d / %d"), String::utf8(s.name.c_str()), String::utf8(deposits::grade_name(s.grade)), int(std::floor(stock + 1e-9)), s.cap);
    }
    // 站在河边：这条河此刻的水情
    const Dictionary river = get_river_at(p_global.x, p_global.z);
    if (!river.is_empty()) {
        const double flow = river["flow"];
        const String state = !bool(river["wet"]) ? String(U"断流了，河床是干的")
                : flow > 1.0                     ? String(U"涨水漫滩")
                : flow > 0.6                     ? String(U"水大")
                : flow < 0.15                    ? String(U"水浅")
                                                 : String(U"水平");
        text += vformat(String(U" · 河宽 %s m（平岸 %s m）· 水深 %s m · %s"), String::num(double(river["width"]), 1),
                String::num(double(river["bankfull_width"]), 1), String::num(double(river["depth"]), 2), state);
    }
    return text;
}

WorldMap *World::map_make_panel() {
    WorldMap *panel = memnew(WorldMap);
    panel->setup(grid, info);
    for (int k = 0; k < get_dock_count(); k++) {
        panel->set_dock(get_dock_id(k), is_dock_berthable(k), get_dock_position(k));
    }
    // 采场标在采掘面上；地标（泉眼、温泉、洞穴）标在摆样子的那一点，岛底的洞标在点上。片与赋存区是一片地：底图的地表色就是林子，这里不标点
    for (const ResourceSite &s : resource_plan.sites) {
        const std::string label = s.name + " · " + deposits::grade_name(s.grade) + "品";
        panel->add_resource(s.id, s.face_x, s.face_z, Color(s.kind->r, s.kind->g, s.kind->b), label, false);
    }
    for (const ResourceDeposit &d : resource_data.deposits) {
        const DepositKind *kind = deposits::kind(d.kind);
        if (!kind || !deposits::is_landmark(*kind)) {
            continue;
        }
        double x = d.x, z = d.z;
        for (const ResourceLandmark &m : resource_plan.landmarks) {
            if (m.id == d.id) {
                x = m.x, z = m.z;
            }
        }
        const std::string label = d.subtype.empty() ? std::string(kind->name) : std::string(kind->name) + "（" + d.subtype + "）";
        panel->add_resource(d.id, x, z, Color(kind->r, kind->g, kind->b), label, true);
    }
    // 资源层：片号 → 种类
    std::vector<int> patch_kinds;
    for (const ResourceDeposit &d : resource_data.deposits) {
        const DepositKind *kind = deposits::kind(d.kind);
        if (d.form != DepositForm::PATCH || !kind || d.id < 0) {
            continue;
        }
        if (size_t(d.id) >= patch_kinds.size()) {
            patch_kinds.resize(size_t(d.id) + 1, -1);
        }
        patch_kinds[size_t(d.id)] = int(kind - deposits::KINDS);
    }
    panel->set_resource_patches(patch_kinds);
    return panel;
}

void World::map_refresh_panel(WorldMap *p_panel) const {
    if (site_stream) {
        p_panel->set_built_sites(site_stream->get_built_docks(), site_stream->get_built_villages());
        p_panel->set_built_resources(site_stream->get_built_resources());
    }
}
