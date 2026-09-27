#include "scatter_stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "buildings.h"
#include "ground_works.h"
#include "scatter_meshes.h"
#include "settlement.h"
#include "site_stream.h"
#include "world.h"
#include "world_village.h"

using namespace godot;

namespace {

constexpr double TRUNK_HEIGHT_M = 6.0; // 碰撞圆柱多高（大小 1 倍）：高过开局的船的舱底
constexpr double HOUSE_MARGIN_TREE_M = 3.0; // 铺出来的房子外这么远不留树（树冠）
constexpr double HOUSE_MARGIN_M = 1.2; // 别的
constexpr double VILLAGE_REACH_M = 30.0; // 离村子外接圆这么远以内的才查房子
constexpr double IN_VIEW_COS = 0.7071; // 挑够得着的那一株：面前 ±45° 以内的算「看着的」，按偏离视线的角度挑
constexpr double UNDERFOOT_M = 0.6; // 中心离人这么近的（人站在它里头）当它偏 17°
constexpr double LOD_BAND_M = 20.0; // 细模与低模在 NEAR_LOD_M 上下这么宽里交接
constexpr double FAR_BAND_M = 40.0; // 低模与远处的林子在 FAR_LOD_M 上下这么宽里交接
constexpr double FAR_FADE_M = 500.0; // 远处的林子在 far_radius 以内这么宽里淡掉
constexpr double FAR_PRIORITY_M = 1000.0; // 远处的林子排在近处的地面、散布、村子后面
constexpr int FLOATS = 16; // 每株的 MultiMesh 数据：变换 3×4 + 实例色

// 各样东西的淡入淡出（米）：(淡入起, 淡入止, 淡出起, 淡出止)；淡入起为负 = 一开始就在
struct Fade {
    float in0 = -2.0f, in1 = -1.0f, out0 = 1e9f, out1 = 2e9f;
};

Fade fade_out(double p_at, double p_band) {
    Fade f;
    f.out0 = float(p_at - p_band);
    f.out1 = float(p_at + p_band);
    return f;
}

// 立着的样子淡到哪（树的细模淡给低模，见 build_visuals）
Fade look_fade(ScatterLook p_look, bool p_felled) {
    switch (p_look) {
        case ScatterLook::BROADLEAF:
        case ScatterLook::CONIFER:
            return p_felled ? fade_out(230.0, 30.0) : fade_out(ScatterStream::NEAR_LOD_M, LOD_BAND_M);
        case ScatterLook::SHRUB:
            return fade_out(415.0, 35.0);
        case ScatterLook::BOULDER:
        case ScatterLook::FLOATSTONE:
        case ScatterLook::ORE_ROCK:
            return fade_out(465.0, 35.0);
        default:
            return fade_out(270.0, 30.0);
    }
}

// 时令（着色器的实例参数 season）：阔叶里落叶树的份额、草本的枯黄、挂雪、随风摆
Vector4 look_season(ScatterLook p_look, bool p_felled) {
    if (p_felled) {
        return Vector4(0.0f, 0.0f, 1.0f, 0.0f);
    }
    switch (p_look) {
        case ScatterLook::BROADLEAF:
            return Vector4(0.8f, 0.0f, 0.7f, 1.0f);
        case ScatterLook::CONIFER:
            return Vector4(0.0f, 0.0f, 0.85f, 0.6f);
        case ScatterLook::SHRUB:
            return Vector4(0.0f, 0.6f, 0.6f, 0.8f);
        case ScatterLook::REEDS:
            return Vector4(0.0f, 1.0f, 0.3f, 1.4f);
        default:
            return Vector4(0.0f, 0.0f, 1.0f, 0.0f);
    }
}

void set_fade(GeometryInstance3D *p_view, const Fade &p_fade, bool p_can_fade, const Vector4 &p_season = Vector4(0.0f, 0.0f, 1.0f, 0.0f)) {
    if (p_can_fade) {
        p_view->set_instance_shader_parameter("season", p_season);
        p_view->set_instance_shader_parameter("fade", Vector4(p_fade.in0, p_fade.in1, p_fade.out0, p_fade.out1));
    } else {
        // 着色器没有：退回按块的可见距离
        if (p_fade.in0 > 0.0f) {
            p_view->set_visibility_range_begin(0.5f * (p_fade.in0 + p_fade.in1));
        }
        if (p_fade.out1 < 1e8f) {
            p_view->set_visibility_range_end(0.5f * (p_fade.out0 + p_fade.out1));
        }
    }
}

int item_cap(const ScatterItem &p_item) {
    return deposits::scatter_yield(p_item.kind(), p_item.scale);
}

// 一株的 MultiMesh 数据：绕 y 转 yaw、横向放大 p_scale_xz、竖向 p_scale_y，平移到（相对 p_x0, p_z0）；实例色
void put_instance(float *r_out, const ScatterItem &p_item, double p_x0, double p_z0, float p_scale_xz, float p_scale_y, const Color &p_color) {
    const float c = std::cos(p_item.yaw), s = std::sin(p_item.yaw);
    // Basis(Y, yaw) 按行：(c, 0, s)、(0, 1, 0)、(−s, 0, c)，各列乘上放大
    const float rows[12] = {
        c * p_scale_xz, 0.0f, s * p_scale_xz, float(p_item.x - p_x0),
        0.0f, p_scale_y, 0.0f, p_item.y,
        -s * p_scale_xz, 0.0f, c * p_scale_xz, float(p_item.z - p_z0),
    };
    std::memcpy(r_out, rows, sizeof(rows));
    r_out[12] = p_color.r;
    r_out[13] = p_color.g;
    r_out[14] = p_color.b;
    r_out[15] = p_color.a;
}

MultiMeshInstance3D *make_view(const String &p_name, const Ref<ArrayMesh> &p_mesh, const PackedFloat32Array &p_buffer, int p_count, bool p_shadow) {
    Ref<MultiMesh> mm;
    mm.instantiate();
    mm->set_transform_format(MultiMesh::TRANSFORM_3D);
    mm->set_use_colors(true);
    mm->set_mesh(p_mesh);
    mm->set_instance_count(p_count);
    mm->set_buffer(p_buffer);
    MultiMeshInstance3D *view = memnew(MultiMeshInstance3D);
    view->set_name(p_name);
    view->set_multimesh(mm);
    view->set_cast_shadows_setting(p_shadow ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    return view;
}

} // namespace

Ref<CylinderShape3D> ScatterStream::trunk_shape(double p_radius) {
    const int bucket = int(std::lround(p_radius * 20.0)); // 5 cm 一档
    Ref<CylinderShape3D> &shape = trunk_shapes[bucket];
    if (shape.is_null()) {
        shape.instantiate();
        shape->set_radius(real_t(std::max(1, bucket) / 20.0));
        shape->set_height(real_t(TRUNK_HEIGHT_M));
    }
    return shape;
}

void ScatterStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_focus", "focus"), &ScatterStream::set_focus);
    ClassDB::bind_method(D_METHOD("ensure_near", "point"), &ScatterStream::ensure_near);
    ClassDB::bind_method(D_METHOD("get_chunk_count"), &ScatterStream::get_chunk_count);
    ClassDB::bind_method(D_METHOD("get_far_chunk_count"), &ScatterStream::get_far_chunk_count);
    ClassDB::bind_method(D_METHOD("get_far_tree_count"), &ScatterStream::get_far_tree_count);
    ClassDB::bind_method(D_METHOD("get_item_count"), &ScatterStream::get_item_count);
    ClassDB::bind_method(D_METHOD("get_solid_count"), &ScatterStream::get_solid_count);
    ClassDB::bind_method(D_METHOD("find_item_near", "point", "range", "kind"), &ScatterStream::find_item_near, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("is_standing", "key"), &ScatterStream::is_standing);
    ClassDB::bind_method(D_METHOD("path_clear", "from", "to", "clearance"), &ScatterStream::path_clear);
    ClassDB::bind_method(D_METHOD("set_world_path", "value"), &ScatterStream::set_world_path);
    ClassDB::bind_method(D_METHOD("get_world_path"), &ScatterStream::get_world_path);
    ClassDB::bind_method(D_METHOD("set_radius", "value"), &ScatterStream::set_radius);
    ClassDB::bind_method(D_METHOD("get_radius"), &ScatterStream::get_radius);
    ClassDB::bind_method(D_METHOD("set_collision_radius", "value"), &ScatterStream::set_collision_radius);
    ClassDB::bind_method(D_METHOD("get_collision_radius"), &ScatterStream::get_collision_radius);
    ClassDB::bind_method(D_METHOD("set_far_radius", "value"), &ScatterStream::set_far_radius);
    ClassDB::bind_method(D_METHOD("get_far_radius"), &ScatterStream::get_far_radius);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "World"), "set_world_path", "get_world_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "100,5000,10,suffix:m"), "set_radius", "get_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_radius", PROPERTY_HINT_RANGE, "20,1000,5,suffix:m"), "set_collision_radius", "get_collision_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far_radius", PROPERTY_HINT_RANGE, "0,20000,100,suffix:m"), "set_far_radius", "get_far_radius");
}

