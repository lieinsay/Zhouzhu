#include "world.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "dock_builder.h"
#include "planet_jobs.h"
#include "farm.h"
#include "shipyard.h"
#include "hinterland.h"
#include "planet_view.h"
#include "scatter_stream.h"
#include "site_stream.h"
#include "weather_view.h"
#include "world_clock.h"
#include "world_ground.h"
#include "world_map.h"

using namespace godot;

namespace {

constexpr uint64_t ADOPT_BUDGET_USEC = 4000; // 每帧最多花这么久往场景里挂地面块（连碰撞）
constexpr int64_t COLLISION_PART_FACES = 2048; // 叶块的碰撞按这么多个三角面一段（一段挂上 1～3 ms）
constexpr uint64_t RIVER_BUDGET_USEC = 2000; // 每帧最多花这么久挂水面块
constexpr double RIVER_RETARGET_M = 64.0; // 焦点挪了这么远才重新挑水面块
constexpr double RIVER_SYNC_M = 1000.0; // 瞬移时这么近的水面块当场建好，远的照常排队
constexpr double PREPARE_RADIUS_M = 80.0; // 瞬移时落点这么近的叶块当场建好
constexpr const char *RIVER_SHADER = "res://shaders/river.gdshader";
constexpr const char *GROUND_SHADER = "res://shaders/ground.gdshader";
constexpr double MARGIN_RETARGET_M = 500.0; // 相机挪了这么远才重算地面块的剔除余量
constexpr double MARGIN_SLACK_M = 40.0;
constexpr double FAR_MODE_M = 9000.0; // 块离相机最近处比这远：着色器压近了它，自己剔、不投影（见 update_cull_margins）
constexpr real_t FAR_MODE_MARGIN_M = 1.0e6f;

double tile_distance(const TileLayout &p_layout, const TileKey &p_key, double p_x, double p_z) {
    double x, z;
    p_layout.corner(p_key, x, z);
    const double s = p_layout.size(p_key.level);
    const double dx = std::max({ x - p_x, 0.0, p_x - (x + s) });
    const double dz = std::max({ z - p_z, 0.0, p_z - (z + s) });
    return std::hypot(dx, dz);
}

// 任务池的号（52 位，高 12 位是种类与通道，见 job_pool.h）：地面块用 TileKey::packed（层号 4 位在 48 位以上、格号各 24 位，
// 叶块 128 m × 2²⁴ ≈ 2,100 km）；水面块的格号各取 26 位（块 × 6,700 万）。都是按群分表的（每群自己的通道），岛群最大也就两三百公里，溢不出
uint64_t river_id(const RiverChunkKey &p_key) {
    return (uint64_t(uint32_t(p_key.ix) & 0x3FFFFFFu) << 26) | uint64_t(uint32_t(p_key.iz) & 0x3FFFFFFu);
}

RiverChunkKey river_key_of(uint64_t p_id) {
    const auto signed26 = [](uint64_t v) { return int(int32_t(uint32_t(v << 6)) >> 6); };
    return RiverChunkKey{ signed26((p_id >> 26) & 0x3FFFFFFu), signed26(p_id & 0x3FFFFFFu) };
}

// 在后台读一个岛群的包（GROUP_LOAD 活的结果）
struct GroupLoadDone : JobResult {
    bool ok = false;
    std::string error;
};

// 平铺的 float（每点 N 个）→ 引擎的数组：直接写内存，不逐个 set（Debug 下逐个 set 一块地面要好几毫秒）
PackedVector3Array vec3s(const std::vector<float> &p_flat) {
    PackedVector3Array out;
    out.resize(int64_t(p_flat.size() / 3));
    Vector3 *w = out.ptrw();
    for (int64_t k = 0; k < out.size(); k++) {
        w[k] = Vector3(p_flat[size_t(k) * 3], p_flat[size_t(k) * 3 + 1], p_flat[size_t(k) * 3 + 2]);
    }
    return out;
}

PackedInt32Array int32s(const std::vector<int32_t> &p_values) {
    PackedInt32Array out;
    out.resize(int64_t(p_values.size()));
    std::copy(p_values.begin(), p_values.end(), out.ptrw());
    return out;
}

PackedFloat32Array float32s(const std::vector<float> &p_values) {
    PackedFloat32Array out;
    out.resize(int64_t(p_values.size()));
    std::copy(p_values.begin(), p_values.end(), out.ptrw());
    return out;
}

} // namespace

