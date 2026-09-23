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
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "dock_builder.h"
#include "farm.h"
#include "shipyard.h"
#include "site_stream.h"
#include "world_ground.h"
#include "world_map.h"

using namespace godot;

namespace {

constexpr uint64_t ADOPT_BUDGET_USEC = 6000; // 每帧最多花这么久往场景里挂块

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
    ClassDB::bind_method(D_METHOD("get_dock_id", "index"), &World::get_dock_id);
    ClassDB::bind_method(D_METHOD("is_dock_berthable", "index"), &World::is_dock_berthable);
    ClassDB::bind_method(D_METHOD("get_dock_berth", "index"), &World::get_dock_berth);
    ClassDB::bind_method(D_METHOD("get_dock_spawn", "index"), &World::get_dock_spawn);
    ClassDB::bind_method(D_METHOD("get_dock_position", "index"), &World::get_dock_position);

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
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "data_dir", PROPERTY_HINT_DIR), "set_data_dir", "get_data_dir");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sites_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "SiteStream"), "set_sites_path", "get_sites_path");
    ADD_SIGNAL(MethodInfo("rebuilt"));
    ADD_PROPERTY(PropertyInfo(Variant::INT, "terrain_seed"), "set_terrain_seed", "get_terrain_seed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "split", PROPERTY_HINT_RANGE, "0.5,4,0.05"), "set_split", "get_split");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "build_threads", PROPERTY_HINT_RANGE, "1,16,1"), "set_build_threads", "get_build_threads");
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
    terrain = WorldTerrain(grid, islands, uint64_t(terrain_seed));
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

    ground_material.instantiate();
    ground_material->set_roughness(1.0);
    ground_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    // 顶点色是按 sRGB 调的；不声明的话引擎当线性值用，整片地会发白
    ground_material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
    return true;
}

void World::start_workers() {
    stopping = false;
    for (int k = 0; k < std::max(1, build_threads); k++) {
        workers.emplace_back([this]() { worker_loop(); });
    }
}

void World::stop_workers() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stopping = true;
        jobs.clear();
    }
    queue_cv.notify_all();
    for (std::thread &t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers.clear();
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

GroundHost *World::map_settlement_at(const Vector3 &p_global) const {
    return site_stream ? site_stream->village_at(p_global) : nullptr;
}

void World::map_focus(const Vector3 &p_global) {
    set_focus(p_global);
    if (site_stream) {
        site_stream->set_focus(p_global);
    }
}

void World::map_prepare(const Vector3 &p_global) {
    prepare_at(p_global);
    if (site_stream) {
        site_stream->ensure_near(p_global); // 码头的栈桥、近处的村子与村民，人落地前就在
    }
}

int World::map_pending() const {
    return get_pending_count();
}

String World::map_location_text(const Vector3 &p_global) const {
    return vformat(String(U"岛 #%d · 东 %s km  南 %s km · 高 %d m"), get_island_at(p_global.x, p_global.z),
            String::num(p_global.x / 1000.0, 2), String::num(p_global.z / 1000.0, 2), int(std::lround(p_global.y)));
}

WorldMap *World::map_make_panel() {
    WorldMap *panel = memnew(WorldMap);
    panel->setup(grid, info);
    for (int k = 0; k < get_dock_count(); k++) {
        panel->set_dock(get_dock_id(k), is_dock_berthable(k), get_dock_position(k));
    }
    return panel;
}

void World::map_refresh_panel(WorldMap *p_panel) const {
    if (site_stream) {
        p_panel->set_built_sites(site_stream->get_built_docks(), site_stream->get_built_villages());
    }
}
