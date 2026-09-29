#include "world_session.h"

#include <algorithm>

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>

using namespace godot;

namespace {

// 堆上的：godot::String 不能做静态全局（扩展载入时 godot-cpp 还没接上，构造就崩）
WorldConfig *g_pending = nullptr;

Dictionary read_json(const String &p_path) {
    if (!FileAccess::file_exists(p_path)) {
        return Dictionary();
    }
    const Variant v = JSON::parse_string(FileAccess::get_file_as_string(p_path));
    return v.get_type() == Variant::DICTIONARY ? Dictionary(v) : Dictionary();
}

} // namespace

void WorldSession::set_pending(const WorldConfig &p_config) {
    delete g_pending;
    g_pending = new WorldConfig(p_config);
}

bool WorldSession::take_pending(WorldConfig &r_config) {
    if (!g_pending) {
        return false;
    }
    r_config = *g_pending;
    delete g_pending;
    g_pending = nullptr;
    return true;
}

bool WorldSession::has_pending() {
    return g_pending != nullptr;
}

WorldConfig WorldSession::story_config() {
    WorldConfig c;
    c.name = STORY_NAME;
    c.mode = "story";
    c.world_dir = String(WORLDS_DIR).path_join(STORY_NAME);
    c.planet_dir = story_start::PLANET_DIR;
    c.story = true;
    c.seed = story_start::SEED;
    c.full_lods = true;
    // 出生群、钟不从 world.json 读（spawn_node / clock_node 留 −1 = 场景里配的，即开局配置）：开局换了（PLAN-LAND P0，2051 → 6329），
    // 旧存档目录的 world.json 还记着 2051，照样跟着开局配置走；这里只把 world.json 里的 spawn 改成此刻的开局，免得看着对不上。
    // groups/ 里缓存的群、memory/ 里离开的群的账都留着（它们本来就是按需生成、按群记的；2051 以后是普通的一群，走近了从缓存或当场生成）。
    const String meta = c.world_dir.path_join("world.json");
    Dictionary d = read_json(meta);
    const bool fresh = d.is_empty();
    if (fresh || int(d.get("spawn", -1)) != story_start::SPAWN_NODE) {
        DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(c.world_dir));
        if (fresh) {
            d["name"] = c.name;
            d["mode"] = c.mode;
            d["seed"] = c.seed;
            d["planet"] = c.planet_dir;
            d["created"] = Time::get_singleton()->get_datetime_string_from_system();
        } else {
            d["spawn_before"] = d.get("spawn", -1);
            d["spawn_changed"] = Time::get_singleton()->get_datetime_string_from_system();
        }
        d["spawn"] = story_start::SPAWN_NODE;
        d["note"] = String(U"剧情模式（D15）：行星包、全部粗版、开局那群与最近的邻群的细版出厂就在仓库里（res://planet/seed42/groups）；"
                           U"这里只存按需生成的岛群、离开的岛群的账。出生群跟着游戏的开局配置（src/story_start.h），不看这里的 spawn");
        Ref<FileAccess> f = FileAccess::open(meta, FileAccess::WRITE);
        if (f.is_valid()) {
            f->store_string(JSON::stringify(d, " "));
        }
    }
    return c;
}

bool WorldSession::load(const String &p_world_dir, WorldConfig &r_config, String &r_error) {
    const Dictionary d = read_json(p_world_dir.path_join("world.json"));
    if (d.is_empty()) {
        r_error = vformat(String(U"%s 下没有 world.json"), p_world_dir);
        return false;
    }
    WorldConfig c;
    c.name = d.get("name", p_world_dir.get_file());
    c.mode = d.get("mode", "survival");
    c.world_dir = p_world_dir;
    c.seed = int(d.get("seed", 0));
    if (c.mode == "story") {
        r_config = story_config();
        return true;
    }
    c.story = false;
    c.full_lods = false;
    c.planet_dir = p_world_dir.path_join(String(d.get("planet", "planet")));
    c.spawn_node = int(d.get("spawn", -1));
    c.clock_node = c.spawn_node; // 生存世界的钟：出生那一群的地方时（D10 的参考经度换成它）
    c.spawn_dir = p_world_dir.path_join("groups").path_join(itos(c.spawn_node));
    c.gen_run = ProjectSettings::get_singleton()->globalize_path(p_world_dir.path_join(String(d.get("run", vformat("gen/seed%d", c.seed)))));
    if (c.spawn_node < 0 || !FileAccess::file_exists(c.spawn_dir.path_join("terrain.bin"))) {
        r_error = vformat(String(U"世界 %s 的出生群 #%d 没有包"), c.name, c.spawn_node);
        return false;
    }
    if (!FileAccess::file_exists(c.planet_dir.path_join("planet.bin"))) {
        r_error = vformat(String(U"世界 %s 没有行星包"), c.name);
        return false;
    }
    r_config = c;
    return true;
}

