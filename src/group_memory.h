#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "farm_model.h"
#include "resource_scatter.h"
#include "resource_sites.h"

// 一个岛群离开细版 / 近景时留下的账（模拟分级，计划 4.6）：GroupStream 留在内存里，行星计划 P4 起另存进世界存档目录
// （user://worlds/<世界>/memory/<节点>.txt），退出再进还在。不含 Godot 头。
struct GroupMemory {
    // 细版 → 近景：各村的账（save_village_state 的文本；sites.settlements 下标 → 文本）与存下时的钟（秒）
    std::map<int, std::string> ledgers;
    double ledgers_at = -1.0;
    // 近景 → 远景：农田里的地（玩家垦的、种的）、采场的存量、散布物的采空账
    std::vector<std::pair<uint64_t, Plot>> plots;
    double plots_at = -1.0;
    std::vector<ResourceStock> stocks;
    ScatterLedger scatter;
    bool has_world = false;

    bool empty() const { return ledgers.empty() && !has_world; }
};

// 存成文本：一行一条，村的账按字节数分帧（账本身是多行的）。格式见 group_memory.cpp 开头
std::string save_group_memory(const GroupMemory &p_memory);
// 读回；失败返回 false，r_error 说哪一行怎么了
bool load_group_memory(const std::string &p_text, GroupMemory &r_memory, std::string &r_error);
