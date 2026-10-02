#include "group_stream.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "far_groups.h"
#include "farm.h"
#include "group_provider.h"
#include "hinterland.h"
#include "planet_view.h"
#include "scatter_stream.h"
#include "shipyard.h"
#include "site_stream.h"
#include "planet_surface.h"
#include "world.h"
#include "world_clock.h"

using namespace godot;

namespace {

constexpr double WORKS_SAVE_S = 3.0; // 营造的账隔这么久看一回、变了就存

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

// 主线程上慢的几步（拆 / 建一群、换锚）报出来：环境变量 ZHOUZHU_STREAM_TRACE=1 时报超过 8 ms 的（查卡帧用）
bool trace_on() {
    static const int on = OS::get_singleton()->has_environment("ZHOUZHU_STREAM_TRACE") ? 1 : 0;
    return on != 0;
}

struct TraceStep {
    const char *what;
    int node;
    uint64_t t0;
    TraceStep(const char *p_what, int p_node) :
            what(p_what), node(p_node), t0(Time::get_singleton()->get_ticks_usec()) {}
    ~TraceStep() {
        const double ms = double(Time::get_singleton()->get_ticks_usec() - t0) / 1000.0;
        if (trace_on() && ms > 8.0) {
            print_line(vformat("GroupStream trace: %s #%d %.1f ms", what, node, ms));
        }
    }
};

constexpr double JUMP_REMEASURE_M = 2000.0; // 焦点一下挪了这么远：到各群陆地的距离当场重量
constexpr double JUMP_LOAD_TIMEOUT_S = 120.0;

// world.json 的 source（群心的经纬度，与 PlanetView 锚定用的同一个数）：它是第一个键，只读开头 2 KB，不整个解析（整份几百 KB 到几 MB）
Dictionary read_source(const String &p_dir) {
    Ref<FileAccess> f = FileAccess::open(p_dir.path_join("world.json"), FileAccess::READ);
    if (f.is_null()) {
        return Dictionary();
    }
    // 只解到第一个「}」（source 那一段）：2 KB 处可能正好切在一个汉字中间
    PackedByteArray bytes = f->get_buffer(2048);
    for (int64_t k = 0; k < bytes.size(); k++) {
        if (bytes[k] == '}') {
            bytes.resize(k + 1);
            break;
        }
    }
    const String head = bytes.get_string_from_utf8();
    const int a = head.find("\"source\"");
    const int b = a >= 0 ? head.find("{", a) : -1;
    const int e = b >= 0 ? head.find("}", b) : -1;
    if (e > b && b > a) {
        const Variant v = JSON::parse_string(head.substr(b, e - b + 1));
        if (v.get_type() == Variant::DICTIONARY) {
            return v;
        }
    }
    const Variant all = JSON::parse_string(FileAccess::get_file_as_string(p_dir.path_join("world.json")));
    return all.get_type() == Variant::DICTIONARY ? Dictionary(Dictionary(all).get("source", Dictionary())) : Dictionary();
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
    ClassDB::bind_method(D_METHOD("to_anchor", "node", "group_point"), &GroupStream::to_anchor);
    ClassDB::bind_method(D_METHOD("from_anchor", "node", "anchor_point"), &GroupStream::from_anchor);
    ClassDB::bind_method(D_METHOD("get_group_sites", "node"), &GroupStream::get_group_sites);
    ClassDB::bind_method(D_METHOD("get_group_hinterland", "node"), &GroupStream::get_group_hinterland);
    ClassDB::bind_method(D_METHOD("get_shift_count"), &GroupStream::get_shift_count);
    ClassDB::bind_method(D_METHOD("is_shift_pending"), &GroupStream::is_shift_pending);
    ClassDB::bind_method(D_METHOD("force_shift_to", "node"), &GroupStream::force_shift_to);
    ClassDB::bind_method(D_METHOD("get_jump_node"), &GroupStream::get_jump_node);
    ClassDB::bind_method(D_METHOD("get_graveyard_size"), &GroupStream::get_graveyard_size);
    ClassDB::bind_method(D_METHOD("get_jump_state"), &GroupStream::get_jump_state);
    ClassDB::bind_method(D_METHOD("save_world"), &GroupStream::save_world);
    ClassDB::bind_method(D_METHOD("_on_group_ready", "node", "dir"), &GroupStream::on_group_ready);
    ClassDB::bind_method(D_METHOD("set_provider_path", "value"), &GroupStream::set_provider_path);
    ClassDB::bind_method(D_METHOD("get_provider_path"), &GroupStream::get_provider_path);
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
    ClassDB::bind_method(D_METHOD("set_far_anchor_m", "value"), &GroupStream::set_far_anchor_m);
    ClassDB::bind_method(D_METHOD("get_far_anchor_m"), &GroupStream::get_far_anchor_m);
    ClassDB::bind_method(D_METHOD("set_far_anchor_gain_m", "value"), &GroupStream::set_far_anchor_gain_m);
    ClassDB::bind_method(D_METHOD("get_far_anchor_gain_m"), &GroupStream::get_far_anchor_gain_m);
    ClassDB::bind_method(D_METHOD("set_far_anchor_speed", "value"), &GroupStream::set_far_anchor_speed);
    ClassDB::bind_method(D_METHOD("get_far_anchor_speed"), &GroupStream::get_far_anchor_speed);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "planet_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PlanetView"), "set_planet_path", "get_planet_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "home_path"), "set_home_path", "get_home_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "far_groups_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FarGroups"), "set_far_groups_path", "get_far_groups_path");
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "provider_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "GroupProvider"), "set_provider_path", "get_provider_path");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "worlds_dir", PROPERTY_HINT_DIR), "set_worlds_dir", "get_worlds_dir");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "settlements_dir", PROPERTY_HINT_DIR), "set_settlements_dir", "get_settlements_dir");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_in", PROPERTY_HINT_RANGE, "1000,200000,100,suffix:m"), "set_detail_in", "get_detail_in");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_out", PROPERTY_HINT_RANGE, "1000,200000,100,suffix:m"), "set_detail_out", "get_detail_out");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near_in", PROPERTY_HINT_RANGE, "1000,300000,100,suffix:m"), "set_near_in", "get_near_in");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near_out", PROPERTY_HINT_RANGE, "1000,300000,100,suffix:m"), "set_near_out", "get_near_out");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shift_margin", PROPERTY_HINT_RANGE, "0,20000,10,suffix:m"), "set_shift_margin", "get_shift_margin");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far_anchor_m", PROPERTY_HINT_RANGE, "10000,2000000,1000,suffix:m"), "set_far_anchor_m", "get_far_anchor_m");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far_anchor_gain_m", PROPERTY_HINT_RANGE, "0,1000000,1000,suffix:m"), "set_far_anchor_gain_m", "get_far_anchor_gain_m");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "far_anchor_speed", PROPERTY_HINT_RANGE, "0,1000000,10,suffix:m/s"), "set_far_anchor_speed", "get_far_anchor_speed");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_shift"), "set_auto_shift", "get_auto_shift");
    ADD_SIGNAL(MethodInfo("anchor_changed", PropertyInfo(Variant::INT, "from_node"), PropertyInfo(Variant::INT, "to_node")));

    BIND_ENUM_CONSTANT(TIER_FAR);
    BIND_ENUM_CONSTANT(TIER_NEAR);
    BIND_ENUM_CONSTANT(TIER_DETAIL);
}

