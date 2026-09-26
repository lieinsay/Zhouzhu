#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "world_grid.h"

// 地表过渡：生成器的地表类是 100 m 一格「这一格是什么」，游戏里要的是「这一点有几成是什么」。
// 两类接壤处不再一刀切（散布按最近格，林缘齐得像剪过），也不是 100 m 宽的一片糊（地皮按格双线性），
// 而是几十米宽、边上犬牙交错的过渡带；地皮与散布（树、灌丛、苇、石块……）按同一份份额长，地皮变色的地方就是树变稀的地方。
//   1. 扭：点按两层低频噪声挪一挪——几百米一道的大弯（bend_m）让格边上一公里长的直线弯起来，几十米的小扭（warp_m）让林缘、田边不成 100 m 的方格
//   2. 软最近格：扭过的点到周围 4×4 格格心的距离 d，各格按 exp(−d²/τ) 加权、按类累加再归一——一半处的交界线正是最近格的边
//      （各类的面积与生成器的格数一样，只有一格的小片也在），离开交界线按 logistic 渐变，τ 定过渡带宽（transition_m，一成到九成）
//   3. 碎：各类的对数份额加一点细噪声，交界线进退十几二十米（breakup_m）：林缘伸出去一丛丛树、凹进来一块块空地
// footprint = 取样间距（米）：远处的粗网格几十上百米一个顶点，按它渐渐换成 4×4 格三次 B 样条的平滑份额（一百多米的渐变）、
// 碎的噪声退掉——颜色不会闪成噪点，块换精度时也不会忽明忽暗。
// 除了地表类，同一套扭与混也给别的逐格量用（片的种类、针叶的比例、溪涧），它们的边就和地表的边对得上。
// 不含 Godot 头、没有状态、线程安全。

struct CoverRules {
    double bend_m = 50.0;
    double bend_feature_m = 350.0;
    double warp_m = 40.0;
    double warp_feature_m = 90.0;
    double transition_m = 40.0; // 近处两类交界的过渡带宽（份额一成到九成）
    double breakup_m = 20.0; // 交界线按细噪声进退的幅度
    double breakup_feature_m = 24.0;
};

// 扭过的点周围 4×4 格：各自的 B 样条权重（和为 1，远处的平滑用）与到格心的距离平方（近处的软最近格用）
struct CoverStencil {
    size_t cell[16] = {};
    float weight[16] = {};
    float dist2[16] = {}; // 米²
    size_t nearest = 0; // 扭过的点最近的格：片号、岛号这类不渐变的东西从这儿取
    double wx = 0.0, wz = 0.0; // 扭过的点
};

constexpr int COVER_CLASSES = 12; // 地表类（world.json landcover.classes，0 = 虚空）

// 一点的地表份额
struct CoverMix {
    float w[COVER_CLASSES] = {};
    size_t cell = 0; // 同 CoverStencil::nearest

    int dominant() const { return int(std::max_element(w, w + COVER_CLASSES) - w); }
    // 这一类的边：份额 0.5 处为 1，全是或全不是为 0
    float edge(int p_class) const { return 4.0f * w[p_class] * (1.0f - w[p_class]); }
};

class CoverBlend {
    const WorldGrid *grid = nullptr;
    uint64_t seed = 0;
    CoverRules rules;

    // 各格的类别（p_labels[16]，∈ [0, p_n)）→ 份额 r_out[0, p_n)
    void resolve(const CoverStencil &p_stencil, double p_x, double p_z, double p_footprint, uint64_t p_stream, int p_n, const int *p_labels, float *r_out) const;

public:
    static constexpr uint64_t STREAM_COVER = 1; // 地表类的碎噪声
    static constexpr int MAX_LABELS = 32;

    CoverBlend() = default;
    // p_grid 须比本对象活得久
    CoverBlend(const WorldGrid &p_grid, uint64_t p_seed, const CoverRules &p_rules = CoverRules());

    bool valid() const { return grid && !grid->empty(); }
    const CoverRules &get_rules() const { return rules; }

    CoverStencil stencil(double p_x, double p_z) const;
    CoverMix mix(const CoverStencil &p_stencil, double p_x, double p_z, double p_footprint = 0.0) const;
    CoverMix mix(double p_x, double p_z, double p_footprint = 0.0) const { return mix(stencil(p_x, p_z), p_x, p_z, p_footprint); }

    // 任一逐格的类别（p_label(格下标) ∈ [0, p_n)，p_n ≤ MAX_LABELS）按同一套软最近格、碎混成份额；p_stream 区分不同的类别（碎的噪声各用一套）
    template <typename Label>
    void blend(const CoverStencil &p_stencil, double p_x, double p_z, double p_footprint, uint64_t p_stream, int p_n, Label p_label, float *r_out) const {
        int labels[16];
        for (int k = 0; k < 16; k++) {
            labels[k] = std::clamp(int(p_label(p_stencil.cell[k])), 0, p_n - 1);
        }
        resolve(p_stencil, p_x, p_z, p_footprint, p_stream, p_n, labels, r_out);
    }
    // 任一逐格的量按同一套扭与 B 样条平滑取（不碎）：针叶的比例、溪涧
    template <typename Value>
    double smooth(const CoverStencil &p_stencil, Value p_value) const {
        double out = 0.0;
        for (int k = 0; k < 16; k++) {
            out += p_stencil.weight[k] * double(p_value(p_stencil.cell[k]));
        }
        return out;
    }
};

// [0, 1] 夹住的 smoothstep：p_value 从 p_lo 到 p_hi 渐变（p_lo > p_hi 时反过来）
double cover_ramp(double p_value, double p_lo, double p_hi);
