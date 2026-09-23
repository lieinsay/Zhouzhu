#pragma once

#include <memory>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "ground_host.h"
#include "settlement_model.h"

// 一处聚落（村落 / 城镇的房子）：地面宿主（GroundHost：游戏尺度的 Island、岛群里的 WorldVillage、平板岛）的子节点，
// 变换留在原点（宿主的局部坐标就是它的局部坐标）；地面高与禁区都问宿主的地面（禁区规矩见 map_ground 的 ground_block）。
// 清单 = 底清单 + 改动：底清单由宿主用 set_base 给（岛群里的村是铺村算法当场铺的；甲乙两岛没有，是空的），
// 改动从 layout_path 指的文本文件读（一行一条：kind@x,z,yaw° 是加，-kind@x,z,yaw° 是拆，# 开头是注释，见 LayoutEdits）。
// 开发者在布置模式里改过之后 save() 把「现在相对底清单改了什么」写回同一个文件——底是空的时候，这就是整份清单，与旧格式一样；
// 所以摆好的村落随仓库提交、能看 diff，岛群里的村也不必整村写死成文件。
// 宿主 rebuilt 后按清单重建几何：每座 = 外壳 + 房屋模板（模板库按款式给，见 TemplateLibrary）拼出来的台基、门前坡道、开了门窗的墙、
// 楼板与梯子、隔墙、家具、天花板与悬山顶；人走得进去（碰撞见 building_parts.h），模板库改了就重画。
// 它只管清单、勘察与画；谁在布置、光标在哪、选了哪种，都是编排的事。
class Settlement : public godot::StaticBody3D {
    GDCLASS(Settlement, godot::StaticBody3D)

    godot::String layout_path;
    GroundHost *host = nullptr;
    std::unique_ptr<MapGround> map; // 宿主给的地面；宿主 rebuilt 时换新
    std::unique_ptr<MapSiteGround> ground; // 地面 + 选址规矩
    SettlementLayout base; // 底清单（宿主给）
    SettlementLayout layout; // 底 + 改动
    SiteRules rules;
    bool dirty = false;
    std::vector<godot::Ref<godot::StandardMaterial3D>> materials; // 按 PartMaterial 的次序

    // 每座盖出来的样子（下标同清单）：节点、分组、用的哪套模板、门与梯子（建筑局部坐标）
    struct Built {
        godot::Node3D *root = nullptr;
        godot::Node *collider = nullptr;
        std::vector<godot::Node3D *> groups; // 地面、各层、顶
        std::string template_id;
        int levels = 1;
        struct Door {
            godot::Vector3 out, in;
            bool front = false;
        };
        std::vector<Door> doors;
        struct Ladder {
            godot::Vector3 foot, top;
        };
        std::vector<Ladder> ladders;
    };
    std::vector<Built> built_list;
    int cutaway_index = -1, cutaway_level = -1;

    void on_host_rebuilt();
    bool ready() const { return host && host->host_ready() && ground; }
    godot::Transform3D host_transform() const { return host->host_node()->get_global_transform(); }
    void clear_generated();
    void build_all();
    void build_one(int p_index);
    godot::Vector3 to_island(const godot::Vector3 &p_global) const;
    void refresh_ground();
    void apply_cutaway(int p_index);
    void rebuild_one(int p_index); // 只重画这一座（模板改了）

protected:
    static void _bind_methods();

public:
    void _ready() override;

    // 清单
    godot::PackedStringArray get_layout() const;
    void set_layout(const godot::PackedStringArray &p_ids); // 整份换掉并重建
    // 底清单（给同在 C++ 里的宿主：铺村算法算出来的）；改动照旧从 layout_path 叠上去
    void set_base(const SettlementLayout &p_base);
    int get_count() const { return layout.size(); }
    const SettlementLayout &get_model() const { return layout; } // 给同在 C++ 里的上层读（村民据此找家门、绕开房子）
    godot::String get_building_kind(int p_index) const;
    godot::Vector3 get_building_position(int p_index) const; // 全局，地坪中心
    double get_building_yaw_deg(int p_index) const;
    godot::Transform3D get_building_transform(int p_index) const; // 全局：原点在地坪中心，−z 是门朝向的反面

    // 勘察与编辑（全局坐标；yaw 弧度，全局）
    godot::String survey(const godot::String &p_kind, const godot::Vector3 &p_global, double p_yaw) const; // 空串 = 能盖
    godot::String survey_building(int p_index) const; // 已有的那座眼下还站得住吗（忽略它自己）
    godot::String place(const godot::String &p_kind, const godot::Vector3 &p_global, double p_yaw, const godot::String &p_style = godot::String()); // 空串 = 放了
    bool remove(int p_index);
    int find_at(const godot::Vector3 &p_global) const; // 点落在哪一座的占地里；没有 −1
    // 离 p_global 最近、能盖 p_kind 的位置（0.5 m 格上搜，半径 p_radius 内）；找不到返回 y = NAN
    godot::Vector3 find_site(const godot::String &p_kind, const godot::Vector3 &p_global, double p_yaw, double p_radius) const;
    // 给同在 C++ 里的上层：勘察报告 + 幽灵该摆的全局位姿（没地面时地坪取 p_global.y）
    SiteReport survey_site_global(const BuildingDef &p_def, const godot::Vector3 &p_global, double p_yaw, godot::Transform3D &r_transform, int p_ignore = -1) const;
    godot::Transform3D building_transform_global(const BuildingDef &p_def, const godot::Vector3 &p_global, double p_yaw, double p_floor_y) const;

    // 房屋模板：款式（清单里写的，空 = 自动挑）、实际用的模板 id；换款式占地不变、不用勘察
    godot::String get_building_style(int p_index) const;
    void set_building_style(int p_index, const godot::String &p_style);
    godot::String get_building_template(int p_index) const;
    // 正门：[门外坡道脚下的地面点, 门里一点]（全局）；楼梯：[梯脚, 梯顶落脚处, …]（全局）
    godot::PackedVector3Array get_building_ladders(int p_index) const;
    godot::PackedVector3Array get_building_entry(int p_index) const;
    // 室内编辑器剖开看：第 p_index 座藏起顶与 p_level 层以上（p_index = −1 都不剖）
    void set_cutaway(int p_index, int p_level);
    void on_templates_changed(const godot::String &p_id);

    // 该处（全局）的地面高（勘察用的那一种）；没有地面 NAN
    double get_ground_height(const godot::Vector3 &p_global) const;

    // 存取
    bool load(); // 按 layout_path 读改动、叠到底清单上；没有文件 = 没改动。返回是否读到了文件
    bool save(); // 把相对底清单的改动写回 layout_path；没设路径返回 false
    bool is_dirty() const { return dirty; }
    int get_base_count() const { return base.size(); }

    void set_layout_path(const godot::String &p_path);
    godot::String get_layout_path() const { return layout_path; }
    void set_max_rise(double p_value);
    double get_max_rise() const { return rules.max_rise_m; }
    GroundHost *get_host() const { return host; }
};
