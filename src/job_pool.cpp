#include "job_pool.h"

#include <algorithm>
#include <limits>

namespace {
// 优先级按离玩家多远给；玩家位置成了 NaN（river_e2e 里掉出世界时就会）时优先级也是 NaN，
// NaN 跟谁比都「不小于」，std::set 的次序就乱了：按值删删不掉、同一个键在 order 里留两份，
// 工人拿到一份在 queued 里已经没有的键，调一个不存在的做法就崩。NaN 一律排到最后。
double sane_priority(double p_priority) {
    return p_priority == p_priority ? p_priority : std::numeric_limits<double>::max();
}
} // namespace

JobPool::~JobPool() {
    stop();
}

void JobPool::start(int p_threads) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!threads.empty()) {
        return;
    }
    stopping = false;
    for (int k = 0; k < std::max(1, p_threads); k++) {
        threads.emplace_back([this]() { loop(); });
    }
}

void JobPool::stop() {
    std::vector<std::thread> joining;
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        queued.clear();
        order.clear();
        joining.swap(threads);
    }
    wake.notify_all();
    for (std::thread &t : joining) {
        if (t.joinable()) {
            t.join();
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    done.clear();
    done_keys.clear();
    running.clear();
}

void JobPool::loop() {
    for (;;) {
        uint64_t key = 0;
        Work work;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this]() { return stopping || !order.empty(); });
            if (stopping) {
                return;
            }
            key = std::get<2>(*order.begin());
            order.erase(order.begin());
            auto it = queued.find(key);
            if (it == queued.end()) {
                continue;
            }
            work = std::move(it->second.work);
            queued.erase(it);
            running.insert(key);
        }
        std::unique_ptr<JobResult> result = work ? work() : nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            running.erase(key);
            ran++;
            if (!stopping) {
                done[bucket_of(key)].push_back({ key, std::move(result) });
                done_keys.insert(key);
            }
        }
        finished.notify_all();
    }
}

void JobPool::enqueue_locked(uint64_t p_key, double p_priority, Work &&p_work) {
    Queued &q = queued[p_key];
    q.priority = sane_priority(p_priority);
    q.seq = next_seq++;
    q.work = std::move(p_work);
    order.insert({ q.priority, q.seq, p_key });
}

void JobPool::dequeue_locked(uint64_t p_key) {
    auto it = queued.find(p_key);
    if (it == queued.end()) {
        return;
    }
    order.erase({ it->second.priority, it->second.seq, p_key });
    queued.erase(it);
}

bool JobPool::submit(uint64_t p_key, double p_priority, Work p_work) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || running.count(p_key) || done_keys.count(p_key)) {
            return false;
        }
        dequeue_locked(p_key);
        enqueue_locked(p_key, p_priority, std::move(p_work));
    }
    wake.notify_one();
    return true;
}

bool JobPool::cancel(uint64_t p_key) {
    std::lock_guard<std::mutex> lock(mutex);
    const bool had = queued.count(p_key) > 0;
    dequeue_locked(p_key);
    return had;
}

void JobPool::cancel_kind(JobKind p_kind, int p_lane) {
    std::lock_guard<std::mutex> lock(mutex);
    const uint16_t b = bucket(p_kind, p_lane);
    for (auto it = queued.begin(); it != queued.end();) {
        if (bucket_of(it->first) == b) {
            order.erase({ it->second.priority, it->second.seq, it->first });
            it = queued.erase(it);
        } else {
            ++it;
        }
    }
}

void JobPool::reprioritize(JobKind p_kind, const std::function<double(uint64_t)> &p_priority, int p_lane) {
    std::lock_guard<std::mutex> lock(mutex);
    const uint16_t b = bucket(p_kind, p_lane);
    std::vector<uint64_t> keys;
    for (const auto &entry : queued) {
        if (bucket_of(entry.first) == b) {
            keys.push_back(entry.first);
        }
    }
    for (uint64_t key : keys) {
        Queued &q = queued[key];
        order.erase({ q.priority, q.seq, key });
        const double priority = sane_priority(p_priority(key));
        if (priority < 0.0) {
            queued.erase(key);
            continue;
        }
        q.priority = priority;
        order.insert({ q.priority, q.seq, key });
    }
}

JobPool::State JobPool::state(uint64_t p_key) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (queued.count(p_key)) {
        return State::QUEUED;
    }
    if (running.count(p_key)) {
        return State::RUNNING;
    }
    return done_keys.count(p_key) ? State::DONE : State::NONE;
}

size_t JobPool::queued_count(JobKind p_kind, int p_lane) const {
    std::lock_guard<std::mutex> lock(mutex);
    const uint16_t b = bucket(p_kind, p_lane);
    size_t n = 0;
    for (const auto &entry : queued) {
        n += bucket_of(entry.first) == b ? 1 : 0;
    }
    return n;
}

size_t JobPool::queued_in_lane(int p_lane) const {
    std::lock_guard<std::mutex> lock(mutex);
    size_t n = 0;
    for (const auto &entry : queued) {
        n += lane_of(entry.first) == (p_lane & 0xF) ? 1 : 0;
    }
    return n;
}

void JobPool::drain_lane(int p_lane) {
    const int lane = p_lane & 0xF;
    std::unique_lock<std::mutex> lock(mutex);
    for (auto it = queued.begin(); it != queued.end();) {
        if (lane_of(it->first) == lane) {
            order.erase({ it->second.priority, it->second.seq, it->first });
            it = queued.erase(it);
        } else {
            ++it;
        }
    }
    // 正在做的做法里拿着这一群的地形：等它们做完
    finished.wait(lock, [&]() {
        if (stopping) {
            return true;
        }
        for (uint64_t k : running) {
            if (lane_of(k) == lane) {
                return false;
            }
        }
        return true;
    });
    for (auto &entry : done) {
        if ((entry.first & 0xFu) != uint16_t(lane)) {
            continue;
        }
        for (Done &d : entry.second) {
            done_keys.erase(d.key);
        }
        entry.second.clear();
    }
}

size_t JobPool::running_count() const {
    std::lock_guard<std::mutex> lock(mutex);
    return running.size();
}

uint64_t JobPool::get_ran() const {
    std::lock_guard<std::mutex> lock(mutex);
    return ran;
}

std::unique_ptr<JobResult> JobPool::run_now(uint64_t p_key) {
    Work work;
    {
        std::unique_lock<std::mutex> lock(mutex);
        auto it = queued.find(p_key);
        if (it != queued.end()) {
            work = std::move(it->second.work);
            dequeue_locked(p_key);
        } else {
            finished.wait(lock, [&]() { return stopping || !running.count(p_key); });
            if (!done_keys.count(p_key)) {
                return nullptr;
            }
            std::vector<Done> &list = done[bucket_of(p_key)];
            for (auto d = list.begin(); d != list.end(); ++d) {
                if (d->key == p_key) {
                    std::unique_ptr<JobResult> result = std::move(d->result);
                    list.erase(d);
                    done_keys.erase(p_key);
                    return result;
                }
            }
            return nullptr;
        }
    }
    return work ? work() : nullptr;
}

void JobPool::collect(JobKind p_kind, std::vector<Done> &r_out, int p_lane) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = done.find(bucket(p_kind, p_lane));
    if (it == done.end()) {
        return;
    }
    for (Done &d : it->second) {
        done_keys.erase(d.key);
        r_out.push_back(std::move(d));
    }
    it->second.clear();
}
