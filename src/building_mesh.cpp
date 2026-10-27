#include "building_mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "mesh_kit.h"
#include "art_mesh_kit.h"

namespace {

constexpr double PROXY_INSET_M = 0.04; // 简模的台基与墙往里缩这么多（细模盖得住它）
constexpr double PROXY_ROOF_DROP_M = 0.06; // 简模的顶往下压这么多

using namespace mesh_kit;

// 悬山顶：两坡从檐口（高 p_eave、前后 ±p_half_d）到屋脊（高 p_eave + p_rise、z = 0），沿 x 伸到 ±p_half_w；两山各一个三角，檐下一面。建筑局部坐标经 p_xf 摆好
void append_gable(MeshPart &r_part, const Xform &p_xf, double p_half_w, double p_half_d, double p_eave, double p_rise, bool p_underside, const Rgba *p_color = nullptr, MeshPart *p_end = nullptr) {
    const double X = p_half_w, e = p_half_d, y0 = p_eave, y1 = p_eave + p_rise;
    const auto P = [&](double x, double y, double z) { return p_xf.xform({ x, y, z }); };
    polygon(r_part, { P(-X, y0, e), P(X, y0, e), P(X, y1, 0), P(-X, y1, 0) }, p_xf.rotate({ 0, e, p_rise }), p_color);
    polygon(r_part, { P(-X, y0, -e), P(X, y0, -e), P(X, y1, 0), P(-X, y1, 0) }, p_xf.rotate({ 0, e, -p_rise }), p_color);
    polygon(p_end?*p_end:r_part, { P(X, y0, e), P(X, y0, -e), P(X, y1, 0) }, p_xf.rotate({ 1, 0, 0 }), p_color);
    polygon(p_end?*p_end:r_part, { P(-X, y0, e), P(-X, y0, -e), P(-X, y1, 0) }, p_xf.rotate({ -1, 0, 0 }), p_color);
    if (p_underside) {
        polygon(r_part, { P(-X, y0, -e), P(X, y0, -e), P(X, y0, e), P(-X, y0, e) }, p_xf.rotate({ 0, -1, 0 }), p_color);
    }
}

Rgba material_rgba(PartMaterial p_material) {
    Rgba c;
    float rough = 1.0f;
    part_material_color(p_material, c.r, c.g, c.b, rough);
    return c;
}

} // namespace

void part_material_color(PartMaterial p_material, float &r_r, float &r_g, float &r_b, float &r_roughness) {
    struct Row {
        float r, g, b, rough;
    };
    static const Row ROWS[int(PartMaterial::COUNT)] = {
        { 0.52f, 0.50f, 0.46f, 1.0f }, // 台基、台阶
        { 0.76f, 0.64f, 0.46f, 1.0f }, // 夯土墙
        { 0.56f, 0.43f, 0.29f, 0.9f }, // 木地板
        { 0.50f, 0.36f, 0.22f, 0.85f }, // 家具
        { 0.30f, 0.20f, 0.12f, 0.95f }, // 门框、窗棂、天花板
        { 0.30f, 0.30f, 0.34f, 0.9f }, // 灰瓦
        { 0.60f, 0.40f, 0.28f, 1.0f }, // 陶、泥
        { 0.66f, 0.60f, 0.50f, 1.0f }, // 布
        { 0.80f, 0.68f, 0.40f, 1.0f }, // 草、席
        { 0.88f, 0.74f, 0.34f, 1.0f }, // 粮
        { 0.58f, 0.14f, 0.10f, 0.6f }, // 朱漆
        { 0.60f, 0.33f, 0.25f, 1.0f }, // 砖（搭建件的砖墙）
        { 0.30f, 0.31f, 0.32f, 0.55f }, // 铁构件
        { 0.66f, 0.54f, 0.38f, 0.85f }, // 革/鼓皮
        { 0.72f, 0.60f, 0.36f, 1.0f }, // 茅草
    };
    const Row &row = ROWS[std::clamp(int(p_material), 0, int(PartMaterial::COUNT) - 1)];
    r_r = row.r;
    r_g = row.g;
    r_b = row.b;
    r_roughness = row.rough;
}

int BuildingMesh::triangle_count() const {
    size_t n = 0;
    for (const auto &group : groups) {
        for (const MeshPart &part : group) {
            n += part.indices.size() / 3;
        }
    }
    return int(n);
}

BuildingFrame building_frame(const SiteGround &p_ground, const BuildingDef &p_def, const Building &p_building, const SiteRules &p_rules) {
    SettlementLayout none;
    const SiteReport report = survey_site(p_ground, p_def, p_building.x, p_building.z, p_building.yaw, none, p_rules);
    BuildingFrame frame;
    frame.floor_y = report.floor_y;
    frame.bottom = report.plinth_bottom;
    if (report.reason.find("没有地面") != std::string::npos) {
        // 地形换了、这座悬在岛外：照画在岸缘高度，开发者一眼能看见它该挪了
        frame.floor_y = p_def.plinth_m;
        frame.bottom = -p_rules.bury_m;
        frame.grounded = false;
    }
    return frame;
}

