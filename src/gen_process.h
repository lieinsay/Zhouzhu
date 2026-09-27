#pragma once

#include <cstdint>
#include <vector>

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

// 调生成器（行星计划 P4，docs/PLAN-PLANET.md 第 4.7–4.9 节）：开发机上游戏只经 tools/zhouzhu_gen.py 这一个包装脚本调 skyisle-gen 的 Python 前端。
// P6e 换成进程内的 C++ 核心时，换掉的就是这一层（GenProcess 与 zhouzhu_gen.py）：上层（GroupProvider、新建世界）只认「交一件活、读进度、拿结果」。

// 生成器与 Python 在哪：项目设置（project.godot 的 [zhouzhu] 段），环境变量能盖掉（测试、别的机器）：
//   zhouzhu/generator/python  Python 解释器（{LOCALAPPDATA} 这类花括号按环境变量展开；ZHOUZHU_PYTHON 盖掉）
//   zhouzhu/generator/root    skyisle-gen 仓库根（相对路径按本仓库根 = res://.. 算；ZHOUZHU_GENERATOR 盖掉）
//   zhouzhu/generator/run     剧情模式（seed42）的 run 目录（相对路径按生成器的根算）
// 包装脚本是本仓库的 tools/zhouzhu_gen.py（res://../tools）。
struct GenSettings {
    godot::String python, root, story_run, script;
    godot::String problem; // 空 = 能用；否则是为什么不能（找不到 Python、找不到生成器……）

    static GenSettings resolve();
    bool ok() const { return problem.is_empty(); }
};

// 一个生成子进程：OS::create_process 起（不阻塞主线程），它把进度一行一个 JSON 追加进状态文件，这里每回 poll 读新写的那几行。
// 进程退出了没写「好了」、或超时（被杀），都算失败，说明里带日志的路径（<状态文件>.log）。
class GenProcess {
public:
    enum State {
        IDLE,
        RUNNING,
        DONE,
        FAILED,
    };

private:
    State state = IDLE;
    int pid = -1;
    godot::String status_path; // 绝对路径
    int64_t read_pos = 0;
    uint64_t started_usec = 0;
    double timeout_s = 120.0;
    double progress = 0.0;
    godot::String stage, text, error;
    godot::Dictionary result;
    double finished_s = 0.0;
    void read_lines(std::vector<godot::Dictionary> *r_lines);

public:
    // p_args 接在「python -X utf8 zhouzhu_gen.py」后面（不含 --status）；p_status 是状态文件（user:// 或绝对路径，旧的先删）
    bool start(const GenSettings &p_settings, const godot::PackedStringArray &p_args, const godot::String &p_status, double p_timeout_s, godot::String &r_error);
    // 读状态文件新写完的那几行（r_lines：每行一个字典，按先后）；进程退出、超时在这里定
    void poll(std::vector<godot::Dictionary> *r_lines = nullptr);
    void kill();

    State get_state() const { return state; }
    bool is_running() const { return state == RUNNING; }
    bool is_done() const { return state == DONE; }
    bool is_failed() const { return state == FAILED; }
    double get_progress() const { return progress; }
    godot::String get_stage() const { return stage; }
    godot::String get_text() const { return text; }
    godot::String get_error() const { return error; }
    godot::Dictionary get_result() const { return result; }
    double elapsed_s() const; // 起了多久（做完了就是做了多久）
    godot::String get_log_path() const { return status_path + godot::String(".log"); }
};