void GroupWorld::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_focus", "focus"), &GroupWorld::set_focus);
    ClassDB::bind_method(D_METHOD("get_focus"), &GroupWorld::get_focus);
    ClassDB::bind_method(D_METHOD("prepare_at", "point", "radius"), &GroupWorld::prepare_at, DEFVAL(160.0));
    ClassDB::bind_method(D_METHOD("is_ready_at", "point"), &GroupWorld::is_ready_at);
    ClassDB::bind_method(D_METHOD("get_surface_height", "x", "z"), &GroupWorld::get_surface_height);
    ClassDB::bind_method(D_METHOD("get_island_at", "x", "z"), &GroupWorld::get_island_at);
    ClassDB::bind_method(D_METHOD("get_cover_at", "x", "z"), &GroupWorld::get_cover_at);
    ClassDB::bind_method(D_METHOD("get_info"), &GroupWorld::get_info);
    ClassDB::bind_method(D_METHOD("is_loaded"), &GroupWorld::is_loaded);
    ClassDB::bind_method(D_METHOD("get_tile_count"), &GroupWorld::get_tile_count);
    ClassDB::bind_method(D_METHOD("get_pending_count"), &GroupWorld::get_pending_count);
    ClassDB::bind_method(D_METHOD("get_dock_count"), &GroupWorld::get_dock_count);
    ClassDB::bind_method(D_METHOD("find_dock", "id"), &GroupWorld::find_dock);
    ClassDB::bind_method(D_METHOD("get_freight_post", "village_id"), &GroupWorld::get_freight_post);
    ClassDB::bind_method(D_METHOD("get_freight_port_ids"), &GroupWorld::get_freight_port_ids);
    ClassDB::bind_method(D_METHOD("get_resource_site", "working_id"), &GroupWorld::get_resource_site);
    ClassDB::bind_method(D_METHOD("get_resource_ids"), &GroupWorld::get_resource_ids);
    ClassDB::bind_method(D_METHOD("find_resource_near", "x", "z", "radius", "kind"), &GroupWorld::find_resource_near, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_landmark", "id"), &GroupWorld::get_landmark);
    ClassDB::bind_method(D_METHOD("find_landmark_near", "x", "z", "radius", "kind"), &GroupWorld::find_landmark_near, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("find_scatter_near", "x", "z", "radius", "kind"), &GroupWorld::find_scatter_near, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("get_scatter_count_near", "x", "z", "radius", "solid_only"), &GroupWorld::get_scatter_count_near);
    ClassDB::bind_method(D_METHOD("get_dock_id", "index"), &GroupWorld::get_dock_id);
    ClassDB::bind_method(D_METHOD("is_dock_berthable", "index"), &GroupWorld::is_dock_berthable);
    ClassDB::bind_method(D_METHOD("get_dock_berth", "index"), &GroupWorld::get_dock_berth);
    ClassDB::bind_method(D_METHOD("get_dock_spawn", "index"), &GroupWorld::get_dock_spawn);
    ClassDB::bind_method(D_METHOD("get_dock_position", "index"), &GroupWorld::get_dock_position);
    ClassDB::bind_method(D_METHOD("get_runoff"), &GroupWorld::get_runoff);
    ClassDB::bind_method(D_METHOD("get_river_at", "x", "z"), &GroupWorld::get_river_at);
    ClassDB::bind_method(D_METHOD("get_river_chunk_count"), &GroupWorld::get_river_chunk_count);
    ClassDB::bind_method(D_METHOD("get_weather_at", "x", "z", "y"), &GroupWorld::get_weather_at, DEFVAL(NAN));
    ClassDB::bind_method(D_METHOD("get_snow_line"), &GroupWorld::get_snow_line);
    ClassDB::bind_method(D_METHOD("find_river_near", "x", "z", "radius", "min_acc_km2"), &GroupWorld::find_river_near);
    ClassDB::bind_method(D_METHOD("set_river_radius", "value"), &GroupWorld::set_river_radius);
    ClassDB::bind_method(D_METHOD("get_river_radius"), &GroupWorld::get_river_radius);
    ClassDB::bind_method(D_METHOD("set_runoff_override", "value"), &GroupWorld::set_runoff_override);
    ClassDB::bind_method(D_METHOD("get_runoff_override"), &GroupWorld::get_runoff_override);
    ClassDB::bind_method(D_METHOD("get_stream_frame", "frame"), &GroupWorld::get_stream_frame);
    ClassDB::bind_method(D_METHOD("get_stream_totals"), &GroupWorld::get_stream_totals);
    ClassDB::bind_method(D_METHOD("reset_stream_meter"), &GroupWorld::reset_stream_meter);

    ClassDB::bind_method(D_METHOD("set_data_dir", "value"), &GroupWorld::set_data_dir);
    ClassDB::bind_method(D_METHOD("get_data_dir"), &GroupWorld::get_data_dir);
    ClassDB::bind_method(D_METHOD("set_terrain_seed", "value"), &GroupWorld::set_terrain_seed);
    ClassDB::bind_method(D_METHOD("get_terrain_seed"), &GroupWorld::get_terrain_seed);
    ClassDB::bind_method(D_METHOD("set_split", "value"), &GroupWorld::set_split);
    ClassDB::bind_method(D_METHOD("get_split"), &GroupWorld::get_split);
    ClassDB::bind_method(D_METHOD("set_build_threads", "value"), &GroupWorld::set_build_threads);
    ClassDB::bind_method(D_METHOD("get_build_threads"), &GroupWorld::get_build_threads);
    ClassDB::bind_method(D_METHOD("set_async_load", "value"), &GroupWorld::set_async_load);
    ClassDB::bind_method(D_METHOD("get_async_load"), &GroupWorld::get_async_load);
    ClassDB::bind_method(D_METHOD("is_loading"), &GroupWorld::is_loading);
    ClassDB::bind_method(D_METHOD("get_node_id"), &GroupWorld::get_node_id);
    ClassDB::bind_method(D_METHOD("get_local_offset_hours"), &GroupWorld::get_local_offset_hours);
    ClassDB::bind_method(D_METHOD("get_lane"), &GroupWorld::get_lane);
    ClassDB::bind_method(D_METHOD("set_physics_on", "on"), &GroupWorld::set_physics_on);
    ClassDB::bind_method(D_METHOD("is_physics_on"), &GroupWorld::is_physics_on);

    ClassDB::bind_method(D_METHOD("set_sites_path", "value"), &GroupWorld::set_sites_path);
    ClassDB::bind_method(D_METHOD("get_sites_path"), &GroupWorld::get_sites_path);
    ClassDB::bind_method(D_METHOD("set_scatter_path", "value"), &GroupWorld::set_scatter_path);
    ClassDB::bind_method(D_METHOD("get_scatter_path"), &GroupWorld::get_scatter_path);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "data_dir", PROPERTY_HINT_DIR), "set_data_dir", "get_data_dir");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sites_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "SiteStream"), "set_sites_path", "get_sites_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "scatter_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "ScatterStream"), "set_scatter_path", "get_scatter_path");
    ADD_SIGNAL(MethodInfo("rebuilt"));
    ADD_PROPERTY(PropertyInfo(Variant::INT, "terrain_seed"), "set_terrain_seed", "get_terrain_seed");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "split", PROPERTY_HINT_RANGE, "0.5,4,0.05"), "set_split", "get_split");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "build_threads", PROPERTY_HINT_RANGE, "1,16,1"), "set_build_threads", "get_build_threads");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "async_load"), "set_async_load", "get_async_load");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "river_radius", PROPERTY_HINT_RANGE, "500,20000,100"), "set_river_radius", "get_river_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "runoff_override", PROPERTY_HINT_RANGE, "-1,8,0.05"), "set_runoff_override", "get_runoff_override");
}

GroupWorld::~GroupWorld() {
    // 线程读着 terrain / layout / scatter（这一群的通道上的活：地面块、水面块、散布、村子……）：先等它们做完，成员再析构
    if (pool) {
        pool->drain_lane(lane);
    }
    if (PlanetJobs *jobs = Object::cast_to<PlanetJobs>(ObjectDB::get_instance(jobs_id))) {
        jobs->release_lane(lane);
    }
}

void GroupWorld::attach_pool() {
    if (pool) {
        return;
    }
    if (PlanetJobs *jobs = PlanetJobs::find(this)) {
        const int l = jobs->acquire_lane();
        if (l > 0) {
            pool = jobs->share_pool();
            lane = l;
            jobs_id = jobs->get_instance_id();
            return;
        }
        ERR_PRINT(String(U"GroupWorld：共用任务池的通道领完了，自己开一个池"));
    }
    // 场景里没有共用的池（甲乙两岛以外只有一个群的图、单独的工具）：自己开一个，照旧用 1 号通道
    pool = std::make_shared<JobPool>();
    pool->start(build_threads);
    lane = 1;
}

