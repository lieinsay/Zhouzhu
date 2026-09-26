#pragma once

#include <map>
#include <string>
#include <vector>

#include "house_template.h"
#include "settlement_model.h"

// 一库房屋模板：id → 模板，外加「这座房子用哪套」的规矩。不含 Godot 头。
// 挑哪套：写了款式且库里有就用它；否则在同外壳、能自动挑的几套里按那座的位置（building_id 去掉款式）哈希挑一套；
// 这种外壳一套都没有就给只在正面开一扇门的空屋（bare_template）。
// TemplateLibrary（autoload）在主线程上改它自己的那张表；后台线程（村子的细模在那儿拼）拿 TemplateSet 快照——一份只读的值，
// 库改一次就另出一份，旧快照照样能用，所以不和主线程的编辑抢同一张表。

using TemplateMap = std::map<std::string, HouseTemplate>;

std::vector<std::string> template_ids_of(const TemplateMap &p_templates, const std::string &p_shell); // 这种外壳的几套（按 id 排序）
std::vector<std::string> template_auto_ids_of(const TemplateMap &p_templates, const std::string &p_shell); // 其中能自动挑的
std::string resolve_template_id(const TemplateMap &p_templates, const Building &p_building); // 空 = 库里没有这种外壳
HouseTemplate resolve_template(const TemplateMap &p_templates, const Building &p_building);

struct TemplateSet {
    TemplateMap templates;

    std::string resolve_id(const Building &p_building) const { return resolve_template_id(templates, p_building); }
    HouseTemplate resolve(const Building &p_building) const { return resolve_template(templates, p_building); }
};
