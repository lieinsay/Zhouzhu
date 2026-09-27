#include "group_provider.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "expedition.h"
#include "farm.h"
#include "group_stream.h"
#include "hinterland.h"
#include "planet_view.h"
#include "site_stream.h"
#include "world.h"
#include "world_clock.h"
#include "world_sites.h"

using namespace godot;

namespace {

constexpr double PREFETCH_EVERY_S = 1.0;
constexpr double LODS_EVERY_S = 2.0;
constexpr double TRACE_S = 3.0; // 船头方向按焦点近 3 秒的挪动算

String abs_path(const String &p_path) {
    return ProjectSettings::get_singleton()->globalize_path(p_path);
}

bool has_package(const String &p_dir) {
    return FileAccess::file_exists(p_dir.path_join("terrain.bin")) && FileAccess::file_exists(p_dir.path_join("world.json")) &&
            FileAccess::file_exists(p_dir.path_join("sites.tsv"));
}

double angle_diff(double p_a, double p_b) {
    double d = std::fmod(std::fabs(p_a - p_b), 360.0);
    return d > 180.0 ? 360.0 - d : d;
}

} // namespace

void GroupProvider::_bind_methods() {
    ClassDB::bind_method(D_METHOD("find_group", "node"), &GroupProvider::find_group);
    ClassDB::bind_method(D_METHOD("is_available", "node"), &GroupProvider::is_available);
    ClassDB::bind_method(D_METHOD("available_nodes"), &GroupProvider::available_nodes);
    ClassDB::bind_method(D_METHOD("can_generate"), &GroupProvider::can_generate);
    ClassDB::bind_method(D_METHOD("get_problem"), &GroupProvider::get_problem);
    ClassDB::bind_method(D_METHOD("request", "node", "priority"), &GroupProvider::request);
    ClassDB::bind_method(D_METHOD("get_request", "node"), &GroupProvider::get_request);
    ClassDB::bind_method(D_METHOD("get_group_info", "node"), &GroupProvider::get_group_info);
    ClassDB::bind_method(D_METHOD("get_stats"), &GroupProvider::get_stats);
    ClassDB::bind_method(D_METHOD("get_world"), &GroupProvider::get_world);
    ClassDB::bind_method(D_METHOD("set_world_dir", "value"), &GroupProvider::set_world_dir);
    ClassDB::bind_method(D_METHOD("get_world_dir"), &GroupProvider::get_world_dir);
    ClassDB::bind_method(D_METHOD("set_repo_dir", "value"), &GroupProvider::set_repo_dir);
    ClassDB::bind_method(D_METHOD("get_repo_dir"), &GroupProvider::get_repo_dir);
    ClassDB::bind_method(D_METHOD("set_story", "value"), &GroupProvider::set_story);
    ClassDB::bind_method(D_METHOD("get_story"), &GroupProvider::get_story);
    ClassDB::bind_method(D_METHOD("set_gen_run", "value"), &GroupProvider::set_gen_run);
    ClassDB::bind_method(D_METHOD("get_gen_run"), &GroupProvider::get_gen_run);
    ClassDB::bind_method(D_METHOD("set_full_lods", "value"), &GroupProvider::set_full_lods);
    ClassDB::bind_method(D_METHOD("get_full_lods"), &GroupProvider::get_full_lods);
    ClassDB::bind_method(D_METHOD("set_prefetch", "value"), &GroupProvider::set_prefetch);
    ClassDB::bind_method(D_METHOD("get_prefetch"), &GroupProvider::get_prefetch);
    ClassDB::bind_method(D_METHOD("set_prefetch_radius", "value"), &GroupProvider::set_prefetch_radius);
    ClassDB::bind_method(D_METHOD("get_prefetch_radius"), &GroupProvider::get_prefetch_radius);
    ClassDB::bind_method(D_METHOD("set_near_radius", "value"), &GroupProvider::set_near_radius);
    ClassDB::bind_method(D_METHOD("get_near_radius"), &GroupProvider::get_near_radius);
    ClassDB::bind_method(D_METHOD("set_lod_radius", "value"), &GroupProvider::set_lod_radius);
    ClassDB::bind_method(D_METHOD("get_lod_radius"), &GroupProvider::get_lod_radius);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "world_dir"), "set_world_dir", "get_world_dir");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "repo_dir", PROPERTY_HINT_DIR), "set_repo_dir", "get_repo_dir");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "story"), "set_story", "get_story");
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "gen_run"), "set_gen_run", "get_gen_run");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "full_lods"), "set_full_lods", "get_full_lods");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "prefetch"), "set_prefetch", "get_prefetch");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "prefetch_radius", PROPERTY_HINT_RANGE, "0,1000000,1000,suffix:m"), "set_prefetch_radius", "get_prefetch_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "near_radius", PROPERTY_HINT_RANGE, "0,500000,1000,suffix:m"), "set_near_radius", "get_near_radius");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_radius", PROPERTY_HINT_RANGE, "0,3000000,1000,suffix:m"), "set_lod_radius", "get_lod_radius");
    ADD_SIGNAL(MethodInfo("group_ready", PropertyInfo(Variant::INT, "node"), PropertyInfo(Variant::STRING, "dir")));
    ADD_SIGNAL(MethodInfo("group_failed", PropertyInfo(Variant::INT, "node"), PropertyInfo(Variant::STRING, "why")));
    ADD_SIGNAL(MethodInfo("lods_added", PropertyInfo(Variant::PACKED_INT32_ARRAY, "nodes")));

    BIND_ENUM_CONSTANT(NONE);
    BIND_ENUM_CONSTANT(QUEUED);
    BIND_ENUM_CONSTANT(RUNNING);
    BIND_ENUM_CONSTANT(READY);
    BIND_ENUM_CONSTANT(FAILED);
    BIND_CONSTANT(URGENT);
    BIND_CONSTANT(NEAR);
    BIND_CONSTANT(PREFETCH);
}