int WorldSession::count_groups(const String &p_world_dir) {
    const String dir = p_world_dir.path_join("groups");
    int n = 0;
    if (!DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(dir))) {
        return 0;
    }
    const PackedStringArray names = DirAccess::get_directories_at(dir);
    for (int64_t k = 0; k < names.size(); k++) {
        n += names[k].is_valid_int() && FileAccess::file_exists(dir.path_join(names[k]).path_join("terrain.bin")) ? 1 : 0;
    }
    return n;
}

Array WorldSession::list_survival() {
    Array out;
    if (!DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(WORLDS_DIR))) {
        return out;
    }
    const PackedStringArray names = DirAccess::get_directories_at(WORLDS_DIR);
    for (int64_t k = 0; k < names.size(); k++) {
        const String dir = String(WORLDS_DIR).path_join(names[k]);
        const Dictionary d = read_json(dir.path_join("world.json"));
        if (d.is_empty() || String(d.get("mode", "")) != "survival") {
            continue;
        }
        Dictionary row;
        row["name"] = d.get("name", names[k]);
        row["dir"] = dir;
        row["seed"] = d.get("seed", 0);
        row["spawn"] = d.get("spawn", -1);
        row["created"] = d.get("created", "");
        row["groups"] = count_groups(dir);
        const String lods = dir.path_join("lods");
        row["lods"] = DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(lods)) ? int(DirAccess::get_files_at(lods).size()) : 0;
        out.push_back(row);
    }
    // 新的在前
    std::vector<Dictionary> rows;
    for (int64_t k = 0; k < out.size(); k++) {
        rows.push_back(out[k]);
    }
    std::sort(rows.begin(), rows.end(), [](const Dictionary &a, const Dictionary &b) { return String(a["created"]) > String(b["created"]); });
    Array sorted;
    for (const Dictionary &r : rows) {
        sorted.push_back(r);
    }
    return sorted;
}

bool WorldMaker::start(int p_seed, String &r_error, const String &p_name, PlanetJobs *p_jobs) {
    if (process.is_running()) {
        r_error = String(U"已经在新建一个世界了");
        return false;
    }
    const bool python = gen_backend_is_python();
    GenSettings settings;
    std::shared_ptr<const GenConfig> cfg;
    if (python) {
        settings = GenSettings::resolve();
        if (!settings.ok()) {
            r_error = settings.problem;
            return false;
        }
    } else {
        // 进程内：生成器的配置取仓库里 seed42 行星包带的 config.tsv（新建的世界照抄一份进它的 planet/）
        cfg = load_gen_config(String(WorldConfig().planet_dir), r_error);
        if (!cfg) {
            return false;
        }
    }
    seed = p_seed;
    if (seed <= 0) {
        // 随机种子：避开已有的世界目录
        const uint64_t t = Time::get_singleton()->get_ticks_usec() ^ (uint64_t(Time::get_singleton()->get_unix_time_from_system()) * 2654435761ull);
        seed = int(1 + (t % 999999));
        while (DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(String(WorldSession::WORLDS_DIR).path_join(vformat("seed%d", seed))))) {
            seed++;
        }
    }
    world_dir = String(WorldSession::WORLDS_DIR).path_join(p_name.is_empty() ? vformat("seed%d", seed) : p_name);
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (DirAccess::dir_exists_absolute(ps->globalize_path(world_dir.path_join("groups")))) {
        r_error = vformat(String(U"%s 已经有世界了（要重建先删掉这个目录）"), world_dir);
        return false;
    }
    if (python) {
        PackedStringArray args;
        args.push_back("world");
        args.push_back("--generator");
        args.push_back(settings.root);
        args.push_back("--seed");
        args.push_back(itos(seed));
        args.push_back("--world");
        args.push_back(ps->globalize_path(world_dir));
        return process.start_python(settings, args, world_dir.path_join("logs/new_world.jsonl"), 240.0, r_error);
    }
    const uint64_t s = uint64_t(seed);
    const std::string dir = ps->globalize_path(world_dir).utf8().get_data();
    return process.start_cpp(p_jobs, -1.0, [cfg, s, dir](gen_core::GenStatus &st) {
        gen_core::WorldResult r;
        std::string err;
        if (!gen_core::make_world(*cfg, s, dir, &st, r, err)) {
            st.fail(err);
            return;
        }
        st.finish(r.seconds);
    }, r_error);
}