void GroupWorld::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    site_stream = Object::cast_to<SiteStream>(get_node_or_null(sites_path));
    scatter_stream = Object::cast_to<ScatterStream>(get_node_or_null(scatter_path));
    attach_pool();
    set_physics_on(physics_on); // 进树前就定了是邻群（GroupStream 建的）：船坞跟着撤出物理
    if (async_load) {
        // 后台读（Debug 下 2051 约 1 s）：这期间主线程不碰地形这些成员；_process 里等它做完再装
        loading = true;
        pool->submit(job_key(JobKind::GROUP_LOAD, 0), 0.0, [this]() -> std::unique_ptr<JobResult> {
            std::unique_ptr<GroupLoadDone> out = std::make_unique<GroupLoadDone>();
            out->ok = load_data(out->error);
            return out;
        });
        set_process(true);
        return;
    }
    std::string error;
    if (!load_data(error)) {
        ERR_PRINT(String("World: ") + String::utf8(error.c_str()));
        set_process(false);
        return;
    }
    install();
    set_process(true);
    emit_signal("rebuilt"); // 挂在下面的 Farm、Shipyard 先于这里 _ready，此刻才等到地面
}

double GroupWorld::get_local_offset_hours() const {
    const PlanetView *pv = PlanetView::active_view();
    if (!pv || node_id < 0 || node_id == pv->get_clock_node()) {
        return 0.0;
    }
    const Dictionary source = info.get("source", Dictionary());
    if (!source.has("lon")) {
        return 0.0;
    }
    double d = std::fmod(double(source["lon"]) - pv->get_clock_longitude(), 360.0);
    if (d > 180.0) {
        d -= 360.0;
    } else if (d < -180.0) {
        d += 360.0;
    }
    return d / 15.0;
}

void GroupWorld::set_cloud_reference(const CloudFrame &p_frame, const std::shared_ptr<const Climate> &p_ref_climate) {
    cloud_frame = p_frame;
    cloud_ref = p_ref_climate;
    if (loaded) {
        weather.set_cloud_frame(cloud_frame);
        if (cloud_ref) {
            weather.share_cloud_drift(*cloud_ref);
        }
    }
}

void GroupWorld::finish_loading_now() {
    if (!loading) {
        return;
    }
    std::unique_ptr<JobResult> r = pool->run_now(job_key(JobKind::GROUP_LOAD, 0));
    if (!r) {
        std::vector<JobPool::Done> done;
        pool->collect(JobKind::GROUP_LOAD, done, lane);
        if (!done.empty()) {
            r = std::move(done.front().result);
        }
    }
    loading = false;
    const GroupLoadDone *g = static_cast<const GroupLoadDone *>(r.get());
    if (!g || !g->ok) {
        ERR_PRINT(String("World: ") + (g ? String::utf8(g->error.c_str()) : String(U"后台载入没有结果")));
        set_process(false);
        return;
    }
    install();
    emit_signal("rebuilt");
}

void GroupWorld::install() {
    const Ref<Shader> ground_shader = ResourceLoader::get_singleton()->load(GROUND_SHADER);
    if (ground_shader.is_valid()) {
        Ref<ShaderMaterial> m;
        m.instantiate();
        m->set_shader(ground_shader);
        ground_material = m;
    } else {
        ERR_PRINT(String("World: 读不了地皮着色器 ") + GROUND_SHADER + String(U"，地皮不换季、不盖雪"));
        Ref<StandardMaterial3D> m;
        m.instantiate();
        m->set_roughness(1.0);
        m->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
        // 顶点色是按 sRGB 调的；不声明的话引擎当线性值用，整片地会发白
        m->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
        ground_material = m;
    }
    if (terrain.has_rivers()) {
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
    loaded = true;
    track_shading(ground_material);
    track_shading(river_material);
    update_runoff();
}

void GroupWorld::track_shading(const Ref<Material> &p_material) {
    const Ref<ShaderMaterial> m = p_material;
    if (m.is_null() || std::find(shaded.begin(), shaded.end(), m) != shaded.end()) {
        return;
    }
    if (elev_texture.is_null()) {
        update_shading(); // 头一回：先把海拔表做出来
    }
    push_shading(m);
    m->set_shader_parameter("wx_group_inv", Projection(get_global_transform().affine_inverse()));
    shaded.push_back(m);
}

void GroupWorld::push_shading(const Ref<ShaderMaterial> &p_material) const {
    p_material->set_shader_parameter("wx_group_elev", elev_texture);
    // 分箱的高低、极向（南半球 +1：背阴坡朝 +z）；w = 1 表示用这一群自己的
    p_material->set_shader_parameter("wx_group", Vector4(real_t(weather.get_h_lo()), real_t(weather.get_h_hi()), weather.calendar().latitude_deg < 0.0 ? 1.0f : -1.0f, 1.0f));
}

void GroupWorld::update_shading() {
    // 这一群的海拔表（与 Atmosphere 给全局 wx_elev 的同一个格式：两行，每箱 RGBA float）；覆盖（开发者改天气）跟着这一群的天气模型
    const WorldClock *clock = WorldClock::get_singleton();
    weather.elevation_table(clock ? clock->get_calendar_day() : 0.0, elev_values);
    const int bins = weather.get_bins(), fields = WeatherModel::table_fields();
    PackedByteArray bytes;
    bytes.resize(int64_t(bins) * 2 * 4 * int64_t(sizeof(float)));
    float *out = reinterpret_cast<float *>(bytes.ptrw());
    for (int b = 0; b < bins; b++) {
        const float *e = &elev_values[size_t(b) * size_t(fields)];
        float *r0 = out + size_t(b) * 4, *r1 = out + (size_t(bins) + size_t(b)) * 4;
        for (int k = 0; k < 4; k++) {
            r0[k] = e[k]; // 积雪 mm、负积温、绿、叶
            r1[k] = e[4 + k]; // 秋、春芽、作物、日均温
        }
    }
    const Ref<Image> image = Image::create_from_data(bins, 2, false, Image::FORMAT_RGBAF, bytes);
    if (elev_texture.is_valid() && elev_texture->get_width() == bins && elev_texture->get_height() == 2) {
        elev_texture->update(image);
    } else {
        elev_texture = ImageTexture::create_from_image(image); // 头一回，或载入之后分箱变了：连分箱的高低一起重设
        for (const Ref<ShaderMaterial> &m : shaded) {
            push_shading(m);
        }
    }
    // 摆放变了（邻群跟着相机、换锚）：世界 → 这一群平面的变换跟上
    const Transform3D xf = get_global_transform();
    if (!shaded_xform_set || !xf.is_equal_approx(shaded_xform)) {
        shaded_xform = xf;
        shaded_xform_set = true;
        const Projection inv(xf.affine_inverse());
        for (const Ref<ShaderMaterial> &m : shaded) {
            m->set_shader_parameter("wx_group_inv", inv);
        }
    }
}

bool GroupWorld::load_data(std::string &r_error) {
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(data_dir.path_join("terrain.bin"));
    std::string error;
    if (bytes.is_empty() || !parse_world_grid(bytes.ptr(), size_t(bytes.size()), grid, error)) {
        r_error = std::string("读不了 ") + data_dir.path_join("terrain.bin").utf8().get_data() + "：" + error;
        return false;
    }
    const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(data_dir.path_join("world.json")));
    if (parsed.get_type() != Variant::DICTIONARY) {
        r_error = std::string("读不了 ") + data_dir.path_join("world.json").utf8().get_data();
        return false;
    }
    info = parsed;
    node_id = int(Dictionary(info.get("source", Dictionary())).get("node", -1));
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
    // 河的水情与天气：weather.tsv（没有就常年一个样、常年晴）；年降水与一年几天也给河网定尺度
    RiverRules river_rules;
    Climate climate;
    const String weather_path = data_dir.path_join("weather.tsv");
    if (FileAccess::file_exists(weather_path)) {
        const PackedByteArray bytes = FileAccess::get_file_as_bytes(weather_path);
        const std::string text(reinterpret_cast<const char *>(bytes.ptr()), size_t(bytes.size()));
        if (RiverRegime::parse(text, regime, error)) {
            river_rules.precip_mm = regime.precip_mm();
            river_rules.year_days = regime.year_days();
        } else {
            ERR_PRINT(String("World: 读不了 ") + weather_path + "：" + String::utf8(error.c_str()));
            regime = RiverRegime::steady();
        }
        if (!parse_climate(text, climate, error)) {
            ERR_PRINT(String("World: weather.tsv 的天气读不了：") + String::utf8(error.c_str()));
            climate = Climate();
        }
    } else {
        regime = RiverRegime::steady();
    }
    terrain = WorldTerrain(grid, islands, uint64_t(terrain_seed), river_rules);
    layout = TileLayout(grid);
    // 天气模型：慢变量（积雪、冰、物候）按海拔分箱，箱从岛群最低的地到最高的峰
    double low = 1e18, high = -1e18;
    for (size_t k = 0; k < grid.height.size(); k++) {
        if (grid.shore[k] > 0.0f) {
            low = std::min(low, double(grid.height[k]));
            high = std::max(high, double(grid.height[k]));
        }
    }
    weather = climate.days.empty() || low > high ? WeatherModel() : WeatherModel(climate, low, high);
    // 行星计划 P3：云团在公共平面上取、按参考群的风漂（GroupStream 载入前设的；场景里摆的那一群就是参考群，单位变换）
    weather.set_cloud_frame(cloud_frame);
    if (cloud_ref) {
        weather.share_cloud_drift(*cloud_ref);
    }

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
    if (terrain.has_rivers()) {
        river_chunk_keys = river_chunks(terrain, river_mesh_rules);
    }
    return true;
}