double GroupProvider::now_s() const {
    return double(Time::get_singleton()->get_ticks_usec()) * 1e-6;
}

PlanetView *GroupProvider::view() const {
    return Object::cast_to<PlanetView>(get_node_or_null(planet_path));
}

GroupStream *GroupProvider::stream() const {
    return Object::cast_to<GroupStream>(get_node_or_null(stream_path));
}

const GenSettings &GroupProvider::gen_settings() {
    if (!settings_read) {
        settings = GenSettings::resolve();
        settings_read = true;
    }
    return settings;
}

String GroupProvider::run_dir() {
    return gen_run.is_empty() ? gen_settings().story_run : gen_run;
}

// ---------------------------------------------------------------- 世界的配置

void GroupProvider::_enter_tree() {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    if (WorldSession::take_pending(config)) {
        configured = true;
        apply_config();
    }
}

void GroupProvider::apply_config() {
    world_dir = config.world_dir;
    world_name = config.name;
    mode = config.mode;
    story = config.story;
    gen_run = config.gen_run;
    full_lods = config.full_lods;
    Node *root = get_parent();
    if (!root) {
        return;
    }
    PlanetView *pv = view();
    GroupStream *gs = stream();
    if (pv) {
        pv->set_planet_dir(config.planet_dir);
        pv->set_extra_lods_dir(world_dir.is_empty() ? String() : world_dir.path_join("lods"));
        if (config.clock_node >= 0) {
            pv->set_clock_node(config.clock_node);
        }
    }
    if (config.spawn_node < 0) {
        return; // 剧情：场景里配的 2051
    }
    // 生存：出生的那一群换进场景里的 World
    if (GroupWorld *w = Object::cast_to<GroupWorld>(get_node_or_null(world_path))) {
        w->set_data_dir(config.spawn_dir);
        w->set_terrain_seed(config.spawn_node);
    }
    if (SiteStream *s = Object::cast_to<SiteStream>(root->get_node_or_null("SiteStream"))) {
        s->set_seed(config.spawn_node);
        s->set_layout_dir(world_dir.path_join("settlements").path_join(itos(config.spawn_node)));
    }
    if (gs) {
        gs->set_settlements_dir(world_dir.path_join("settlements"));
    }
    // 出发码头：出生群的主码头（没有标主码头的取第一个）；没有目的码头
    WorldSites sites;
    std::string error;
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(config.spawn_dir.path_join("sites.tsv"));
    int origin = -1;
    if (!bytes.is_empty() && parse_world_sites(std::string(reinterpret_cast<const char *>(bytes.ptr()), size_t(bytes.size())), sites, error)) {
        for (const SiteDock &d : sites.docks) {
            if (d.main) {
                origin = d.id;
                break;
            }
        }
        if (origin < 0 && !sites.docks.empty()) {
            origin = sites.docks.front().id;
        }
    }
    if (Expedition *e = Object::cast_to<Expedition>(root)) {
        e->set_origin_dock(origin);
        e->set_destination_dock(-1);
    }
}

