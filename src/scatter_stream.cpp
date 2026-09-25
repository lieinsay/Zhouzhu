#include "scatter_stream.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "buildings.h"
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

// 样子摆到多远（MultiMesh 的可见距离；0 = 跟着块一起拆）
float visible_range(ScatterLook p_look) {
    switch (p_look) {
        case ScatterLook::BROADLEAF:
        case ScatterLook::CONIFER:
            return 0.0f;
        case ScatterLook::SHRUB:
            return 450.0f;
        case ScatterLook::BOULDER:
        case ScatterLook::FLOATSTONE:
        case ScatterLook::ORE_ROCK:
            return 500.0f;
        default:
            return 300.0f;
    }
}

int item_cap(const ScatterItem &p_item) {
    return deposits::scatter_yield(p_item.kind(), p_item.scale);
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
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "world_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "World"), "set_world_path", "get_world_path");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "100,5000,10,suffix:m"), "set_radius", "get_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_radius", PROPERTY_HINT_RANGE, "20,1000,5,suffix:m"), "set_collision_radius", "get_collision_radius");
}

void ScatterStream::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    world = Object::cast_to<World>(get_node_or_null(world_path));
    ERR_FAIL_NULL_MSG(world, "ScatterStream 找不到 World（world_path）");
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

void ScatterStream::_process(double p_delta) {
    if (!world || !world->is_loaded() || !world->get_scatter().valid() || !has_focus) {
        return;
    }
    since_check += p_delta;
    since_regrow += p_delta;
    if (since_check >= check_interval) {
        since_check = 0.0;
        refresh(false);
    }
    // 生成好的块：近的先挂，时间用完了下帧再挂
    world->scatter_collect(waiting);
    if (!waiting.empty()) {
        std::sort(waiting.begin(), waiting.end(), [&](const auto &a, const auto &b) { return chunk_distance(a.first, focus) < chunk_distance(b.first, focus); });
        const uint64_t started = Time::get_singleton()->get_ticks_usec();
        size_t k = 0;
        for (; k < waiting.size() && Time::get_singleton()->get_ticks_usec() - started < uint64_t(adopt_budget_usec); k++) {
            if (chunk_distance(waiting[k].first, focus) <= radius * hysteresis) {
                adopt(waiting[k].first, std::move(waiting[k].second));
            }
        }
        waiting.erase(waiting.begin(), waiting.begin() + int64_t(k));
    }
    // 倒着的长回来没有
    if (since_regrow >= regrow_interval) {
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

void ScatterStream::refresh(bool p_all) {
    const ResourceScatter &scatter = world->get_scatter();
    const double size = scatter.get_rules().chunk_m;
    // 远了拆；碰撞近了加、远了拆
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
    // 村子铺了新的：重查压着房子的
    SiteStream *sites = world->get_site_stream();
    const int64_t version = sites ? sites->get_village_version() : 0;
    if (version != village_version) {
        village_version = version;
        for (auto &[packed, chunk] : chunks) {
            const std::vector<char> before = chunk.hidden;
            filter_houses(chunk);
            if (chunk.hidden != before) {
                build_visuals(chunk);
                if (chunk.body) {
                    build_body(chunk);
                }
            }
        }
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
            bool queued = false;
            for (const auto &w : waiting) {
                queued = queued || w.first == key;
            }
            if (!queued) {
                wanted.emplace_back(d, key);
            }
        }
    }
    std::sort(wanted.begin(), wanted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    if (p_all) {
        // 瞬移：碰撞半径内的同步生成
        std::vector<ScatterChunkKey> later;
        for (const auto &[d, key] : wanted) {
            if (d <= collision_radius) {
                adopt(key, scatter.chunk(key));
            } else {
                later.push_back(key);
            }
        }
        world->scatter_request(later);
        return;
    }
    std::vector<ScatterChunkKey> keys;
    keys.reserve(wanted.size());
    for (const auto &w : wanted) {
        keys.push_back(w.second);
    }
    (void)size;
    world->scatter_request(keys);
}

void ScatterStream::ensure_near(const Vector3 &p_point) {
    focus = p_point;
    has_focus = true;
    if (world && world->is_loaded() && world->get_scatter().valid()) {
        refresh(true);
    }
}

void ScatterStream::adopt(const ScatterChunkKey &p_key, std::vector<ScatterItem> &&p_items) {
    if (chunks.count(p_key.packed())) {
        return;
    }
    Chunk &chunk = chunks[p_key.packed()];
    chunk.key = p_key;
    chunk.items = std::move(p_items);
    filter_houses(chunk);
    snapshot_standing(chunk);
    build_visuals(chunk);
    if (chunk_distance(p_key, focus) <= collision_radius) {
        build_body(chunk);
    }
}

void ScatterStream::filter_houses(Chunk &r_chunk) const {
    r_chunk.hidden.assign(r_chunk.items.size(), 0);
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
                    break;
                }
            }
        }
    }
}

