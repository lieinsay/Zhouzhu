#include "planet_view.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "map_host.h"
#include "orbit_camera.h"
#include "world.h"

using namespace godot;

namespace {

constexpr const char *DECK_SHADER = "res://shaders/cloud_deck.gdshader";
constexpr int DECK_SEGMENTS = 96;
constexpr double DECK_FIRST_RING_M = 60.0; // 最里一圈；往外每圈放大 DECK_GROWTH
constexpr double DECK_GROWTH = 1.16;
constexpr double NEAR_FAR_RATIO = 1.2e6; // 远平面 / 近平面最多这么多（地面上 60 km / 0.05 m，P1 验过不退化）

Vector3 v3(const PlanetVec &p_v) {
    return Vector3(real_t(p_v.x), real_t(p_v.y), real_t(p_v.z));
}

PlanetVec pv(const Vector3 &p_v) {
    return PlanetVec(p_v.x, p_v.y, p_v.z);
}

double smooth01(double p_e0, double p_e1, double p_x) {
    const double t = std::clamp((p_x - p_e0) / (p_e1 - p_e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

PlanetView *PlanetView::active = nullptr;

void PlanetView::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_active"), &PlanetView::is_active);
    ClassDB::bind_method(D_METHOD("get_anchor_node"), &PlanetView::get_anchor_node);
    ClassDB::bind_method(D_METHOD("get_camera_latlon"), &PlanetView::get_camera_latlon);
    ClassDB::bind_method(D_METHOD("get_camera_planet_position"), &PlanetView::get_camera_planet_position);
    ClassDB::bind_method(D_METHOD("get_clock_longitude"), &PlanetView::get_clock_longitude);
    ClassDB::bind_method(D_METHOD("get_local_hour_offset"), &PlanetView::get_local_hour_offset);
    ClassDB::bind_method(D_METHOD("get_ground_clearance"), &PlanetView::get_ground_clearance);
    ClassDB::bind_method(D_METHOD("latlon_at", "flat"), &PlanetView::latlon_at);
    ClassDB::bind_method(D_METHOD("to_visual", "flat"), &PlanetView::to_visual);
    ClassDB::bind_method(D_METHOD("to_flat", "visual"), &PlanetView::to_flat);
    ClassDB::bind_method(D_METHOD("group_peak_visual", "node"), &PlanetView::group_peak_visual);
    ClassDB::bind_method(D_METHOD("group_transform", "node"), &PlanetView::group_transform);
    ClassDB::bind_method(D_METHOD("bearing_to_node", "node"), &PlanetView::bearing_to_node);
    ClassDB::bind_method(D_METHOD("distance_to_node", "node"), &PlanetView::distance_to_node);
    ClassDB::bind_method(D_METHOD("nearest_node"), &PlanetView::nearest_node);
    ClassDB::bind_method(D_METHOD("drop_at", "distance"), &PlanetView::drop_at);

    ClassDB::bind_method(D_METHOD("add_lod_file", "path"), &PlanetView::add_lod_file);
    ClassDB::bind_method(D_METHOD("is_weather_ready"), &PlanetView::is_weather_ready);
    ClassDB::bind_method(D_METHOD("flat_of_latlon", "lat", "lon"), &PlanetView::flat_of_latlon);
    ClassDB::bind_method(D_METHOD("node_latlon", "node"), &PlanetView::node_latlon);
    ClassDB::bind_method(D_METHOD("get_weather_state"), &PlanetView::get_weather_state);
    ClassDB::bind_method(D_METHOD("set_extra_lods_dir", "value"), &PlanetView::set_extra_lods_dir);
    ClassDB::bind_method(D_METHOD("get_extra_lods_dir"), &PlanetView::get_extra_lods_dir);
    ClassDB::bind_method(D_METHOD("set_planet_dir", "value"), &PlanetView::set_planet_dir);
    ClassDB::bind_method(D_METHOD("get_planet_dir"), &PlanetView::get_planet_dir);
    ClassDB::bind_method(D_METHOD("set_map_path", "value"), &PlanetView::set_map_path);
    ClassDB::bind_method(D_METHOD("get_map_path"), &PlanetView::get_map_path);
    ClassDB::bind_method(D_METHOD("set_cloud_sea_path", "value"), &PlanetView::set_cloud_sea_path);
    ClassDB::bind_method(D_METHOD("get_cloud_sea_path"), &PlanetView::get_cloud_sea_path);
    ClassDB::bind_method(D_METHOD("set_clock_node", "value"), &PlanetView::set_clock_node);
    ClassDB::bind_method(D_METHOD("get_clock_node"), &PlanetView::get_clock_node);
    ClassDB::bind_method(D_METHOD("set_camera_far", "value"), &PlanetView::set_camera_far);
    ClassDB::bind_method(D_METHOD("get_camera_far"), &PlanetView::get_camera_far);
    ClassDB::bind_method(D_METHOD("set_cloud_deck_radius", "value"), &PlanetView::set_cloud_deck_radius);
    ClassDB::bind_method(D_METHOD("get_cloud_deck_radius"), &PlanetView::get_cloud_deck_radius);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "planet_dir", PROPERTY_HINT_DIR), "set_planet_dir", "get_planet_dir");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "extra_lods_dir"), "set_extra_lods_dir", "get_extra_lods_dir");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "map_path"), "set_map_path", "get_map_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "cloud_sea_path"), "set_cloud_sea_path", "get_cloud_sea_path");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "clock_node"), "set_clock_node", "get_clock_node");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "camera_far", PROPERTY_HINT_RANGE, "50000,200000,1000,suffix:m"), "set_camera_far", "get_camera_far");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_deck_radius", PROPERTY_HINT_RANGE, "100000,3000000,1000,suffix:m"), "set_cloud_deck_radius", "get_cloud_deck_radius");
}