BuildingMesh build_building_mesh(const MapGround &p_map, const BuildingFrame &p_frame, const Building &p_building, const HouseTemplate &p_template, int p_index) {
    BuildingMesh out;
    out.index = p_index;
    out.frame = p_frame;
    const BuildingDef *def = buildings::by_id(p_building.kind);
    if (!def) {
        return out;
    }
    // 船台不是房屋：不挑模板（building_parts 照 land_berth 拼）
    const HouseTemplate tmpl = def->house ? p_template : HouseTemplate();
    out.template_id = def->house ? tmpl.id : std::string(def->id);
    out.levels = def->levels;
    double ux = 0.0, uz = 0.0, vx = 0.0, vz = 0.0;
    building_axes(p_building.yaw, ux, uz, vx, vz);
    const double floor_y = p_frame.floor_y;
    const GroundDrop drop = [&](double p_x, double p_z) {
        double y = 0.0;
        if (!p_map.site_height(p_building.x + p_x * ux + p_z * vx, p_building.z + p_x * uz + p_z * vz, y)) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return floor_y - y;
    };
    const BuildingParts parts = building_parts(*def, tmpl, floor_y - p_frame.bottom, drop);
    out.group_count = parts.group_count;
    out.groups.resize(size_t(parts.group_count));
    for (const BoxPart &part : parts.boxes) {
        const Xform xf(part.yaw, part.pitch, { part.x, part.y, part.z });
        const V3 size{ part.sx, part.sy, part.sz };
        if (part.visual) {
            art_mesh_kit::part(out.groups[size_t(part.group)][size_t(part.mat)],part);
        }
        if (part.solid) {
            append_box_faces(out.collision, xf, size);
        }
    }
    const double w = def->width_m, d = def->depth_m, wall = def->wall_m, roof = def->roof_m;
    if (def->house) {
        // 屋脊一道 + 悬山（出檐 ROOF_OVERHANG），与天花板同在顶那一组（船台没有顶）
        MeshPart &tiles = out.groups[size_t(out.roof_group())][size_t(PartMaterial::ROOF)];
        append_box(tiles, Xform(0.0, 0.0, { 0.0, wall + roof, 0.0 }), { w + 2 * house_dims::ROOF_OVERHANG + 0.4, 0.35, 0.5 });
        append_gable(tiles, Xform(0.0, 0.0, {}), 0.5 * w + house_dims::ROOF_OVERHANG, 0.5 * d + house_dims::ROOF_OVERHANG, wall, roof, true, nullptr, &out.groups[size_t(out.roof_group())][size_t(PartMaterial::WALL)]);
        // 屋里：每层一盏不投影的暖光（远了就淡掉）
        const double level_h = level_height(*def);
        for (int l = 0; l < def->levels; l++) {
            BuildingLamp lamp;
            lamp.y = l * level_h + std::min(level_h, wall) - 0.6;
            lamp.range = 0.5 * std::sqrt(w * w + d * d);
            lamp.group = 1 + l;
            out.lamps.push_back(lamp);
        }
    }
    out.ladders = parts.ladders;
    out.doors = parts.doors;
    return out;
}

void append_building_proxy(MeshPart &r_part, const BuildingDef &p_def, const Building &p_building, const BuildingFrame &p_frame, double p_origin_x, double p_origin_z) {
    const Xform frame(p_building.yaw, 0.0, { p_building.x - p_origin_x, p_frame.floor_y, p_building.z - p_origin_z });
    const double w = p_def.width_m, d = p_def.depth_m, wall = p_def.wall_m;
    const double plinth_h = std::max(0.1, p_frame.floor_y - p_frame.bottom);
    const Rgba stone = material_rgba(PartMaterial::STONE), mud = material_rgba(PartMaterial::WALL), tile = material_rgba(PartMaterial::ROOF);
    const double margin = p_def.house ? house_dims::PLINTH_MARGIN : 0.0;
    const Xform plinth(p_building.yaw, 0.0, frame.xform({ 0.0, -0.5 * plinth_h - 0.5 * PROXY_INSET_M, 0.0 }));
    append_box(r_part, plinth, { w + 2 * margin - 2 * PROXY_INSET_M, plinth_h - PROXY_INSET_M, d + 2 * margin - 2 * PROXY_INSET_M }, &stone);
    if (!p_def.house) {
        return;
    }
    const Xform walls(p_building.yaw, 0.0, frame.xform({ 0.0, 0.5 * wall - 0.5 * PROXY_INSET_M, 0.0 }));
    append_box(r_part, walls, { w - 2 * PROXY_INSET_M, wall - PROXY_INSET_M, d - 2 * PROXY_INSET_M }, &mud);
    append_gable(r_part, frame, 0.5 * w + house_dims::ROOF_OVERHANG, 0.5 * d + house_dims::ROOF_OVERHANG, wall - PROXY_ROOF_DROP_M, p_def.roof_m, false, &tile);
}
