#include "group_stream.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "far_groups.h"
#include "farm.h"
#include "hinterland.h"
#include "planet_view.h"
#include "scatter_stream.h"
#include "shipyard.h"
#include "site_stream.h"
#include "world.h"
#include "world_clock.h"

using namespace godot;

namespace {

Vector3 v3(const PlanetVec &p_v) {
    return Vector3(real_t(p_v.x), real_t(p_v.y), real_t(p_v.z));
}

PlanetVec pv(const Vector3 &p_v) {
    return PlanetVec(p_v.x, p_v.y, p_v.z);
}

double now_s() {
    return double(Time::get_singleton()->get_ticks_usec()) * 1e-6;
}

Basis basis_of(const PlanetBasis &p_b) {
    return Basis(v3(p_b.cols[0]), v3(p_b.cols[1]), v3(p_b.cols[2]));
}

// 绕 y 的转角：Godot 的 y 转 θ 把 x 轴转到 (cos θ, 0, −sin θ)
double yaw_of(const PlanetBasis &p_b) {
    return std::atan2(-p_b.cols[0].z, p_b.cols[0].x);
}

} // namespace

Vector3 GroupShift::point(const Vector3 &p_old) const {
    return v3(rebase_point(from, to, pv(p_old)));
}

Basis GroupShift::rotation(const Vector3 &p_old) const {
    return basis_of(rebase_rotation(from, to, pv(p_old)));
}

double GroupShift::yaw(const Vector3 &p_old) const {
    return yaw_of(rebase_rotation(from, to, pv(p_old)));
}

void GroupStream::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_ready"), &GroupStream::is_ready);
    ClassDB::bind_method(D_METHOD("get_focus"), &GroupStream::get_focus);
    ClassDB::bind_method(D_METHOD("get_anchor_node"), &GroupStream::get_anchor_node);
    ClassDB::bind_method(D_METHOD("get_group_nodes"), &GroupStream::get_group_nodes);
    ClassDB::bind_method(D_METHOD("get_group_state", "node"), &GroupStream::get_group_state);
    ClassDB::bind_method(D_METHOD("get_group_world", "node"), &GroupStream::get_group_world);
    ClassDB::bind_method(D_METHOD("get_group_sites", "node"), &GroupStream::get_group_sites);
    ClassDB::bind_method(D_METHOD("get_group_hinterland", "node"), &GroupStream::get_group_hinterland);
    ClassDB::bind_method(D_METHOD("get_shift_count"), &GroupStream::get_shift_count);
    ClassDB::bind_method(D_METHOD("is_shift_pending"), &GroupStream::is_shift_pending);
    ClassDB::bind_method(D_METHOD("force_shift_to", "node"), &GroupStream::force_shift_to);
    ClassDB::bind_method(D_METHOD("get_stats"), &GroupStream::get_stats);
    ClassDB::bind_method(D_METHOD("set_auto_shift", "value"), &GroupStream::set_auto_shift);
    ClassDB::bind_method(D_METHOD("get_auto_shift"), &GroupStream::get_auto_shift);
    ClassDB::bind_method(D_METHOD("set_planet_path", "value"), &GroupStream::set_planet_path);
    ClassDB::bind_method(D_METHOD("get_planet_path"), &GroupStream::get_planet_path);
    ClassDB::bind_method(D_METHOD("set_home_path", "value"), &GroupStream::set_home_path);
    ClassDB::bind_method(D_METHOD("get_home_path"), &GroupStream::get_home_path);
    ClassDB::bind_method(D_METHOD("set_far_groups_path", "value"), &GroupStream::set_far_groups_path);
    ClassDB::bind_method(D_METHOD("get_far_groups_path"), &GroupStream::get_far_groups_path);
    ClassDB::bind_method(D_METHOD("set_worlds_dir", "value"), &GroupStream::set_worlds_dir);
    ClassDB::bind_method(D_METHOD("get_worlds_dir"), &GroupStream::get_worlds_dir);
    ClassDB::bind_method(D_METHOD("set_settlements_dir", "value"), &GroupStream::set_settlements_dir);
    ClassDB::bind_method(D_METHOD("get_settlements_dir"), &GroupStream::get_settlements_dir);
    ClassDB::bind_method(D_METHOD("set_detail_in", "value"), &GroupStream::set_detail_in);
    ClassDB::bind_method(D_METHOD("get_detail_in"), &GroupStream::get_detail_in);
    ClassDB::bind_method(D_METHOD("set_detail_out", "value"), &GroupStream::set_detail_out);
    ClassDB::bind_method(D_METHOD("get_detail_out"), &GroupStream::get_detail_out);
    ClassDB::bind_method(D_METHOD("set_near_in", "value"), &GroupStream::set_near_in);
    ClassDB::bind_method(D_METHOD("get_near_in"), &GroupStream::get_near_in);
    ClassDB::bind_method(D_METHOD("set_near_out", "value"), &GroupStream::set_near_out);
    ClassDB::bind_method(D_METHOD("get_near_out"), &GroupStream::get_near_out);
    ClassDB::bind_method(D_METHOD("set_shift_margin", "value"), &GroupStream::set_shift_margin);
    ClassDB::bind_method(D_METHOD("get_shift_margin"), &GroupStream::get_shift_margin);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "planet_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PlanetView"), "set_planet_path", "get_planet_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "home_path"), "set_home_path", "get_home_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "far_groups_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FarGroups"), "set_far_groups_path", "get_far_groups_path");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "worlds_dir", PROPERTY_HINT_DIR), "set_worlds_dir", "get_worlds_dir");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "settlements_dir", PROPERTY_HINT_DIR), "set_settlements_dir", "get_settlements_dir");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_in", PROPERTY_HINT_RANGE, "1000,200000,100,suffix:m"), "set_detail_in", "get_detail_in");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_out", PROPERTY_HINT_RANGE, "1000,200000,100,suffix:m"), "set_detail_out", "get_detail_out");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near_in", PROPERTY_HINT_RANGE, "1000,300000,100,suffix:m"), "set_near_in", "get_near_in");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near_out", PROPERTY_HINT_RANGE, "1000,300000,100,suffix:m"), "set_near_out", "get_near_out");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shift_margin", PROPERTY_HINT_RANGE, "0,20000,10,suffix:m"), "set_shift_margin", "get_shift_margin");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_shift"), "set_auto_shift", "get_auto_shift");
    ADD_SIGNAL(MethodInfo("anchor_changed", PropertyInfo(Variant::INT, "from_node"), PropertyInfo(Variant::INT, "to_node")));

    BIND_ENUM_CONSTANT(TIER_FAR);
    BIND_ENUM_CONSTANT(TIER_NEAR);
    BIND_ENUM_CONSTANT(TIER_DETAIL);
}