PlanetView::~PlanetView() {
    if (active == this) {
        active = nullptr;
    }
}

PlanetView *PlanetView::active_view() {
    return active && active->is_active() ? active : nullptr;
}

void PlanetView::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) {
        if (active == this) {
            active = nullptr;
        }
        if (globals_on) {
            // 全局参数跨场景留着：换到没有行星数据的地图前关掉
            RenderingServer::get_singleton()->global_shader_parameter_set("planet_curve_on", 0.0);
            globals_on = false;
        }
    }
}

void PlanetView::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    set_process_priority(100); // 相机这一帧都挪完了再算
    loaded = load_planet();
    set_process(loaded);
}

bool PlanetView::load_planet() {
    planet = std::make_shared<PlanetData>();
    std::string error;
    const PackedByteArray pb = FileAccess::get_file_as_bytes(planet_dir.path_join("planet.bin"));
    if (pb.is_empty() || !planet->parse_planet(pb.ptr(), size_t(pb.size()), error)) {
        ERR_PRINT(String(U"PlanetView：读不了行星包 ") + planet_dir + String(U"/planet.bin：") + String::utf8(error.c_str()) + String(U"——不弯、没有远处的岛群"));
        planet.reset();
        return false;
    }
    const PackedByteArray lb = FileAccess::get_file_as_bytes(planet_dir.path_join("lods.bin"));
    if (lb.is_empty() || !planet->parse_lods(lb.ptr(), size_t(lb.size()), error)) {
        ERR_PRINT(String(U"PlanetView：读不了粗版 ") + planet_dir + String(U"/lods.bin：") + String::utf8(error.c_str()) + String(U"——远处的岛群画不出来"));
    }
    // 按需生成过的粗版（生存模式的世界存档目录 lods/<节点>.bin）：退出再进不重生成；它们的逐日天气（<节点>.wx）交给天气场
    std::vector<String> extra_weather;
    if (!extra_lods_dir.is_empty() && DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(extra_lods_dir))) {
        const PackedStringArray files = DirAccess::get_files_at(extra_lods_dir);
        int added = 0;
        for (int64_t k = 0; k < files.size(); k++) {
            if (files[k].get_extension() == "bin" && files[k].get_basename().is_valid_int()) {
                added += add_lod_file(extra_lods_dir.path_join(files[k])) ? 1 : 0;
            }
        }
        if (added > 0) {
            print_line(vformat(String(U"PlanetView：存档目录里按需生成过的粗版 %d 份"), added));
        }
        weather_pending.clear(); // add_lod_file 排进来的 .wx 一起交给后台
        for (int64_t k = 0; k < files.size(); k++) {
            if (files[k].get_extension() == "wx" && files[k].get_basename().is_valid_int()) {
                extra_weather.push_back(extra_lods_dir.path_join(files[k]));
            }
        }
    }
    // 行星天气场（行星计划 P5）：逐日天气包在后台解开、建场（Debug 下几百毫秒），开局不等它；好了 GroupStream、天与光、行星贴图才挂上
    start_weather(FileAccess::get_file_as_bytes(planet_dir.path_join("weather.bin")), extra_weather);
    return true;
}

