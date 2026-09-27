#pragma once

#include <string>

#include "skyisle/config.hpp"

// 生成器展平后的配置（行星计划 P6e）：随行星包带着的 config.tsv（tools/export_planet.py 用生成器自己的展平函数导出，格式见那个脚本开头），
// 读成 C++ 核心库要的三张扁平键值表。游戏进程内新建世界、生成岛群都读它，不经 Python。不含 Godot 头。
struct GenConfig {
    skyisle::Config planet; // 行星层 ①–⑨（shared / skeleton / s01–s09 / 槽位表 / 手工特征表）
    skyisle::Config island; // 第三层的 [island] 段
    skyisle::Config engine; // [engine]（threads）
    std::string text; // config.tsv 原文（新建世界时照抄一份进 planet/）
    std::string sha1; // 原文的 SHA-1（stamp.json 的缓存键）
    std::string island_sha1; // island 表那几行的 SHA-1

    int threads() const;
};

// 读 config.tsv 的原文；出错返回 false、r_error 说哪一行
bool parse_gen_config(const std::string &p_text, GenConfig &r_config, std::string &r_error);