void GroupProvider::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    add_to_group(GROUP);
    set_process_priority(95); // GroupStream（90）之后
    restore_home();
}

void GroupProvider::restore_home() {
    // 场景里那一群上回离开时存下的账（退出游戏前人在别的群）：World 已经读完、账房还没开账——交给它们
    GroupWorld *w = Object::cast_to<GroupWorld>(get_node_or_null(world_path));
    if (world_dir.is_empty() || !w || !w->is_loaded()) {
        return;
    }
    GroupMemory memory;
    if (!load_memory(w->get_node_id(), memory)) {
        return;
    }
    if (Hinterland *h = Object::cast_to<Hinterland>(get_node_or_null(hinterland_path)); h && !memory.ledgers.empty()) {
        h->set_saved(memory.ledgers, memory.ledgers_at);
    }
    if (memory.has_world) {
        if (Farm *farm = w->host_farm()) {
            farm->restore_plots(memory.plots, memory.plots_at);
        }
        if (memory.stocks.size() == w->get_resource_stocks().size()) {
            w->set_resource_stocks(memory.stocks);
        }
        w->get_scatter_ledger() = memory.scatter;
    }
    print_line(vformat(String(U"GroupProvider：群 #%d 上回离开时的账从存档目录补回（%d 处村子%s）"), w->get_node_id(), int(memory.ledgers.size()),
            memory.has_world ? String(U"、农田采场散布") : String()));
    save_memory(w->get_node_id(), GroupMemory()); // 补回了：存档里这份作废（人在这一群，账在活的账房里）
}

// ---------------------------------------------------------------- 查

String GroupProvider::find_group(int p_node) const {
    if (p_node < 0) {
        return String();
    }
    if (story) {
        const String dir = repo_dir.path_join(itos(p_node));
        if (has_package(dir)) {
            return dir;
        }
    }
    if (!world_dir.is_empty()) {
        const String dir = world_dir.path_join("groups").path_join(itos(p_node));
        if (has_package(dir)) {
            return dir;
        }
    }
    return String();
}

PackedInt32Array GroupProvider::available_nodes() const {
    std::set<int> nodes;
    auto scan = [&](const String &p_dir) {
        if (!DirAccess::dir_exists_absolute(abs_path(p_dir))) {
            return;
        }
        const PackedStringArray names = DirAccess::get_directories_at(p_dir);
        for (int64_t k = 0; k < names.size(); k++) {
            if (names[k].is_valid_int() && has_package(p_dir.path_join(names[k]))) {
                nodes.insert(int(names[k].to_int()));
            }
        }
    };
    if (story) {
        scan(repo_dir);
    }
    if (!world_dir.is_empty()) {
        scan(world_dir.path_join("groups"));
    }
    PackedInt32Array out;
    for (int n : nodes) {
        out.push_back(n);
    }
    return out;
}

bool GroupProvider::can_generate() {
    return get_problem().is_empty();
}

