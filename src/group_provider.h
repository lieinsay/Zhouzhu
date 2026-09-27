#pragma once

#include <map>
#include <set>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "gen_job.h"
#include "ground_works.h"
#include "group_memory.h"
#include "world_session.h"

class GroupStream;
class PlanetView;

// 岛群包从哪来（行星计划 P4，docs/PLAN-PLANET.md 第五节 P4 第 1–3 条、第 4.7 节）：一个场景一个（world_2051.tscn，挂在 World 后面）。按这个次序找：
//   仓库里的 res://worlds/<节点>/（只有剧情模式 seed42 的世界算：仓库里的 2051、2050 是 seed42 生成的）；
//   这个世界的存档目录 user://worlds/<世界>/groups/<节点>/；
//   都没有就在后台生成（行星计划 P6e 起在进程内：生成器的 C++ 核心库，GenJob 放进场景的任务池 PlanetJobs，一群一两秒；
//   项目设置 zhouzhu/generator/backend = "python" 时照旧起 Python 子进程 tools/zhouzhu_gen.py，留作开发时的对照），有进度、出错报得清楚；
//   生成好了发 group_ready，GroupStream 接着载入。生成器的配置是行星包里的 config.tsv（剧情 res://planet/seed42，生存世界 <世界>/planet）。
//   这个世界的行星层（①–⑨，C++ 约 1 s）第一回要生成时在后台算一次、进程里留着；有存档目录的场景一开就先在后台备好。
// 同时只做一件生成岛群的活：瞬移要的（急用）插到最前，其次是焦点 60 km 内的（快到了），再次是船头方向 150 km 内的（预取，走得快时才预取）；
// 同一档里离此刻的焦点近的先做，排着的离焦点远过 1000 km 就撤掉（高空飞过去时顺路排上的一长串不挡着落地那一处）。
// 缓存键（4.7）：包里的 stamp.json 记着生成器的提交号、run 的配置哈希、[island] 段哈希、导出版本；已经生成过的不因换版本重生成，只读出来给地图看。
// 生存模式（行星包里没有粗版，full_lods = false）：另有一条粗版的活——相机 600 km 内还没有粗版的群由近及远一批批生成（进程内 make_lod / zhouzhu_gen.py lods），
//   存进存档目录的 lods/<节点>.bin，加进行星包（PlanetView::add_lod_file），远处的替身陆续换成粗版。
// 进树时（_enter_tree，比场景里谁都早）把「这一回用哪个世界」（WorldSession 的 pending）配到场景的各节点上：World 的包、SiteStream、PlanetView 的行星包与钟、
// GroupStream 的村落改动目录、编排的出发码头。没有 pending（测试、工具直接实例化场景）就照场景里配的：没有存档目录，只有仓库里的包。
// _ready（World 已经读完、账房还没开账）：这个世界存着场景里那一群上回离开时的账，交给它的账房、农田。
class GroupProvider : public godot::Node {
    GDCLASS(GroupProvider, godot::Node)

public:
    enum State {
        NONE,
        QUEUED,
        RUNNING,
        READY,
        FAILED,
    };
    enum Priority {
        URGENT = 0, // 瞬移要去的
        NEAR = 1, // 焦点 near_radius 以内
        PREFETCH = 2, // 船头方向 prefetch_radius 以内
    };

private:
    godot::String world_dir; // user://worlds/<世界>；空 = 不存档（只有仓库里的包，不生成）
    godot::String world_name;
    godot::String mode = "story";
    godot::String repo_dir = "res://worlds";
    bool story = true;
    godot::String gen_run; // 空 = 项目设置 zhouzhu/generator/run
    bool full_lods = true;
    bool prefetch = true;
    double near_radius = 60000.0;
    double prefetch_radius = 150000.0;
    double prefetch_cone_deg = 50.0;
    double prefetch_speed = 15.0; // 焦点走得比这快（米 / 秒，开船）才按船头方向预取
    double lod_radius = 600000.0;
    int lod_batch = 8;
    double group_timeout = 180.0;
    godot::NodePath planet_path = "../PlanetView";
    godot::NodePath stream_path = "../GroupStream";
    godot::NodePath world_path = "../World";
    godot::NodePath hinterland_path = "../Hinterland";

    bool configured = false;
    WorldConfig config;
    GenSettings settings;
    bool settings_read = false;
    std::shared_ptr<const GenConfig> gen_config; // 行星包里的 config.tsv（进程内生成读它）
    godot::String gen_config_problem;
    bool gen_config_read = false;
    GenJob warm_job; // 这个世界的行星层：有存档目录就先在后台备好
    bool warm_started = false;
    double warm_wait = 0.0;

