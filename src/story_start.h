#pragma once

// 剧情世界（seed42）的开局（地貌与聚落计划 P0，docs/PLAN-LAND.md 第三节 L4、L5；行星计划 D2、D10、D11）：
// 哪一群出生、钟按哪一群的地方时、开局是一年里的哪天、仓库里带哪几群的细版——都在这一处。
// 游戏里各节点的缺省（WorldSession、GroupProvider、GroupStream、PlanetView、WorldClock、World、SiteStream、WorldLedger）、
// 单测、体检工具与 GDScript（StoryStart 类的静态方法）都从这里读；换开局只改这里，再照 README「地貌与聚落 P0」重导仓库里带的包
// （tools/zhouzhu_gen.cpp 的 group 与 near-lods）、重录 e2e。
// 场景 world_seed42.tscn 里的出发码头（开局那群的主码头 #1）、目的码头是这一群的码头号，也跟着开局走。
// 不含 Godot 头：单测、体检（纯 C++）也用。
namespace story_start {

constexpr int SEED = 42; // 世界种子（行星包 game/planet/seed42，存档目录 user://worlds/seed42）
constexpr int SPAWN_NODE = 6329; // 出生的岛群（L5：地区 1，北纬 34.2°、东经 27.8°，年降水 810 mm；2026-09-29 以前是 #2051）
constexpr int CLOCK_NODE = 6329; // 钟点 = 这一群的地方时（D10）
constexpr int CALENDAR_START = 85; // 开局是一年里的第几天（L5：四月初二，开春第一个晴天；#2051 时是 3）
constexpr int NEIGHBOR_NODE = 6327; // 仓库里另带细版的邻群：离开局那群最近（群心之间的大圆距离 84 km，同当初 2050 离 2051 114 km 的量法）

constexpr const char *PLANET_DIR = "res://planet/seed42"; // 行星包（planet.bin、lods.bin、weather.bin、config.tsv）
constexpr const char *GROUPS_DIR = "res://planet/seed42/groups"; // 仓库里带的岛群细版：<GROUPS_DIR>/<节点>/（同存档目录里的 groups/<节点>/）
constexpr const char *SETTLEMENTS_DIR = "res://settlements"; // 剧情世界的村落改动（布置模式 F5）：<SETTLEMENTS_DIR>/<节点>/

// 相对仓库根（单测、体检工具用）
constexpr const char *PLANET_REL = "game/planet/seed42";
constexpr const char *GROUPS_REL = "game/planet/seed42/groups";

} // namespace story_start
