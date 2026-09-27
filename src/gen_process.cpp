#include "gen_process.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>

using namespace godot;

namespace {

constexpr const char *SET_PYTHON = "zhouzhu/generator/python";
constexpr const char *SET_ROOT = "zhouzhu/generator/root";
constexpr const char *SET_RUN = "zhouzhu/generator/run";

// 「{LOCALAPPDATA}/…」里的花括号按环境变量展开
String expand_env(const String &p_value) {
    String out = p_value;
    OS *os = OS::get_singleton();
    for (int guard = 0; guard < 8; guard++) {
        const int a = out.find("{");
        const int b = a >= 0 ? out.find("}", a) : -1;
        if (a < 0 || b < 0) {
            break;
        }
        const String name = out.substr(a + 1, b - a - 1);
        out = out.substr(0, a) + os->get_environment(name) + out.substr(b + 1);
    }
    return out.replace("\\", "/");
}

bool is_absolute(const String &p_path) {
    return p_path.is_absolute_path() || (p_path.length() > 1 && p_path[1] == ':');
}

String setting(const char *p_name, const String &p_default) {
    ProjectSettings *ps = ProjectSettings::get_singleton();
    return ps->has_setting(p_name) ? String(ps->get_setting(p_name)) : p_default;
}

} // namespace

GenSettings GenSettings::resolve() {
    GenSettings s;
    OS *os = OS::get_singleton();
    ProjectSettings *ps = ProjectSettings::get_singleton();
    const String repo = ps->globalize_path("res://").path_join("..").simplify_path();
    s.python = expand_env(os->has_environment("ZHOUZHU_PYTHON") ? os->get_environment("ZHOUZHU_PYTHON")
                                                                 : setting(SET_PYTHON, "{LOCALAPPDATA}/Programs/Python/Python312/python.exe"));
    String root = expand_env(os->has_environment("ZHOUZHU_GENERATOR") ? os->get_environment("ZHOUZHU_GENERATOR") : setting(SET_ROOT, "../skyisle-gen"));
    s.root = (is_absolute(root) ? root : repo.path_join(root)).simplify_path();
    const String run = expand_env(setting(SET_RUN, "out/seed42"));
    s.story_run = (is_absolute(run) ? run : s.root.path_join(run)).simplify_path();
    s.script = repo.path_join("tools/zhouzhu_gen.py");
    if (s.python.is_empty() || (s.python.contains("/") && !FileAccess::file_exists(s.python))) {
        s.problem = vformat(String(U"找不到 Python（%s，项目设置 %s）"), s.python, SET_PYTHON);
    } else if (!DirAccess::dir_exists_absolute(s.root.path_join("skyisle_gen"))) {
        s.problem = vformat(String(U"找不到生成器 skyisle-gen（%s，项目设置 %s）"), s.root, SET_ROOT);
    } else if (!FileAccess::file_exists(s.script)) {
        s.problem = vformat(String(U"找不到包装脚本 %s"), s.script);
    }
    return s;
}

bool GenProcess::start(const GenSettings &p_settings, const PackedStringArray &p_args, const String &p_status, double p_timeout_s, String &r_error) {
    if (state == RUNNING) {
        r_error = String(U"上一件还在做");
        return false;
    }
    if (!p_settings.ok()) {
        r_error = p_settings.problem;
        state = FAILED;
        error = r_error;
        return false;
    }
    ProjectSettings *ps = ProjectSettings::get_singleton();
    // 状态文件一件活一个：上一件的进程刚写完「好了」可能还没退出、还拿着文件（Windows 上删不掉），接着用同一个文件会从头读到上一件的「好了」
    const String base = ps->globalize_path(p_status);
    DirAccess::make_dir_recursive_absolute(base.get_base_dir());
    status_path = base;
    for (int k = 1; FileAccess::file_exists(status_path) && k < 1000; k++) {
        if (DirAccess::remove_absolute(status_path) == OK) {
            DirAccess::remove_absolute(status_path + String(".log"));
            break;
        }
        status_path = base.get_basename() + vformat("_%d.", k) + base.get_extension();
    }
    PackedStringArray args;
    args.push_back("-X");
    args.push_back("utf8"); // 同 PYTHONUTF8=1：路径、输出都按 UTF-8（存档目录在「州渚」下）
    args.push_back(p_settings.script);
    args.append_array(p_args);
    args.push_back("--status");
    args.push_back(status_path);
    pid = OS::get_singleton()->create_process(p_settings.python, args, false);
    if (pid <= 0) {
        r_error = vformat(String(U"起不了生成进程（%s）"), p_settings.python);
        state = FAILED;
        error = r_error;
        return false;
    }
    state = RUNNING;
    read_pos = 0;
    started_usec = Time::get_singleton()->get_ticks_usec();
    timeout_s = p_timeout_s;
    progress = 0.0;
    stage = String("start");
    text = String(U"起了生成进程");
    error = String();
    result = Dictionary();
    return true;
}

