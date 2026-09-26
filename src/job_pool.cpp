#include "job_pool.h"

#include <algorithm>

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
                done[uint8_t(kind_of(key))].push_back({ key, std::move(result) });
                done_keys.insert(key);
            }
        }
        finished.notify_all();
    }
}

void JobPool::enqueue_locked(uint64_t p_key, double p_priority, Work &&p_work) {
    Queued &q = queued[p_key];
    q.priority = p_priority;
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

void JobPool::cancel_kind(JobKind p_kind) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = queued.begin(); it != queued.end();) {
        if (kind_of(it->first) == p_kind) {
            order.erase({ it->second.priority, it->second.seq, it->first });
            it = queued.erase(it);
        } else {
            ++it;
        }
    }
}

void JobPool::reprioritize(JobKind p_kind, const std::function<double(uint64_t)> &p_priority) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<uint64_t> keys;
    for (const auto &entry : queued) {
        if (kind_of(entry.first) == p_kind) {
            keys.push_back(entry.first);
        }
    }
    for (uint64_t key : keys) {
        Queued &q = queued[key];
        order.erase({ q.priority, q.seq, key });
        const double priority = p_priority(key);
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

size_t JobPool::queued_count(JobKind p_kind) const {
    std::lock_guard<std::mutex> lock(mutex);
    size_t n = 0;
    for (const auto &entry : queued) {
        n += kind_of(entry.first) == p_kind ? 1 : 0;
    }
    return n;
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
            std::vector<Done> &list = done[uint8_t(kind_of(p_key))];
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

void JobPool::collect(JobKind p_kind, std::vector<Done> &r_out) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = done.find(uint8_t(p_kind));
    if (it == done.end()) {
        return;
    }
    for (Done &d : it->second) {
        done_keys.erase(d.key);
        r_out.push_back(std::move(d));
    }
    it->second.clear();
}
