#pragma once

#include <cstdint>

// 确定性随机流（SplitMix64）。
// 不用 <random> 的分布：它们的输出随标准库实现而变，同一个种子在另一台机器上会长出另一座岛。
class Rng {
    uint64_t state;

public:
    explicit Rng(uint64_t p_seed) :
            state(p_seed) {}

    uint64_t next_u64() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // [0, 1)
    double uniform() { return double(next_u64() >> 11) * (1.0 / 9007199254740992.0); }

    double uniform(double p_lo, double p_hi) { return p_lo + (p_hi - p_lo) * uniform(); }

    // [p_lo, p_hi) 上的整数
    int range(int p_lo, int p_hi) { return p_lo + int(uniform() * double(p_hi - p_lo)); }

    // 按部件名分出互不相干的子流：改一个部件取多少随机数，不会牵动别的部件。
    Rng fork(uint64_t p_stream) const {
        Rng r(state ^ (0xD1B54A32D192ED03ull * (p_stream + 1)));
        r.next_u64();
        return r;
    }
};