void ScatterStream::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    world = Object::cast_to<GroupWorld>(get_node_or_null(world_path));
    ERR_FAIL_NULL_MSG(world, "ScatterStream 找不到 World（world_path）");
    world_id = world->get_instance_id();
    world->track_shading(meshes.material()); // 这一群的海拔表、极向、平面坐标（行星计划 P3：每群一份）
}

void ScatterStream::_notification(int p_what) {
    if (p_what != NOTIFICATION_EXIT_TREE) {
        return;
    }
    // 拆了：自己在共用池里排着的活撤掉、做完没取的丢掉（见 SiteStream::_notification）
    GroupWorld *w = Object::cast_to<GroupWorld>(ObjectDB::get_instance(world_id));
    if (!w || !w->share_pool()) {
        return;
    }
    JobPool &pool = w->get_pool();
    for (JobKind kind : { JobKind::SCATTER, JobKind::FAR_TREES }) {
        pool.cancel_kind(kind, w->get_lane());
        std::vector<JobPool::Done> done;
        pool.collect(kind, done, w->get_lane());
    }
}

void ScatterStream::set_focus(const Vector3 &p_focus) {
    focus = p_focus;
    has_focus = true;
}

double ScatterStream::chunk_distance(const ScatterChunkKey &p_key, const Vector3 &p_point) const {
    const double size = world->get_scatter().get_rules().chunk_m;
    double x0 = 0.0, z0 = 0.0;
    world->get_scatter().chunk_corner(p_key, x0, z0);
    const double dx = std::max({ x0 - p_point.x, 0.0, double(p_point.x) - (x0 + size) });
    const double dz = std::max({ z0 - p_point.z, 0.0, double(p_point.z) - (z0 + size) });
    return std::hypot(dx, dz);
}

double ScatterStream::far_distance(int p_ix, int p_iz, double &r_farthest) const {
    const double x0 = p_ix * FAR_CHUNK_M, z0 = p_iz * FAR_CHUNK_M, x1 = x0 + FAR_CHUNK_M, z1 = z0 + FAR_CHUNK_M;
    const double dx = std::max({ x0 - focus.x, 0.0, double(focus.x) - x1 }), dz = std::max({ z0 - focus.z, 0.0, double(focus.z) - z1 });
    r_farthest = std::hypot(std::max(std::abs(x0 - focus.x), std::abs(x1 - focus.x)), std::max(std::abs(z0 - focus.z), std::abs(z1 - focus.z)));
    return std::hypot(dx, dz);
}

void ScatterStream::update_eye() {
    Viewport *viewport = get_viewport();
    Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    meshes.set_eye(camera ? camera->get_global_position() : focus);
}