void GroupStream::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    add_to_group(GROUP);
    // 焦点（Expedition 在 _process 里喂）之后、PlanetView（100）与 FarGroups（110）之前：邻群的摆放这一帧就对
    set_process_priority(90);
}

bool GroupStream::setup() {
    planet = Object::cast_to<PlanetView>(get_node_or_null(planet_path));
    GroupWorld *home = Object::cast_to<GroupWorld>(get_node_or_null(home_path));
    if (!planet || !planet->is_active() || !home || !home->is_loaded()) {
        return false; // PlanetView 晚一帧锚定：等它
    }
    far_groups = Object::cast_to<FarGroups>(get_node_or_null(far_groups_path));
    const PlanetData *pd = planet->get_planet();
    PlanetShape shape;
    shape.sea_radius_m = pd->radius_km * 1000.0;

    // 目录：worlds_dir 下以节点号命名、有 terrain.bin 与 world.json 的（仓库里是 2051 与 2050；P4 起加 user:// 的缓存）
    std::vector<int> nodes;
    const PackedStringArray dirs = DirAccess::get_directories_at(worlds_dir);
    for (int64_t k = 0; k < dirs.size(); k++) {
        const String name = dirs[k];
        if (!name.is_valid_int()) {
            continue;
        }
        const int node = int(name.to_int());
        const String dir = worlds_dir.path_join(name);
        if (node < 0 || node >= pd->node_count() || !FileAccess::file_exists(dir.path_join("terrain.bin")) || !FileAccess::file_exists(dir.path_join("world.json"))) {
            continue;
        }
        nodes.push_back(node);
    }
    if (std::find(nodes.begin(), nodes.end(), home->get_node_id()) == nodes.end()) {
        nodes.push_back(home->get_node_id());
    }
    for (int node : nodes) {
        std::unique_ptr<Group> g = std::make_unique<Group>();
        g->node = node;
        g->home = node == home->get_node_id();
        g->data_dir = g->home ? home->get_data_dir() : worlds_dir.path_join(itos(node));
        // 群心按 world.json 的 source（与 PlanetView 锚定用的同一个数）；读不到用行星包的
        double lat = pd->node(node).lat, lon = pd->node(node).lon;
        const Dictionary info = g->home ? home->get_info() : Dictionary(JSON::parse_string(FileAccess::get_file_as_string(g->data_dir.path_join("world.json"))));
        const Dictionary source = info.get("source", Dictionary());
        if (source.has("lat") && source.has("lon")) {
            lat = double(source["lat"]);
            lon = double(source["lon"]);
        }
        g->frame = GroupFrame(lat, lon, shape);
        // 粗版的陆地格心：算焦点到这一群的陆地多远
        const PlanetLod lod = pd->lod(node);
        g->land_pad = 0.5 * lod.res_m;
        for (int r = 0; r < lod.rows; r++) {
            for (int c = 0; c < lod.cols; c++) {
                if (lod.land[lod.index(r, c)] < 32) {
                    continue;
                }
                double x = 0.0, z = 0.0;
                lod.cell_center(r, c, x, z);
                g->land_x.push_back(x);
                g->land_z.push_back(z);
                g->reach_m = std::max(g->reach_m, std::hypot(x, z) + g->land_pad);
            }
        }
        // 码头表（没载入时也要给位置）
        const PackedByteArray bytes = FileAccess::get_file_as_bytes(g->data_dir.path_join("sites.tsv"));
        std::string error;
        if (bytes.is_empty() || !parse_world_sites(std::string(reinterpret_cast<const char *>(bytes.ptr()), size_t(bytes.size())), g->sites, error)) {
            g->sites = WorldSites();
        }
        if (g->home) {
            // 场景里摆的那一群：节点在场景根下，已经载入，是锚、细版
            g->parent = home->get_parent();
            g->world = home;
            g->site_stream = home->get_site_stream();
            g->scatter_stream = home->get_scatter_stream();
            g->hinterland = g->site_stream ? g->site_stream->get_hinterland() : nullptr;
            g->tier = TIER_DETAIL;
            g->loaded_at = now_s();
            anchor = int(groups.size());
        }
        groups.push_back(std::move(g));
    }
    return anchor >= 0;
}