void GroupWorld::set_runoff_override(double p_value) {
    runoff_override = p_value;
    if (loaded) {
        update_runoff();
    }
}

void GroupWorld::update_runoff() {
    if (runoff_override >= 0.0) {
        runoff = runoff_override;
    } else {
        const WorldClock *clock = WorldClock::get_singleton();
        runoff = regime.runoff(clock ? clock->get_calendar_day() : 0.0);
    }
    if (river_material.is_valid()) {
        river_material->set_shader_parameter("runoff", runoff);
    }
}

JobPool::Work GroupWorld::river_job(const RiverChunkKey &p_key) const {
    const WorldTerrain *t = &terrain;
    const RiverMeshRules rules = river_mesh_rules;
    return [t, rules, p_key]() -> std::unique_ptr<JobResult> {
        const RiverMesh m = build_river_mesh(*t, p_key, rules);
        std::unique_ptr<RiverArrays> out = std::make_unique<RiverArrays>();
        out->key = p_key;
        out->origin_x = m.origin_x;
        out->origin_z = m.origin_z;
        out->min_y = m.min_y;
        out->max_y = m.max_y;
        if (m.empty()) {
            return out;
        }
        out->vertices = vec3s(m.positions);
        out->normals = vec3s(m.normals);
        out->uvs.resize(int64_t(m.uvs.size() / 2));
        Vector2 *uv = out->uvs.ptrw();
        for (int64_t k = 0; k < out->uvs.size(); k++) {
            uv[k] = Vector2(m.uvs[size_t(k) * 2], m.uvs[size_t(k) * 2 + 1]);
        }
        out->custom0 = float32s(m.custom0);
        out->custom1 = float32s(m.custom1);
        out->indices = int32s(m.indices);
        return out;
    };
}

double GroupWorld::river_distance(const RiverChunkKey &p_key) const {
    const double chunk = river_mesh_rules.chunk_m;
    const double x0 = p_key.ix * chunk, z0 = p_key.iz * chunk;
    const double dx = std::max({ x0 - focus.x, 0.0, double(focus.x) - (x0 + chunk) });
    const double dz = std::max({ z0 - focus.z, 0.0, double(focus.z) - (z0 + chunk) });
    return std::hypot(dx, dz);
}