String GroupProvider::get_problem() {
    if (world_dir.is_empty()) {
        return String(U"这个场景没有世界存档目录（测试、工具直接开的场景），只能去仓库里有的岛群");
    }
    const GenSettings &s = gen_settings();
    if (!s.ok()) {
        return s.problem;
    }
    const String run = run_dir();
    if (!FileAccess::file_exists(run.path_join("config.resolved.toml")) || !FileAccess::file_exists(run.path_join("manifest.json"))) {
        return vformat(String(U"生成器的 run 目录不对（%s：没有 config.resolved.toml / manifest.json）"), run);
    }
    return String();
}

int GroupProvider::request(int p_node, int p_priority) {
    if (is_available(p_node)) {
        return READY;
    }
    Request &r = requests[p_node];
    if (r.state == RUNNING || r.state == READY) {
        r.priority = std::min(r.priority, p_priority);
        return r.state;
    }
    if (r.state == FAILED && p_priority != URGENT) {
        return FAILED; // 失败过的不再预取；瞬移再点一回就重试
    }
    if (r.state == QUEUED) {
        r.priority = std::min(r.priority, p_priority);
        return QUEUED;
    }
    r.node = p_node;
    r.priority = p_priority;
    r.state = QUEUED;
    r.progress = 0.0;
    r.error = String();
    r.text = String(U"排队等生成");
    r.queued_at = now_s();
    return QUEUED;
}

Dictionary GroupProvider::get_request(int p_node) const {
    Dictionary d;
    const auto it = requests.find(p_node);
    if (it == requests.end()) {
        d["state"] = is_available(p_node) ? int(READY) : int(NONE);
        d["progress"] = is_available(p_node) ? 1.0 : 0.0;
        return d;
    }
    const Request &r = it->second;
    d["state"] = int(r.state);
    d["priority"] = r.priority;
    d["progress"] = r.progress;
    d["stage"] = r.stage;
    d["text"] = r.text;
    d["error"] = r.error;
    d["seconds"] = r.seconds;
    d["queued_s"] = r.started_at > 0.0 ? r.started_at - r.queued_at : now_s() - r.queued_at;
    d["run_s"] = r.finished_at > 0.0 ? r.finished_at - r.started_at : (r.started_at > 0.0 ? now_s() - r.started_at : 0.0);
    return d;
}

Dictionary GroupProvider::get_group_info(int p_node) {
    Dictionary d;
    const String dir = find_group(p_node);
    d["available"] = !dir.is_empty();
    d["dir"] = dir;
    d["source"] = dir.is_empty() ? String() : (dir.begins_with("res://") ? String("repo") : String("cache"));
    if (!dir.is_empty() && dir.begins_with("user://")) {
        auto it = stamp_commits.find(p_node);
        if (it == stamp_commits.end()) {
            const Variant v = JSON::parse_string(FileAccess::get_file_as_string(dir.path_join("stamp.json")));
            String commit = "?";
            if (v.get_type() == Variant::DICTIONARY) {
                const Dictionary gen = Dictionary(v).get("generator", Dictionary());
                commit = gen.get("commit", "?");
            }
            it = stamp_commits.emplace(p_node, commit).first;
        }
        d["commit"] = it->second;
        d["stale"] = !current_commit.is_empty() && it->second != current_commit;
    }
    const Dictionary r = get_request(p_node);
    d["state"] = r["state"];
    d["progress"] = r["progress"];
    d["text"] = r.get("text", String());
    d["error"] = r.get("error", String());
    return d;
}

Dictionary GroupProvider::get_stats() {
    Dictionary d;
    int queued = 0;
    for (const auto &kv : requests) {
        queued += kv.second.state == QUEUED ? 1 : 0;
    }
    d["generated"] = generated;
    d["failed"] = failed;
    d["queued"] = queued;
    d["prefetched"] = prefetched;
    d["running"] = group_job.is_running() ? group_job_node : -1;
    d["lods_added"] = lods_added;
    d["lod_running"] = lod_job.is_running();
    d["lod_failed"] = int(lod_failed.size());
    d["busy_s"] = busy_s;
    d["world_dir"] = world_dir;
    d["story"] = story;
    d["full_lods"] = full_lods;
    d["can_generate"] = can_generate();
    return d;
}