void GroupStream::_notification(int p_what) {
    if (p_what == NOTIFICATION_EXIT_TREE) {
        // 后台在登记的群（读着 PlanetView 的行星包）：场景拆之前等它们做完
        for (auto &it : adding) {
            if (it.second.valid()) {
                it.second.wait();
            }
        }
        adding.clear();
    }
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
    provider = Object::cast_to<GroupProvider>(get_node_or_null(provider_path));
    const PlanetData *pd = planet->get_planet();

    // 有哪些群的包：GroupProvider 说（仓库里的 + 世界存档目录里的，行星计划 P4）；没有它就照 P3 看 worlds_dir 下以节点号命名的
    std::vector<int> nodes;
    if (provider) {
        const PackedInt32Array a = provider->available_nodes();
        for (int64_t k = 0; k < a.size(); k++) {
            nodes.push_back(a[k]);
        }
    } else {
        const PackedStringArray dirs = DirAccess::get_directories_at(worlds_dir);
        for (int64_t k = 0; k < dirs.size(); k++) {
            const String name = dirs[k];
            const String dir = worlds_dir.path_join(name);
            if (name.is_valid_int() && FileAccess::file_exists(dir.path_join("terrain.bin")) && FileAccess::file_exists(dir.path_join("world.json"))) {
                nodes.push_back(int(name.to_int()));
            }
        }
    }
    home_node = home->get_node_id();
    if (std::find(nodes.begin(), nodes.end(), home_node) == nodes.end()) {
        nodes.push_back(home_node);
    }
    for (int node : nodes) {
        if (node < 0 || node >= pd->node_count()) {
            continue;
        }
        if (node == home_node) {
            // 场景里摆的那一群：节点在场景根下，已经载入，是锚、细版
            Group *g = add_group(node, home->get_data_dir());
            g->parent = home->get_parent();
            g->world = home;
            home->set_player_works(g->works); // 存档里的营造账换上（场景里的 World 开局自己建了一份空的）
            g->site_stream = home->get_site_stream();
            g->scatter_stream = home->get_scatter_stream();
            g->hinterland = g->site_stream ? g->site_stream->get_hinterland() : nullptr;
            g->tier = TIER_DETAIL;
            g->loaded_at = now_s();
            g->memory = GroupMemory(); // 上回存下的账 GroupProvider 开局就交给了场景里的账房、农田
            anchor = index_of(g);
        } else {
            add_group(node, provider ? provider->find_group(node) : worlds_dir.path_join(itos(node)));
        }
    }
    if (anchor < 0) {
        return false;
    }
    // 行星天气场（行星计划 P5）：PlanetView 在后台读逐日天气包，好了就挂到各群的天气模型上（attach_weather，每帧看一眼）
    attach_weather();
    if (provider) {
        provider->connect("group_ready", Callable(this, "_on_group_ready"));
    }
    return true;
}

