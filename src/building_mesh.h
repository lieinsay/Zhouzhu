#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "building_parts.h"
#include "map_ground.h"
#include "settlement_model.h"

// 一座建筑盖出来的样子（纯数据）：Settlement 照它建节点——拼几何这一大半就能放到后台线程上做。不含 Godot 头，无状态。
//
// 地坪（building_frame）：勘察占地（survey_site，不管别的房子）给出地坪高与台基底；没有地面（地形换了、这座悬在岛外）
//   照画在岸缘高度（地坪 = 台基高），开发者一眼能看见它该挪了。
// 细模（build_building_mesh）：building_parts（外壳 + 房屋模板）的盒子按组、按材质合并成三角网格——建筑局部坐标，原点在地坪中心，
//   Godot 的正面顺时针；挡人的盒子合成整座一份碰撞三角面（上层设成两面都挡）。悬山顶（檐口上一道屋脊 + 两坡 + 两山的三角 + 檐下一面）
//   也拼进顶那一组的瓦面里，不另挂网格。每层一盏灯（位置、照多远、在哪一组）、梯子区域、外门照 building_parts 给。
// 简模（append_building_proxy）：远处看的——台基、四面墙（不开门窗）、悬山顶，顶点色上色（与细模的材质同色），
//   按调用方给的原点合进同一份网格（一格一个，见 village_prep.h）。台基与墙往里缩一点：细模挂上的那几帧两份都在，细模盖得住简模。

struct MeshPart {
    std::vector<float> positions; // xyz
    std::vector<float> normals; // xyz
    std::vector<float> colors; // rgba，sRGB（只有简模用）
    std::vector<int32_t> indices;
    std::vector<float> uvs; // 米制逐构件UV，v沿构件长轴。
    std::vector<float> uv2s; // y=16为米制UV；两项为负时存端面宽高，单次映射端纹。原生光照UV不会误判。

    bool empty() const { return indices.empty(); }
    int vertex_count() const { return int(positions.size() / 3); }
};

struct BuildingFrame {
    double floor_y = 0.0; // 地坪（宿主局部坐标的 y）
    double bottom = 0.0; // 台基底
    bool grounded = true; // 勘察时占地里有地面
};

BuildingFrame building_frame(const SiteGround &p_ground, const BuildingDef &p_def, const Building &p_building, const SiteRules &p_rules);

struct BuildingLamp {
    double x = 0.0, y = 0.0, z = 0.0; // 建筑局部坐标
    double range = 1.0;
    int group = 0;
};

struct BuildingMesh {
    int index = -1; // 清单下标
    BuildingFrame frame;
    std::string template_id; // 用的哪套模板（不是房屋的是外壳 id）
    int levels = 1;
    int group_count = 0; // 0 = 地面上的，1 + L = 第 L 层，最后一组 = 顶
    std::vector<std::array<MeshPart, size_t(PartMaterial::COUNT)>> groups;
    std::vector<float> collision; // 每三个点（xyz）一个三角面
    std::vector<LadderZone> ladders;
    std::vector<DoorEntry> doors;
    std::vector<BuildingLamp> lamps;
    std::vector<BoxPart> visual_models; // 主线程挂载作者模型；纯数据线程不读取Godot资源或玩法草稿。

    int roof_group() const { return group_count - 1; }
    int triangle_count() const; // 画的三角面（各组各材质加起来）
};

// p_map 是宿主的地面（量门前坡道落到哪）；地坪已经量好（building_frame）。p_template 是这座该用的房屋模板（不是房屋的不看）
BuildingMesh build_building_mesh(const MapGround &p_map, const BuildingFrame &p_frame, const Building &p_building, const HouseTemplate &p_template, int p_index,bool p_separate_furniture=false);

// 细模各材质的颜色（sRGB）与粗糙度：Settlement 的材质、简模的顶点色都用这一份
void part_material_color(PartMaterial p_material, float &r_r, float &r_g, float &r_b, float &r_roughness);

// 简模：p_origin 是网格的原点（宿主局部坐标），顶点 = 宿主局部坐标 − 原点
void append_building_proxy(MeshPart &r_part, const BuildingDef &p_def, const Building &p_building, const BuildingFrame &p_frame, double p_origin_x, double p_origin_z);
