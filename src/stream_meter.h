#pragma once

#include <array>
#include <cstdint>
#include <vector>

// 主线程分项计时：流式加载的各个子系统（地面块、水面、码头、村子、村民、采场、散布……）每一帧在主线程上花了多少微秒。
// 上层量好时间（Time::get_ticks_usec 前后一减）连同帧号报进来；这里只记账：最近 RING 帧逐帧的分项、各项累计与单帧最多。
// 找卡顿用：实测工具在一帧很慢时按帧号查那一帧的分项，就知道是谁。不含 Godot 头，时间不自己取。

enum class StreamChannel : uint8_t {
    TILES, // 地面块：挂网格与碰撞、拆旧块
    RIVERS, // 水面块
    DOCKS, // 码头栈桥
    VILLAGES, // 村与散户：铺底、盖房子
    FOLK, // 村民：招（导航格、开账 / 展开、小人）与散
    SITES, // 采场与地标的实物
    SCATTER, // 散布：挂块（MultiMesh）
    SCATTER_FILTER, // 散布：村子铺了 / 拆了，重查压着房子的
    SCATTER_BODY, // 散布：树干碰撞
    SCATTER_REGROW, // 散布：看倒着的长回来没有
    HINTERLAND, // 远处账房
    COUNT,
};

constexpr int STREAM_CHANNELS = int(StreamChannel::COUNT);

// 频道的短名（给脚本、日志）
const char *stream_channel_key(StreamChannel p_channel);

class StreamMeter {
public:
    static constexpr int RING = 512; // 记最近这么多帧的逐帧分项

    struct Frame {
        uint64_t frame = UINT64_MAX; // 帧号；UINT64_MAX = 这格还没用过
        std::array<uint32_t, STREAM_CHANNELS> usec{};
        uint32_t total() const;
    };

    struct Totals {
        uint64_t usec = 0; // 累计
        uint32_t max_frame_usec = 0; // 单帧最多
        uint32_t frames = 0; // 有过开销的帧数
    };

private:
    std::array<Frame, RING> ring{};
    std::array<Totals, STREAM_CHANNELS> totals{};

    Frame &slot(uint64_t p_frame);

public:
    // 第 p_frame 帧在 p_channel 上又花了 p_usec
    void add(uint64_t p_frame, StreamChannel p_channel, uint64_t p_usec);
    // 第 p_frame 帧的分项；不在最近 RING 帧里（或那一帧什么都没记）返回 nullptr
    const Frame *frame(uint64_t p_frame) const;
    const Totals &get_totals(StreamChannel p_channel) const { return totals[size_t(p_channel)]; }
    void reset();
};