GroupStream::Group *GroupStream::add_group(int p_node, const String &p_dir) {
    if (Group *found = find(p_node)) {
        return found;
    }
    return adopt_group(make_group(p_node, p_dir));
}

std::unique_ptr<GroupStream::Group> GroupStream::make_group(int p_node, const String &p_dir) const {
    const PlanetData *pd = planet->get_planet();
    PlanetShape shape;
    shape.sea_radius_m = pd->radius_km * 1000.0;
    std::unique_ptr<Group> g = std::make_unique<Group>();
    g->node = p_node;
    g->home = p_node == home_node;
    g->data_dir = p_dir;
    // 群心按 world.json 的 source（与 PlanetView 锚定用的同一个数）；读不到用行星包的
    double lat = pd->node(p_node).lat, lon = pd->node(p_node).lon;
    const Dictionary source = read_source(g->data_dir);
    if (source.has("lat") && source.has("lon")) {
        lat = double(source["lat"]);
        lon = double(source["lon"]);
    }
    g->frame = GroupFrame(lat, lon, shape);
    // 粗版的陆地格心：算焦点到这一群的陆地多远（生存模式还没有粗版的：替身）
    PlanetLod lod = pd->lod(p_node);
    if (lod.empty()) {
        lod = surface_stand_in(*pd, p_node, 2000.0);
    }
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
    // 上回离开时存进世界存档目录的账（退出再进还在）
    if (provider && !g->home) {
        provider->load_memory(p_node, g->memory);
    }
    // 玩家营造的账（切片十一）：场景里那一群也从存档读
    g->works = std::make_shared<GroundWorks>();
    if (provider && provider->load_works(p_node, *g->works)) {
        g->works_saved = save_ground_works(*g->works);
    }
    return g;
}

void GroupStream::save_works(Group &r_group, bool p_force) {
    if (!provider || !r_group.works) {
        return;
    }
    const std::string text = r_group.works->empty() ? std::string() : save_ground_works(*r_group.works);
    if (text == r_group.works_saved && !p_force) {
        return;
    }
    if (text != r_group.works_saved) {
        provider->save_works(r_group.node, text);
        r_group.works_saved = text;
    }
}

GroupStream::Group *GroupStream::adopt_group(std::unique_ptr<Group> p_group) {
    if (Group *found = find(p_group->node)) {
        return found;
    }
    groups.push_back(std::move(p_group));
    return groups.back().get();
}

void GroupStream::request_add(int p_node, const String &p_dir) {
    if (find(p_node) || adding.count(p_node)) {
        return;
    }
    adding[p_node] = std::async(std::launch::async, [this, p_node, p_dir]() { return make_group(p_node, p_dir); });
}

void GroupStream::poll_adding() {
    for (auto it = adding.begin(); it != adding.end();) {
        if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        adopt_group(it->second.get());
        it = adding.erase(it);
    }
}

void GroupStream::on_group_ready(int p_node, const String &p_dir) {
    if (!ready || find(p_node) || !planet || p_node < 0 || p_node >= planet->get_planet()->node_count() || p_node == jump_node) {
        return;
    }
    request_add(p_node, p_dir);
}