void PlanetView::start_weather(const PackedByteArray &p_pack, const std::vector<String> &p_extra) {
    std::vector<uint8_t> pack(p_pack.ptr(), p_pack.ptr() + p_pack.size());
    std::vector<std::vector<uint8_t>> extra;
    for (const String &path : p_extra) {
        const PackedByteArray b = FileAccess::get_file_as_bytes(path);
        extra.emplace_back(b.ptr(), b.ptr() + b.size());
    }
    const std::shared_ptr<const PlanetData> data = planet;
    weather_started = Time::get_singleton()->get_ticks_usec();
    weather_loading = std::async(std::launch::async, [data, pack = std::move(pack), extra = std::move(extra)]() -> std::shared_ptr<PlanetWeather> {
        auto pw = std::make_shared<PlanetWeather>(data);
        std::string error;
        if (!pack.empty()) {
            WeatherPack wp;
            if (wp.parse(pack.data(), pack.size(), error)) {
                pw->add_pack(wp);
            } else {
                ERR_PRINT(String(U"PlanetView：逐日天气包读不了（") + String::utf8(error.c_str()) + String(U"）——各群的天气只有载入了的细版，别处按气候场"));
            }
        }
        for (const std::vector<uint8_t> &b : extra) {
            WeatherPack wp;
            if (!b.empty() && wp.parse(b.data(), b.size(), error)) {
                pw->add_pack(wp);
            }
        }
        return pw;
    });
}