void GroupWorld::adopt_river(std::unique_ptr<RiverArrays> p_river) {
    const RiverArrays &m = *p_river;
    if (river_nodes.count(m.key.packed()) || river_distance(m.key) > river_radius * 1.25) {
        return; // 已经有了，或者焦点走远了
    }
    if (m.empty() || river_material.is_null()) {
        river_nodes[m.key.packed()] = nullptr;
        return;
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = m.vertices;
    arrays[Mesh::ARRAY_NORMAL] = m.normals;
    arrays[Mesh::ARRAY_TEX_UV] = m.uvs;
    arrays[Mesh::ARRAY_CUSTOM0] = m.custom0;
    arrays[Mesh::ARRAY_CUSTOM1] = m.custom1;
    arrays[Mesh::ARRAY_INDEX] = m.indices;
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
    river_nodes[m.key.packed()] = view;
}

void GroupWorld::stream_rivers(bool p_all) {
    if (river_chunk_keys.empty()) {
        return;
    }
    // 远了拆；排着的远了撤掉
    for (auto it = river_nodes.begin(); it != river_nodes.end();) {
        const RiverChunkKey key{ int(it->first >> 32), int(int32_t(uint32_t(it->first))) };
        if (river_distance(key) > river_radius * 1.25) {
            if (it->second) {
                it->second->queue_free();
            }
            it = river_nodes.erase(it);
        } else {
            ++it;
        }
    }
    pool->reprioritize(
            JobKind::RIVER, [this](uint64_t p_key) {
                const double d = river_distance(river_key_of(JobPool::id_of(p_key)));
                return d > river_radius * 1.25 ? -1.0 : d;
            },
            lane);
    // 缺的交给后台（近的先）；瞬移时 RIVER_SYNC_M 以内的当场做完挂上
    for (const RiverChunkKey &key : river_chunk_keys) {
        const double d = river_distance(key);
        if (d > river_radius || river_nodes.count(key.packed())) {
            continue;
        }
        const uint64_t job = job_key(JobKind::RIVER, river_id(key));
        if (!p_all || d > RIVER_SYNC_M) {
            pool->submit(job, d, river_job(key));
            continue;
        }
        std::unique_ptr<JobResult> done = pool->run_now(job);
        if (!done) {
            done = river_job(key)();
        }
        adopt_river(std::unique_ptr<RiverArrays>(static_cast<RiverArrays *>(done.release())));
    }
}

void GroupWorld::adopt_ready_rivers() {
    std::vector<JobPool::Done> done;
    pool->collect(JobKind::RIVER, done, lane);
    for (JobPool::Done &d : done) {
        river_ready.emplace_back(static_cast<RiverArrays *>(d.result.release()));
    }
    if (river_ready.empty()) {
        return;
    }
    std::sort(river_ready.begin(), river_ready.end(), [this](const auto &a, const auto &b) { return river_distance(a->key) < river_distance(b->key); });
    const uint64_t started = Time::get_singleton()->get_ticks_usec();
    size_t k = 0;
    for (; k < river_ready.size() && Time::get_singleton()->get_ticks_usec() - started < RIVER_BUDGET_USEC; k++) {
        adopt_river(std::move(river_ready[k]));
    }
    river_ready.erase(river_ready.begin(), river_ready.begin() + int64_t(k));
}

int GroupWorld::get_river_chunk_count() const {
    int n = 0;
    for (const auto &[key, node] : river_nodes) {
        n += node != nullptr;
    }
    return n;
}

Dictionary GroupWorld::get_weather_at(double p_x, double p_z, double p_y) const {
    if (!loaded) {
        return Dictionary();
    }
    double y = p_y;
    if (std::isnan(y)) {
        y = get_surface_height(p_x, p_z);
        y = std::isnan(y) ? 0.0 : y;
    }
    const WorldClock *clock = WorldClock::get_singleton();
    return weather_view::to_dictionary(weather.sample(clock ? clock->get_calendar_day() : 0.0, p_x, p_z, y));
}

double GroupWorld::get_snow_line() const {
    const WorldClock *clock = WorldClock::get_singleton();
    return loaded ? weather.snow_line(clock ? clock->get_calendar_day() : 0.0) : 1e9;
}

Dictionary GroupWorld::get_river_at(double p_x, double p_z) const {
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

Dictionary GroupWorld::find_river_near(double p_x, double p_z, double p_radius, double p_min_acc_km2) const {
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

JobPool::Work GroupWorld::tile_job(const TileKey &p_key) const {
    const WorldTerrain *t = &terrain;
    const TileLayout *l = &layout;
    return [t, l, p_key]() -> std::unique_ptr<JobResult> {
        const TileMesh m = build_tile_mesh(*t, *l, p_key);
        std::unique_ptr<TileArrays> out = std::make_unique<TileArrays>();
        out->key = p_key;
        out->origin_x = m.origin_x;
        out->origin_z = m.origin_z;
        if (m.empty()) {
            return out;
        }
        out->vertices = vec3s(m.positions);
        out->normals = vec3s(m.normals);
        out->colors.resize(int64_t(m.colors.size() / 4));
        Color *c = out->colors.ptrw();
        for (int64_t k = 0; k < out->colors.size(); k++) {
            c[k] = Color(m.colors[size_t(k) * 4], m.colors[size_t(k) * 4 + 1], m.colors[size_t(k) * 4 + 2], m.colors[size_t(k) * 4 + 3]);
        }
        out->season.resize(int64_t(m.season.size()));
        std::copy(m.season.begin(), m.season.end(), out->season.ptrw());
        out->indices = int32s(m.indices);
        // 碰撞按生成的先后切段（一行行生成的，相邻的三角面挨着：每段是一条带子）
        const int64_t faces = int64_t(m.collision.size() / 9);
        for (int64_t f0 = 0; f0 < faces; f0 += COLLISION_PART_FACES) {
            const int64_t n = std::min(COLLISION_PART_FACES, faces - f0);
            PackedVector3Array part;
            part.resize(n * 3);
            Vector3 *w = part.ptrw();
            const float *src = m.collision.data() + f0 * 9;
            for (int64_t k = 0; k < n * 3; k++) {
                w[k] = Vector3(src[k * 3], src[k * 3 + 1], src[k * 3 + 2]);
            }
            out->face_parts.push_back(std::move(part));
        }
        return out;
    };
}

bool GroupWorld::target_has(const TileKey &p_key) const {
    return std::binary_search(target.begin(), target.end(), p_key);
}

void GroupWorld::retarget() {
    targeted_focus = focus;
    has_target = true;
    target = layout.select(focus.x, focus.z, split);
    std::sort(target.begin(), target.end());
    // 还没开建的旧活全撤（正在建的建完再说：挂的时候不在目标里就丢），缺的按离焦点的远近重排；一样近时细的先建
    pool->cancel_kind(JobKind::TILE, lane);
    const double fx = focus.x, fz = focus.z;
    for (const TileKey &key : target) {
        if (tiles.find(key.packed()) == tiles.end()) {
            pool->submit(job_key(JobKind::TILE, key.packed()), tile_distance(layout, key, fx, fz) - 1e-3 * key.level, tile_job(key));
        }
    }
}

void GroupWorld::adopt(std::unique_ptr<TileArrays> p_tile) {
    const TileArrays &m = *p_tile;
    const TileKey key = m.key;
    if (!target_has(key) || tiles.find(key.packed()) != tiles.end()) {
        return;
    }
    Tile tile;
    tile.key = key;
    if (!m.empty()) {
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = m.vertices;
        arrays[Mesh::ARRAY_NORMAL] = m.normals;
        arrays[Mesh::ARRAY_COLOR] = m.colors;
        arrays[Mesh::ARRAY_CUSTOM0] = m.season;
        arrays[Mesh::ARRAY_INDEX] = m.indices;
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        const uint64_t format = uint64_t(Mesh::ARRAY_CUSTOM_RGBA8_UNORM) << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), BitField<Mesh::ArrayFormat>(format));
        mesh->surface_set_material(0, ground_material);

        Node3D *root = memnew(Node3D);
        root->set_name(vformat("Tile_%d_%d_%d", key.level, key.ix, key.iz));
        root->set_position(Vector3(real_t(m.origin_x), 0, real_t(m.origin_z)));
        MeshInstance3D *view = memnew(MeshInstance3D);
        view->set_mesh(mesh);
        root->add_child(view);
        tile.view = view;
        margins_dirty = true;
        if (!m.face_parts.empty()) {
            tile.body = memnew(StaticBody3D);
            // 邻群的地面块照样建碰撞体，只是撤出物理（DISABLE_MODE_REMOVE）：换锚成了锚群时不用重建
            tile.body->set_process_mode(physics_on ? PROCESS_MODE_INHERIT : PROCESS_MODE_DISABLED);
            root->add_child(tile.body);
            tile.parts_left = int(m.face_parts.size());
            for (const PackedVector3Array &faces : m.face_parts) {
                collision_ready.push_back({ key, faces });
            }
        }
        add_child(root);
        tile.node = root;
    }
    tiles[key.packed()] = tile;
}

void GroupWorld::mount_collision(const CollisionPart &p_part) {
    const auto found = tiles.find(p_part.key.packed());
    if (found == tiles.end() || !found->second.body) {
        return; // 块已经拆了
    }
    Ref<ConcavePolygonShape3D> shape;
    shape.instantiate();
    shape->set_faces(p_part.faces);
    CollisionShape3D *collider = memnew(CollisionShape3D);
    collider->set_shape(shape);
    found->second.body->add_child(collider);
    found->second.parts_left--;
}

void GroupWorld::adopt_ready_tiles() {
    std::vector<JobPool::Done> done;
    pool->collect(JobKind::TILE, done, lane);
    for (JobPool::Done &d : done) {
        tile_ready.emplace_back(static_cast<TileArrays *>(d.result.release()));
    }
    if (tile_ready.empty() && collision_ready.empty()) {
        return;
    }
    // 近的先挂；时间用完了剩下的下帧再挂
    const double fx = focus.x, fz = focus.z;
    std::sort(tile_ready.begin(), tile_ready.end(), [&](const std::unique_ptr<TileArrays> &a, const std::unique_ptr<TileArrays> &b) {
        return tile_distance(layout, a->key, fx, fz) < tile_distance(layout, b->key, fx, fz);
    });
    const uint64_t started = Time::get_singleton()->get_ticks_usec();
    const auto in_budget = [&]() { return Time::get_singleton()->get_ticks_usec() - started < ADOPT_BUDGET_USEC; };
    size_t k = 0;
    for (; k < tile_ready.size() && in_budget(); k++) {
        adopt(std::move(tile_ready[k]));
    }
    tile_ready.erase(tile_ready.begin(), tile_ready.begin() + int64_t(k));
    // 碰撞一段段挂：近的先
    if (collision_ready.empty() || !in_budget()) {
        return;
    }
    std::stable_sort(collision_ready.begin(), collision_ready.end(), [&](const CollisionPart &a, const CollisionPart &b) {
        return tile_distance(layout, a.key, fx, fz) < tile_distance(layout, b.key, fx, fz);
    });
    size_t c = 0;
    for (; c < collision_ready.size() && in_budget(); c++) {
        mount_collision(collision_ready[c]);
    }
    collision_ready.erase(collision_ready.begin(), collision_ready.begin() + int64_t(c));
}

void GroupWorld::prune() {
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

void GroupWorld::_process(double /* p_delta */) {
    if (loading) {
        // 后台载入：做完了在主线程装上材质、发 rebuilt（Farm、Shipyard 等着它）
        std::vector<JobPool::Done> done;
        pool->collect(JobKind::GROUP_LOAD, done, lane);
        if (done.empty()) {
            return;
        }
        loading = false;
        const GroupLoadDone *r = static_cast<const GroupLoadDone *>(done.front().result.get());
        if (!r || !r->ok) {
            ERR_PRINT(String("World: ") + (r ? String::utf8(r->error.c_str()) : String(U"后台载入没有结果")));
            set_process(false);
            return;
        }
        install();
        emit_signal("rebuilt");
        return;
    }
    if (!loaded) {
        return;
    }
    update_runoff();
    update_shading();
    {
        StreamTimer timer(this, StreamChannel::RIVERS);
        if (!river_targeted || Vector2(focus.x - river_focus.x, focus.z - river_focus.z).length() > RIVER_RETARGET_M) {
            river_focus = focus;
            river_targeted = true;
            stream_rivers(false);
        }
        adopt_ready_rivers();
    }
    StreamTimer timer(this, StreamChannel::TILES);
    if (!has_target || Vector2(focus.x - targeted_focus.x, focus.z - targeted_focus.z).length() > retarget_distance) {
        retarget();
    }
    adopt_ready_tiles();
    prune();
    update_cull_margins();
}

void GroupWorld::set_physics_on(bool p_on) {
    physics_on = p_on;
    const ProcessMode mode = physics_on ? PROCESS_MODE_INHERIT : PROCESS_MODE_DISABLED;
    for (auto &it : tiles) {
        if (it.second.body) {
            it.second.body->set_process_mode(mode);
        }
    }
    // 船坞（主码头旁的坪、坞墩、门架）也是碰撞体：邻群的撤出物理
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        if (Shipyard *yard = Object::cast_to<Shipyard>(children[k])) {
            yard->set_process_mode(mode);
        }
    }
}

void GroupWorld::update_cull_margins() {
    const PlanetView *pv = PlanetView::active_view();
    if (!pv) {
        if (margins_on) {
            for (auto &it : tiles) {
                Tile &t = it.second;
                if (t.view) {
                    t.view->set_extra_cull_margin(0.0f);
                    t.view->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
                    t.view->set_visible(true);
                    t.far_mode = false;
                    t.shown = true;
                }
            }
            margins_on = false;
        }
        return;
    }
    // 相机在这一群的局部坐标里（锚群是原点、不转；邻群有 GroupStream 摆的刚体变换）
    const Transform3D xf = get_global_transform();
    const PlanetVec cg = pv->camera_flat();
    const Vector3 cam = xf.affine_inverse().xform(Vector3(real_t(cg.x), real_t(cg.y), real_t(cg.z)));
    const PlanetVec c(cam.x, cam.y, cam.z);
    const bool moved = !margins_on || Vector2(cam.x - margin_cam.x, cam.z - margin_cam.z).length() > MARGIN_RETARGET_M;
    if (moved || margins_dirty) {
        margins_on = true;
        margins_dirty = false;
        margin_cam = cam;
        for (auto &it : tiles) {
            Tile &t = it.second;
            if (!t.view) {
                continue;
            }
            double x = 0.0, z = 0.0;
            layout.corner(t.key, x, z);
            const double s = layout.size(t.key.level);
            const double dx = std::max(std::abs(x - c.x), std::abs(x + s - c.x)), dz = std::max(std::abs(z - c.z), std::abs(z + s - c.z));
            const double nx = std::max({ x - c.x, 0.0, c.x - (x + s) }), nz = std::max({ z - c.z, 0.0, c.z - (z + s) });
            // 离相机最远的角：下弯 d²/2R；另加相机还会挪的那一段（MARGIN_RETARGET_M 内）与水平的细微收缩
            const double far = std::hypot(dx, dz) + MARGIN_RETARGET_M;
            const double near = std::max(0.0, std::hypot(nx, nz) - MARGIN_RETARGET_M);
            const bool far_mode = near > FAR_MODE_M;
            if (far_mode) {
                // 远块：着色器把它沿视线压到 26 km 以内（CurveSqueeze），按平面位置的包围盒会被远平面剪掉、侧面放宽了又白画——
                // 引擎不剔（余量放到极大），每帧按视锥的四个侧面自己剔（侧面过相机，压近不改方向）；也不投影（阴影只画 500 m 内，压过的位置投出来是错的）
                const AABB box = t.view->get_aabb();
                t.sphere_center = t.node->get_position() + box.get_center(); // 局部坐标：剔的时候换到全局（邻群的变换每帧会动）
                t.sphere_radius = 0.5 * box.size.length() + pv->drop_at(far) + MARGIN_SLACK_M;
                if (!t.far_mode) {
                    t.view->set_extra_cull_margin(FAR_MODE_MARGIN_M);
                    t.view->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
                }
            } else {
                t.view->set_extra_cull_margin(real_t(pv->drop_at(far) + (far - CurveSqueeze::distance(far)) + MARGIN_SLACK_M));
                if (t.far_mode) {
                    t.view->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_ON);
                }
                if (!t.shown) {
                    t.view->set_visible(true);
                    t.shown = true;
                }
            }
            t.far_mode = far_mode;
        }
    }
    // 远块按视锥的四个侧面自己剔
    const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
    if (!camera) {
        return;
    }
    const TypedArray<Plane> planes = camera->get_frustum(); // 近、远、左、上、右、下（法线朝外）
    if (planes.size() < 6) {
        return;
    }
    Plane sides[4];
    for (int k = 0; k < 4; k++) {
        sides[k] = planes[k + 2];
    }
    for (auto &it : tiles) {
        Tile &t = it.second;
        if (!t.view || !t.far_mode) {
            continue;
        }
        const Vector3 center = xf.xform(t.sphere_center);
        bool inside = true;
        for (const Plane &p : sides) {
            if (p.distance_to(center) > t.sphere_radius) {
                inside = false;
                break;
            }
        }
        if (inside != t.shown) {
            t.view->set_visible(inside);
            t.shown = inside;
        }
    }
}