Dictionary GroupProvider::get_world() const {
    Dictionary d;
    d["name"] = world_name;
    d["mode"] = mode;
    d["world_dir"] = world_dir;
    d["seed"] = configured ? config.seed : 42;
    d["spawn"] = configured ? config.spawn_node : -1;
    d["story"] = story;
    d["full_lods"] = full_lods;
    d["configured"] = configured;
    return d;
}

// ---------------------------------------------------------------- 离开的岛群的账

bool GroupProvider::load_memory(int p_node, GroupMemory &r_memory) const {
    if (world_dir.is_empty()) {
        return false;
    }
    const String path = world_dir.path_join("memory").path_join(itos(p_node) + ".txt");
    if (!FileAccess::file_exists(path)) {
        return false;
    }
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
    std::string error;
    if (!load_group_memory(std::string(reinterpret_cast<const char *>(bytes.ptr()), size_t(bytes.size())), r_memory, error)) {
        ERR_PRINT(vformat(String(U"GroupProvider：%s 读不回来（%s），这一群的账重新开"), path, String::utf8(error.c_str())));
        return false;
    }
    return !r_memory.empty();
}

void GroupProvider::save_memory(int p_node, const GroupMemory &p_memory) const {
    if (world_dir.is_empty()) {
        return;
    }
    const String dir = world_dir.path_join("memory");
    const String path = dir.path_join(itos(p_node) + ".txt");
    if (p_memory.empty()) {
        if (FileAccess::file_exists(path)) {
            DirAccess::remove_absolute(abs_path(path));
        }
        return;
    }
    DirAccess::make_dir_recursive_absolute(abs_path(dir));
    const std::string text = save_group_memory(p_memory);
    Ref<FileAccess> f = FileAccess::open(path + String(".tmp"), FileAccess::WRITE);
    if (f.is_null()) {
        ERR_PRINT(vformat(String(U"GroupProvider：写不了 %s"), path));
        return;
    }
    PackedByteArray bytes;
    bytes.resize(int64_t(text.size()));
    std::copy(text.begin(), text.end(), bytes.ptrw());
    f->store_buffer(bytes);
    f->close();
    DirAccess::rename_absolute(abs_path(path + String(".tmp")), abs_path(path));
}

// ---------------------------------------------------------------- 生成

void GroupProvider::_process(double p_delta) {
    poll_group_job();
    if (!group_job.is_running()) {
        start_next_group();
    }
    update_prefetch(p_delta);
    update_lods(p_delta);
}

void GroupProvider::start_next_group() {
    Request *best = nullptr;
    for (auto &kv : requests) {
        Request &r = kv.second;
        if (r.state != QUEUED) {
            continue;
        }
        if (!best || r.priority < best->priority || (r.priority == best->priority && r.queued_at < best->queued_at)) {
            best = &r;
        }
    }
    if (!best) {
        return;
    }
    if (is_available(best->node)) {
        best->state = READY;
        best->progress = 1.0;
        emit_signal("group_ready", best->node, find_group(best->node));
        return;
    }
    const String problem = get_problem();
    if (!problem.is_empty()) {
        best->state = FAILED;
        best->error = problem;
        best->text = problem;
        failed++;
        emit_signal("group_failed", best->node, problem);
        return;
    }
    PackedStringArray args;
    args.push_back("group");
    args.push_back("--generator");
    args.push_back(gen_settings().root);
    args.push_back("--run");
    args.push_back(run_dir());
    args.push_back("--node");
    args.push_back(itos(best->node));
    args.push_back("--out");
    args.push_back(abs_path(world_dir.path_join("groups").path_join(itos(best->node))));
    if (!full_lods) {
        args.push_back("--lod-dir");
        args.push_back(abs_path(world_dir.path_join("lods")));
    }
    String error;
    if (!group_job.start(gen_settings(), args, world_dir.path_join("logs").path_join(vformat("group_%d.jsonl", best->node)), group_timeout, error)) {
        best->state = FAILED;
        best->error = error;
        best->text = error;
        failed++;
        emit_signal("group_failed", best->node, error);
        return;
    }
    best->state = RUNNING;
    best->started_at = now_s();
    best->text = String(U"起了生成进程");
    group_job_node = best->node;
}