void GroupStream::finish_ledgers(Group &r_group, bool p_wait) {
    if (!r_group.ledgers_saving.valid()) {
        return;
    }
    if (!p_wait && r_group.ledgers_saving.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    for (const auto &entry : r_group.ledgers_saving.get()) {
        r_group.memory.ledgers[entry.first] = entry.second;
    }
    r_group.ledgers_saving = std::shared_future<std::map<int, std::string>>();
    save_memory(r_group);
}

void GroupStream::save_memory(const Group &p_group) const {
    if (provider) {
        provider->save_memory(p_group.node, p_group.memory);
    }
}

void GroupStream::save_world() {
    if (!ready || !provider) {
        return;
    }
    for (auto &gp : groups) {
        save_works(*gp, false);
        finish_ledgers(*gp, true);
        const Group &g = *gp;
        GroupMemory m = g.memory; // 离开时存下、还没补回去的照留
        if (g.hinterland) {
            g.hinterland->save_states(m.ledgers, m.ledgers_at);
        }
        if (g.world && g.world->is_loaded()) {
            m.has_world = true;
            m.plots.clear();
            if (Farm *farm = g.world->host_farm()) {
                m.plots = farm->get_model().plot_entries();
            }
            const WorldClock *clock = WorldClock::get_singleton();
            m.plots_at = clock ? clock->get_seconds() : 0.0;
            m.stocks = g.world->get_resource_stocks();
            m.scatter = g.world->get_scatter_ledger();
        }
        provider->save_memory(g.node, m);
    }
}

void GroupStream::attach_weather() {
    const std::shared_ptr<PlanetWeather> pw = planet ? planet->get_planet_weather() : nullptr;
    if (!pw) {
        return;
    }
    for (auto &gp : groups) {
        if (gp->world && !gp->world->has_planet_weather()) {
            gp->world->set_planet_weather(pw, gp->node, gp->frame);
        }
    }
}

void GroupStream::set_focus(const Vector3 &p_focus) {
    // 坏了的位置（NaN）不认：按它换算到别的群会落在群心上（AEQD 的逆对 NaN 给 0），把邻群错载进来
    if (!p_focus.is_finite()) {
        return;
    }
    // 一下挪远了（瞬移、跨群瞬移、换锚后摆到落点）：到各群陆地的距离当场重量——天气的混法、HUD 按它，
    // 不然这一帧还按上回（0.25 s 一量）的，瞬移那一帧的天是离开的那一群的（行星计划 P4 查出来的：落地后几秒还阴着、下着那边的雨）
    const bool jumped = ready && has_focus && focus.distance_to(p_focus) > JUMP_REMEASURE_M;
    focus = p_focus;
    has_focus = true;
    if (jumped) {
        for (auto &g : groups) {
            g->land_distance = land_distance(*g, focus);
        }
        since_check = check_interval; // 下一帧就重新分档
    }
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
    sim_time += p_delta;
    // 平面坐标过了对跖点：每帧都看（高空一秒几百公里，等 0.25 s 一回的分档会越过几十公里）
    if (auto_shift && pending < 0 && a && a->world && !canonical_focus()) {
        pending = anchor;
    }
    if (since_check >= check_interval) {
        since_check = 0.0;
        measure_speed();
        update_tiers();
        if (auto_shift) {
            update_anchor_choice();
        }
    }
    poll_loading();
    poll_adding();
    attach_weather();
    for (auto &gp : groups) {
        finish_ledgers(*gp, false);
    }
    // 营造的账（改地形、搭建件、摆件、木箱里的东西）：隔几秒看一回，变了就存（只看载入着的群，拆了的在拆的时候存过）
    works_save_timer += p_delta;
    if (works_save_timer >= WORKS_SAVE_S) {
        works_save_timer = 0.0;
        for (auto &gp : groups) {
            if (gp->world) {
                save_works(*gp, false);
            }
        }
    }
    update_jump();
    update_placements(false);
    feed_focus();
    reap();
}

void GroupStream::bury(Node *p_node) {
    // 停下（不再 _process / _physics_process）、藏起来（邻群的还在地平线上）、改名（同名的新节点照旧叫 World……），等 reap 一点点拆
    p_node->set_process_mode(PROCESS_MODE_DISABLED);
    if (Node3D *n3 = Object::cast_to<Node3D>(p_node)) {
        n3->set_visible(false);
    }
    p_node->set_name(vformat("Buried%d_%s", ++buried, String(p_node->get_name())));
    graveyard.push_back(ObjectID(p_node->get_instance_id()));
}

void GroupStream::reap() {
    // 先埋的先拆（账房在 World 前面）；每回摘最深的最后一个叶子（出树只通知它自己），摘完根再放它
    const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
    const uint64_t budget = uint64_t(reap_budget_ms * 1000.0);
    int count = 0;
    while (!graveyard.empty()) {
        Node *root = Object::cast_to<Node>(ObjectDB::get_instance(graveyard.front()));
        if (!root) {
            graveyard.erase(graveyard.begin());
            continue;
        }
        // 账房后台还在开账（读着这一群的地形）：等它们做完再拆（出树时它会等；排在它后面的 World 也跟着等）
        if (const Hinterland *h = Object::cast_to<Hinterland>(root); h && h->openings_pending()) {
            break;
        }
        Node *leaf = root;
        while (leaf->get_child_count() > 0) {
            leaf = leaf->get_child(leaf->get_child_count() - 1);
        }
        const uint64_t t1 = Time::get_singleton()->get_ticks_usec();
        if (Node *parent = leaf->get_parent()) {
            parent->remove_child(leaf);
        }
        leaf->queue_free();
        const uint64_t t2 = Time::get_singleton()->get_ticks_usec();
        if (trace_on() && t2 - t1 > 4000) {
            print_line(vformat("GroupStream trace: reap %s (%s) %.1f ms", String(leaf->get_name()), leaf->get_class(), double(t2 - t1) / 1000.0));
        }
        if (leaf == root) {
            graveyard.erase(graveyard.begin());
        }
        count++;
        if (t2 - t0 > budget) {
            break;
        }
    }
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
    bool dropped = false; // 这一回已经降了一群（村子的账存成文本、摘节点要一二十毫秒）：别的下一回（0.25 s 后）再降
    for (Group *g : order) {
        Tier want = TIER_FAR;
        if (g == a || index_of(g) == pending || g->pinned) {
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
            const bool drop = want < g->tier && (g->world || g->site_stream);
            if (drop && (dropped || now_s() < drop_hold_until)) {
                continue;
            }
            dropped = dropped || drop;
            set_tier(*g, want);
        }
    }
}

void GroupStream::set_tier(Group &r_group, Tier p_tier) {
    TraceStep trace(p_tier == TIER_FAR ? "set_tier far" : (p_tier == TIER_NEAR ? "set_tier near" : "set_tier detail"), r_group.node);
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
    w->set_player_works(r_group.works); // 这一群的营造账（拆了又载入照旧是同一份）
    if (const std::shared_ptr<PlanetWeather> pw = planet->get_planet_weather()) {
        w->set_planet_weather(pw, r_group.node, r_group.frame); // 行星天气场：载入时挂到它的天气模型上
    }
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
        TraceStep t("world memory", r_group.node);
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
        save_memory(r_group);
    }
    save_works(r_group, true); // 营造的账留在内存（下回载入交回去），也存进存档
    set_far_hidden(r_group, false);
    // World 交给坟地（它的地面块、水面、农田、船坞一帧拆一点；村子那些已经先埋了，先拆）；别的群的容器随它一起埋
    if (r_group.container) {
        bury(r_group.container);
        r_group.container = nullptr;
    } else {
        bury(w);
    }
    r_group.world = nullptr;
    r_group.loaded_at = -1.0;
    r_group.placed = false;
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
    finish_ledgers(r_group, true);
    if (!r_group.memory.ledgers.empty()) {
        h->set_saved(r_group.memory.ledgers, r_group.memory.ledgers_at);
        r_group.memory.ledgers.clear();
        save_memory(r_group); // 交给账房了：存档里只剩农田那一份（没有就删）
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
        TraceStep t("retire_folk", r_group.node);
        r_group.site_stream->retire_folk();
    }
    if (r_group.hinterland) {
        // 存成文本在后台做（大群几百本账要五六十毫秒）；做完 finish_ledgers 并进来、存进存档目录
        TraceStep t("save_states_async", r_group.node);
        finish_ledgers(r_group, true); // 上一回的还没做完（很少）：先等它
        r_group.ledgers_saving = r_group.hinterland->save_states_async(r_group.memory.ledgers_at);
    }
    for (Node *n : { static_cast<Node *>(r_group.hinterland), static_cast<Node *>(r_group.scatter_stream), static_cast<Node *>(r_group.site_stream) }) {
        if (n) {
            bury(n);
        }
    }
    r_group.site_stream = nullptr;
    r_group.scatter_stream = nullptr;
    r_group.hinterland = nullptr;
}

