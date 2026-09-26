#include "building_mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double PROXY_INSET_M = 0.04; // 简模的台基与墙往里缩这么多（细模盖得住它）
constexpr double PROXY_ROOF_DROP_M = 0.06; // 简模的顶往下压这么多

struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

V3 operator+(const V3 &a, const V3 &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 operator-(const V3 &a, const V3 &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 operator*(const V3 &a, double s) { return { a.x * s, a.y * s, a.z * s }; }
V3 cross(const V3 &a, const V3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// 先绕自己的 x 转 pitch、再绕 y 转 yaw、再平移（与 Godot 的 Basis(Y, yaw) * Basis(X, pitch) 同）
struct Xform {
    double m[3][3];
    V3 o;

    Xform(double p_yaw, double p_pitch, const V3 &p_origin) :
            o(p_origin) {
        const double cy = std::cos(p_yaw), sy = std::sin(p_yaw), cp = std::cos(p_pitch), sp = std::sin(p_pitch);
        const double r[3][3] = { { cy, sy * sp, sy * cp }, { 0.0, cp, -sp }, { -sy, cy * sp, cy * cp } };
        std::copy(&r[0][0], &r[0][0] + 9, &m[0][0]);
    }
    V3 rotate(const V3 &v) const {
        return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
    }
    V3 xform(const V3 &v) const { return rotate(v) + o; }
};

// 盒子的六个面：法线 n 与面上的两根轴 u、w（u × w = n）；四角按 u、w 逆时针排
const V3 FACE_N[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
const V3 FACE_U[6] = { { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
const V3 FACE_W[6] = { { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } };
const double SU[4] = { -1, 1, 1, -1 }, SW[4] = { -1, -1, 1, 1 };

V3 corner(const V3 &p_half, int p_face, double p_su, double p_sw) {
    const V3 c = FACE_N[p_face] + FACE_U[p_face] * p_su + FACE_W[p_face] * p_sw;
    return { c.x * p_half.x, c.y * p_half.y, c.z * p_half.z };
}

void push3(std::vector<float> &r_out, const V3 &p_v) {
    r_out.push_back(float(p_v.x));
    r_out.push_back(float(p_v.y));
    r_out.push_back(float(p_v.z));
}

struct Rgba {
    float r = 1, g = 1, b = 1, a = 1;
};

// 一个平面多边形（凸），p_points 顺序随意绕，按 p_normal 摆正：从外面看逆时针的点，三角形反着连（Godot 的正面顺时针）
void polygon(MeshPart &r_part, std::vector<V3> p_points, const V3 &p_normal, const Rgba *p_color) {
    if (p_points.size() < 3) {
        return;
    }
    if (dot(cross(p_points[1] - p_points[0], p_points[2] - p_points[0]), p_normal) < 0.0) {
        std::reverse(p_points.begin(), p_points.end());
    }
    const int base = r_part.vertex_count();
    const double len = std::sqrt(dot(p_normal, p_normal));
    const V3 n = len > 0.0 ? p_normal * (1.0 / len) : V3{ 0, 1, 0 };
    for (const V3 &p : p_points) {
        push3(r_part.positions, p);
        push3(r_part.normals, n);
        if (p_color) {
            r_part.colors.insert(r_part.colors.end(), { p_color->r, p_color->g, p_color->b, p_color->a });
        }
    }
    for (int k = 1; k + 1 < int(p_points.size()); k++) {
        r_part.indices.insert(r_part.indices.end(), { base, base + k + 1, base + k });
    }
}

void append_box(MeshPart &r_part, const Xform &p_xf, const V3 &p_size, const Rgba *p_color = nullptr) {
    const V3 half = p_size * 0.5;
    for (int f = 0; f < 6; f++) {
        std::vector<V3> pts;
        for (int k = 0; k < 4; k++) {
            pts.push_back(p_xf.xform(corner(half, f, SU[k], SW[k])));
        }
        polygon(r_part, pts, p_xf.rotate(FACE_N[f]), p_color);
    }
}

void append_box_faces(std::vector<float> &r_faces, const Xform &p_xf, const V3 &p_size) {
    const V3 half = p_size * 0.5;
    for (int f = 0; f < 6; f++) {
        V3 c[4];
        for (int k = 0; k < 4; k++) {
            c[k] = p_xf.xform(corner(half, f, SU[k], SW[k]));
        }
        for (int i : { 0, 2, 1, 0, 3, 2 }) {
            push3(r_faces, c[i]);
        }
    }
}

// 悬山顶：两坡从檐口（高 p_eave、前后 ±p_half_d）到屋脊（高 p_eave + p_rise、z = 0），沿 x 伸到 ±p_half_w；两山各一个三角，檐下一面。建筑局部坐标经 p_xf 摆好
void append_gable(MeshPart &r_part, const Xform &p_xf, double p_half_w, double p_half_d, double p_eave, double p_rise, bool p_underside, const Rgba *p_color = nullptr) {
    const double X = p_half_w, e = p_half_d, y0 = p_eave, y1 = p_eave + p_rise;
    const auto P = [&](double x, double y, double z) { return p_xf.xform({ x, y, z }); };
    polygon(r_part, { P(-X, y0, e), P(X, y0, e), P(X, y1, 0), P(-X, y1, 0) }, p_xf.rotate({ 0, e, p_rise }), p_color);
    polygon(r_part, { P(-X, y0, -e), P(X, y0, -e), P(X, y1, 0), P(-X, y1, 0) }, p_xf.rotate({ 0, e, -p_rise }), p_color);
    polygon(r_part, { P(X, y0, e), P(X, y0, -e), P(X, y1, 0) }, p_xf.rotate({ 1, 0, 0 }), p_color);
    polygon(r_part, { P(-X, y0, e), P(-X, y0, -e), P(-X, y1, 0) }, p_xf.rotate({ -1, 0, 0 }), p_color);
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
            append_box(out.groups[size_t(part.group)][size_t(part.mat)], xf, size);
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
        append_gable(tiles, Xform(0.0, 0.0, {}), 0.5 * w + house_dims::ROOF_OVERHANG, 0.5 * d + house_dims::ROOF_OVERHANG, wall, roof, true);
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