void GroupStream::set_focus(const Vector3 &p_focus) {
    // 坏了的位置（NaN）不认：按它换算到别的群会落在群心上（AEQD 的逆对 NaN 给 0），把邻群错载进来
    if (!p_focus.is_finite()) {
        return;
    }
    focus = p_focus;
    has_focus = true;
}

GroupStream::Group *GroupStream::find(int p_node) const {
    for (const auto &g : groups) {
        if (g->node == p_node) {
            return g.get();
        }
    }
    return nullptr;
}

int GroupStream::index_of(const Group *p_group) const {
    for (size_t k = 0; k < groups.size(); k++) {
        if (groups[k].get() == p_group) {
            return int(k);
        }
    }
    return -1;
}

PlanetVec GroupStream::to_group(const Group &p_group, const Vector3 &p_anchor_point) const {
    const Group *a = anchor_group();
    if (!a || a == &p_group) {
        return pv(p_anchor_point);
    }
    return rebase_point(a->frame, p_group.frame, pv(p_anchor_point));
}

Vector3 GroupStream::from_group(const Group &p_group, const PlanetVec &p_group_point) const {
    const Group *a = anchor_group();
    if (!a || a == &p_group) {
        return v3(p_group_point);
    }
    return v3(rebase_point(p_group.frame, a->frame, p_group_point));
}

Transform3D GroupStream::transform_from_group(const Group &p_group, const Transform3D &p_group_xform) const {
    const Group *a = anchor_group();
    if (!a || a == &p_group) {
        return p_group_xform;
    }
    const PlanetVec p = pv(p_group_xform.origin);
    const Basis r = basis_of(rebase_rotation(p_group.frame, a->frame, p));
    return Transform3D(r * p_group_xform.basis, v3(rebase_point(p_group.frame, a->frame, p)));
}