void GroupWorld::prepare_at(const Vector3 &p_point, double p_radius) {
    if (!loaded) {
        return;
    }
    focus = p_point;
    retarget();
    river_focus = focus;
    river_targeted = true;
    stream_rivers(true);
    // 落点周围的叶块：做好了没挂的先挂，排着的当场做
    std::vector<JobPool::Done> done;
    pool->collect(JobKind::TILE, done, lane);
    for (JobPool::Done &d : done) {
        tile_ready.emplace_back(static_cast<TileArrays *>(d.result.release()));
    }
    for (const TileKey &key : target) {
        if (key.level != layout.get_max_level() || tiles.find(key.packed()) != tiles.end() || tile_distance(layout, key, p_point.x, p_point.z) > p_radius) {
            continue;
        }
        auto ready = std::find_if(tile_ready.begin(), tile_ready.end(), [&](const std::unique_ptr<TileArrays> &t) { return t->key == key; });
        if (ready != tile_ready.end()) {
            std::unique_ptr<TileArrays> tile = std::move(*ready);
            tile_ready.erase(ready);
            adopt(std::move(tile));
            continue;
        }
        std::unique_ptr<JobResult> result = pool->run_now(job_key(JobKind::TILE, key.packed()));
        if (!result) {
            result = tile_job(key)();
        }
        adopt(std::unique_ptr<TileArrays>(static_cast<TileArrays *>(result.release())));
    }
    for (auto it = collision_ready.begin(); it != collision_ready.end();) {
        if (tile_distance(layout, it->key, p_point.x, p_point.z) <= p_radius) {
            mount_collision(*it);
            it = collision_ready.erase(it);
        } else {
            ++it;
        }
    }
}