void GroupProvider::poll_group_job() {
    if (group_job_node < 0) {
        return;
    }
    group_job.poll();
    Request &r = requests[group_job_node];
    r.progress = group_job.get_progress();
    r.stage = group_job.get_stage();
    r.text = group_job.get_text();
    if (group_job.is_running()) {
        return;
    }
    const int node = group_job_node;
    group_job_node = -1;
    r.finished_at = now_s();
    busy_s += group_job.elapsed_s();
    if (group_job.is_done() && is_available(node)) {
        r.state = READY;
        r.progress = 1.0;
        r.seconds = Dictionary(group_job.get_result()).get("seconds", Dictionary());
        generated++;
        const Variant v = JSON::parse_string(FileAccess::get_file_as_string(find_group(node).path_join("stamp.json")));
        if (v.get_type() == Variant::DICTIONARY) {
            current_commit = Dictionary(Dictionary(v).get("generator", Dictionary())).get("commit", "");
        }
        // 生存模式：这一群的粗版一起出来了
        if (!full_lods) {
            if (PlanetView *pv = view()) {
                const String lod = world_dir.path_join("lods").path_join(itos(node) + ".bin");
                if (FileAccess::file_exists(lod) && pv->add_lod_file(lod)) {
                    lods_added++;
                    PackedInt32Array nodes;
                    nodes.push_back(node);
                    emit_signal("lods_added", nodes);
                }
            }
        }
        print_line(vformat(String(U"GroupProvider：群 #%d 生成好了，%.1f s（排队 %.1f s）→ %s"), node, r.finished_at - r.started_at, r.started_at - r.queued_at, find_group(node)));
        emit_signal("group_ready", node, find_group(node));
    } else {
        r.state = FAILED;
        r.error = group_job.is_done() ? String(U"生成器说好了，包却不全") : group_job.get_error();
        r.text = String(U"生成失败：") + r.error;
        failed++;
        ERR_PRINT(vformat(String(U"GroupProvider：群 #%d 生成失败——%s"), node, r.error));
        emit_signal("group_failed", node, r.error);
    }
}

void GroupProvider::update_prefetch(double p_delta) {
    since_prefetch += p_delta;
    if (!prefetch || since_prefetch < PREFETCH_EVERY_S) {
        return;
    }
    since_prefetch = 0.0;
    GroupStream *gs = stream();
    PlanetView *pv = view();
    if (!gs || !gs->is_ready() || !pv || !pv->is_active() || !can_generate()) {
        return;
    }
    const PlanetData *pd = pv->get_planet();
    const Vector3 f = gs->get_focus();
    const int anchor = gs->get_anchor_node();
    const double t = now_s();
    // 焦点的轨迹（换了锚就从头记）：近 3 秒挪了多远、往哪
    if (!trace.empty() && trace.back().anchor != anchor) {
        trace.clear();
    }
    trace.push_back(Trace{ t, f, anchor });
    while (trace.size() > 2 && t - trace.front().t > TRACE_S) {
        trace.erase(trace.begin());
    }
    const Vector2 ll = pv->latlon_at(f);
    const double dt = trace.back().t - trace.front().t;
    const Vector3 d = trace.back().p - trace.front().p;
    const double speed = dt > 0.5 ? std::hypot(d.x, d.z) / dt : 0.0;
    const double heading = std::atan2(double(d.x), -double(d.z)) * 180.0 / Math_PI; // 北起顺时针（锚群平面 x 东 z 南，近处与地理的东南差一个收敛角）
    for (int node : pd->nodes_within(ll.x, ll.y, std::max(prefetch_radius, near_radius))) {
        if (node == anchor || is_available(node)) {
            continue;
        }
        const auto it = requests.find(node);
        if (it != requests.end() && it->second.state != NONE && it->second.state != FAILED) {
            continue;
        }
        const PlanetNode &n = pd->node(node);
        const double dist = great_circle_m(ll.x, ll.y, n.lat, n.lon, pv->get_radius());
        if (dist <= near_radius) {
            request(node, NEAR);
            prefetched++;
        } else if (speed >= prefetch_speed && dist <= prefetch_radius && angle_diff(bearing_deg(ll.x, ll.y, n.lat, n.lon), heading) <= prefetch_cone_deg) {
            request(node, PREFETCH);
            prefetched++;
        }
    }
}

