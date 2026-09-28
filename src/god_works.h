#pragma once

#include <string>
#include <utility>
#include <vector>

// 开发者营造（拓展切片十一）：god 模式下不看背包，从开发者营造面板（F8）挑一件工具直接挖地、搭建。不含 Godot 头，无状态。
//
// 工具清单的次序：挖、垒、十一种搭建件（搭建件表的次序）、每套整座样式。挖当成手上拿着石镐（土石都挖得动），垒当成拿着土，
// 件就是那件；样式由搭建（PieceHand）自己拿着。挖垒带一个笔刷：以准星那一格为心的 n × n 格（1、3、5、9），按一下每格改一级。

enum class GodToolKind {
    DIG,
    RAISE,
    PIECE,
    STYLE,
};

struct GodTool {
    GodToolKind kind = GodToolKind::DIG;
    std::string item; // 件：物品 id
    int style = -1; // 样式：第几套
};

// 清单：挖、垒、十一种件、p_style_count 套样式
std::vector<GodTool> god_tool_list(int p_style_count);
// 这件工具当成手上拿着的什么物品（挖 = 石镐、垒 = 土、件 = 那件、样式 = 空）
std::string god_tool_held(const GodTool &p_tool);
// 挖垒才有笔刷
inline bool god_tool_brushes(const GodTool &p_tool) { return p_tool.kind == GodToolKind::DIG || p_tool.kind == GodToolKind::RAISE; }

// 笔刷：1、3、5、9 格见方
constexpr int GOD_BRUSH_SIZES[] = { 1, 3, 5, 9 };
// 换一档（p_dir = ±1，到头不绕）；不在档上的先落到最近的档
int god_brush_step(int p_size, int p_dir);
// 以 (p_i, p_j) 为心的 p_size × p_size 格，心那一格在最前，其余按离心由近到远（同距按行列）
std::vector<std::pair<int, int>> god_brush_cells(int p_i, int p_j, int p_size);