void ScatterStream::_process(double p_delta) {
    if (!world || !world->is_loaded() || !world->get_scatter().valid() || !has_focus) {
        return;
    }
    update_eye();
    // 玩家营造的账变了（改地形、搭建件、摆件）：当场重查压着的（不等下一回 refresh）
    if (world->get_works_revision() != works_revision) {
        StreamTimer timer(world, StreamChannel::SCATTER_FILTER);
        works_revision = world->get_works_revision();
        refilter_works();
    }
    since_check += p_delta;
    since_regrow += p_delta;
    if (since_check >= check_interval) {
        since_check = 0.0;
        refresh(false);
    }
    // 生成好的块：近的先挂，时间用完了下帧再挂；远处的林子排在后面
    std::vector<JobPool::Done> done;
    world->get_pool().collect(JobKind::SCATTER, done, world->get_lane());
    for (JobPool::Done &d : done) {
        waiting.emplace_back(static_cast<ScatterChunkJob *>(d.result.release()));
    }
    done.clear();
    world->get_pool().collect(JobKind::FAR_TREES, done, world->get_lane());
    for (JobPool::Done &d : done) {
        far_waiting.emplace_back(static_cast<FarTreesJob *>(d.result.release()));
    }
    if (!waiting.empty() || !far_waiting.empty()) {
        StreamTimer timer(world, StreamChannel::SCATTER);
        std::sort(waiting.begin(), waiting.end(), [&](const auto &a, const auto &b) { return chunk_distance(a->key, focus) < chunk_distance(b->key, focus); });
        const uint64_t started = Time::get_singleton()->get_ticks_usec();
        const auto in_budget = [&]() { return Time::get_singleton()->get_ticks_usec() - started < uint64_t(adopt_budget_usec); };
        size_t k = 0;
        for (; k < waiting.size() && in_budget(); k++) {
            if (chunk_distance(waiting[k]->key, focus) <= radius * hysteresis) {
                adopt(std::move(waiting[k]));
            }
        }
        waiting.erase(waiting.begin(), waiting.begin() + int64_t(k));
        size_t f = 0;
        for (; f < far_waiting.size() && in_budget(); f++) {
            adopt_far(std::move(far_waiting[f]));
        }
        far_waiting.erase(far_waiting.begin(), far_waiting.begin() + int64_t(f));
    }
    // 倒着的长回来没有
    if (since_regrow >= regrow_interval) {
        StreamTimer timer(world, StreamChannel::SCATTER_REGROW);
        since_regrow = 0.0;
        const ScatterLedger &ledger = world->get_scatter_ledger();
        for (auto &[packed, chunk] : chunks) {
            if (!ledger.any_felled_in(chunk.key) && std::all_of(chunk.standing.begin(), chunk.standing.end(), [](char s) { return s != 0; })) {
                continue;
            }
            const std::vector<char> before = chunk.standing;
            snapshot_standing(chunk);
            if (chunk.standing != before) {
                build_visuals(chunk);
                if (chunk.body) {
                    build_body(chunk);
                }
            }
        }
    }
}

JobPool::Work ScatterStream::chunk_job(const ScatterChunkKey &p_key) const {
    const ResourceScatter *source = &world->get_scatter();
    return [source, p_key]() -> std::unique_ptr<JobResult> {
        std::unique_ptr<ScatterChunkJob> out = std::make_unique<ScatterChunkJob>();
        out->key = p_key;
        out->items = source->chunk(p_key);
        double x0 = 0.0, z0 = 0.0;
        source->chunk_corner(p_key, x0, z0);
        out->instances.resize(out->items.size() * FLOATS);
        for (size_t k = 0; k < out->items.size(); k++) {
            const ScatterItem &item = out->items[k];
            put_instance(out->instances.data() + k * FLOATS, item, x0, z0, item.scale, item.scale, scatter_meshes::instance_color(item.kind(), item.sub, scatter_key(p_key, item.index)));
        }
        return out;
    };
}

// 村子铺了 / 拆了：只重查挨着这几处的块（铺出来的比估的大，压着房子的藏掉）
void ScatterStream::refilter_villages() {
    SiteStream *sites = world->get_site_stream();
    std::vector<VillageMark> now;
    if (sites) {
        for (WorldVillage *village : sites->built_villages()) {
            const SiteSettlement &site = village->get_site();
            now.push_back({ village, site.x, site.z, village->get_radius() + VILLAGE_REACH_M });
        }
    }
    std::vector<VillageMark> changed;
    const auto has = [](const std::vector<VillageMark> &p_list, const WorldVillage *p_village) {
        return std::any_of(p_list.begin(), p_list.end(), [&](const VillageMark &m) { return m.village == p_village; });
    };
    for (const VillageMark &m : now) {
        if (!has(filtered, m.village)) {
            changed.push_back(m);
        }
    }
    for (const VillageMark &m : filtered) {
        if (!has(now, m.village)) {
            changed.push_back(m);
        }
    }
    filtered = now;
    if (changed.empty()) {
        return;
    }
    const double size = world->get_scatter().get_rules().chunk_m;
    for (auto &[packed, chunk] : chunks) {
        double x0 = 0.0, z0 = 0.0;
        world->get_scatter().chunk_corner(chunk.key, x0, z0);
        const bool near = std::any_of(changed.begin(), changed.end(), [&](const VillageMark &m) {
            const double dx = std::max({ x0 - m.x, 0.0, m.x - (x0 + size) }), dz = std::max({ z0 - m.z, 0.0, m.z - (z0 + size) });
            return std::hypot(dx, dz) <= m.reach;
        });
        if (!near) {
            continue;
        }
        const std::vector<char> before = chunk.hidden;
        filter_houses(chunk);
        filter_works(chunk);
        if (chunk.hidden != before) {
            build_visuals(chunk);
            if (chunk.body) {
                build_body(chunk);
            }
        }
    }
}