double GroupStream::land_distance(const Group &p_group, const Vector3 &p_anchor_point) const {
    const Group *a = anchor_group();
    if (a && a != &p_group) {
        // 群心离这一点远过它的陆地外沿再 200 km：不用一格格量
        const PlanetVec pa = a->frame.to_planet(pv(p_anchor_point));
        const PlanetVec gc = p_group.frame.up * p_group.frame.radius;
        const double chord = (pa - gc).length();
        if (chord > p_group.reach_m + 200000.0) {
            return chord - p_group.reach_m;
        }
    }
    const PlanetVec q = to_group(p_group, p_anchor_point);
    double best = 1e18;
    for (size_t k = 0; k < p_group.land_x.size(); k++) {
        const double dx = p_group.land_x[k] - q.x, dz = p_group.land_z[k] - q.z;
        best = std::min(best, dx * dx + dz * dz);
    }
    return best >= 1e17 ? 1e18 : std::max(0.0, std::sqrt(best) - p_group.land_pad);
}

GroupStream::Group *GroupStream::group_near(const Vector3 &p_anchor_point, double p_within) const {
    Group *best = nullptr;
    double best_d = p_within;
    for (const auto &g : groups) {
        const double d = land_distance(*g, p_anchor_point);
        if (d <= best_d) {
            best_d = d;
            best = g.get();
        }
    }
    return best;
}

void GroupStream::_process(double p_delta) {
    if (!ready) {
        ready = setup();
        if (!ready) {
            return;
        }
    }
    Group *a = anchor_group();
    if (!has_focus && a && a->world && a->world->get_focus().is_finite()) {
        focus = a->world->get_focus(); // 地图宿主还没喂过：用锚群自己的焦点（Expedition 直接接 World 时）
    }
    since_check += p_delta;
    if (since_check >= check_interval) {
        since_check = 0.0;
        update_tiers();
        if (auto_shift) {
            update_anchor_choice();
        }
    }
    poll_loading();
    update_placements(false);
    feed_focus();
}

void GroupStream::update_tiers() {
    for (auto &g : groups) {
        g->land_distance = land_distance(*g, focus);
    }
    // 由近及远：细版的名额（锚占一个）先给近的
    std::vector<Group *> order;
    for (auto &g : groups) {
        order.push_back(g.get());
    }
    std::sort(order.begin(), order.end(), [](const Group *a, const Group *b) { return a->land_distance < b->land_distance; });
    int detail_left = std::max(0, max_detail - 1);
    const Group *a = anchor_group();
    for (Group *g : order) {
        Tier want = TIER_FAR;
        if (g == a || index_of(g) == pending) {
            want = TIER_DETAIL;
        } else {
            const double d = g->land_distance;
            const bool detail = g->tier == TIER_DETAIL ? d < detail_out : d < detail_in;
            const bool near = g->tier >= TIER_NEAR ? d < near_out : d < near_in;
            if (detail && detail_left > 0) {
                want = TIER_DETAIL;
                detail_left--;
            } else if (near || detail) {
                want = TIER_NEAR;
            }
        }
        if (want != g->tier) {
            set_tier(*g, want);
        }
    }
}

void GroupStream::set_tier(Group &r_group, Tier p_tier) {
    if (p_tier == TIER_FAR) {
        if (r_group.tier == TIER_DETAIL) {
            unload_detail(r_group);
        }
        unload_world(r_group);
        r_group.tier = TIER_FAR;
        return;
    }
    if (!r_group.world) {
        load_world(r_group, false); // 后台载入；细版等载入完了再挂（poll_loading / 下一回 update_tiers）
    }
    if (p_tier == TIER_NEAR) {
        if (r_group.tier == TIER_DETAIL) {
            unload_detail(r_group);
        }
        r_group.tier = TIER_NEAR;
        return;
    }
    // 细版：地图得先载入完
    if (!r_group.world->is_loaded()) {
        r_group.tier = TIER_NEAR;
        return;
    }
    load_detail(r_group);
    r_group.tier = TIER_DETAIL;
}