double GenProcess::elapsed_s() const {
    if (state == IDLE) {
        return 0.0;
    }
    if (state != RUNNING) {
        return finished_s;
    }
    return double(Time::get_singleton()->get_ticks_usec() - started_usec) * 1e-6;
}

void GenProcess::read_lines(std::vector<Dictionary> *r_lines) {
    // 状态文件新写的：只认以换行结尾的整行
    if (!FileAccess::file_exists(status_path)) {
        return;
    }
    Ref<FileAccess> f = FileAccess::open(status_path, FileAccess::READ);
    if (f.is_null() || int64_t(f->get_length()) <= read_pos) {
        return;
    }
    f->seek(uint64_t(read_pos));
    const PackedByteArray bytes = f->get_buffer(int64_t(f->get_length()) - read_pos);
    int64_t last = -1;
    for (int64_t k = bytes.size() - 1; k >= 0; k--) {
        if (bytes[k] == 10) { // 换行
            last = k;
            break;
        }
    }
    if (last < 0) {
        return;
    }
    const String chunk = bytes.slice(0, last + 1).get_string_from_utf8();
    read_pos += last + 1;
    const PackedStringArray lines = chunk.split(String::chr(10), false);
    for (int64_t k = 0; k < lines.size() && state == RUNNING; k++) {
        const Variant v = JSON::parse_string(lines[k]);
        if (v.get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary d = v;
        if (d.has("progress")) {
            progress = double(d["progress"]);
        }
        if (d.has("stage")) {
            stage = d["stage"];
        }
        if (d.has("text")) {
            text = d["text"];
        }
        if (r_lines) {
            r_lines->push_back(d);
        }
        if (d.has("error")) {
            error = String(d["error"]) + vformat(String(U"（日志 %s）"), get_log_path());
            state = FAILED;
        } else if (d.has("done")) {
            result = d.get("result", Dictionary());
            state = DONE;
            progress = 1.0;
        }
    }
}

void GenProcess::poll(std::vector<Dictionary> *r_lines) {
    if (state != RUNNING) {
        return;
    }
    const double elapsed = double(Time::get_singleton()->get_ticks_usec() - started_usec) * 1e-6;
    read_lines(r_lines);
    if (state == RUNNING && !OS::get_singleton()->is_process_running(pid)) {
        read_lines(r_lines); // 退出前刚写完的最后一行
        if (state == RUNNING) {
            const int code = OS::get_singleton()->get_process_exit_code(pid);
            state = FAILED;
            error = vformat(String(U"生成进程退出了（退出码 %d），没说好了（日志 %s）"), code, get_log_path());
        }
    }
    if (state == RUNNING && elapsed > timeout_s) {
        kill();
        error = vformat(String(U"生成超过 %d 秒，杀掉了（日志 %s）"), int(timeout_s), get_log_path());
    }
    if (state != RUNNING) {
        finished_s = elapsed;
    }
}

void GenProcess::kill() {
    if (state == RUNNING && pid > 0) {
        finished_s = elapsed_s();
        OS::get_singleton()->kill(pid);
        state = FAILED;
        error = String(U"杀掉了");
    }
}
