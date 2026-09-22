#pragma once

#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "island.h"
#include "settlement_model.h"

// 一座岛上的聚落（村落 / 城镇的房子）：Island 的子节点，变换留在原点（岛的局部坐标就是它的局部坐标）。
// 建筑清单在 SettlementLayout 里，从 layout_path 指的文本文件读（一座一行 "kind@x,z,yaw°"，# 开头是注释），
// 开发者在布置模式里改过之后 save() 写回同一个文件——所以摆好的村落随仓库提交，场景文件本身不用动。
// 岛 rebuilt 后按清单重建几何：每座 = 台基（埋进坡里、把地坪架平）+ 墙 + 悬山顶 + 门，台基与墙带碰撞。
// 它只管清单、勘察与画；谁在布置、光标在哪、选了哪种，都是 Voyage 的事。
class Settlement : public godot::StaticBody3D {
    GDCLASS(Settlement, godot::StaticBody3D)

    godot::String layout_path;
    Island *island = nullptr;
    SettlementLayout layout;
    SiteRules rules;
    bool dirty = false;
    godot::Ref<godot::StandardMaterial3D> mat_stone, mat_wall, mat_roof, mat_door;

    void on_island_rebuilt();
    void clear_generated();
    void build_all();
    void build_one(int p_index);
    godot::Vector3 to_island(const godot::Vector3 &p_global) const;
    double dock_apron() const;

protected:
    static void _bind_methods();

public:
    void _ready() override;

    // 清单
    godot::PackedStringArray get_layout() const;
    void set_layout(const godot::PackedStringArray &p_ids); // 整份换掉并重建
    int get_count() const { return layout.size(); }
    godot::String get_building_kind(int p_index) const;
    godot::Vector3 get_building_position(int p_index) const; // 全局，地坪中心
    double get_building_yaw_deg(int p_index) const;
    godot::Transform3D get_building_transform(int p_index) const; // 全局：原点在地坪中心，−z 是门朝向的反面

    // 勘察与编辑（全局坐标；yaw 弧度，全局）
    godot::String survey(const godot::String &p_kind, const godot::Vector3 &p_global, double p_yaw) const; // 空串 = 能盖
    godot::String survey_building(int p_index) const; // 已有的那座眼下还站得住吗（忽略它自己）
    godot::String place(const godot::String &p_kind, const godot::Vector3 &p_global, double p_yaw); // 空串 = 放了
    bool remove(int p_index);
    int find_at(const godot::Vector3 &p_global) const; // 点落在哪一座的占地里；没有 −1
    // 离 p_global 最近、能盖 p_kind 的位置（0.5 m 格上搜，半径 p_radius 内）；找不到返回 y = NAN
    godot::Vector3 find_site(const godot::String &p_kind, const godot::Vector3 &p_global, double p_yaw, double p_radius) const;
    // 给同在 C++ 里的上层：勘察报告 + 幽灵该摆的全局位姿（没地面时地坪取 p_global.y）
    SiteReport survey_site_global(const BuildingDef &p_def, const godot::Vector3 &p_global, double p_yaw, godot::Transform3D &r_transform, int p_ignore = -1) const;
    godot::Transform3D building_transform_global(const BuildingDef &p_def, const godot::Vector3 &p_global, double p_yaw, double p_floor_y) const;

    // 存取
    bool load(); // 按 layout_path 读；没有文件 = 空清单。返回是否读到了文件
    bool save(); // 写回 layout_path；没设路径返回 false
    bool is_dirty() const { return dirty; }

    void set_layout_path(const godot::String &p_path);
    godot::String get_layout_path() const { return layout_path; }
    void set_max_rise(double p_value);
    double get_max_rise() const { return rules.max_rise_m; }
    Island *get_island() const { return island; }
};