void GroupStream::load_world(Group &r_group, bool p_sync) {
    if (r_group.world) {
        return;
    }
    if (r_group.home) {
        r_group.parent = get_parent();
    } else {
        if (!r_group.container) {
            r_group.container = memnew(Node3D);
            r_group.container->set_name(vformat("Group%d", r_group.node));
            add_child(r_group.container);
        }
        r_group.parent = r_group.container;
    }
    // 场景里摆的那一群回来时照旧是 World（脚本里 `as World`）；别的群是 GroupWorld
    GroupWorld *w = r_group.home ? memnew(World) : memnew(GroupWorld);
    w->set_name("World");
    w->set_data_dir(r_group.data_dir);
    w->set_terrain_seed(r_group.node);
    w->set_async_load(!p_sync);
    w->set_physics_on(false);
    Farm *farm = memnew(Farm);
    farm->set_name("Farm");
    w->add_child(farm);
    Shipyard *yard = memnew(Shipyard);
    yard->set_name("Shipyard");
    w->add_child(yard);
    r_group.world = w;
    r_group.placement = placement_for(r_group);
    r_group.placed_at = focus;
    r_group.placed = true;
    w->set_transform(r_group.placement);
    r_group.parent->add_child(w);
    r_group.loaded_at = -1.0;
    loads++;
}

void GroupStream::unload_world(Group &r_group) {
    if (!r_group.world) {
        return;
    }
    // 近景 → 远景：农田里的地、采场的存量、散布物的采空账留在内存（下回载入补回）
    GroupWorld *w = r_group.world;
    if (w->is_loaded()) {
        GroupMemory &m = r_group.memory;
        m.has_world = true;
        m.plots.clear();
        if (Farm *farm = w->host_farm()) {
            m.plots = farm->get_model().plot_entries();
        }
        const WorldClock *clock = WorldClock::get_singleton();
        m.plots_at = clock ? clock->get_seconds() : 0.0;
        m.stocks = w->get_resource_stocks();
        m.scatter = w->get_scatter_ledger();
    }
    set_far_hidden(r_group, false);
    if (Node *parent = w->get_parent()) {
        parent->remove_child(w);
    }
    w->queue_free();
    r_group.world = nullptr;
    r_group.loaded_at = -1.0;
    r_group.placed = false;
    if (r_group.container && r_group.container->get_child_count() == 0) {
        r_group.container->queue_free();
        r_group.container = nullptr;
    }
    unloads++;
}

void GroupStream::load_detail(Group &r_group) {
    if (r_group.site_stream || !r_group.world) {
        return;
    }
    SiteStream *s = memnew(SiteStream);
    s->set_name("SiteStream");
    s->set_seed(r_group.node);
    s->set_layout_dir(settlements_dir.path_join(itos(r_group.node))); // 定制的村子按群分开：settlements/<节点>
    ScatterStream *sc = memnew(ScatterStream);
    sc->set_name("ScatterStream");
    Hinterland *h = memnew(Hinterland);
    h->set_name("Hinterland");
    s->set_transform(r_group.placement);
    sc->set_transform(r_group.placement);
    // 上回离开细版时存下的各村的账：交给新的账房，开账时补回、补结（模拟分级）
    if (!r_group.memory.ledgers.empty()) {
        h->set_saved(r_group.memory.ledgers, r_group.memory.ledgers_at);
        r_group.memory.ledgers.clear();
    }
    // SiteStream 先进树（账房进树时按路径找它）；它自己找账房时账房还没在，挂好了再告诉它
    r_group.parent->add_child(s);
    r_group.parent->add_child(sc);
    r_group.parent->add_child(h);
    s->set_hinterland(h);
    r_group.world->set_streams(s, sc);
    r_group.site_stream = s;
    r_group.scatter_stream = sc;
    r_group.hinterland = h;
}

void GroupStream::unload_detail(Group &r_group) {
    if (r_group.world) {
        r_group.world->set_streams(nullptr, nullptr);
    }
    // 近处的村民先散（账收回账房），账房再把各村的账存成文本留在内存（模拟分级）
    if (r_group.site_stream) {
        r_group.site_stream->retire_folk();
    }
    if (r_group.hinterland) {
        r_group.hinterland->save_states(r_group.memory.ledgers, r_group.memory.ledgers_at);
    }
    for (Node *n : { static_cast<Node *>(r_group.site_stream), static_cast<Node *>(r_group.scatter_stream), static_cast<Node *>(r_group.hinterland) }) {
        if (!n) {
            continue;
        }
        if (Node *parent = n->get_parent()) {
            parent->remove_child(n);
        }
        n->queue_free();
    }
    r_group.site_stream = nullptr;
    r_group.scatter_stream = nullptr;
    r_group.hinterland = nullptr;
}