bool GroupWorld::is_ready_at(const Vector3 &p_point) const {
    if (!loaded) {
        return false;
    }
    const auto found = tiles.find(layout.leaf_at(p_point.x, p_point.z).packed());
    return found != tiles.end() && found->second.parts_left == 0;
}

double GroupWorld::get_surface_height(double p_x, double p_z) const {
    double y = 0.0;
    if (!loaded || !terrain.surface(p_x, p_z, y)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return y;
}

int GroupWorld::get_island_at(double p_x, double p_z) const {
    return loaded ? int(grid.island[grid.nearest_cell(p_x, p_z)]) : -1;
}

Dictionary GroupWorld::get_cover_at(double p_x, double p_z) const {
    Dictionary out;
    if (!loaded) {
        return out;
    }
    const CoverMix m = terrain.cover(p_x, p_z);
    PackedFloat32Array weights;
    weights.resize(COVER_CLASSES);
    for (int k = 0; k < COVER_CLASSES; k++) {
        weights.set(k, m.w[k]);
    }
    const int dominant = m.dominant();
    const Array classes = Dictionary(info.get("landcover", Dictionary())).get("classes", Array());
    out["dominant"] = dominant;
    out["name"] = dominant < classes.size() ? String(classes[dominant]) : String();
    out["weights"] = weights;
    return out;
}

Dictionary GroupWorld::get_stream_frame(int64_t p_frame) const {
    Dictionary out;
    const StreamMeter::Frame *f = meter.frame(uint64_t(p_frame));
    if (!f) {
        return out;
    }
    for (int c = 0; c < STREAM_CHANNELS; c++) {
        if (f->usec[size_t(c)] > 0) {
            out[stream_channel_key(StreamChannel(c))] = f->usec[size_t(c)] / 1000.0;
        }
    }
    return out;
}

Dictionary GroupWorld::get_stream_totals() const {
    Dictionary out;
    for (int c = 0; c < STREAM_CHANNELS; c++) {
        const StreamMeter::Totals &t = meter.get_totals(StreamChannel(c));
        Dictionary row;
        row["total_ms"] = double(t.usec) / 1000.0;
        row["max_ms"] = t.max_frame_usec / 1000.0;
        row["frames"] = int64_t(t.frames);
        out[stream_channel_key(StreamChannel(c))] = row;
    }
    return out;
}

StreamTimer::StreamTimer(GroupWorld *p_world, StreamChannel p_channel) :
        meter(p_world ? &p_world->get_meter() : nullptr), channel(p_channel), started(Time::get_singleton()->get_ticks_usec()) {}

StreamTimer::~StreamTimer() {
    if (meter) {
        meter->add(Engine::get_singleton()->get_process_frames(), channel, Time::get_singleton()->get_ticks_usec() - started);
    }
}

int GroupWorld::get_pending_count() const {
    int missing = 0;
    for (const TileKey &key : target) {
        const auto found = tiles.find(key.packed());
        missing += found == tiles.end() || found->second.parts_left > 0 ? 1 : 0; // 没挂上的，或碰撞还没挂齐的
    }
    return missing;
}

int GroupWorld::find_dock(int p_id) const {
    for (size_t k = 0; k < sites.docks.size(); k++) {
        if (sites.docks[k].id == p_id) {
            return int(k);
        }
    }
    return -1;
}

int GroupWorld::get_dock_id(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(sites.docks.size()), -1);
    return sites.docks[size_t(p_index)].id;
}

bool GroupWorld::is_dock_berthable(int p_index) const {
    return p_index >= 0 && p_index < int(works.docks.size()) && works.docks[size_t(p_index)].ok;
}

