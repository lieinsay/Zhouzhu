#include "god_works.h"

#include <algorithm>
#include <cstdlib>

#include "build_pieces.h"

std::vector<GodTool> god_tool_list(int p_style_count) {
    std::vector<GodTool> out;
    GodTool dig;
    dig.kind = GodToolKind::DIG;
    out.push_back(dig);
    GodTool raise;
    raise.kind = GodToolKind::RAISE;
    out.push_back(raise);
    for (int k = 0; k < pieces::COUNT; k++) {
        GodTool t;
        t.kind = GodToolKind::PIECE;
        t.item = pieces::DEFS[k].item;
        out.push_back(t);
    }
    for (int s = 0; s < p_style_count; s++) {
        GodTool t;
        t.kind = GodToolKind::STYLE;
        t.style = s;
        out.push_back(t);
    }
    return out;
}

std::string god_tool_held(const GodTool &p_tool) {
    switch (p_tool.kind) {
        case GodToolKind::DIG:
            return "stone_pick";
        case GodToolKind::RAISE:
            return "earth";
        case GodToolKind::PIECE:
            return p_tool.item;
        default:
            return std::string();
    }
}

int god_brush_step(int p_size, int p_dir) {
    constexpr int N = int(sizeof(GOD_BRUSH_SIZES) / sizeof(GOD_BRUSH_SIZES[0]));
    int at = 0;
    for (int k = 1; k < N; k++) {
        if (std::abs(GOD_BRUSH_SIZES[k] - p_size) < std::abs(GOD_BRUSH_SIZES[at] - p_size)) {
            at = k;
        }
    }
    return GOD_BRUSH_SIZES[std::clamp(at + (p_dir > 0 ? 1 : p_dir < 0 ? -1 : 0), 0, N - 1)];
}

std::vector<std::pair<int, int>> god_brush_cells(int p_i, int p_j, int p_size) {
    const int r = std::max(0, (p_size - 1) / 2);
    std::vector<std::pair<int, int>> out;
    out.reserve(size_t((2 * r + 1) * (2 * r + 1)));
    for (int di = -r; di <= r; di++) {
        for (int dj = -r; dj <= r; dj++) {
            out.push_back({ di, dj });
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
        return a.first * a.first + a.second * a.second < b.first * b.first + b.second * b.second;
    });
    for (auto &c : out) {
        c.first += p_i;
        c.second += p_j;
    }
    return out;
}
