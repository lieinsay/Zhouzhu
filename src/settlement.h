#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include "building_mesh.h"
#include "ground_host.h"
#include "settlement_model.h"
#include "shared_looks.h"

// 后台备好的一座细模：BuildingMesh 的网格转成了引擎的数组（to_building_arrays 可以在后台线程上调），主线程只建节点
struct BuildingArrays {
    BuildingMesh mesh; // 网格已经挪走（各组各材质的数组清空了）；地坪、门、梯子、灯、模板 id 照旧
    struct Surface {
        int group = 0;
        int material = 0; // PartMaterial
        godot::PackedVector3Array vertices, normals;
        godot::PackedVector2Array uvs, uv2s;
        godot::PackedInt32Array indices;
    };
    std::vector<Surface> surfaces;
    godot::PackedVector3Array collision;
};
BuildingArrays to_building_arrays(BuildingMesh &&p_mesh);

// 一处聚落（村落 / 城镇的房子）：地面宿主（GroundHost：游戏尺度的 Island、岛群里的 WorldVillage、平板岛）的子节点，
// 变换留在原点（宿主的局部坐标就是它的局部坐标）；地面高与禁区都问宿主的地面（禁区规矩见 map_ground 的 ground_block）。
// 清单 = 底清单 + 改动：底清单由宿主用 set_base 给（岛群里的村是铺村算法铺的；甲乙两岛没有，是空的），
// 改动从 layout_path 指的文本文件读（一行一条：kind@x,z,yaw° 是加，-kind@x,z,yaw° 是拆，# 开头是注释，见 LayoutEdits）。
// 开发者在布置模式里改过之后 save() 把「现在相对底清单改了什么」写回同一个文件——底是空的时候，这就是整份清单，与旧格式一样；
// 所以摆好的村落随仓库提交、能看 diff，岛群里的村也不必整村写死成文件。
//
// 每座房子先有数据、再挂节点：宿主 rebuilt 后按清单量好每座的地坪（BuildingFrame）——位姿、船台的接触线都只看它；
// 细模（building_mesh.h：外壳 + 房屋模板拼出来的台基、门前坡道、开了门窗的墙、楼板与梯子、隔墙、家具、天花板与悬山顶，人走得进去）
// 是另挂的节点：一座一个根节点（按组分：地面、各层、顶，给室内编辑器剖开看）+ 聚落这个静态体下的一份凹多边形碰撞 + 梯子区域 + 屋里的灯。
// 平常（甲乙两岛、平板岛）一进树就全挂上；deferred（岛群里的村，进树之前设）时只备数据，节点由上层按格子一座座挂（mount / unmount，
// 细模在后台线程上备好：to_building_arrays），远处的村子只画简模（WorldVillage）。布置模式改了清单，挂着的那几座照新清单当场重挂。
// 材质问宿主要共用的一份（host_looks），宿主不给就自己建一份。模板库改了就重画用到它的、挂着的房子。
// 它只管清单、勘察与画；谁在布置、光标在哪、选了哪种，都是编排的事。
class Settlement : public godot::StaticBody3D {
    GDCLASS(Settlement, godot::StaticBody3D)

public:
    static constexpr const char *GROUP = "settlements";

private:
    godot::String layout_path;
    GroundHost *host = nullptr;
    std::unique_ptr<MapGround> map; // 宿主给的地面；宿主 rebuilt 时换新
    std::unique_ptr<MapSiteGround> ground; // 地面 + 选址规矩
    SettlementLayout base; // 底清单（宿主给）
    SettlementLayout layout; // 底 + 改动
    SiteRules rules;
    bool dirty = false;
    bool deferred = false;
    SharedLooks *looks = nullptr; // 宿主给的，或者 own_looks
    std::unique_ptr<SharedLooks> own_looks;
    std::vector<BuildingFrame> frames; // 与清单一一对应；宿主好了、清单变了就重量
    SettlementLayout prepared_layout; // 进树前给的地坪（后台量好的）对应的清单：对得上就不再量
    std::vector<BuildingFrame> prepared_frames;
    bool has_prepared = false;
    std::set<std::string> remount; // build_all 时 deferred 也当场挂的那几座（building_id）：重建清单之前挂着的
    int64_t layout_version = 0;

    // 每座挂出来的节点（下标同清单；没挂的 root 为 nullptr）：分组、用的哪套模板、门与梯子（建筑局部坐标）
    struct Built {
        godot::Node3D *root = nullptr;
        godot::Node *collider = nullptr;
        std::vector<godot::Node3D *> groups; // 地面、各层、顶
        std::string template_id;
        int levels = 1;
        std::vector<DoorEntry> doors;
        std::vector<LadderZone> ladders;
    };
    std::vector<Built> built_list;
    int cutaway_index = -1, cutaway_level = -1;

    void on_host_rebuilt();
    bool ready() const { return host && host->host_ready() && ground; }
    godot::Transform3D host_transform() const { return host->host_node()->get_global_transform(); }
    void clear_generated();
    std::set<std::string> mounted_ids() const; // 挂着的那几座（building_id）：重建清单之前记下，deferred 时照新清单再挂
    void build_all();
    void build_one(int p_index);
    godot::Vector3 to_island(const godot::Vector3 &p_global) const;
    void refresh_ground();
    void apply_cutaway(int p_index);
    void rebuild_one(int p_index); // 只重画这一座（模板改了）
    godot::Transform3D local_frame(int p_index) const; // 这一座在宿主局部坐标里的位姿（地坪中心）
    const Built &parts_of(int p_index, Built &r_scratch) const; // 挂着就是挂着的那份，没挂就当场拼一份（只要门与梯子）

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
    int64_t get_layout_version() const { return layout_version; } // 清单（或地坪）每重建一次加一：后台按旧清单备的细模据此作废
    godot::String get_building_kind(int p_index) const;
    godot::Vector3 get_building_position(int p_index) const; // 全局，地坪中心
    // 船台（land_berth.h）：清单里的那几座的下标；某座的接触线（全局，X 指向跳板）；某座台面上的一点（船台局部 x、z → 全局，台面高）
    std::vector<int> find_berths() const;
    godot::PackedInt32Array get_berth_indices() const;
    godot::Transform3D get_berth_contact(int p_index) const;
    godot::Vector3 get_berth_point(int p_index, double p_x, double p_z) const;
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

    // 挂细模（岛群里的村由上层按格子调；平常进树就全挂了）
    void set_deferred(bool p_value) { deferred = p_value; } // 进树之前设
    bool is_deferred() const { return deferred; }
    // 进树之前给后台量好的地坪（与 p_layout 一一对应）：进树叠完改动、清单对得上就用它，不再逐座勘察
    void set_prepared_frames(const SettlementLayout &p_layout, const std::vector<BuildingFrame> &p_frames);
    const std::vector<BuildingFrame> &get_frames() const { return frames; }
    BuildingMesh make_mesh(int p_index) const; // 一座的细模（纯计算，主线程上当场拼；按库里此刻的模板）
    void mount(const BuildingArrays &p_arrays); // 挂上一座（已挂的先拆）；下标越界或清单换过（地坪对不上）就不挂
    void unmount(int p_index);
    bool is_mounted(int p_index) const { return p_index >= 0 && size_t(p_index) < built_list.size() && built_list[size_t(p_index)].root; }
    int get_mounted_count() const;

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
    const SiteRules &get_rules() const { return rules; }
    GroundHost *get_host() const { return host; }
};