void PlanetView::poll_weather() {
    if (weather || !weather_loading.valid()) {
        return;
    }
    if (weather_loading.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    weather = weather_loading.get();
    weather_ms = double(Time::get_singleton()->get_ticks_usec() - weather_started) / 1000.0;
    for (const String &path : weather_pending) {
        add_weather_file(path);
    }
    weather_pending.clear();
    for (int n : reach_pending) {
        weather->refresh_reach(n);
    }
    reach_pending.clear();
    print_line(vformat(String(U"PlanetView：行星天气场好了——%d 群有逐日天气，后台 %.0f ms"), weather ? weather->table_count() : 0, weather_ms));
}

bool PlanetView::add_weather_file(const String &p_path) {
    if (!weather) {
        weather_pending.push_back(p_path);
        return true;
    }
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
    WeatherPack wp;
    std::string error;
    if (bytes.is_empty() || !wp.parse(bytes.ptr(), size_t(bytes.size()), error)) {
        ERR_PRINT(String(U"PlanetView：逐日天气 ") + p_path + String(U" 读不了：") + String::utf8(error.c_str()));
        return false;
    }
    weather->add_pack(wp);
    return true;
}

Vector3 PlanetView::flat_of_latlon(double p_lat, double p_lon) const {
    const PlanetVec f = anchor.latlon_flat(p_lat, p_lon);
    return Vector3(real_t(f.x), 0.0f, real_t(f.z));
}

Vector2 PlanetView::node_latlon(int p_node) const {
    if (!planet || p_node < 0 || p_node >= planet->node_count()) {
        return Vector2();
    }
    return Vector2(real_t(planet->node(p_node).lat), real_t(planet->node(p_node).lon));
}

Dictionary PlanetView::get_weather_state() const {
    Dictionary d;
    d["ready"] = weather != nullptr;
    d["tables"] = weather ? weather->table_count() : 0;
    d["ms"] = weather_ms;
    return d;
}

bool PlanetView::add_lod_file(const String &p_path) {
    if (!planet) {
        return false;
    }
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
    std::string error;
    std::vector<int> nodes;
    if (bytes.is_empty() || !planet->add_lods(bytes.ptr(), size_t(bytes.size()), error, &nodes)) {
        ERR_PRINT(String(U"PlanetView：粗版 ") + p_path + String(U" 读不了：") + String::utf8(error.c_str()));
        return false;
    }
    // 行星天气场：这一群管多远按粗版重量（生存世界；场还在后台建的话等它好了再量）
    for (int n : nodes) {
        if (weather) {
            weather->refresh_reach(n);
        } else {
            reach_pending.push_back(n);
        }
    }
    // 同一群的逐日天气（按需生成时一起写的 <节点>.wx，行星计划 P5）
    const String wx = p_path.get_basename() + ".wx";
    if (FileAccess::file_exists(wx)) {
        add_weather_file(wx);
    }
    return true;
}

bool PlanetView::try_anchor() {
    Node *node = get_node_or_null(map_path);
    map = MapHost::of(node);
    GroupWorld *world = Object::cast_to<GroupWorld>(node);
    if (!world && map) {
        world = Object::cast_to<GroupWorld>(map->map_node()); // 地图宿主是 PlanetHost（行星计划 P3）：锚 = 它此刻的锚群
    }
    if (!world || !world->is_loaded()) {
        return false;
    }
    const Dictionary info = world->get_info();
    const Dictionary source = info.get("source", Dictionary());
    if (!source.has("lat") || !source.has("lon")) {
        ERR_PRINT(String(U"PlanetView：地图的 world.json 没有 source.lat / lon，不弯"));
        set_process(false);
        return false;
    }
    PlanetShape shape;
    shape.sea_radius_m = planet->radius_km * 1000.0;
    anchor = GroupFrame(double(source["lat"]), double(source["lon"]), shape);
    anchor_node = int(source.get("node", -1));
    clock_lon = clock_node >= 0 && clock_node < planet->node_count() ? planet->node(clock_node).lon : anchor.lon_deg;
    anchored = true;
    active = this;
    cloud_sea = Object::cast_to<Node3D>(get_node_or_null(cloud_sea_path));
    if (cloud_sea) {
        cloud_sea->set_visible(false);
    }
    build_deck();
    return true;
}

void PlanetView::set_anchor_world(const GroupWorld *p_world) {
    if (!p_world || !planet) {
        return;
    }
    const Dictionary source = p_world->get_info().get("source", Dictionary());
    if (!source.has("lat") || !source.has("lon")) {
        return;
    }
    PlanetShape shape;
    shape.sea_radius_m = planet->radius_km * 1000.0;
    anchor = GroupFrame(double(source["lat"]), double(source["lon"]), shape);
    anchor_node = int(source.get("node", -1));
}

void PlanetView::build_deck() {
    const Ref<Shader> shader = ResourceLoader::get_singleton()->load(DECK_SHADER);
    if (shader.is_null()) {
        ERR_PRINT(String(U"PlanetView：读不了云带顶的着色器 ") + DECK_SHADER + String(U"，留着原来的平板云海"));
        if (cloud_sea) {
            cloud_sea->set_visible(true);
        }
        return;
    }
    // 同心环：中心一点，往外一圈圈放大到 cloud_deck_radius
    std::vector<double> radii;
    for (double r = DECK_FIRST_RING_M; r < cloud_deck_radius; r *= DECK_GROWTH) {
        radii.push_back(r);
    }
    radii.push_back(cloud_deck_radius);
    PackedVector3Array vertices, normals;
    PackedInt32Array indices;
    vertices.push_back(Vector3());
    normals.push_back(Vector3(0, 1, 0));
    for (double r : radii) {
        for (int k = 0; k < DECK_SEGMENTS; k++) {
            const double a = Math_TAU * k / DECK_SEGMENTS;
            vertices.push_back(Vector3(real_t(r * std::cos(a)), 0.0f, real_t(r * std::sin(a))));
            normals.push_back(Vector3(0, 1, 0));
        }
    }
    auto ring = [](int i, int k) { return 1 + i * DECK_SEGMENTS + (k % DECK_SEGMENTS); };
    for (int k = 0; k < DECK_SEGMENTS; k++) {
        indices.push_back(0); // 从上往下看（x 右、z 下）顺时针
        indices.push_back(ring(0, k));
        indices.push_back(ring(0, k + 1));
    }
    for (int i = 0; i + 1 < int(radii.size()); i++) {
        for (int k = 0; k < DECK_SEGMENTS; k++) {
            indices.push_back(ring(i, k));
            indices.push_back(ring(i + 1, k));
            indices.push_back(ring(i + 1, k + 1));
            indices.push_back(ring(i, k));
            indices.push_back(ring(i + 1, k + 1));
            indices.push_back(ring(i, k + 1));
        }
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_INDEX] = indices;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    Ref<ShaderMaterial> material;
    material.instantiate();
    material->set_shader(shader);
    mesh->surface_set_material(0, material);
    // 顶点在着色器里按真球面下弯（1,000 km 外降 78 km）：包围盒往下放足，永远画
    const real_t R = real_t(cloud_deck_radius * 1.05);
    const real_t drop = real_t(drop_at(cloud_deck_radius) * 1.2 + 2000.0);
    mesh->set_custom_aabb(AABB(Vector3(-R, -drop, -R), Vector3(2 * R, drop + 200.0f, 2 * R)));
    deck = memnew(MeshInstance3D);
    deck->set_name("CloudDeck");
    deck->set_mesh(mesh);
    deck->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    deck->set_as_top_level(true);
    add_child(deck);
}

double PlanetView::surface_at(const PlanetVec &p_flat) const {
    return map ? double(map->map_surface(v3(p_flat))) : NAN;
}

double PlanetView::drop_at(double p_distance) const {
    return p_distance * p_distance / (2.0 * anchor.radius);
}

void PlanetView::_process(double /* p_delta */) {
    poll_weather();
    if (!anchored && !try_anchor()) {
        return;
    }
    Viewport *viewport = get_viewport();
    Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    if (!camera) {
        return;
    }
    const Vector3 p = camera->get_global_position();
    if (!p.is_finite()) {
        return;
    }
    cam = pv(p);
    // 开发者的轨道相机（F7）自己记着双精度的位置：两万公里高、离锚群几千公里时单精度只到米
    if (const OrbitCamera *orbit = Object::cast_to<OrbitCamera>(camera)) {
        if (orbit->has_true_position()) {
            cam = orbit->get_true_flat();
        }
    }
    view = CurveView::at(anchor, cam);
    cam_planet = anchor.to_planet(cam);
    for (int k = 0; k < 3; k++) {
        const PlanetVec &c = view.local.cols[k]; // 锚的切平面坐标 → 行星系
        cam_axes.cols[k] = anchor.east * c.x + anchor.up * c.y - anchor.north * c.z;
    }
    planet_latlon(cam_planet, cam_lat, cam_lon);
    const double ground = map ? map->map_surface(p) : NAN;
    ground_clearance = p.y - (std::isfinite(ground) ? ground : VerticalDatum::CLOUD_TOP_Y);
    push_globals();
    update_camera(camera);
    if (deck) {
        deck->set_global_position(Vector3(p.x, real_t(VerticalDatum::CLOUD_TOP_Y), p.z));
    }
}

void PlanetView::push_globals() {
    RenderingServer *rs = RenderingServer::get_singleton();
    const Basis basis(v3(view.local.cols[0]), v3(view.local.cols[1]), v3(view.local.cols[2])); // 列 = 相机处的东、上、南
    rs->global_shader_parameter_set("planet_curve_on", 1.0);
    rs->global_shader_parameter_set("planet_curve_cam", v3(view.cam));
    rs->global_shader_parameter_set("planet_curve_basis", basis);
    rs->global_shader_parameter_set("planet_curve_dcam", v3(view.delta_cam));
    rs->global_shader_parameter_set("planet_curve_radius", view.radius);
    // 远处往相机压：30 km 以下 10 km / 26 km（P1），相机再高按高度放大（P2，CurveSqueeze::at_height）
    CurveSqueeze::at_height(cam.y, squeeze_keep, squeeze_limit);
    rs->global_shader_parameter_set("planet_curve_squeeze", Vector2(real_t(squeeze_keep), real_t(squeeze_limit)));
    globals_on = true;
}

void PlanetView::update_camera(Camera3D *p_camera) {
    const uint64_t id = uint64_t(p_camera->get_instance_id());
    auto found = base_near.find(id);
    if (found == base_near.end()) {
        found = base_near.emplace(id, std::pair<double, double>(std::max(near_min, double(p_camera->get_near())), double(p_camera->get_far()))).first;
    }
    const double base = found->second.first;
    // 远平面：盖过压到的上限；近平面：离地高了放大（P1），再不小于远平面的百万分之一（投影矩阵单精度：比值过 1e6 远平面就退化）
    const double far = std::max({ camera_far, found->second.second, squeeze_limit * 1.1 + 5000.0 });
    const double near = std::max(base + (std::max(base, near_max) - base) * smooth01(near_rise_from, near_rise_to, ground_clearance), far / NEAR_FAR_RATIO);
    if (std::abs(p_camera->get_near() - near) > 1e-4 * std::max(1.0, near)) {
        p_camera->set_near(real_t(near));
    }
    if (std::abs(p_camera->get_far() - far) > 1e-3 * far) {
        p_camera->set_far(real_t(far));
    }
}

Transform3D PlanetView::group_transform(int p_node) const {
    if (!is_active() || p_node < 0 || p_node >= planet->node_count()) {
        return Transform3D();
    }
    const PlanetNode &n = planet->node(p_node);
    const GroupFrame g(n.lat, n.lon, PlanetShape{ planet->radius_km * 1000.0 });
    const PlanetVec axes[3] = { g.east, g.up, g.north * -1.0 }; // 群的切平面轴（东、上、南）在行星系
    const PlanetVec rel = g.up * g.radius - cam_planet;
    Vector3 cols[3];
    for (int k = 0; k < 3; k++) {
        cols[k] = Vector3(real_t(cam_axes.cols[0].dot(axes[k])), real_t(cam_axes.cols[1].dot(axes[k])), real_t(cam_axes.cols[2].dot(axes[k])));
    }
    const PlanetVec origin = cam + PlanetVec(cam_axes.cols[0].dot(rel), cam_axes.cols[1].dot(rel), cam_axes.cols[2].dot(rel));
    return Transform3D(Basis(cols[0], cols[1], cols[2]), v3(origin));
}

double PlanetView::group_distance(int p_node) const {
    if (!planet || p_node < 0 || p_node >= planet->node_count()) {
        return INFINITY;
    }
    const PlanetNode &n = planet->node(p_node);
    return great_circle_m(cam_lat, cam_lon, n.lat, n.lon, anchor.radius);
}

Vector3 PlanetView::enu_to_visual(const Vector3 &p_enu) const {
    if (!is_active()) {
        return p_enu;
    }
    const PlanetVec up = cam_planet.normalized();
    PlanetVec east = PlanetVec(0.0, 0.0, 1.0).cross(up); // 北极 × 上 = 东
    if (east.length() < 1e-9) {
        east = anchor.east; // 正在极点上：随便挑一个
    }
    east = east.normalized();
    const PlanetVec north = up.cross(east);
    const PlanetVec w = east * p_enu.x + up * p_enu.y - north * p_enu.z;
    return Vector3(real_t(cam_axes.cols[0].dot(w)), real_t(cam_axes.cols[1].dot(w)), real_t(cam_axes.cols[2].dot(w)));
}

PackedFloat64Array PlanetView::get_camera_planet_position() const {
    PackedFloat64Array out;
    out.push_back(cam_planet.x);
    out.push_back(cam_planet.y);
    out.push_back(cam_planet.z);
    return out;
}

double PlanetView::get_local_hour_offset() const {
    double d = std::fmod(cam_lon - clock_lon, 360.0);
    if (d > 180.0) {
        d -= 360.0;
    } else if (d < -180.0) {
        d += 360.0;
    }
    return d / 15.0;
}

Vector2 PlanetView::latlon_at(const Vector3 &p_flat) const {
    if (!is_active()) {
        return Vector2();
    }
    double lat = 0.0, lon = 0.0;
    anchor.flat_latlon(pv(p_flat), lat, lon);
    return Vector2(real_t(lat), real_t(lon));
}

Vector3 PlanetView::to_visual(const Vector3 &p_flat) const {
    return is_active() ? v3(view.visual(anchor, pv(p_flat))) : p_flat;
}

Vector3 PlanetView::to_flat(const Vector3 &p_visual) const {
    return is_active() ? v3(view.flat_of(anchor, pv(p_visual))) : p_visual;
}

Vector3 PlanetView::group_peak_visual(int p_node) const {
    if (!is_active() || p_node < 0 || p_node >= planet->node_count()) {
        return Vector3();
    }
    const PlanetLod lod = planet->lod(p_node);
    if (lod.empty()) {
        return Vector3();
    }
    size_t best = 0;
    for (size_t k = 0; k < lod.height.size(); k++) {
        if (lod.land[k] > 0 && lod.height[k] > lod.height[best]) {
            best = k;
        }
    }
    double x = 0.0, z = 0.0;
    lod.cell_center(int(best / size_t(lod.cols)), int(best % size_t(lod.cols)), x, z);
    const PlanetVec q(x, lod.height[best], z);
    if (p_node == anchor_node) {
        return v3(view.visual(anchor, q));
    }
    const PlanetNode &n = planet->node(p_node);
    const GroupFrame g(n.lat, n.lon, PlanetShape{ planet->radius_km * 1000.0 });
    const PlanetVec rel = g.to_planet(q) - cam_planet;
    return v3(cam + PlanetVec(cam_axes.cols[0].dot(rel), cam_axes.cols[1].dot(rel), cam_axes.cols[2].dot(rel)));
}

double PlanetView::bearing_to_node(int p_node) const {
    if (!planet || p_node < 0 || p_node >= planet->node_count()) {
        return 0.0;
    }
    const PlanetNode &n = planet->node(p_node);
    return bearing_deg(cam_lat, cam_lon, n.lat, n.lon);
}

int PlanetView::nearest_node() const {
    return planet ? planet->nearest_node(cam_lat, cam_lon) : -1;
}