Transform3D GroupWorld::get_dock_berth(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(works.docks.size()), Transform3D());
    return dock_builder::berth_contact(works.docks[size_t(p_index)]);
}

Transform3D GroupWorld::get_dock_spawn(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(works.docks.size()), Transform3D());
    Transform3D xf = dock_builder::spawn(works.docks[size_t(p_index)]);
    double y = 0.0;
    if (terrain.surface(xf.origin.x, xf.origin.z, y)) {
        xf.origin.y = real_t(y);
    }
    return xf;
}

Vector3 GroupWorld::get_dock_position(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(works.docks.size()), Vector3());
    return dock_builder::point(works.docks[size_t(p_index)], 0.0, 0.0);
}

std::unique_ptr<MapGround> GroupWorld::host_ground() const {
    return std::make_unique<WorldGround>(terrain, 0.0, 0.0);
}

Farm *GroupWorld::host_farm() const {
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        if (Farm *farm = Object::cast_to<Farm>(children[k])) {
            return farm;
        }
    }
    return nullptr;
}

YardPlan GroupWorld::host_yard() const {
    for (const YardPlan &yard : works.yards) {
        if (yard.present) {
            return yard;
        }
    }
    return YardPlan();
}

int GroupWorld::map_island_at(const Vector3 &p_global) const {
    return std::isnan(get_surface_height(p_global.x, p_global.z)) ? -1 : get_island_at(p_global.x, p_global.z);
}

bool GroupWorld::map_footing(const Vector3 &p_global) const {
    return is_ready_at(p_global) && !std::isnan(get_surface_height(p_global.x, p_global.z));
}

bool GroupWorld::map_dock_open(int p_index) const {
    return is_dock_berthable(p_index) && site_stream && site_stream->get_dock(get_dock_id(p_index));
}

int GroupWorld::map_dock_island(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(sites.docks.size()), -1);
    return sites.docks[size_t(p_index)].island;
}

String GroupWorld::map_dock_label(int p_index) const {
    ERR_FAIL_INDEX_V(p_index, int(sites.docks.size()), String());
    const SiteDock &dock = sites.docks[size_t(p_index)];
    return vformat(String(U"岛 #%d 码头 #%d"), dock.island, dock.id);
}

std::vector<Shipyard *> GroupWorld::map_shipyards() const {
    std::vector<Shipyard *> out;
    const TypedArray<Node> children = get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        if (Shipyard *yard = Object::cast_to<Shipyard>(children[k])) {
            out.push_back(yard);
        }
    }
    return out;
}

FreightPost GroupWorld::map_freight_post_near(const Vector3 &p_global, double p_range) {
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

bool GroupWorld::map_freight_target(int p_port, Transform3D &r_target) const {
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

ResourceStock *GroupWorld::resource_stock(int p_index) {
    return p_index >= 0 && p_index < int(resource_stocks.size()) ? &resource_stocks[size_t(p_index)] : nullptr;
}

double GroupWorld::clock_hours() {
    const WorldClock *clock = WorldClock::get_singleton();
    return clock ? clock->get_total_hours() : 0.0;
}

ResourceSpot GroupWorld::map_resource_near(const Vector3 &p_global, double p_range) {
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

ResourceSpot GroupWorld::map_gather_near(const Vector3 &p_global, double p_face_range, double p_facing_yaw) {
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

void GroupWorld::map_resource_taken(const ResourceSpot &p_spot) {
    if (!p_spot.scatter) {
        return;
    }
    if (scatter_ledger.settle(p_spot.scatter_key, p_spot.cap, p_spot.refill_hours, clock_hours()) && scatter_stream) {
        scatter_stream->refresh_chunk(scatter_key_chunk(p_spot.scatter_key)); // 倒了：换成树桩、拆掉树干的碰撞
    }
}

bool GroupWorld::map_water_at(double p_x, double p_z) const {
    return WorldLandingGround(terrain).water(p_x, p_z);
}

Dictionary GroupWorld::get_resource_site(int p_working_id) const {
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

Dictionary GroupWorld::get_landmark(int p_id) const {
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

int GroupWorld::find_landmark_near(double p_x, double p_z, double p_radius, const String &p_kind) const {
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

Dictionary GroupWorld::find_scatter_near(double p_x, double p_z, double p_radius, const String &p_kind) const {
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

int GroupWorld::get_scatter_count_near(double p_x, double p_z, double p_radius, bool p_solid_only) const {
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

PackedInt32Array GroupWorld::get_resource_ids() const {
    PackedInt32Array ids;
    for (const ResourceSite &s : resource_plan.sites) {
        ids.push_back(s.id);
    }
    return ids;
}

int GroupWorld::find_resource_near(double p_x, double p_z, double p_radius, const String &p_kind) const {
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

Vector3 GroupWorld::get_freight_post(int p_village_id) const {
    WorldVillage *village = site_stream ? site_stream->get_village(p_village_id) : nullptr;
    Vector3 at;
    return village && village->get_freight_post(at) ? at : Vector3();
}

PackedInt32Array GroupWorld::get_freight_port_ids() const {
    PackedInt32Array ids;
    for (const FreightPort &p : freight_ports) {
        ids.push_back(p.site_id);
    }
    return ids;
}

GroundHost *GroupWorld::map_settlement_at(const Vector3 &p_global) const {
    return site_stream ? site_stream->village_at(p_global) : nullptr;
}

void GroupWorld::map_focus(const Vector3 &p_global) {
    set_focus(p_global);
    if (site_stream) {
        site_stream->set_focus(p_global);
    }
    if (scatter_stream) {
        scatter_stream->set_focus(p_global);
    }
}

void GroupWorld::map_prepare(const Vector3 &p_global) {
    prepare_at(p_global, PREPARE_RADIUS_M); // 脚下与周围一圈的叶块（带碰撞）当场建好，更远的照常排队
    if (site_stream) {
        site_stream->ensure_near(p_global); // 码头的栈桥、落点跟前的村子（细模带碰撞），人落地前就在
    }
    if (scatter_stream) {
        scatter_stream->ensure_near(p_global); // 近处的树与树干的碰撞（村子先铺：压着房子的要藏）
    }
}

int GroupWorld::map_pending() const {
    return get_pending_count();
}

String GroupWorld::map_location_text(const Vector3 &p_global) const {
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

WorldMap *GroupWorld::map_make_panel() {
    WorldMap *panel = memnew(WorldMap);
    map_fill_panel(panel);
    return panel;
}

void GroupWorld::map_fill_panel(WorldMap *panel) const {
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
}

void GroupWorld::map_refresh_panel(WorldMap *p_panel) const {
    if (site_stream) {
        p_panel->set_built_sites(site_stream->get_built_docks(), site_stream->get_built_villages());
        p_panel->set_built_resources(site_stream->get_built_resources());
    }
}