void GroupStream::poll_loading() {
    for (auto &gp : groups) {
        Group &g = *gp;
        if (!g.world || !g.world->is_loaded()) {
            continue;
        }
        if (g.loaded_at < 0.0) {
            g.loaded_at = now_s();
            // 近景 → 远景时存下的农田、采场、散布的账补回来
            if (g.memory.has_world) {
                if (Farm *farm = g.world->host_farm()) {
                    farm->restore_plots(g.memory.plots, g.memory.plots_at);
                }
                if (g.memory.stocks.size() == g.world->get_resource_stocks().size()) {
                    g.world->set_resource_stocks(g.memory.stocks);
                }
                g.world->get_scatter_ledger() = g.memory.scatter;
            }
        }
        // 地面块挂齐了才让 FarGroups 藏起粗版（锚群 FarGroups 自己不画）
        const bool show_own = g.world->get_tile_count() > 0 && g.world->get_pending_count() == 0;
        if (show_own && !g.far_hidden) {
            set_far_hidden(g, true);
        }
    }
}

void GroupStream::set_far_hidden(Group &r_group, bool p_hidden) {
    r_group.far_hidden = p_hidden;
    if (far_groups) {
        far_groups->set_hidden(r_group.node, p_hidden);
    }
}

Transform3D GroupStream::placement_for(const Group &p_group) const {
    const Group *a = anchor_group();
    if (!a || a == &p_group) {
        return Transform3D();
    }
    // 焦点处的线性近似：焦点在这一群平面里是 q，这一群的平面轴在锚群平面里转了 θ（只绕 y：两边的高度都沿径向）
    const PlanetVec q = rebase_point(a->frame, p_group.frame, pv(focus));
    const double theta = yaw_of(rebase_rotation(p_group.frame, a->frame, q));
    const Basis basis(Vector3(0, 1, 0), real_t(theta));
    const Vector3 qh(real_t(q.x), 0.0f, real_t(q.z));
    Vector3 origin = Vector3(focus.x, 0.0f, focus.z) - basis.xform(qh);
    origin.y = 0.0f;
    return Transform3D(basis, origin);
}

void GroupStream::place(Group &r_group, const Transform3D &p_placement) {
    r_group.placement = p_placement;
    for (Node3D *n : { static_cast<Node3D *>(r_group.world), static_cast<Node3D *>(r_group.site_stream), static_cast<Node3D *>(r_group.scatter_stream) }) {
        if (n) {
            n->set_transform(p_placement);
        }
    }
}

void GroupStream::update_placements(bool p_force) {
    const Group *a = anchor_group();
    for (auto &gp : groups) {
        Group &g = *gp;
        if (!g.world) {
            continue;
        }
        if (&g == a) {
            if (!g.placement.is_equal_approx(Transform3D())) {
                place(g, Transform3D());
            }
            continue;
        }
        if (!p_force && g.placed && Vector2(focus.x - g.placed_at.x, focus.z - g.placed_at.z).length() < place_step) {
            continue;
        }
        g.placed_at = focus;
        g.placed = true;
        place(g, placement_for(g));
    }
}

void GroupStream::feed_focus() {
    // 邻群的地面块按焦点在它平面里的位置选块（近处细、远处粗）；村子、散布不接（只到近景）
    const Group *a = anchor_group();
    for (auto &gp : groups) {
        Group &g = *gp;
        if (&g == a || !g.world || !g.world->is_loaded()) {
            continue;
        }
        g.world->set_focus(v3(to_group(g, focus)));
    }
}

void GroupStream::update_anchor_choice() {
    Group *a = anchor_group();
    if (pending >= 0 || !a || !a->world) {
        return;
    }
    Group *best = nullptr;
    for (auto &gp : groups) {
        Group &g = *gp;
        if (&g == a || g.tier != TIER_DETAIL || !g.world || !g.world->is_loaded()) {
            continue;
        }
        if (!best || g.land_distance < best->land_distance) {
            best = &g;
        }
    }
    if (!best || best->land_distance + shift_margin >= a->land_distance) {
        return;
    }
    // 只在云海上空换：脚下两群都没有地
    const PlanetVec q = to_group(*best, focus);
    if (!std::isnan(a->world->get_surface_height(focus.x, focus.z)) || !std::isnan(best->world->get_surface_height(q.x, q.z))) {
        return;
    }
    pending = index_of(best);
}