void ScatterStream::refresh(bool p_all) {
    const ResourceScatter &scatter = world->get_scatter();
    // 远了拆；碰撞近了加、远了拆
    {
        StreamTimer body_timer(world, StreamChannel::SCATTER_BODY);
        for (auto it = chunks.begin(); it != chunks.end();) {
            Chunk &chunk = it->second;
            const double d = chunk_distance(chunk.key, focus);
            if (d > radius * hysteresis) {
                drop(chunk);
                it = chunks.erase(it);
                continue;
            }
            if (!chunk.body && d <= collision_radius) {
                build_body(chunk);
            } else if (chunk.body && d > collision_radius * hysteresis) {
                chunk.body->queue_free();
                chunk.body = nullptr;
            }
            ++it;
        }
    }
    // 村子铺了新的：重查压着房子的
    SiteStream *sites = world->get_site_stream();
    const int64_t version = sites ? sites->get_village_version() : 0;
    if (version != village_version) {
        StreamTimer timer(world, StreamChannel::SCATTER_FILTER);
        village_version = version;
        refilter_villages();
    }
    // 缺的交给后台生成（近的先）
    const ScatterChunkKey lo = scatter.chunk_at(focus.x - radius, focus.z - radius), hi = scatter.chunk_at(focus.x + radius, focus.z + radius);
    std::vector<std::pair<double, ScatterChunkKey>> wanted;
    for (int iz = lo.iz; iz <= hi.iz; iz++) {
        for (int ix = lo.ix; ix <= hi.ix; ix++) {
            const ScatterChunkKey key{ ix, iz };
            const double d = chunk_distance(key, focus);
            if (d > radius || chunks.count(key.packed())) {
                continue;
            }
            const bool queued = std::any_of(waiting.begin(), waiting.end(), [&](const auto &w) { return w->key == key; });
            if (!queued) {
                wanted.emplace_back(d, key);
            }
        }
    }
    std::sort(wanted.begin(), wanted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    // 还没开工的旧活全撤，按新的远近重排（正在生成的做完照样交回来，挂不挂由距离定）；瞬移时碰撞半径内的同步生成
    JobPool &pool = world->get_pool();
    pool.cancel_kind(JobKind::SCATTER, world->get_lane());
    for (const auto &[d, key] : wanted) {
        if (p_all && d <= collision_radius) {
            std::unique_ptr<JobResult> ready = pool.run_now(world->job_key(JobKind::SCATTER, key.packed()));
            if (!ready) {
                ready = chunk_job(key)();
            }
            adopt(std::unique_ptr<ScatterChunkJob>(static_cast<ScatterChunkJob *>(ready.release())));
            continue;
        }
        pool.submit(world->job_key(JobKind::SCATTER, key.packed()), d, chunk_job(key));
    }
    refresh_far();
}

// 远处的林子：far_radius 以内、还有一部分在 FAR_LOD_M 外的远块交给后台；远了、整块都在近处树的范围里了就拆
void ScatterStream::refresh_far() {
    if (far_radius <= FAR_LOD_M || !meshes.can_fade()) {
        return;
    }
    const double inner = FAR_LOD_M - FAR_BAND_M;
    for (auto it = far_chunks.begin(); it != far_chunks.end();) {
        const ScatterChunkKey key = ScatterChunkKey::unpack(it->first);
        double farthest = 0.0;
        const double d = far_distance(key.ix, key.iz, farthest);
        if (d > far_radius * 1.1 || farthest < inner * 0.9) {
            if (it->second) {
                it->second->queue_free();
            }
            it = far_chunks.erase(it);
        } else {
            ++it;
        }
    }
    JobPool &pool = world->get_pool();
    pool.cancel_kind(JobKind::FAR_TREES, world->get_lane());
    const ResourceScatter *source = &world->get_scatter();
    const int lo_x = int(std::floor((focus.x - far_radius) / FAR_CHUNK_M)), hi_x = int(std::floor((focus.x + far_radius) / FAR_CHUNK_M));
    const int lo_z = int(std::floor((focus.z - far_radius) / FAR_CHUNK_M)), hi_z = int(std::floor((focus.z + far_radius) / FAR_CHUNK_M));
    for (int iz = lo_z; iz <= hi_z; iz++) {
        for (int ix = lo_x; ix <= hi_x; ix++) {
            double farthest = 0.0;
            const double d = far_distance(ix, iz, farthest);
            const uint64_t packed = ScatterChunkKey{ ix, iz }.packed();
            if (d > far_radius || farthest < inner || far_chunks.count(packed) ||
                    std::any_of(far_waiting.begin(), far_waiting.end(), [&](const auto &w) { return w->ix == ix && w->iz == iz; })) {
                continue;
            }
            pool.submit(world->job_key(JobKind::FAR_TREES, packed), d + FAR_PRIORITY_M, [source, ix, iz]() -> std::unique_ptr<JobResult> {
                std::unique_ptr<FarTreesJob> out = std::make_unique<FarTreesJob>();
                out->ix = ix;
                out->iz = iz;
                const std::vector<ScatterItem> items = source->far_trees(ix, iz, FAR_CHUNK_M, FAR_STEP_M);
                for (const ScatterItem &item : items) {
                    (item.kind() == ScatterLook::CONIFER ? out->conifer_count : out->broadleaf_count)++;
                }
                out->broadleaf.resize(int64_t(out->broadleaf_count) * FLOATS);
                out->conifer.resize(int64_t(out->conifer_count) * FLOATS);
                float *b = out->broadleaf.ptrw(), *c = out->conifer.ptrw();
                const double x0 = ix * FAR_CHUNK_M, z0 = iz * FAR_CHUNK_M;
                for (const ScatterItem &item : items) {
                    // 一团树冠：横向放大、竖向只高一点（一片树冠连起来，不是一棵巨树）
                    const float up = item.scale > 1.2f ? 1.2f + 0.2f * (item.scale - 1.2f) : item.scale;
                    const uint64_t hash = (uint64_t(uint32_t(ix)) << 40) ^ (uint64_t(uint32_t(iz)) << 20) ^ item.index;
                    float *&w = item.kind() == ScatterLook::CONIFER ? c : b;
                    put_instance(w, item, x0, z0, item.scale, up, scatter_meshes::instance_color(item.kind(), item.sub, hash));
                    w += FLOATS;
                }
                return out;
            });
        }
    }
}

void ScatterStream::adopt_far(std::unique_ptr<FarTreesJob> p_job) {
    const uint64_t packed = ScatterChunkKey{ p_job->ix, p_job->iz }.packed();
    double farthest = 0.0;
    const double d = far_distance(p_job->ix, p_job->iz, farthest);
    if (far_chunks.count(packed) || d > far_radius * 1.1) {
        return;
    }
    if (p_job->broadleaf_count + p_job->conifer_count == 0) {
        far_chunks[packed] = nullptr;
        return;
    }
    Node3D *root = memnew(Node3D);
    root->set_name(vformat("FarTrees_%d_%d", p_job->ix, p_job->iz));
    root->set_position(Vector3(real_t(p_job->ix * FAR_CHUNK_M), 0, real_t(p_job->iz * FAR_CHUNK_M)));
    Fade fade;
    fade.in0 = float(FAR_LOD_M - FAR_BAND_M);
    fade.in1 = float(FAR_LOD_M + FAR_BAND_M);
    fade.out0 = float(far_radius - FAR_FADE_M);
    fade.out1 = float(far_radius);
    for (int k = 0; k < 2; k++) {
        const bool conifer = k == 1;
        const int count = conifer ? p_job->conifer_count : p_job->broadleaf_count;
        if (count == 0) {
            continue;
        }
        MultiMeshInstance3D *view = make_view(conifer ? "conifer" : "broadleaf", meshes.low_mesh(conifer ? ScatterLook::CONIFER : ScatterLook::BROADLEAF),
                conifer ? p_job->conifer : p_job->broadleaf, count, false);
        set_fade(view, fade, true, look_season(conifer ? ScatterLook::CONIFER : ScatterLook::BROADLEAF, false));
        root->add_child(view);
    }
    add_child(root);
    far_chunks[packed] = root;
}

void ScatterStream::ensure_near(const Vector3 &p_point) {
    focus = p_point;
    has_focus = true;
    if (world && world->is_loaded() && world->get_scatter().valid()) {
        update_eye();
        refresh(true);
    }
}

void ScatterStream::adopt(std::unique_ptr<ScatterChunkJob> p_job) {
    const ScatterChunkKey key = p_job->key;
    if (chunks.count(key.packed())) {
        return;
    }
    Chunk &chunk = chunks[key.packed()];
    chunk.key = key;
    chunk.items = std::move(p_job->items);
    chunk.instances = std::move(p_job->instances);
    filter_houses(chunk);
    filter_works(chunk);
    snapshot_standing(chunk);
    build_visuals(chunk);
    if (chunk_distance(key, focus) <= collision_radius) {
        build_body(chunk);
    }
}

void ScatterStream::filter_houses(Chunk &r_chunk) const {
    r_chunk.hidden.assign(r_chunk.items.size(), 0);
    r_chunk.house_hidden.assign(r_chunk.items.size(), 0);
    SiteStream *sites = world->get_site_stream();
    if (!sites || r_chunk.items.empty()) {
        return;
    }
    const double size = world->get_scatter().get_rules().chunk_m;
    double x0 = 0.0, z0 = 0.0;
    world->get_scatter().chunk_corner(r_chunk.key, x0, z0);
    for (WorldVillage *village : sites->built_villages()) {
        Settlement *settlement = village->get_settlement();
        if (!settlement) {
            continue;
        }
        const SiteSettlement &site = village->get_site();
        const double reach = village->get_radius() + VILLAGE_REACH_M;
        const double dx = std::max({ x0 - site.x, 0.0, site.x - (x0 + size) }), dz = std::max({ z0 - site.z, 0.0, site.z - (z0 + size) });
        if (std::hypot(dx, dz) > reach) {
            continue;
        }
        const SettlementLayout &layout = settlement->get_model();
        for (size_t k = 0; k < r_chunk.items.size(); k++) {
            const ScatterItem &item = r_chunk.items[k];
            if (r_chunk.hidden[k] || std::hypot(item.x - site.x, item.z - site.z) > reach) {
                continue;
            }
            const bool tree = deposits::scatter(item.kind()).solid;
            for (const Building &b : layout.get_items()) {
                const BuildingDef *def = buildings::by_id(b.kind);
                if (def && footprint_contains(*def, b, item.x - site.x, item.z - site.z, tree ? HOUSE_MARGIN_TREE_M : HOUSE_MARGIN_M)) {
                    r_chunk.hidden[k] = 1;
                    r_chunk.house_hidden[k] = 1;
                    break;
                }
            }
        }
    }
}

bool ScatterStream::item_standing(const ScatterChunkKey &p_key, const ScatterItem &p_item) const {
    return world->get_scatter_ledger().standing(scatter_key(p_key, p_item.index), item_cap(p_item), deposits::scatter(p_item.kind()).refill_hours,
            GroupWorld::clock_hours());
}

void ScatterStream::snapshot_standing(Chunk &r_chunk) const {
    r_chunk.standing.resize(r_chunk.items.size());
    for (size_t k = 0; k < r_chunk.items.size(); k++) {
        r_chunk.standing[k] = item_standing(r_chunk.key, r_chunk.items[k]) ? 1 : 0;
    }
}

void ScatterStream::build_visuals(Chunk &r_chunk) {
    if (r_chunk.node) {
        r_chunk.node->queue_free();
        r_chunk.node = nullptr;
    }
    double x0 = 0.0, z0 = 0.0;
    world->get_scatter().chunk_corner(r_chunk.key, x0, z0);
    if (r_chunk.instances.size() != r_chunk.items.size() * FLOATS) {
        // 没有后台备好的数据（不该有）：当场算
        r_chunk.instances.resize(r_chunk.items.size() * FLOATS);
        for (size_t k = 0; k < r_chunk.items.size(); k++) {
            const ScatterItem &item = r_chunk.items[k];
            put_instance(r_chunk.instances.data() + k * FLOATS, item, x0, z0, item.scale, item.scale, scatter_meshes::instance_color(item.kind(), item.sub, scatter_key(r_chunk.key, item.index)));
        }
    }
    // 按样子分组：立着的一组、倒了的（树桩、茬）一组
    const size_t looks = size_t(deposits::SCATTER_COUNT);
    std::vector<std::vector<size_t>> up(looks), down(looks);
    for (size_t k = 0; k < r_chunk.items.size(); k++) {
        if (r_chunk.hidden[k]) {
            continue;
        }
        (r_chunk.standing[k] ? up : down)[r_chunk.items[k].look].push_back(k);
    }
    const bool can_fade = meshes.can_fade();
    Node3D *root = memnew(Node3D);
    root->set_name(vformat("Scatter_%d_%d", r_chunk.key.ix, r_chunk.key.iz));
    root->set_position(Vector3(real_t(x0), 0, real_t(z0)));
    for (int look = 0; look < deposits::SCATTER_COUNT; look++) {
        const ScatterLook l = ScatterLook(look);
        const bool tree = l == ScatterLook::BROADLEAF || l == ScatterLook::CONIFER;
        for (int felled = 0; felled < 2; felled++) {
            const std::vector<size_t> &list = felled ? down[size_t(look)] : up[size_t(look)];
            const Ref<ArrayMesh> mesh = felled ? meshes.felled_mesh(l) : meshes.mesh(l);
            if (list.empty() || mesh.is_null()) {
                continue;
            }
            PackedFloat32Array buffer;
            buffer.resize(int64_t(list.size()) * FLOATS);
            float *w = buffer.ptrw();
            for (size_t k = 0; k < list.size(); k++) {
                std::memcpy(w + k * FLOATS, r_chunk.instances.data() + list[k] * FLOATS, sizeof(float) * FLOATS);
            }
            const String name = String(deposits::scatter(l).key) + (felled ? String("_felled") : String());
            MultiMeshInstance3D *view = make_view(name, mesh, buffer, int(list.size()), tree && !felled);
            set_fade(view, look_fade(l, felled != 0), can_fade, look_season(l, felled != 0));
            root->add_child(view);
            // 树：细模往外接一份低模（同一批实例），到 FAR_LOD_M 交给远处的林子
            if (tree && !felled && can_fade) {
                Fade fade;
                fade.in0 = float(NEAR_LOD_M - LOD_BAND_M);
                fade.in1 = float(NEAR_LOD_M + LOD_BAND_M);
                fade.out0 = float(FAR_LOD_M - FAR_BAND_M);
                fade.out1 = float(FAR_LOD_M + FAR_BAND_M);
                MultiMeshInstance3D *low = make_view(name + String("_low"), meshes.low_mesh(l), buffer, int(list.size()), true);
                set_fade(low, fade, true, look_season(l, false));
                root->add_child(low);
            }
        }
    }
    add_child(root);
    r_chunk.node = root;
}

void ScatterStream::build_body(Chunk &r_chunk) {
    if (r_chunk.body) {
        r_chunk.body->queue_free();
        r_chunk.body = nullptr;
    }
    double x0 = 0.0, z0 = 0.0;
    world->get_scatter().chunk_corner(r_chunk.key, x0, z0);
    StaticBody3D *body = memnew(StaticBody3D);
    body->set_name(vformat("Trunks_%d_%d", r_chunk.key.ix, r_chunk.key.iz));
    body->set_position(Vector3(real_t(x0), 0, real_t(z0)));
    int count = 0;
    for (size_t k = 0; k < r_chunk.items.size(); k++) {
        const ScatterItem &item = r_chunk.items[k];
        if (r_chunk.hidden[k] || !r_chunk.standing[k] || !deposits::scatter(item.kind()).solid) {
            continue;
        }
        const uint32_t owner = body->create_shape_owner(body);
        body->shape_owner_add_shape(owner, trunk_shape(scatter_solid_radius(item)));
        body->shape_owner_set_transform(owner, Transform3D(Basis(), Vector3(real_t(item.x - x0), real_t(item.y + 0.5 * TRUNK_HEIGHT_M * item.scale), real_t(item.z - z0))));
        count++;
    }
    if (count == 0) {
        memdelete(body);
        return;
    }
    add_child(body);
    r_chunk.body = body;
}

void ScatterStream::drop(Chunk &r_chunk) {
    if (r_chunk.node) {
        r_chunk.node->queue_free();
        r_chunk.node = nullptr;
    }
    if (r_chunk.body) {
        r_chunk.body->queue_free();
        r_chunk.body = nullptr;
    }
}

void ScatterStream::refresh_chunk(const ScatterChunkKey &p_key) {
    const auto found = chunks.find(p_key.packed());
    if (found == chunks.end()) {
        return;
    }
    Chunk &chunk = found->second;
    snapshot_standing(chunk);
    build_visuals(chunk);
    if (chunk.body || chunk_distance(chunk.key, focus) <= collision_radius) {
        build_body(chunk);
    }
}

bool ScatterStream::nearest_item(const Vector3 &p_point, double p_facing_yaw, Hit &r_hit) const {
    if (!world || !world->get_scatter().valid()) {
        return false;
    }
    const ScatterRules &rules = world->get_scatter().get_rules();
    const double look = 4.0; // 最大的一株的半径 + 够得着的距离
    const ScatterChunkKey lo = world->get_scatter().chunk_at(p_point.x - look, p_point.z - look);
    const ScatterChunkKey hi = world->get_scatter().chunk_at(p_point.x + look, p_point.z + look);
    const double fx = -std::sin(p_facing_yaw), fz = -std::cos(p_facing_yaw);
    bool found = false;
    double best = 0.0;
    for (int iz = lo.iz; iz <= hi.iz; iz++) {
        for (int ix = lo.ix; ix <= hi.ix; ix++) {
            const auto it = chunks.find(ScatterChunkKey{ ix, iz }.packed());
            if (it == chunks.end()) {
                continue;
            }
            const Chunk &chunk = it->second;
            for (size_t k = 0; k < chunk.items.size(); k++) {
                const ScatterItem &item = chunk.items[k];
                if (chunk.hidden[k]) {
                    continue;
                }
                const double dx = item.x - p_point.x, dz = item.z - p_point.z;
                const double d = std::hypot(dx, dz);
                const double gap = d - deposits::scatter(item.kind()).radius * item.scale;
                const double ahead = d > UNDERFOOT_M ? (dx * fx + dz * fz) / d : 0.956; // 1 正前方 … −1 正背后
                // 视线里的按偏离视线的角度（弧度）比，总比视线外的先；视线外的按离它边的距离比
                const double score = ahead >= IN_VIEW_COS ? std::acos(std::min(1.0, ahead)) : 10.0 + gap;
                if (d > scatter_reach(item, rules) || (found && score >= best) || !item_standing(chunk.key, item)) {
                    continue;
                }
                r_hit.key = scatter_key(chunk.key, item.index);
                r_hit.item = item;
                r_hit.gap = gap;
                best = score;
                found = true;
            }
        }
    }
    return found;
}

bool ScatterStream::is_trunk(Object *p_collider) {
    for (Node *node = Object::cast_to<Node>(p_collider); node; node = node->get_parent()) {
        if (Object::cast_to<ScatterStream>(node)) {
            return true;
        }
    }
    return false;
}

int ScatterStream::get_far_chunk_count() const {
    int n = 0;
    for (const auto &entry : far_chunks) {
        n += entry.second != nullptr;
    }
    return n;
}

int ScatterStream::get_far_tree_count() const {
    int n = 0;
    for (const auto &entry : far_chunks) {
        if (!entry.second) {
            continue;
        }
        const TypedArray<Node> views = entry.second->get_children();
        for (int64_t k = 0; k < views.size(); k++) {
            if (MultiMeshInstance3D *view = Object::cast_to<MultiMeshInstance3D>(views[k])) {
                n += view->get_multimesh()->get_instance_count();
            }
        }
    }
    return n;
}

int ScatterStream::get_item_count() const {
    int n = 0;
    for (const auto &[packed, chunk] : chunks) {
        for (size_t k = 0; k < chunk.items.size(); k++) {
            n += !chunk.hidden[k] && chunk.standing[k] ? 1 : 0;
        }
    }
    return n;
}

int ScatterStream::get_solid_count() const {
    int n = 0;
    for (const auto &[packed, chunk] : chunks) {
        n += chunk.body ? chunk.body->get_shape_owners().size() : 0;
    }
    return n;
}

Dictionary ScatterStream::find_item_near(const Vector3 &p_point, double p_range, const String &p_kind) const {
    Dictionary out;
    if (!world || !world->get_scatter().valid()) {
        return out;
    }
    const std::string kind = p_kind.utf8().get_data();
    double best = p_range;
    for (const auto &[packed, chunk] : chunks) {
        if (chunk_distance(chunk.key, p_point) > p_range) {
            continue;
        }
        for (size_t k = 0; k < chunk.items.size(); k++) {
            const ScatterItem &item = chunk.items[k];
            const ScatterKind &sk = deposits::scatter(item.kind());
            const double d = std::hypot(item.x - p_point.x, item.z - p_point.z);
            if (chunk.hidden[k] || d > best || (!kind.empty() && kind != sk.key) || !item_standing(chunk.key, item)) {
                continue;
            }
            best = d;
            const uint64_t key = scatter_key(chunk.key, item.index);
            const int cap = item_cap(item);
            const ResourceStock *stock = world->get_scatter_ledger().find(key);
            out["key"] = int64_t(key);
            out["kind"] = String(sk.key);
            out["name"] = String::utf8(deposits::scatter_name(item.kind(), item.sub).c_str());
            out["product"] = String::utf8(deposits::scatter_product(item.kind(), item.sub).c_str());
            out["verb"] = String::utf8(deposits::scatter_verb(item.kind(), item.sub).c_str());
            out["position"] = Vector3(item.x, item.y, item.z);
            out["reach"] = scatter_reach(item, world->get_scatter().get_rules());
            out["radius"] = sk.radius * item.scale;
            out["solid"] = sk.solid;
            out["cap"] = cap;
            out["stock"] = stock ? stock_amount(*stock, cap, sk.refill_hours, GroupWorld::clock_hours()) : double(cap);
            out["standing"] = true;
        }
    }
    return out;
}

bool ScatterStream::is_standing(int64_t p_key) const {
    const ScatterChunkKey key = scatter_key_chunk(uint64_t(p_key));
    const auto found = chunks.find(key.packed());
    if (found == chunks.end()) {
        return true;
    }
    const int index = scatter_key_index(uint64_t(p_key));
    for (size_t k = 0; k < found->second.items.size(); k++) {
        if (found->second.items[k].index == index) {
            return item_standing(key, found->second.items[k]);
        }
    }
    return true;
}

bool ScatterStream::solid_standing_near(double p_x, double p_z, double p_radius) const {
    const ScatterChunkKey lo = world->get_scatter().chunk_at(p_x - p_radius - 2.0, p_z - p_radius - 2.0);
    const ScatterChunkKey hi = world->get_scatter().chunk_at(p_x + p_radius + 2.0, p_z + p_radius + 2.0);
    for (int iz = lo.iz; iz <= hi.iz; iz++) {
        for (int ix = lo.ix; ix <= hi.ix; ix++) {
            const auto found = chunks.find(ScatterChunkKey{ ix, iz }.packed());
            if (found == chunks.end()) {
                continue;
            }
            const Chunk &chunk = found->second;
            for (size_t k = 0; k < chunk.items.size(); k++) {
                const ScatterItem &item = chunk.items[k];
                if (chunk.hidden[k] || !chunk.standing[k] || !deposits::scatter(item.kind()).solid) {
                    continue;
                }
                if (std::hypot(item.x - p_x, item.z - p_z) < p_radius + scatter_solid_radius(item)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool ScatterStream::works_bounds(double &r_x0, double &r_z0, double &r_x1, double &r_z1) const {
    const std::shared_ptr<GroundWorks> works = world->get_player_works();
    r_x0 = r_z0 = 1e18;
    r_x1 = r_z1 = -1e18;
    if (!works) {
        return false;
    }
    auto grow = [&](double x, double z, double r) {
        r_x0 = std::min(r_x0, x - r), r_z0 = std::min(r_z0, z - r), r_x1 = std::max(r_x1, x + r), r_z1 = std::max(r_z1, z + r);
    };
    if (works->edits) {
        for (int64_t key : works->edits->chunk_keys()) {
            int ci = 0, cj = 0;
            EditGrid::chunk_unkey(key, ci, cj);
            const EditGrid &g = works->edits->get_grid();
            grow(g.cell_x(cj * EditGrid::CHUNK), g.cell_z(ci * EditGrid::CHUNK), 1.0);
            grow(g.cell_x((cj + 1) * EditGrid::CHUNK), g.cell_z((ci + 1) * EditGrid::CHUNK), 1.0);
        }
    }
    for (const Structure &s : works->structures.get()) {
        for (const Piece &p : s.pieces) {
            double x, y, z;
            s.to_host((p.spot.x + 0.5) * piece_dims::GRID_M, 0.0, (p.spot.z + 0.5) * piece_dims::GRID_M, x, y, z);
            grow(x, z, piece_dims::GRID_M);
        }
    }
    for (const PlacedProp &p : works->props.get_props()) {
        grow(p.x, p.z, 2.0);
    }
    return r_x0 <= r_x1;
}

void ScatterStream::filter_works(Chunk &r_chunk) const {
    // 从压着房子的那一份重来（filter_houses 算过的），再加上压着营造的
    if (r_chunk.house_hidden.size() == r_chunk.items.size()) {
        r_chunk.hidden = r_chunk.house_hidden;
    }
    const std::shared_ptr<GroundWorks> works = world->get_player_works();
    if (!works || r_chunk.items.empty() || works->empty()) {
        return;
    }
    double bx0, bz0, bx1, bz1;
    if (!works_bounds(bx0, bz0, bx1, bz1)) {
        return;
    }
    double x0 = 0.0, z0 = 0.0;
    world->get_scatter().chunk_corner(r_chunk.key, x0, z0);
    const double size = world->get_scatter().get_rules().chunk_m;
    if (x0 > bx1 || x0 + size < bx0 || z0 > bz1 || z0 + size < bz0) {
        return;
    }
    const std::shared_ptr<const TerrainEdits> edits = works->edits;
    // 地基格的中心（宿主坐标）与半对角：先按圆粗筛
    struct Foot {
        const Structure *s;
        double x, z;
    };
    std::vector<Foot> feet;
    for (const Structure &s : works->structures.get()) {
        for (const Piece &p : s.pieces) {
            const PieceDef *def = pieces::find(p.item);
            if (def && def->role == PieceRole::FOUNDATION) {
                double x, y, z;
                s.to_host((p.spot.x + 0.5) * piece_dims::GRID_M, 0.0, (p.spot.z + 0.5) * piece_dims::GRID_M, x, y, z);
                feet.push_back({ &s, x, z });
            }
        }
    }
    constexpr double FOOT_MARGIN_M = 0.4; // 地基外这么远的也藏（灌丛不从台基边上戳出来）
    for (size_t k = 0; k < r_chunk.items.size(); k++) {
        if (r_chunk.hidden[k]) {
            continue;
        }
        const ScatterItem &item = r_chunk.items[k];
        bool hide = edits && edits->delta_at(item.x, item.z) != 0.0;
        for (size_t f = 0; f < feet.size() && !hide; f++) {
            if (std::hypot(item.x - feet[f].x, item.z - feet[f].z) > piece_dims::GRID_M) {
                continue;
            }
            double lx, ly, lz;
            feet[f].s->to_local(item.x, item.y, item.z, lx, ly, lz);
            const double cx = std::floor(lx / piece_dims::GRID_M), cz = std::floor(lz / piece_dims::GRID_M);
            for (int dx = -1; dx <= 1 && !hide; dx++) {
                for (int dz = -1; dz <= 1 && !hide; dz++) {
                    const int x = int(cx) + dx, z = int(cz) + dz;
                    if (!feet[f].s->has_floor(x, z, 0)) {
                        continue;
                    }
                    const double ex = std::max({ x * piece_dims::GRID_M - lx, 0.0, lx - (x + 1) * piece_dims::GRID_M });
                    const double ez = std::max({ z * piece_dims::GRID_M - lz, 0.0, lz - (z + 1) * piece_dims::GRID_M });
                    hide = std::hypot(ex, ez) < FOOT_MARGIN_M;
                }
            }
        }
        for (const PlacedProp &p : works->props.get_props()) {
            if (hide) {
                break;
            }
            hide = prop_distance(p, item.x, item.z) < 0.3;
        }
        if (hide) {
            r_chunk.hidden[k] = 1;
        }
    }
}

void ScatterStream::refilter_works() {
    double x0, z0, x1, z1;
    const bool any = works_bounds(x0, z0, x1, z1);
    // 这回的框并上回的框：撤掉的东西底下要重新露出来
    if (works_x0 <= works_x1) {
        if (!any) {
            x0 = works_x0, z0 = works_z0, x1 = works_x1, z1 = works_z1;
        } else {
            x0 = std::min(x0, works_x0), z0 = std::min(z0, works_z0), x1 = std::max(x1, works_x1), z1 = std::max(z1, works_z1);
        }
    } else if (!any) {
        return;
    }
    if (any) {
        works_bounds(works_x0, works_z0, works_x1, works_z1);
    } else {
        works_x0 = works_z0 = 0.0, works_x1 = works_z1 = -1.0;
    }
    const double size = world->get_scatter().get_rules().chunk_m;
    for (auto &[packed, chunk] : chunks) {
        double cx = 0.0, cz = 0.0;
        world->get_scatter().chunk_corner(chunk.key, cx, cz);
        if (cx > x1 || cx + size < x0 || cz > z1 || cz + size < z0) {
            continue;
        }
        const std::vector<char> before = chunk.hidden;
        filter_works(chunk); // 压着房子的那一份不变（村子铺了 / 拆了由 refilter_villages 管）
        if (chunk.hidden != before) {
            build_visuals(chunk);
            if (chunk.body) {
                build_body(chunk);
            }
        }
    }
}

bool ScatterStream::path_clear(const Vector3 &p_from, const Vector3 &p_to, double p_clearance) const {
    const double ax = p_from.x, az = p_from.z, bx = p_to.x, bz = p_to.z;
    const double len2 = std::max(1e-9, (bx - ax) * (bx - ax) + (bz - az) * (bz - az));
    for (const auto &[packed, chunk] : chunks) {
        double x0 = 0.0, z0 = 0.0;
        world->get_scatter().chunk_corner(chunk.key, x0, z0);
        const double size = world->get_scatter().get_rules().chunk_m, pad = p_clearance + 2.0;
        if (std::max(ax, bx) < x0 - pad || std::min(ax, bx) > x0 + size + pad || std::max(az, bz) < z0 - pad || std::min(az, bz) > z0 + size + pad) {
            continue;
        }
        for (size_t k = 0; k < chunk.items.size(); k++) {
            const ScatterItem &item = chunk.items[k];
            if (chunk.hidden[k] || !chunk.standing[k] || !deposits::scatter(item.kind()).solid) {
                continue;
            }
            const double t = std::clamp(((item.x - ax) * (bx - ax) + (item.z - az) * (bz - az)) / len2, 0.0, 1.0);
            const double d = std::hypot(item.x - (ax + t * (bx - ax)), item.z - (az + t * (bz - az)));
            if (d < p_clearance + scatter_solid_radius(item)) {
                return false;
            }
        }
    }
    return true;
}
