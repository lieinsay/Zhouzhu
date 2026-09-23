#pragma once

#include <map>
#include <string>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "house_template.h"
#include "settlement_model.h"

// 房屋模板库（autoload「BuildingTemplates」，game/building_templates.tscn）：全局只有一份。
// 开局把 dir（res://buildings）下 <外壳>/<款式>.txt 全读进来；write_dir 另设时（测试用 user://）也读它、同 id 的以它为准，存盘写到它。
// 聚落按清单里每座的款式向它要模板：写了款式就用那一套，没写就按那座的位置（building_id 去掉款式）哈希在同外壳的几套里挑一套，
// 所以自动铺的村子家家不一样、每次进游戏又都一样。库里没有这种外壳的模板就给「只在正面开一扇门」的空屋。
// 室内编辑器改了模板用 put 换进来（发 changed，聚落据此重画用到它的房子），F5 用 save 写回文件。
class TemplateLibrary : public godot::Node {
    GDCLASS(TemplateLibrary, godot::Node)

    static TemplateLibrary *singleton;

    godot::String dir = "res://buildings";
    godot::String write_dir; // 空 = 与 dir 同
    std::map<std::string, HouseTemplate> templates; // id → 模板
    std::map<std::string, bool> dirty; // 改过没存的
    bool loaded = false;

    void ensure_loaded() const;
    void load_dir(const godot::String &p_dir);

protected:
    static void _bind_methods();

public:
    static TemplateLibrary *get_singleton() { return singleton; }

    TemplateLibrary();
    ~TemplateLibrary() override;

    // 给同在 C++ 里的聚落与编辑器
    const HouseTemplate *find(const std::string &p_id) const;
    std::vector<std::string> ids_of(const std::string &p_shell) const; // 这种外壳的几套（按 id 排序）
    std::vector<std::string> auto_ids_of(const std::string &p_shell) const; // 其中能自动挑的
    // 这座建筑用哪套：写了款式且库里有就用它，否则按位置挑；返回的是拷贝
    HouseTemplate resolve(const Building &p_building) const;
    std::string resolve_id(const Building &p_building) const;
    void put(const HouseTemplate &p_template); // 换进来（没有就加），发 changed
    std::string unused_style(const std::string &p_shell, const std::string &p_base) const; // 「另存为」用：p_base2、p_base3……里第一个没用过的
    bool is_dirty(const std::string &p_id) const;

    // 给脚本与测试
    void reload();
    bool save(const godot::String &p_id); // 写到 write_dir/<外壳>/<款式>.txt
    godot::String path_of(const godot::String &p_id) const;
    godot::PackedStringArray get_ids(const godot::String &p_shell) const;
    godot::PackedStringArray get_lines(const godot::String &p_id) const; // 模板的文本（没有返回空）
    godot::String get_name_of(const godot::String &p_id) const;
    godot::PackedStringArray get_problems(const godot::String &p_id) const; // interior_report 的毛病

    void set_dir(const godot::String &p_value);
    godot::String get_dir() const { return dir; }
    void set_write_dir(const godot::String &p_value);
    godot::String get_write_dir() const { return write_dir; }
};