bool GroupStream::begin_shift(GroupShift &r_shift) const {
    const Group *a = anchor_group(), *b = group_at(pending);
    if (!a || !b) {
        return false;
    }
    r_shift.from = a->frame;
    r_shift.to = b->frame;
    r_shift.from_node = a->node;
    r_shift.to_node = b->node;
    return true;
}

void GroupStream::commit_shift() {
    Group *a = anchor_group(), *b = group_at(pending);
    if (!a || !b || !b->world) {
        pending = -1;
        return;
    }
    focus = v3(rebase_point(a->frame, b->frame, pv(focus)));
    anchor = pending;
    pending = -1;
    // 新锚归位、挂上碰撞；旧锚按焦点摆上刚体变换、撤掉碰撞
    place(*b, Transform3D());
    b->placed = false;
    b->world->set_physics_on(true);
    a->world->set_physics_on(false);
    a->placed = false;
    update_placements(true);
    if (planet) {
        planet->set_anchor_world(b->world);
    }
    // 这一群的粗版 FarGroups 不再画（锚群）；旧锚的粗版照旧藏着（它的地面块还挂着）
    if (far_groups) {
        far_groups->set_hidden(b->node, true);
        far_groups->set_hidden(a->node, a->far_hidden);
    }
    shifts++;
    emit_signal("anchor_changed", a->node, b->node);
}

bool GroupStream::request_shift_toward(const Vector3 &p_anchor_point) {
    Group *g = group_near(p_anchor_point, 5000.0);
    Group *a = anchor_group();
    if (!g || g == a) {
        return false;
    }
    if (!g->world) {
        load_world(*g, true);
    }
    if (!g->world->is_loaded()) {
        g->world->finish_loading_now();
    }
    if (!g->world->is_loaded()) {
        return false;
    }
    if (g->tier != TIER_DETAIL) {
        load_detail(*g);
        g->tier = TIER_DETAIL;
    }
    pending = index_of(g);
    return true;
}

bool GroupStream::force_shift_to(int p_node) {
    Group *g = find(p_node);
    if (!g || g == anchor_group() || g->tier != TIER_DETAIL || !g->world || !g->world->is_loaded()) {
        return false;
    }
    pending = index_of(g);
    return true;
}

PackedInt32Array GroupStream::get_group_nodes() const {
    PackedInt32Array out;
    for (const auto &g : groups) {
        out.push_back(g->node);
    }
    return out;
}

Dictionary GroupStream::get_group_state(int p_node) const {
    Dictionary d;
    const Group *g = find(p_node);
    if (!g) {
        return d;
    }
    d["node"] = g->node;
    d["tier"] = int(g->tier);
    d["anchor"] = g == anchor_group();
    d["pending"] = index_of(g) == pending;
    d["land_km"] = g->land_distance / 1000.0;
    d["loaded"] = g->world && g->world->is_loaded();
    d["loading"] = g->world && g->world->is_loading();
    d["tiles"] = g->world ? g->world->get_tile_count() : 0;
    d["pending_tiles"] = g->world ? g->world->get_pending_count() : 0;
    d["hidden_far"] = g->far_hidden;
    d["detail"] = g->site_stream != nullptr;
    d["ledgers_saved"] = int(g->memory.ledgers.size());
    d["plots_saved"] = int(g->memory.plots.size());
    d["placement"] = g->placement;
    return d;
}

GroupWorld *GroupStream::get_group_world(int p_node) const {
    const Group *g = find(p_node);
    return g ? g->world : nullptr;
}

SiteStream *GroupStream::get_group_sites(int p_node) const {
    const Group *g = find(p_node);
    return g ? g->site_stream : nullptr;
}

Hinterland *GroupStream::get_group_hinterland(int p_node) const {
    const Group *g = find(p_node);
    return g ? g->hinterland : nullptr;
}

Dictionary GroupStream::get_stats() const {
    Dictionary d;
    int detail = 0, near = 0;
    for (const auto &g : groups) {
        detail += g->tier == TIER_DETAIL ? 1 : 0;
        near += g->tier == TIER_NEAR ? 1 : 0;
    }
    d["groups"] = int(groups.size());
    d["anchor"] = get_anchor_node();
    d["shifts"] = shifts;
    d["loads"] = loads;
    d["unloads"] = unloads;
    d["detail"] = detail;
    d["near"] = near;
    return d;
}
