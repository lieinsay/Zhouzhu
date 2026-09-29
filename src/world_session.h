#pragma once

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "gen_job.h"
#include "story_start.h"

class PlanetJobs;

// 世界（行星计划 P4，docs/PLAN-PLANET.md 第 4.8 节、D15）：一个世界 = 种子 + 生成器版本 + 存档目录 user://worlds/<世界>/——
//   world.json    这个世界是什么：名字、模式（story 剧情 / survival 生存）、种子、出生的岛群、生成器版本、run 与行星包在哪；
//   planet/       行星包（planet.bin、lods.bin、weather.bin 同格式，config.tsv 是生成器的配置）：生存模式的；剧情模式直接用仓库里的 res://planet/seed42
//                 （仓库里带的岛群细版在它底下的 groups/<节点>/，同这里的 groups/）；
//                 进程内按需生成岛群、粗版读它的 config.tsv 与 world.json 的种子（行星计划 P6e）；
//   gen/seed<N>/  生成器的 run 目录（行星层 ①–⑨）：只有 Python 那条路（开发时对照）建的世界有，那条路按需生成读它；剧情模式用项目设置里生成器的 out/seed42；
//   groups/<节点>/ 生成过的岛群细版（export_skyisle 的五份 + stamp.json 缓存键，4.7）；
//   lods/<节点>.bin 生成过的粗版（lods.bin 同格式、一条）；
//   memory/<节点>.txt 离开的岛群的账（模拟分级，4.6）；
//   settlements/<节点>/ 布置模式的改动（生存模式；剧情模式照旧 res://settlements）。
// 两种世界都用 world_seed42.tscn：场景里的 GroupProvider 进树时（_enter_tree，比谁都早）把「下一个场景用哪个世界」（pending）配到各节点上。
// 直接实例化场景（测试、工具）不走这里：没有存档目录，只有仓库里的岛群包，一切照 P3。
// 剧情世界的开局（出生的群、钟的群、开局是一年里哪天、仓库里带哪几群）在 story_start.h 一处（PLAN-LAND P0）：场景里各节点的缺省就是它，
// 所以剧情世界不另配出生群（spawn_node = −1）；存档目录里旧的 world.json 记着别的出生群（2026-09-29 以前是 2051）也不管，照开局配置走。
struct WorldConfig {
    godot::String name; // 显示名 / 目录名
    godot::String mode = "story"; // story / survival
    godot::String world_dir; // user://worlds/<名字>；空 = 不存档
    godot::String planet_dir = story_start::PLANET_DIR;
    bool story = true; // 仓库里的岛群包（story_start::GROUPS_DIR）属于这个世界（seed42）
    int seed = story_start::SEED;
    int spawn_node = -1; // −1 = 场景里配的（= 开局配置的 story_start::SPAWN_NODE）
    godot::String spawn_dir; // 出生群的包
    godot::String gen_run; // 生成器的 run 目录（绝对路径；空 = 项目设置 zhouzhu/generator/run）
    int clock_node = -1; // 钟的那一群（D10）；−1 = 场景里配的（= story_start::CLOCK_NODE）
    bool full_lods = true; // 行星包里全行星都有粗版（剧情）；否则远处先画替身，粗版在后台由近及远补
};

class WorldSession {
public:
    static constexpr const char *WORLDS_DIR = "user://worlds";
    static constexpr const char *STORY_NAME = "seed42";

    // 下一个行星场景用哪个世界（DevTools 换场景前设，GroupProvider 进树时取走）
    static void set_pending(const WorldConfig &p_config);
    static bool take_pending(WorldConfig &r_config);
    static bool has_pending();

    // 剧情模式：seed42，存档目录 user://worlds/seed42（没有 world.json 就写一份）
    static WorldConfig story_config();
    // 生存模式的世界：读 <world_dir>/world.json
    static bool load(const godot::String &p_world_dir, WorldConfig &r_config, godot::String &r_error);
    // user://worlds 下的生存世界：[{ name, dir, seed, spawn, groups（生成过几群）, lods, created }]，新的在前
    static godot::Array list_survival();
    // 存档目录里生成过几群（groups/<节点>/ 有 terrain.bin 的）
    static int count_groups(const godot::String &p_world_dir);
};

// 新建世界（生存模式，D15）：行星层 → 行星包（没有粗版）→ 出生的岛群 → world.json。一次一个。
// 行星计划 P6e 起在进程内做（生成器的 C++ 核心库，配置取仓库里 seed42 行星包的 config.tsv，几秒）；项目设置 zhouzhu/generator/backend = "python"
// 时照旧调 zhouzhu_gen.py world（开发时对照）。
class WorldMaker {
    GenJob process;
    godot::String world_dir;
    int seed = 0;

public:
    // p_seed ≤ 0：随机种子；p_name 空 = seed<N>（存档目录 user://worlds/<名字>，已有的先删掉）；p_jobs 给了就进它的任务池，否则自己开线程
    bool start(int p_seed, godot::String &r_error, const godot::String &p_name = godot::String(), PlanetJobs *p_jobs = nullptr);
    void poll() { process.poll(); }
    bool is_running() const { return process.is_running(); }
    bool is_done() const { return process.is_done(); }
    bool is_failed() const { return process.is_failed(); }
    double get_progress() const { return process.get_progress(); }
    godot::String get_text() const { return process.get_text(); }
    godot::String get_error() const { return process.get_error(); }
    double elapsed_s() const { return process.elapsed_s(); }
    godot::String get_world_dir() const { return world_dir; }
    int get_seed() const { return seed; }
};
