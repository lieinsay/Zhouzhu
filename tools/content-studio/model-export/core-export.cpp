// 独立内容工坊导出器：直接调用游戏的纯数据几何入口，不链接 Godot、不改游戏资产。
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "build_pieces.h"
#include "building_mesh.h"
#include "buildings.h"
#include "house_template.h"
#include "map_ground.h"
#include "piece_mesh.h"

namespace {

class PreviewGround final : public MapGround {
public:
    bool site_height(double, double, double &r_y) const override {
        r_y = 0.0;
        return true;
    }
    bool walk_height(double p_x, double p_z, double &r_y) const override { return site_height(p_x, p_z, r_y); }
};

std::string json_string(const std::string &p_value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : p_value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                } else {
                    out << char(c);
                }
        }
    }
    out << '"';
    return out.str();
}

template <typename T>
void json_numbers(std::ostream &r_out, const std::vector<T> &p_values) {
    r_out << '[';
    for (size_t i = 0; i < p_values.size(); ++i) {
        if (i) r_out << ',';
        if (!std::isfinite(double(p_values[i]))) throw std::runtime_error("网格含非有限数值");
        r_out << p_values[i];
    }
    r_out << ']';
}

void write_part(std::ostream &r_out, const MeshPart &p_part, PartMaterial p_material, int p_group) {
    if (p_part.positions.size() % 3 || p_part.positions.size() != p_part.normals.size() || p_part.indices.size() % 3) {
        throw std::runtime_error("网格顶点、法线或三角索引长度不一致");
    }
    for (const int32_t index : p_part.indices) {
        if (index < 0 || index >= p_part.vertex_count()) throw std::runtime_error("网格索引越界");
    }
    float r = 1.0f, g = 1.0f, b = 1.0f, roughness = 1.0f;
    part_material_color(p_material, r, g, b, roughness);
    r_out << "{\"positions\":";
    json_numbers(r_out, p_part.positions);
    r_out << ",\"normals\":";
    json_numbers(r_out, p_part.normals);
    r_out << ",\"uvs\":";json_numbers(r_out,p_part.uvs);
    r_out << ",\"uv2s\":";json_numbers(r_out,p_part.uv2s);
    r_out << ",\"indices\":[";
    // 游戏几何正面顺时针，WebGL 正面逆时针；只翻索引，顶点和法线保持原值。
    for (size_t i = 0; i < p_part.indices.size(); i += 3) {
        if (i) r_out << ',';
        r_out << p_part.indices[i] << ',' << p_part.indices[i + 2] << ',' << p_part.indices[i + 1];
    }
    r_out << "],\"color\":[" << r << ',' << g << ',' << b << "],\"roughness\":" << roughness
          << ",\"materialIndex\":" << int(p_material) << ",\"group\":" << p_group << '}';
}

struct ModelStats {
    size_t vertices = 0;
    size_t triangles = 0;
    size_t parts = 0;
};

template <typename Groups>
ModelStats write_mesh(const std::filesystem::path &p_path, const Groups &p_groups) {
    std::ofstream out(p_path, std::ios::binary);
    if (!out) throw std::runtime_error("无法创建网格文件：" + p_path.string());
    out.imbue(std::locale::classic());
    out << std::setprecision(9) << "{\"schemaVersion\":1,\"units\":\"m\",\"upAxis\":\"y\",\"winding\":\"ccw\",\"parts\":[";
    ModelStats stats;
    for (size_t group = 0; group < p_groups.size(); ++group) {
        for (size_t material = 0; material < size_t(PartMaterial::COUNT); ++material) {
            const MeshPart &part = p_groups[group][material];
            if (part.empty()) continue;
            if (stats.parts) out << ',';
            write_part(out, part, PartMaterial(material), int(group));
            stats.vertices += size_t(part.vertex_count());
            stats.triangles += part.indices.size() / 3;
            ++stats.parts;
        }
    }
    if (!stats.parts) throw std::runtime_error("真实几何生成入口返回空网格：" + p_path.string());
    out << "]}\n";
    out.flush();
    if (!out) throw std::runtime_error("写入网格失败：" + p_path.string());
    return stats;
}

const char *template_path(const std::string &p_kind) {
    if (p_kind == "house") return "game/buildings/house/sanjian.txt";
    if (p_kind == "hall") return "game/buildings/hall/citang.txt";
    if (p_kind == "granary") return "game/buildings/granary/cang.txt";
    if (p_kind == "tower") return "game/buildings/tower/wanglou.txt";
    return nullptr;
}

HouseTemplate load_template(const std::filesystem::path &p_root, const BuildingDef &p_def) {
    const char *relative = template_path(p_def.id);
    if (!relative) return HouseTemplate(); // 船台没有房屋模板。
    std::ifstream input(p_root / relative, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("缺少当前游戏模板：") + relative);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (input.bad()) throw std::runtime_error(std::string("读取模板失败：") + relative);
    HouseTemplate tmpl;
    std::vector<std::string> errors;
    if (!parse_template(lines, tmpl, &errors) || !errors.empty() || tmpl.shell() != p_def.id) {
        std::string message = std::string("当前模板解析失败：") + relative;
        for (const auto &error : errors) message += "\n" + error;
        throw std::runtime_error(message);
    }
    return tmpl;
}

