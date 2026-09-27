#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 后台任务池：几条工作线程按优先级取活（数小的先；一样小的先来先做）。不含 Godot 头。
// 流式加载的各种备料（地面块的网格、散布块、水面块、村子的铺底与简模、一格房子的细模、村民的导航格……）都进这一个池，
// 优先级一律填「离焦点多远」（米），所以近处的地面、村子、树不分种类先做。
//
// 活有键：高 8 位是种类（JobKind），其余是这种里的号。同键的活还在排队时再交一次，就换成新的做法与优先级；
// 正在做（或做完了还没取）的不再排第二份，做完照样交回——用不用由交活的那头定。
// 做完的结果放在「做完了」里，由主线程按种类取走（collect）。排着的可以按键、按种类撤掉，焦点挪了可以按种类重排优先级。
// 急用（瞬移、开局）：run_now 把排着的那件拿到调用的线程上当场做；正在做就等它做完；两样都不是就返回空。
// 工作线程只跑交进来的做法，做法自己保证只读共享的东西（地形、模板快照……建好后不再变）。

struct JobResult {
    virtual ~JobResult() = default;
};

enum class JobKind : uint8_t {
    TILE = 1, // 地面块
    SCATTER = 2, // 散布块
    RIVER = 3, // 水面块
    VILLAGE = 4, // 村子备料：铺底、地坪、简模、占地
    CELL = 5, // 村子里一格的细模
    NAV = 6, // 村民的导航格
    FAR_TREES = 7, // 远处的林子
    FAR_GROUP = 8, // 远处的岛群（行星计划 P1 的低模）
    PLANET = 9, // 整颗行星：行星贴图的六个面、大气的两张表（行星计划 P2）
    TEST = 200, // 单元测试用
};

class JobPool {
public:
    using Work = std::function<std::unique_ptr<JobResult>()>;
    struct Done {
        uint64_t key = 0;
        std::unique_ptr<JobResult> result;
    };
    enum class State : uint8_t {
        NONE,
        QUEUED,
        RUNNING,
        DONE, // 做完了，还没取走
    };

    static uint64_t key(JobKind p_kind, uint64_t p_id) { return (uint64_t(p_kind) << 56) | (p_id & 0x00FFFFFFFFFFFFFFull); }
    static JobKind kind_of(uint64_t p_key) { return JobKind(uint8_t(p_key >> 56)); }
    static uint64_t id_of(uint64_t p_key) { return p_key & 0x00FFFFFFFFFFFFFFull; }

private:
    struct Queued {
        double priority = 0.0;
        uint64_t seq = 0;
        Work work;
    };
    using Order = std::tuple<double, uint64_t, uint64_t>; // 优先级、先后、键

    mutable std::mutex mutex;
    std::condition_variable wake; // 有活了 / 要停了
    std::condition_variable finished; // 有一件做完了（run_now 等正在做的）
    std::unordered_map<uint64_t, Queued> queued;
    std::set<Order> order;
    std::unordered_set<uint64_t> running;
    std::map<uint8_t, std::vector<Done>> done;
    std::unordered_set<uint64_t> done_keys;
    std::vector<std::thread> threads;
    uint64_t next_seq = 0;
    bool stopping = false;
    uint64_t ran = 0;

    void loop();
    void enqueue_locked(uint64_t p_key, double p_priority, Work &&p_work);
    void dequeue_locked(uint64_t p_key);

public:
    JobPool() = default;
    ~JobPool();
    JobPool(const JobPool &) = delete;
    JobPool &operator=(const JobPool &) = delete;

    // 开 p_threads 条工作线程（已经开着就不动）；stop 撤掉排着的、等正在做的做完、收线程（做完的结果也丢掉）
    void start(int p_threads);
    void stop();
    int thread_count() const { return int(threads.size()); }

    // 交一件活：排上了（或换掉了排着的同键活）返回 true；同键正在做或做完没取返回 false
    bool submit(uint64_t p_key, double p_priority, Work p_work);
    bool cancel(uint64_t p_key); // 撤掉排着的；撤了返回 true
    void cancel_kind(JobKind p_kind); // 这种排着的全撤
    // 这种排着的逐件重算优先级（p_priority 返回 < 0 就撤掉）
    void reprioritize(JobKind p_kind, const std::function<double(uint64_t)> &p_priority);

    State state(uint64_t p_key) const;
    bool busy(uint64_t p_key) const { return state(p_key) != State::NONE; }
    size_t queued_count(JobKind p_kind) const;
    size_t running_count() const;
    uint64_t get_ran() const; // 一共做完了几件（统计）

    // 这件急用：排着就在本线程当场做；正在做就等它做完；做完没取就取走。拿到结果返回它（不再进「做完了」），否则返回空
    std::unique_ptr<JobResult> run_now(uint64_t p_key);
    // 取走这种做完了的（追加进 r_out，按做完的先后）
    void collect(JobKind p_kind, std::vector<Done> &r_out);
};