bool ScatterStream::item_standing(const ScatterChunkKey &p_key, const ScatterItem &p_item) const {
    return world->get_scatter_ledger().standing(scatter_key(p_key, p_item.index), item_cap(p_item), deposits::scatter(p_item.kind()).refill_hours,
            World::clock_hours());
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
    // 按样子分组：立着的一组、倒了的（树桩、茬）一组
    const size_t looks = size_t(deposits::SCATTER_COUNT);
    std::vector<std::vector<size_t>> up(looks), down(looks);
    for (size_t k = 0; k < r_chunk.items.size(); k++) {
        if (r_chunk.hidden[k]) {
            continue;
        }
        (r_chunk.standing[k] ? up : down)[r_chunk.items[k].look].push_back(k);
    }
    Node3D *root = memnew(Node3D);
    root->set_name(vformat("Scatter_%d_%d", r_chunk.key.ix, r_chunk.key.iz));
    root->set_position(Vector3(real_t(x0), 0, real_t(z0)));
    auto add = [&](const Ref<ArrayMesh> &p_mesh, const std::vector<size_t> &p_items, ScatterLook p_look, bool p_felled) {
        if (p_items.empty() || p_mesh.is_null()) {
            return;
        }
        Ref<MultiMesh> mm;
        mm.instantiate();
        mm->set_transform_format(MultiMesh::TRANSFORM_3D);
        mm->set_use_colors(true);
        mm->set_mesh(p_mesh);
        mm->set_instance_count(int(p_items.size()));
        for (int k = 0; k < int(p_items.size()); k++) {
            const ScatterItem &item = r_chunk.items[p_items[size_t(k)]];
            const real_t s = item.scale;
            const Basis basis = Basis(Vector3(0, 1, 0), item.yaw).scaled(Vector3(s, s, s));
            mm->set_instance_transform(k, Transform3D(basis, Vector3(real_t(item.x - x0), item.y, real_t(item.z - z0))));
            mm->set_instance_color(k, scatter_meshes::instance_color(p_look, item.sub, scatter_key(r_chunk.key, item.index)));
        }
        MultiMeshInstance3D *view = memnew(MultiMeshInstance3D);
        view->set_name(String(deposits::scatter(p_look).key) + (p_felled ? String("_felled") : String()));
        view->set_multimesh(mm);
        const bool tree = p_look == ScatterLook::BROADLEAF || p_look == ScatterLook::CONIFER;
        view->set_cast_shadows_setting(tree && !p_felled ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        const float range = visible_range(p_look);
        if (range > 0.0f) {
            view->set_visibility_range_end(range);
        }
        root->add_child(view);
    };
    for (int look = 0; look < deposits::SCATTER_COUNT; look++) {
        add(meshes.mesh(ScatterLook(look)), up[size_t(look)], ScatterLook(look), false);
        add(meshes.felled_mesh(ScatterLook(look)), down[size_t(look)], ScatterLook(look), true);
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
            out["stock"] = stock ? stock_amount(*stock, cap, sk.refill_hours, World::clock_hours()) : double(cap);
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
