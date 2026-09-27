#include "gen_job.h"

#include <atomic>
#include <cmath>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>

#include "job_pool.h"
#include "planet_jobs.h"

using namespace godot;

namespace {

std::atomic<uint64_t> g_next_job{ 1 };

String utf8(const std::string &p_s) {
    return String::utf8(p_s.c_str(), int(p_s.size()));
}

} // namespace

bool gen_backend_is_python() {
    OS *os = OS::get_singleton();
    String b = os->has_environment("ZHOUZHU_GEN_BACKEND") ? os->get_environment("ZHOUZHU_GEN_BACKEND") : String();
    if (b.is_empty()) {
        ProjectSettings *ps = ProjectSettings::get_singleton();
        b = ps->has_setting("zhouzhu/generator/backend") ? String(ps->get_setting("zhouzhu/generator/backend")) : String("cpp");
    }
    return b.strip_edges().to_lower() == "python";
}

std::shared_ptr<const GenConfig> load_gen_config(const String &p_planet_dir, String &r_error) {
    const String path = p_planet_dir.path_join("config.tsv");
    if (!FileAccess::file_exists(path)) {
        r_error = vformat(String(U"行星包里没有生成器的配置 %s（tools/export_planet.py --config-only 补上）"), path);
        return nullptr;
    }
    const PackedByteArray bytes = FileAccess::get_file_as_bytes(path);
    auto cfg = std::make_shared<GenConfig>();
    std::string error;
    if (!parse_gen_config(std::string(reinterpret_cast<const char *>(bytes.ptr()), size_t(bytes.size())), *cfg, error)) {
        r_error = vformat(String(U"%s 读不出来：%s"), path, utf8(error));
        return nullptr;
    }
    return cfg;
}

GenJob::~GenJob() {
    // 场景拆了：自己开的线程上还在做的活，一批一批做的（补粗版）在两群之间就停，别的做完；等它收尾
    if (status) {
        status->cancel();
    }
    join();
}

void GenJob::join() {
    if (thread.joinable()) {
        thread.join();
    }
}

double GenJob::elapsed_s() const {
    if (python) {
        return process.elapsed_s();
    }
    if (state == IDLE) {
        return 0.0;
    }
    if (state != RUNNING) {
        return finished_s;
    }
    return double(Time::get_singleton()->get_ticks_usec() - started_usec) * 1e-6;
}

bool GenJob::start_cpp(PlanetJobs *p_jobs, double p_priority, Work p_work, String &r_error) {
    if (is_running()) {
        r_error = String(U"上一件还在做");
        return false;
    }
    join();
    python = false;
    status = std::make_shared<gen_core::GenStatus>();
    state = RUNNING;
    progress = 0.0;
    stage = String("start");
    text = String(U"排进后台");
    error = String();
    result = Dictionary();
    started_usec = Time::get_singleton()->get_ticks_usec();
    std::shared_ptr<gen_core::GenStatus> st = status;
    auto run = [st, p_work]() {
        try {
            p_work(*st);
        } catch (const std::exception &e) {
            st->fail(e.what());
        } catch (...) {
            st->fail("未知错误");
        }
        if (!st->settled()) {
            st->fail("做完了却没说好了");
        }
    };
    if (p_jobs) {
        pool = p_jobs->share_pool();
        pool_key = JobPool::key(JobKind::GENERATE, g_next_job.fetch_add(1), 0);
        pool->submit(pool_key, p_priority, [run]() -> std::unique_ptr<JobResult> {
            run();
            return nullptr;
        });
    } else {
        pool.reset();
        pool_key = 0;
        thread = std::thread(run);
    }
    return true;
}

bool GenJob::start_python(const GenSettings &p_settings, const PackedStringArray &p_args, const String &p_status, double p_timeout_s, String &r_error) {
    if (is_running()) {
        r_error = String(U"上一件还在做");
        return false;
    }
    join();
    python = true;
    return process.start(p_settings, p_args, p_status, p_timeout_s, r_error);
}

void GenJob::poll(std::vector<Dictionary> *r_lines) {
    if (python) {
        process.poll(r_lines);
        return;
    }
    if (state != RUNNING || !status) {
        return;
    }
    // 任务池里这件已经没有了（场景拆了、池停了）却没说好了：没做成
    const bool gone = pool && pool->state(pool_key) == JobPool::State::NONE;
    gen_core::GenStatus::Snapshot snap = status->take();
    if (pool) {
        std::vector<JobPool::Done> done;
        pool->collect(JobKind::GENERATE, done); // 结果都在 status 里：做完的条目取走丢掉
    }
    progress = snap.progress;
    stage = utf8(snap.stage);
    text = utf8(snap.text);
    for (const gen_core::GenStatus::Event &e : snap.events) {
        if (r_lines) {
            Dictionary d;
            d[utf8(e.key)] = e.node;
            d["text"] = utf8(e.text);
            r_lines->push_back(d);
        }
    }
    if (snap.failed) {
        state = FAILED;
        error = utf8(snap.error);
    } else if (snap.done) {
        state = DONE;
        progress = 1.0;
        Dictionary secs;
        for (const auto &kv : snap.seconds) {
            secs[utf8(kv.first)] = std::round(kv.second * 100.0) / 100.0;
        }
        result = Dictionary();
        result["seconds"] = secs;
    } else if (gone) {
        state = FAILED;
        error = String(U"后台任务池停了，这件活没做完");
    }
    if (state != RUNNING) {
        finished_s = double(Time::get_singleton()->get_ticks_usec() - started_usec) * 1e-6;
        join();
    }
}

void GenJob::kill() {
    if (python) {
        process.kill();
        return;
    }
    if (state != RUNNING) {
        return;
    }
    if (pool) {
        pool->cancel(pool_key); // 排着的撤掉；在算的撤不掉，由它算完
    }
    if (status) {
        status->cancel();
    }
    finished_s = elapsed_s();
    state = FAILED;
    error = String(U"撤掉了");
    status.reset();
}
