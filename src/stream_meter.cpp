#include "stream_meter.h"

#include <algorithm>

const char *stream_channel_key(StreamChannel p_channel) {
    switch (p_channel) {
        case StreamChannel::TILES:
            return "tiles";
        case StreamChannel::RIVERS:
            return "rivers";
        case StreamChannel::DOCKS:
            return "docks";
        case StreamChannel::VILLAGES:
            return "villages";
        case StreamChannel::FOLK:
            return "folk";
        case StreamChannel::SITES:
            return "sites";
        case StreamChannel::SCATTER:
            return "scatter";
        case StreamChannel::SCATTER_FILTER:
            return "scatter_filter";
        case StreamChannel::SCATTER_BODY:
            return "scatter_body";
        case StreamChannel::SCATTER_REGROW:
            return "scatter_regrow";
        case StreamChannel::HINTERLAND:
            return "hinterland";
        case StreamChannel::COUNT:
            break;
    }
    return "?";
}

uint32_t StreamMeter::Frame::total() const {
    uint32_t sum = 0;
    for (uint32_t u : usec) {
        sum += u;
    }
    return sum;
}

StreamMeter::Frame &StreamMeter::slot(uint64_t p_frame) {
    Frame &f = ring[size_t(p_frame % RING)];
    if (f.frame != p_frame) {
        f = Frame();
        f.frame = p_frame;
    }
    return f;
}

void StreamMeter::add(uint64_t p_frame, StreamChannel p_channel, uint64_t p_usec) {
    const size_t c = size_t(p_channel);
    if (c >= size_t(STREAM_CHANNELS)) {
        return;
    }
    Frame &f = slot(p_frame);
    const uint32_t before = f.usec[c];
    f.usec[c] = uint32_t(std::min<uint64_t>(uint64_t(before) + p_usec, UINT32_MAX));
    Totals &t = totals[c];
    t.usec += p_usec;
    t.max_frame_usec = std::max(t.max_frame_usec, f.usec[c]);
    if (before == 0 && f.usec[c] > 0) {
        t.frames++;
    }
}

const StreamMeter::Frame *StreamMeter::frame(uint64_t p_frame) const {
    const Frame &f = ring[size_t(p_frame % RING)];
    return f.frame == p_frame ? &f : nullptr;
}

void StreamMeter::reset() {
    ring.fill(Frame());
    totals.fill(Totals());
}