void GroupProvider::update_lods(double p_delta) {
    poll_lod_job();
    since_lods += p_delta;
    if (full_lods || lod_job.is_running() || since_lods < LODS_EVERY_S) {
        return;
    }
    since_lods = 0.0;
    PlanetView *pv = view();
    if (!pv || !pv->is_active() || !can_generate()) {
        return;
    }
    const PlanetData *pd = pv->get_planet();
    const Vector2 ll = pv->get_camera_latlon();
    std::vector<int> todo;
    for (int node : pd->nodes_within(ll.x, ll.y, lod_radius)) { // 由近及远
        if (pd->has_lod(node) || lod_failed.count(node)) {
            continue;
        }
        if (group_job.is_running() && group_job_node == node) {
            continue; // 生成细版时顺带出它的粗版
        }
        todo.push_back(node);
        if (int(todo.size()) >= lod_batch) {
            break;
        }
    }
    if (todo.empty()) {
        return;
    }
    PackedStringArray args;
    args.push_back("lods");
    args.push_back("--generator");
    args.push_back(gen_settings().root);
    args.push_back("--run");
    args.push_back(run_dir());
    String nodes;
    for (size_t k = 0; k < todo.size(); k++) {
        nodes += (k ? "," : "") + itos(todo[k]);
    }
    args.push_back("--nodes");
    args.push_back(nodes);
    args.push_back("--lod-dir");
    args.push_back(abs_path(world_dir.path_join("lods")));
    String error;
    if (!lod_job.start(gen_settings(), args, world_dir.path_join("logs").path_join("lods.jsonl"), 60.0 + 20.0 * double(todo.size()), error)) {
        ERR_PRINT(String(U"GroupProvider：粗版生成起不来——") + error);
        for (int n : todo) {
            lod_failed.insert(n);
        }
        return;
    }
    lod_job_nodes = todo;
}

void GroupProvider::poll_lod_job() {
    if (lod_job_nodes.empty()) {
        return;
    }
    std::vector<Dictionary> lines;
    lod_job.poll(&lines);
    PackedInt32Array added;
    PlanetView *pv = view();
    for (const Dictionary &d : lines) {
        if (d.has("lod_done") && pv) {
            const int node = int(d["lod_done"]);
            if (pv->add_lod_file(world_dir.path_join("lods").path_join(itos(node) + ".bin"))) {
                added.push_back(node);
                lods_added++;
            }
        } else if (d.has("lod_failed")) {
            lod_failed.insert(int(d["lod_failed"]));
        }
    }
    if (!added.is_empty()) {
        emit_signal("lods_added", added);
    }
    if (!lod_job.is_running()) {
        busy_s += lod_job.elapsed_s();
        if (lod_job.is_failed()) {
            ERR_PRINT(String(U"GroupProvider：粗版这一批没做完——") + lod_job.get_error());
            for (int n : lod_job_nodes) {
                if (pv && !pv->get_planet()->has_lod(n)) {
                    lod_failed.insert(n);
                }
            }
        }
        lod_job_nodes.clear();
    }
}