void GroupStream::poll_loading() {
    TraceStep trace("poll_loading", -1);
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
                // 补回去了：内存里、存档里都不再留这一份
                g.memory.has_world = false;
                g.memory.plots.clear();
                g.memory.stocks.clear();
                g.memory.scatter = ScatterLedger();
                save_memory(g);
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
        if (g.node == jump_node && jump_landing_set) {
            g.world->set_focus(jump_spawn.origin); // 瞬移要去的：落点周围的地面块先建好（它自己的平面）
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
    // 平面坐标规范化：焦点在锚群平面里远过对跖点（高空绕着行星转了大半圈）——换到锚群自己，坐标回到半圈以内（编排照换锚那样换人船）
    if (!canonical_focus()) {
        pending = anchor;
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
    if (best) {
        const PlanetVec q = to_group(*best, focus);
        const bool on_anchor = !std::isnan(a->world->get_surface_height(focus.x, focus.z));
        const bool on_best = !std::isnan(best->world->get_surface_height(q.x, q.z));
        // 行星计划 P4 补的规矩（挨着的群）：脚下是别的群的地、不是锚群的地——换过去，不等云海上空、不看回滞（粗版量的陆地距离差一两公里，真地形说了算）；
        // 两群的地叠着（脚下两群都有地）不换，锚群照旧
        if (on_best && !on_anchor) {
            pending = index_of(best);
            return;
        }
        // 只在云海上空换：脚下两群都没有地
        if (best->land_distance + shift_margin < a->land_distance && !on_anchor && !on_best) {
            pending = index_of(best);
            return;
        }
    }
    update_far_anchor();
}

bool GroupStream::canonical_focus() const {
    const Group *a = anchor_group();
    if (!a) {
        return true;
    }
    // 过了对跖点 1 km 才换（回滞：换过去以后在对跖点以内 1 km，要再往外挪 2 km 才又换）
    return std::hypot(double(focus.x), double(focus.z)) <= Math_PI * a->frame.radius + 1000.0;
}

void GroupStream::measure_speed() {
    const Group *a = anchor_group();
    if (!a || !focus.is_finite()) {
        return;
    }
    // 在行星系里量（换锚、规范化时平面坐标跳，真球面上的位置不跳）；只看水平
    const PlanetVec u = a->frame.to_planet(PlanetVec(focus.x, 0.0, focus.z)).normalized();
    if (speed_ref_t >= 0.0 && sim_time > speed_ref_t) {
        focus_speed = (u - speed_ref).length() * a->frame.radius / (sim_time - speed_ref_t);
    }
    speed_ref = u;
    speed_ref_t = sim_time;
}

void GroupStream::drop_far_target() {
    if (Group *t = group_at(far_target)) {
        if (t->node != jump_node) {
            t->pinned = false; // 不再为它留细版：照距离分档（多半降回远景）
        }
    }
    far_target = -1;
}

void GroupStream::update_far_anchor() {
    Group *a = anchor_group();
    // 离锚群的陆地还不远、平着挪得快（高空巡航：一路上一群群地载入不划算，只做规范化）、正在瞬移：不换
    if (!a || jump_node >= 0 || a->land_distance <= far_anchor_m || focus_speed >= far_anchor_speed) {
        drop_far_target();
        return;
    }
    // 认得的群里离焦点的陆地最近的（按真球面量，update_tiers 刚量过）
    Group *best = nullptr;
    for (auto &gp : groups) {
        Group &g = *gp;
        if (&g != a && (!best || g.land_distance < best->land_distance)) {
            best = &g;
        }
    }
    // 焦点下面最近的那一群还没有包：向 GroupProvider 要（生成好了登记进来，下一回就挑得上它）；
    // 它比认得的群都近、正在生成，就等它，不先载入远处的一群再换一回
    if (provider && planet) {
        double lat = 0.0, lon = 0.0;
        a->frame.flat_latlon(pv(focus), lat, lon);
        const PlanetData *pd = planet->get_planet();
        const int n = pd->nearest_node(lat, lon);
        if (n >= 0 && !find(n) && !adding.count(n) && provider->can_generate()) {
            const int state = provider->request(n, GroupProvider::NEAR);
            const double d = great_circle_m(lat, lon, pd->node(n).lat, pd->node(n).lon, a->frame.radius);
            if ((state == GroupProvider::QUEUED || state == GroupProvider::RUNNING) && (!best || d + far_anchor_gain_m < best->land_distance)) {
                drop_far_target();
                return;
            }
        }
    }
    if (!best || best->land_distance + far_anchor_gain_m > a->land_distance) {
        drop_far_target();
        return;
    }
    if (group_at(far_target) != best) {
        drop_far_target();
        far_target = index_of(best);
        best->pinned = true; // update_tiers 给它细版：先后台载入，载入完了挂上村子、散布
        if (!best->world) {
            load_world(*best, false);
            best->tier = TIER_NEAR;
        }
    }
    if (best->world && best->world->is_loaded() && best->tier == TIER_DETAIL) {
        pending = far_target; // 不看脚下有没有地：离锚群已经很远了
    }
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
    TraceStep trace("commit_shift", pending >= 0 ? groups[size_t(pending)]->node : -1);
    Group *a = anchor_group(), *b = group_at(pending);
    if (!a || !b || !b->world) {
        pending = -1;
        return;
    }
    focus = v3(rebase_point(a->frame, b->frame, pv(focus)));
    if (a == b) {
        // 平面坐标规范化（换到锚群自己）：锚、碰撞、PlanetView 都不动；邻群按新写法的焦点重摆
        pending = -1;
        rebases++;
        update_placements(true);
        return;
    }
    if (group_at(far_target) == b) {
        far_shifts++;
        drop_far_target(); // 成了锚：锚本来就是细版
    }
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
    drop_hold_until = now_s() + 2.0;
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

// ---------------------------------------------------------------- 跨群瞬移（行星计划 P4）

bool GroupStream::jump_begin(int p_node, String &r_why) {
    if (!ready) {
        r_why = String(U"岛群的流式还没就绪");
        return false;
    }
    if (p_node < 0 || p_node >= planet->get_planet()->node_count()) {
        r_why = vformat(String(U"没有群 #%d"), p_node);
        return false;
    }
    if (p_node == get_anchor_node()) {
        r_why = vformat(String(U"就在群 #%d"), p_node);
        return false;
    }
    jump_end();
    jump_node = p_node;
    jump_error = String();
    jump_landing_set = false;
    jump_started = now_s();
    if (!find(p_node)) {
        if (!provider) {
            r_why = String(U"这个场景没有 GroupProvider，只能去认得的群");
            jump_node = -1;
            return false;
        }
        const String dir = provider->find_group(p_node);
        if (dir.is_empty()) {
            const String problem = provider->get_problem();
            if (!problem.is_empty()) {
                r_why = vformat(String(U"群 #%d 还没生成过，生成不了：%s"), p_node, problem);
                jump_node = -1;
                return false;
            }
            provider->request(p_node, GroupProvider::URGENT);
            jump_phase = JUMP_GENERATING;
            return true;
        }
        request_add(p_node, dir);
        jump_phase = JUMP_GENERATING; // 包已经在：等后台登记完（update_jump 接着建 World）
        return true;
    }
    jump_phase = JUMP_LOADING;
    Group *g = find(p_node);
    g->pinned = true;
    if (!g->world) {
        load_world(*g, false); // 后台载入
        g->tier = TIER_NEAR;
    }
    return true;
}

void GroupStream::update_jump() {
    if (jump_node < 0) {
        return;
    }
    TraceStep trace("update_jump", jump_node);
    if (jump_phase == JUMP_GENERATING) {
        const Dictionary r = provider ? provider->get_request(jump_node) : Dictionary();
        const int state = int(r.get("state", 0));
        if (state == GroupProvider::FAILED) {
            jump_phase = JUMP_FAILED;
            jump_error = r.get("error", String(U"生成失败"));
            return;
        }
        if (state != GroupProvider::READY) {
            return;
        }
        Group *g = find(jump_node);
        if (!g) {
            request_add(jump_node, provider->find_group(jump_node)); // 后台登记（sites.tsv、粗版），好了下一帧再建 World
            return;
        }
        g->pinned = true;
        if (!g->world) {
            load_world(*g, false);
            g->tier = TIER_NEAR;
        }
        jump_phase = JUMP_LOADING;
        return;
    }
    if (jump_phase != JUMP_LOADING) {
        return;
    }
    Group *g = find(jump_node);
    if (!g || !g->world) {
        jump_phase = JUMP_FAILED;
        jump_error = String(U"要去的群没载入起来");
        return;
    }
    if (!g->world->is_loaded()) {
        if (now_s() - jump_started > JUMP_LOAD_TIMEOUT_S) {
            jump_phase = JUMP_FAILED;
            jump_error = String(U"载入超时");
        }
        return;
    }
    if (!jump_landing_set) {
        if (!find_landing(*g)) {
            jump_phase = JUMP_FAILED;
            jump_error = String(U"这一群没有码头、也没有村子，找不到落点");
            return;
        }
        jump_landing_set = true;
        g->world->set_focus(jump_spawn.origin);
    }
    if (g->tier != TIER_DETAIL) {
        load_detail(*g);
        g->tier = TIER_DETAIL;
    }
    // 落点周围的叶块（带碰撞）挂齐了才算好
    if (g->world->is_ready_at(jump_spawn.origin)) {
        jump_phase = JUMP_READY;
    }
}

bool GroupStream::find_landing(Group &r_group) {
    GroupWorld *w = r_group.world;
    const WorldSites &s = w->get_sites();
    // 主码头；没有能靠的主码头就取第一个能靠的码头
    int pick = -1;
    for (int k = 0; k < w->get_dock_count() && k < int(s.docks.size()); k++) {
        if (s.docks[size_t(k)].main && w->is_dock_berthable(k)) {
            pick = k;
            break;
        }
    }
    for (int k = 0; pick < 0 && k < w->get_dock_count(); k++) {
        if (w->is_dock_berthable(k)) {
            pick = k;
        }
    }
    if (pick >= 0) {
        jump_spawn = w->get_dock_spawn(pick);
        jump_berth = w->get_dock_berth(pick);
        jump_has_berth = true;
        jump_where = vformat(String(U"%s #%d"), s.docks[size_t(pick)].main ? String(U"主码头") : String(U"码头"), s.docks[size_t(pick)].id);
        return true;
    }
    // 没有码头：邑治（没有邑治就第一个村）的村心，按地面高
    const SiteSettlement *seat = nullptr;
    for (const SiteSettlement &v : s.settlements) {
        if (v.seat && !v.hamlet) {
            seat = &v;
            break;
        }
    }
    if (!seat) {
        for (const SiteSettlement &v : s.settlements) {
            if (!v.hamlet) {
                seat = &v;
                break;
            }
        }
    }
    if (!seat) {
        // Uninhabited groups are valid in the new settlement model. Developer
        // jumps land on natural ground without fabricating a dock or village.
        const auto &terrain=w->get_terrain();
        const auto &grid=terrain.get_grid();
        size_t best=grid.height.size();
        double score=-1;
        for(size_t k=0;k<grid.height.size();k++) {
            if(grid.shore[k]<0.5*grid.res || grid.island[k]!=0) continue;
            const double x=grid.x0+double(k%size_t(grid.cols))*grid.res;
            const double z=grid.z0+double(k/size_t(grid.cols))*grid.res;
            const double candidate=grid.shore[k]/(1+std::hypot(x,z)/grid.res);
            if(candidate<=score) continue;
            const auto p=terrain.sample(x,z);
            if(!p.land || p.bank>0) continue;
            const double dx=terrain.sample(x+2,z).top-terrain.sample(x-2,z).top;
            const double dz=terrain.sample(x,z+2).top-terrain.sample(x,z-2).top;
            if(std::hypot(dx,dz)>2.0) continue;
            if(candidate>score) { score=candidate; best=k; }
        }
        if(best==grid.height.size()) return false;
        const double x=grid.x0+double(best%size_t(grid.cols))*grid.res;
        const double z=grid.z0+double(best/size_t(grid.cols))*grid.res;
        jump_spawn=Transform3D(Basis(),Vector3(real_t(x),real_t(terrain.sample(x,z).top),real_t(z)));
        jump_has_berth=false;
        jump_where=String(U"无人岛自然落点");
        return true;
    }
    // 村心可能正是堂屋：往外挪到第一块没有房子的平地（按地面高、离村心 30–120 m）
    Vector3 at(real_t(seat->x), 0.0f, real_t(seat->z));
    for (double r = 30.0; r <= 120.0; r += 15.0) {
        const double y = w->get_surface_height(seat->x + r, seat->z);
        if (!std::isnan(y)) {
            at = Vector3(real_t(seat->x + r), real_t(y), real_t(seat->z));
            break;
        }
    }
    if (at.y == 0.0f) {
        const double y = w->get_surface_height(seat->x, seat->z);
        at.y = real_t(std::isnan(y) ? 0.0 : y);
    }
    jump_spawn = Transform3D(Basis(), at);
    jump_berth = Transform3D();
    jump_has_berth = false;
    jump_where = vformat(String(U"邑治 %s"), String::utf8(seat->name.c_str()));
    return true;
}

int GroupStream::jump_poll(double &r_progress, String &r_text) {
    if (jump_node < 0) {
        r_progress = 0.0;
        r_text = String();
        return -1;
    }
    update_jump();
    switch (jump_phase) {
        case JUMP_GENERATING: {
            const Dictionary r = provider ? provider->get_request(jump_node) : Dictionary();
            r_progress = 0.7 * double(r.get("progress", 0.0));
            const int state = int(r.get("state", 0));
            r_text = state == GroupProvider::QUEUED ? vformat(String(U"群 #%d 排队等生成（前面还在生成别的群）"), jump_node)
                    : state == GroupProvider::READY  ? vformat(String(U"读群 #%d 的包"), jump_node)
                                                     : vformat(String(U"生成群 #%d：%s"), jump_node, String(r.get("text", String())));
            return 0;
        }
        case JUMP_LOADING: {
            const Group *g = find(jump_node);
            const bool loaded = g && g->world && g->world->is_loaded();
            r_progress = loaded ? 0.9 : 0.75;
            r_text = loaded ? vformat(String(U"群 #%d：建落点（%s）周围的地面"), jump_node, jump_where) : vformat(String(U"载入群 #%d"), jump_node);
            return 0;
        }
        case JUMP_READY:
            r_progress = 1.0;
            r_text = vformat(String(U"群 #%d 好了"), jump_node);
            return 1;
        case JUMP_FAILED:
        default:
            r_progress = 0.0;
            r_text = jump_error;
            return -1;
    }
}

bool GroupStream::jump_commit() {
    Group *g = find(jump_node);
    if (jump_phase != JUMP_READY || !g || !g->world || !g->world->is_loaded()) {
        return false;
    }
    pending = index_of(g);
    return true;
}

bool GroupStream::jump_landing(Transform3D &r_spawn, Transform3D &r_berth, bool &r_has_berth, String &r_where) {
    const Group *g = find(jump_node);
    if (!g || !jump_landing_set) {
        return false;
    }
    r_spawn = transform_from_group(*g, jump_spawn);
    r_berth = transform_from_group(*g, jump_berth);
    r_has_berth = jump_has_berth;
    r_where = jump_where;
    return true;
}

void GroupStream::jump_end() {
    if (Group *g = find(jump_node)) {
        g->pinned = false;
    }
    jump_node = -1;
    jump_phase = JUMP_NONE;
    jump_landing_set = false;
}

Dictionary GroupStream::get_jump_state() const {
    Dictionary d;
    d["node"] = jump_node;
    d["phase"] = int(jump_phase);
    d["error"] = jump_error;
    d["where"] = jump_where;
    d["seconds"] = jump_node >= 0 ? now_s() - jump_started : 0.0;
    return d;
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
    const_cast<GroupStream *>(this)->finish_ledgers(*const_cast<Group *>(g), true); // 后台还在存的账：等它存完再报
    d["ledgers_saved"] = int(g->memory.ledgers.size());
    d["plots_saved"] = int(g->memory.plots.size());
    d["placement"] = g->placement;
    return d;
}

Vector3 GroupStream::to_anchor(int p_node, const Vector3 &p_group_point) const {
    const Group *g = find(p_node);
    return g ? from_group(*g, pv(p_group_point)) : p_group_point;
}

Vector3 GroupStream::from_anchor(int p_node, const Vector3 &p_anchor_point) const {
    const Group *g = find(p_node);
    return g ? v3(to_group(*g, p_anchor_point)) : p_anchor_point;
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
    d["rebases"] = rebases;
    d["far_shifts"] = far_shifts;
    d["far_target"] = group_at(far_target) ? group_at(far_target)->node : -1;
    d["focus_speed"] = focus_speed;
    d["loads"] = loads;
    d["unloads"] = unloads;
    d["detail"] = detail;
    d["near"] = near;
    return d;
}
