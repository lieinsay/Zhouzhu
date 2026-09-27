#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "gen_config.h"
#include "gen_core.h"
#include "gen_process.h"

class JobPool;
class PlanetJobs;

// 一件生成的活（行星计划 P6e）：上层（GroupProvider、新建世界）只认「交一件活、读进度（阶段、进度、说明，好了 / 出错）、拿结果文件」——
// 默认在进程内用生成器的 C++ 核心库做（src/gen_core.*，放进场景的后台任务池 PlanetJobs；没有任务池就自己开一条线程）；
// 项目设置 zhouzhu/generator/backend = "python" 时照旧起 Python 子进程（GenProcess → tools/zhouzhu_gen.py），留作开发时的对照。
// 两样读进度的口子一样：poll 把新的事件（生成粗版时每群一条 lod_done / lod_failed）按 GenProcess 的状态行的样子（Dictionary）交出来。
class GenJob {
public:
    enum State {
        IDLE,
        RUNNING,
        DONE,
        FAILED,
    };
    using Work = std::function<void(gen_core::GenStatus &)>; // 做完调 status.finish、出错调 status.fail

private:
    // python
    GenProcess process;
    bool python = false;
    // 进程内
    std::shared_ptr<gen_core::GenStatus> status;
    std::shared_ptr<JobPool> pool;
    uint64_t pool_key = 0;
    std::thread thread;
    State state = IDLE;
    double progress = 0.0;
    godot::String stage, text, error;
    godot::Dictionary result;
    uint64_t started_usec = 0;
    double finished_s = 0.0;
    void join();

public:
    GenJob() = default;
    ~GenJob();
    GenJob(const GenJob &) = delete;
    GenJob &operator=(const GenJob &) = delete;

    // 进程内：p_jobs 给了就进它的任务池（p_priority 同任务池的口径：数小的先），否则开一条线程
    bool start_cpp(PlanetJobs *p_jobs, double p_priority, Work p_work, godot::String &r_error);
    // Python 子进程（开发时对照）：同 GenProcess::start
    bool start_python(const GenSettings &p_settings, const godot::PackedStringArray &p_args, const godot::String &p_status, double p_timeout_s,
            godot::String &r_error);
    void poll(std::vector<godot::Dictionary> *r_lines = nullptr);
    // 撤掉：排着的撤掉；已经在算的进程内的活停不下来，由它算完（结果文件照写，这边不再理会）
    void kill();

    State get_state() const { return python ? State(process.get_state()) : state; }
    bool is_running() const { return get_state() == RUNNING; }
    bool is_done() const { return get_state() == DONE; }
    bool is_failed() const { return get_state() == FAILED; }
    bool is_python() const { return python; }
    double get_progress() const { return python ? process.get_progress() : progress; }
    godot::String get_stage() const { return python ? process.get_stage() : stage; }
    godot::String get_text() const { return python ? process.get_text() : text; }
    godot::String get_error() const { return python ? process.get_error() : error; }
    godot::Dictionary get_result() const { return python ? process.get_result() : result; }
    double elapsed_s() const;
};

// 生成器走哪条路（项目设置 zhouzhu/generator/backend：cpp（默认，进程内）/ python（开发时对照）；环境变量 ZHOUZHU_GEN_BACKEND 盖掉）
bool gen_backend_is_python();

// 读一个行星包里的 config.tsv（res:// 或 user://，经 FileAccess）；读不了或不对返回空、r_error 说为什么
std::shared_ptr<const GenConfig> load_gen_config(const godot::String &p_planet_dir, godot::String &r_error);