    struct Request {
        int node = -1;
        int priority = PREFETCH;
        State state = NONE;
        double progress = 0.0;
        godot::String stage, text, error;
        double queued_at = 0.0, started_at = 0.0, finished_at = 0.0;
        godot::Dictionary seconds;
    };
    std::map<int, Request> requests;
    GenJob group_job;
    int group_job_node = -1;
    GenJob lod_job;
    std::vector<int> lod_job_nodes;
    std::set<int> lod_failed;
    std::map<int, godot::String> stamp_commits; // 读过的 stamp.json 的生成器提交号
    godot::String current_commit; // 最近一回生成用的生成器提交号
    double since_prefetch = 1e9, since_lods = 1e9;
    struct Trace {
        double t = 0.0;
        godot::Vector3 p;
        int anchor = -1;
        int shifts = 0; // 换锚、规范化了几次（平面坐标会跳）
    };
    std::vector<Trace> trace; // 焦点近几秒的轨迹：船头往哪
    int generated = 0, failed = 0, lods_added = 0, prefetched = 0, dropped = 0;
    double busy_s = 0.0; // 生成进程一共跑了多久

    PlanetView *view() const;
    GroupStream *stream() const;
    const GenSettings &gen_settings();
    godot::String run_dir();
    const std::shared_ptr<const GenConfig> &gen_cfg();
    godot::String planet_dir() const;
    uint64_t world_seed() const;
    PlanetJobs *jobs() const;
    void warm_up(double p_delta);
    void apply_config();
    void restore_home();
    void start_next_group();
    void poll_group_job();
    void update_prefetch(double p_delta);
    void update_lods(double p_delta);
    void poll_lod_job();
    double now_s() const;

protected:
    static void _bind_methods();

public:
    static constexpr const char *GROUP = "group_provider";

    void _enter_tree() override;
    void _ready() override;
    void _process(double p_delta) override;

    // 这一群的包在哪（仓库 / 存档目录）；没有返回空
    godot::String find_group(int p_node) const;
    bool is_available(int p_node) const { return !find_group(p_node).is_empty(); }
    // 仓库里、存档目录里都有哪些群的包
    godot::PackedInt32Array available_nodes() const;
    // 能不能生成（有存档目录；进程内：行星包里有 config.tsv；Python：找得到 Python 与生成器、run 目录在）；不能时 get_problem 说为什么
    bool can_generate();
    godot::String get_problem();
    // 要这一群：已有返回 READY；否则排进队（同一群再要一次只提前、不重排）
    int request(int p_node, int p_priority);
    // { state（0 没要 1 排着 2 在生成 3 好了 4 失败）, progress, stage, text, error, seconds, queued_s（排了多久）}
    godot::Dictionary get_request(int p_node) const;
    // 给行星地图：{ available, source（"repo" / "cache" / ""）, dir, commit（stamp 里的生成器提交号）, stale（与最近一回生成的提交号不同）, state, progress, text, error }
    godot::Dictionary get_group_info(int p_node);
    // 离开的岛群的账（存档目录的 memory/<节点>.txt）：读 / 写（空的删掉）；没有存档目录时什么都不做
    bool load_memory(int p_node, GroupMemory &r_memory) const;
    void save_memory(int p_node, const GroupMemory &p_memory) const;
    // 玩家营造的账（切片十一，存档目录的 works/<节点>.txt）：读（读到了返回 true）/ 写（空串删掉）；没有存档目录时什么都不做
    bool load_works(int p_node, GroundWorks &r_works) const;
    void save_works(int p_node, const std::string &p_text) const;
    // { generated, failed, queued, prefetched, dropped（离焦点远了撤掉的）, running（在生成的群号，−1 = 没有）, lods_added, lod_running, busy_s, world_dir, story, full_lods, can_generate, backend（cpp / python）}
    godot::Dictionary get_stats();
    godot::Dictionary get_world() const; // { name, mode, world_dir, seed, spawn, story, full_lods }

    void set_world_dir(const godot::String &p_value) { world_dir = p_value; }
    godot::String get_world_dir() const { return world_dir; }
    void set_repo_dir(const godot::String &p_value) { repo_dir = p_value; }
    godot::String get_repo_dir() const { return repo_dir; }
    void set_story(bool p_value) { story = p_value; }
    bool get_story() const { return story; }
    void set_gen_run(const godot::String &p_value) { gen_run = p_value; }
    godot::String get_gen_run() const { return gen_run; }
    void set_full_lods(bool p_value) { full_lods = p_value; }
    bool get_full_lods() const { return full_lods; }
    void set_prefetch(bool p_value) { prefetch = p_value; }
    bool get_prefetch() const { return prefetch; }
    void set_prefetch_radius(double p_value) { prefetch_radius = p_value; }
    double get_prefetch_radius() const { return prefetch_radius; }
    void set_near_radius(double p_value) { near_radius = p_value; }
    double get_near_radius() const { return near_radius; }
    void set_lod_radius(double p_value) { lod_radius = p_value; }
    double get_lod_radius() const { return lod_radius; }
};

VARIANT_ENUM_CAST(GroupProvider::State);