void metadata(std::ostream &r_out, const std::string &p_id, const std::string &p_file, const std::string &p_usage,
        const std::string &p_record, const std::string &p_path, const std::string &p_note, const ModelStats &p_stats,
        const std::string &p_template = {}) {
    const bool piece = p_usage == "construction";
    r_out << "{\"id\":" << json_string(p_id) << ",\"url\":" << json_string("/assets/models/" + p_file)
          << ",\"format\":\"zhouzhu-mesh-json\",\"usage\":" << json_string(p_usage)
          << ",\"collection\":" << json_string(piece ? "items" : "buildings")
          << ",\"recordIds\":[" << json_string(p_record) << "],\"itemIds\":[";
    if (piece) r_out << json_string(p_record);
    r_out << "],\"source\":{\"path\":" << json_string(p_path) << ",\"kind\":\"game-procedural\",\"note\":" << json_string(p_note) << '}';
    if (!p_template.empty()) r_out << ",\"templateId\":" << json_string(p_template);
    r_out << ",\"vertices\":" << p_stats.vertices << ",\"triangles\":" << p_stats.triangles << ",\"parts\":" << p_stats.parts << '}';
}

void export_models(const std::filesystem::path &p_destination, const std::filesystem::path &p_root, std::ostream &r_metadata) {
    std::filesystem::create_directories(p_destination);
    r_metadata << "{\"schemaVersion\":1,\"models\":[";
    bool first = true;
    for (int i = 0; i < pieces::COUNT; ++i) {
        const PieceDef &def = pieces::DEFS[i];
        Piece piece;
        piece.id = 1;
        piece.item = def.item;
        piece.spot.slot = def.slot;
        if (def.role == PieceRole::FOUNDATION) piece.bottom = -0.45;
        if (def.role == PieceRole::FLOOR) piece.spot.level = 1;
        Structure context;
        context.pieces.push_back(piece);
        std::string note = "直接复用 build_piece_mesh；主材质与所有辅材质来自当前游戏定义，坐标单位为米。";
        if (def.role == PieceRole::GABLE) {
            // 北边的山墙，右低左高的真实瓦坡顶提供三角形上下文；只导出山墙自身。
            Piece roof;
            roof.id = 2;
            roof.item = "tile_roof";
            roof.spot.slot = PieceSlot::CELL;
            roof.spot.dir = 1;
            context.pieces.push_back(roof);
            double h0 = 0.0, h1 = 0.0;
            if (!gable_shape(context, { piece.spot.x, piece.spot.z, piece.spot.dir }, piece.spot.level, h0, h1)) {
                throw std::runtime_error("山墙缺少真实相邻坡顶上下文");
            }
            note += "山墙形状取同格朝 +x 下坡的 tile_roof，导出不包含该坡顶。";
        }
        const StructureMesh mesh = build_piece_mesh(context, piece);
        const std::string file = "piece-" + std::string(def.item) + ".json";
        const std::vector<std::array<MeshPart, size_t(PartMaterial::COUNT)>> groups{ mesh.parts };
        const ModelStats stats = write_mesh(p_destination / file, groups);
        if (!first) r_metadata << ',';
        first = false;
        metadata(r_metadata, "piece:" + std::string(def.item), file, "construction", def.item, "src/piece_mesh.cpp", note, stats);
    }
    PreviewGround ground;
    const SiteRules rules;
    const MapSiteGround site(ground, rules);
    for (int i = 0; i < buildings::COUNT; ++i) {
        const BuildingDef &def = buildings::DEFS[i];
        const HouseTemplate tmpl = load_template(p_root, def);
        Building building;
        building.kind = def.id;
        building.style = tmpl.style();
        const BuildingFrame frame = building_frame(site, def, building, rules);
        const BuildingMesh mesh = build_building_mesh(ground, frame, building, tmpl, i);
        const std::string file = "building-" + std::string(def.id) + ".json";
        const ModelStats stats = write_mesh(p_destination / file, mesh.groups);
        if (!first) r_metadata << ',';
        first = false;
        const char *relative = template_path(def.id);
        std::string note = "直接复用 build_building_mesh/building_parts；当前 BuildingDef 尺寸，在零高程平地按默认 SiteRules 勘察台基及坡道；包含所有真实材质和楼层。";
        if (relative) note += std::string("选用当前游戏模板 ") + relative + "，不是库存物品独立实体。";
        else note += "船台按当前 land_berth::add_parts 生成，无房屋模板。";
        metadata(r_metadata, "building:" + std::string(def.id), file, "building", def.id, "src/building_mesh.cpp", note, stats, tmpl.id);
    }
    r_metadata << "]}\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "用法：core-export.exe <工具模型输出目录> <Zhouzhu仓库根目录>\n";
        return 2;
    }
    try {
        // 元数据只在所有模型成功导出后输出，供 Node 包装和 SHA-256 清单合并。
        std::ostringstream metadata_out;
        metadata_out.imbue(std::locale::classic());
        export_models(std::filesystem::u8path(argv[1]), std::filesystem::u8path(argv[2]), metadata_out);
        std::cout << metadata_out.str();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "内容工坊核心模型导出失败：" << error.what() << '\n';
        return 1;
    }
}
