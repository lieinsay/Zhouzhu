#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "building_parts.h"
#include "wild_landing.h"

// 搭建件（切片十一）：玩家拿一件件原件拼房子——地基、楼板、墙（夯土 / 砖 / 门 / 窗）、山墙、坡顶（草 / 瓦）、木柱、梯子。不含 Godot 头。
//
// 一座（Structure）有自己的格子：格 2 m 见方、层高 3 m、坡顶一格升 1 m；原点是格 (0, 0) 的西北角、y 是地坪（0 层地基的面），朝向 yaw
// （局部 +x → (cos yaw, −sin yaw)、+z → (sin yaw, cos yaw)，与 Godot 的 Basis(Y, yaw) 同）。宿主坐标 = 原点 + 转过的局部坐标。
// 槽位（PieceSpot）：
//   格（CELL）：(x, z) 格号，占 [x·G, (x+1)·G] × [z·G, (z+1)·G]；地基在 0 层、楼板在 1 层以上（板面 = level·H）；
//     坡顶在 level 层的墙上：tier 第几阶（檐口那阶 0，往上一阶高 R），dir 往哪边低（檐口朝向：0 +z 1 +x 2 −z 3 −x）；
//     梯子在 level 层，dir 靠哪一边（同上：0 = 靠 +z 那边）。
//   边（EDGE）：(x, z) 与 dir——dir 0 = 格 (x, z) 的北边（z·G 那条，沿 x），dir 1 = 西边（x·G 那条，沿 z）；墙在 level 层（地坪 level·H 到 (level+1)·H），
//     山墙也在 level 层、墙顶以上：从墙顶补到挨着这条边的坡顶底下（形状按坡顶算）。
//   角（CORNER）：(x, z) 是格 (x, z) 的西北角；木柱在 level 层。
// 支撑（摆的时候查；拆的时候查剩下的还站不站得住，站不住就不让拆）：
//   地基：地面（格里九个点）都在地坪下、max_plinth_m 以内，不在水里；新开一座时地坪 = 格里最高的地面 + min_plinth_m；
//     同一座里的地基要挨着已有的地基（共一条边）。离已有的一座（它的地基）不到 G + snap_m 就只能接到它上面，不另开一座（免得两套格子叠着）。
//   墙 / 门 / 窗：这条边两侧的格有这一层的地基 / 楼板，或下一层同一条边有墙。
//   楼板：四条边上有下一层的墙，或四个角有下一层的柱，或挨着的格有这一层的楼板；同一格下一层有梯子就不行（梯口）。
//   坡顶：四条边上有这一层的墙，或四个角有这一层的柱，或挨着的格有这一层的坡顶（连着往上接）。
//   山墙：这条边这一层有墙，且挨着这条边的格有这一层的坡顶、坡顶底在这条边上高过墙顶（有缝可补）。
//   木柱：四周的格有这一层的地基 / 楼板，或下一层同一个角有柱。
//   梯子：这一格这一层有地基 / 楼板，上一层同一格没有楼板。
// 一座里同一个槽只能有一件；墙、门、窗共一条边（都算墙）。

namespace piece_dims {
constexpr double GRID_M = 2.0; // 格边长
constexpr double STOREY_M = 3.0; // 层高
constexpr double ROOF_RISE_M = 1.0; // 坡顶一格升多高（约 27°）
constexpr double WALL_T = 0.24;
constexpr double FLOOR_T = 0.2;
constexpr double ROOF_T = 0.14;
constexpr double EAVE_M = 0.5; // 檐口那阶往外出檐
constexpr double GABLE_EAVE_M = 0.3; // 山面（没挨着别的坡顶的那头）出际
constexpr double PILLAR_W = 0.3;
constexpr double DOOR_W = 1.1, DOOR_H = 2.2;
constexpr double WINDOW_W = 1.0, WINDOW_H = 0.9, WINDOW_SILL = 1.0;
constexpr int MAX_LEVELS = 4;
constexpr int MAX_TIERS = 4;
} // namespace piece_dims

enum class PieceSlot : uint8_t {
    CELL,
    EDGE,
    CORNER,
};

enum class PieceRole : uint8_t {
    FOUNDATION,
    FLOOR,
    WALL,
    DOOR,
    WINDOW,
    GABLE,
    ROOF,
    PILLAR,
    LADDER,
};

struct PieceDef {
    const char *item; // 物品 id（背包里的那样东西，crops.h）
    PieceRole role;
    PieceSlot slot;
    PartMaterial material; // 主材质：墙夯土 / 砖、顶草 / 瓦……
};

namespace pieces {

extern const PieceDef DEFS[];
extern const int COUNT;

const PieceDef *find(const std::string &p_item); // 不是搭建件返回 nullptr
inline bool is_piece(const std::string &p_item) { return find(p_item) != nullptr; }
bool is_wall_role(PieceRole p_role); // 墙、门、窗（占一条边的一层）

} // namespace pieces

struct PieceSpot {
    PieceSlot slot = PieceSlot::CELL;
    int x = 0, z = 0;
    int level = 0;
    int tier = 0;
    int dir = 0;

    bool operator==(const PieceSpot &p_other) const {
        return slot == p_other.slot && x == p_other.x && z == p_other.z && level == p_other.level && tier == p_other.tier && dir == p_other.dir;
    }
};

struct Piece {
    int id = 0;
    std::string item;
    PieceSpot spot;
    int open = 0; // 门：0 关着，1 / −1 朝边的法线正 / 反面开着
    double bottom = 0.0; // 地基：台基底（局部 y，负数）
};

struct Structure {
    int id = 0;
    double x = 0.0, y = 0.0, z = 0.0; // 原点（宿主坐标）：格 (0, 0) 的西北角，y = 地坪
    double yaw = 0.0;
    std::vector<Piece> pieces;

    // 局部 ↔ 宿主
    void to_host(double p_lx, double p_ly, double p_lz, double &r_x, double &r_y, double &r_z) const;
    void to_local(double p_x, double p_y, double p_z, double &r_lx, double &r_ly, double &r_lz) const;

    const Piece *find(int p_piece) const;
    // 这种件在这个槽上占着的那件（墙、门、窗同一条边算一个槽；地基 / 楼板同一格同一层；坡顶一格一层只一件）；没有返回 nullptr
    const Piece *at(PieceRole p_role, const PieceSpot &p_spot) const;
    const Piece *cell_piece(PieceRole p_role, int p_x, int p_z, int p_level) const; // 这一格这一层的地基 / 楼板 / 梯子
    const Piece *roof_at(int p_x, int p_z, int p_level) const; // 这一格这一层的坡顶（任一阶）
    const Piece *wall_at(int p_x, int p_z, int p_dir, int p_level) const; // 这条边这一层的墙 / 门 / 窗
    const Piece *gable_at(int p_x, int p_z, int p_dir, int p_level) const;
    const Piece *pillar_at(int p_x, int p_z, int p_level) const;
    bool has_floor(int p_x, int p_z, int p_level) const; // 地基（0 层）或楼板
};

// 格的四条边：0 北（dir 0，格自己的）1 南（下一格的北边）2 西（dir 1，格自己的）3 东（右一格的西边）
struct EdgeRef {
    int x, z, dir;
};
EdgeRef cell_edge(int p_x, int p_z, int p_side);
// 一条边两侧的格
void edge_cells(const EdgeRef &p_edge, int &r_x0, int &r_z0, int &r_x1, int &r_z1);
// 坡顶在这条边上（局部坐标）两端离墙顶多高（米）：边不挨着这一格返回 false。两端按边的走向（沿 x 或 z 从小到大）排
bool roof_edge_heights(const Piece &p_roof, const EdgeRef &p_edge, double &r_h0, double &r_h1);
// 山墙的形状：挨着这条边、这一层、坡顶底在这条边上高过墙顶的那件坡顶，两端离墙顶多高；没有返回 false
bool gable_shape(const Structure &p_structure, const EdgeRef &p_edge, int p_level, double &r_h0, double &r_h1);

struct PieceRules {
    double min_plinth_m = 0.45; // 地基至少露出地面这么高（同村里房子的台基；往坡上接地基时这就是能让的高差）
    double max_plinth_m = 3.0; // 地面到地坪最多这么高（再高先垒土）
    double bury_m = 0.3; // 台基往地下埋这么深
    double snap_m = 1.6; // 准星离槽的吸附点多远以内才吸过去
    double reach_m = 7.0; // 离人多远以内够得着（水平）
    double new_turn_deg = 15.0; // 新开一座时朝向按视线取整到这么多度
};

// 准星：落点（宿主坐标）、人站在哪、朝哪看、Q/R 转了几下
struct PieceAim {
    double x = 0.0, y = 0.0, z = 0.0;
    double from_x = 0.0, from_y = 0.0, from_z = 0.0; // 人
    double look_yaw = 0.0; // Player 的 look_yaw（0 = 朝 −z）
    int turns = 0;
};

struct PiecePlan {
    bool ok = false;
    std::string why;
    int structure = -1; // 摆进哪一座（Structures 里的下标）；−1 = 新开一座（frame 是新的那座）
    Structure frame; // 那一座的原点与朝向（新开的一座 pieces 为空）
    PieceSpot spot;
    double bottom = 0.0; // 地基的台基底（局部 y）
    double snap_x = 0.0, snap_y = 0.0, snap_z = 0.0; // 吸附点（宿主坐标，给提示与测试）
};

class Structures {
    std::vector<Structure> list;
    int next_structure = 1;
    int next_piece = 1;
    int revision = 0;

public:
    const std::vector<Structure> &get() const { return list; }
    int size() const { return int(list.size()); }
    int get_revision() const { return revision; }
    int index_of(int p_structure) const;
    const Structure *find(int p_structure) const;
    Structure *edit(int p_structure);
    int piece_count() const;

    // 按计划摆一件：返回 (座 id, 件 id)
    std::pair<int, int> add(const std::string &p_item, const PiecePlan &p_plan);
    // 摆一整座（样式）：座与件重新编号，返回座 id
    int add_structure(const Structure &p_structure);
    // 拆一件（座空了整座拿掉）；拆了剩下的站不住返回 false、r_why 说为什么
    bool remove(int p_structure, int p_piece, std::string &r_why);
    // 门开关（open 见 Piece::open）
    bool set_open(int p_structure, int p_piece, int p_open);
    // 读回 / 换锚：整份换掉（ids 接着最大的往下编）
    void assign(const std::vector<Structure> &p_list);
    void shift(const std::function<void(double &, double &, double &, double &)> &p_move); // 原点与朝向按宿主坐标挪（x, y, z, yaw）
    void touch() { revision++; }
};

// 能不能摆、摆在哪：p_ground 问宿主坐标的地面（改过的地形照算）与水
PiecePlan plan_piece(const Structures &p_structures, const PieceDef &p_def, const PieceAim &p_aim, const LandingGround &p_ground, const PieceRules &p_rules = PieceRules());
// 这一件现在有没有撑着（给拆的时候复查剩下的，以及测试）
bool piece_supported(const Structure &p_structure, const Piece &p_piece);
// 一件在局部坐标里的外包盒（中心、半尺寸、绕 y 不转——局部格子是正的）：点选、物理查挡用
void piece_bounds(const Structure &p_structure, const Piece &p_piece, double &r_cx, double &r_cy, double &r_cz, double &r_hx, double &r_hy, double &r_hz);
// 局部坐标里这一点落在哪一件上（外包盒外扩 p_margin 里最近的）；没有 −1（返回件 id）
int piece_at(const Structure &p_structure, double p_lx, double p_ly, double p_lz, double p_margin = 0.08);

// 完整的屋：每层把地基 / 楼板格按没有墙的边连成屋；四周全是墙、至少一扇门、每格头顶有坡顶或上一层的楼板、
// 挨着的坡顶接得上（没有台阶缝）、墙顶与坡顶之间的缝有山墙补上——就是一座完整的屋。
struct HouseRoom {
    int level = 0;
    std::vector<std::pair<int, int>> cells;
    bool enclosed = false;
    int doors = 0;
    int missing_walls = 0; // 屋边上没墙的边
    int missing_roof = 0; // 头顶没顶的格
    int roof_gaps = 0; // 挨着的坡顶没接上的边
    int missing_gables = 0; // 墙顶与坡顶之间有缝、没补山墙的边
    bool complete() const { return enclosed && doors > 0 && missing_roof == 0 && roof_gaps == 0 && missing_gables == 0; }
};

struct HouseReport {
    std::vector<HouseRoom> rooms;
    bool complete = false; // 至少一间完整的屋
    int complete_rooms = 0;
    std::string missing; // 最像屋的那一间还缺什么（「墙 2 面、顶 1 格、门」）；完整为空
};

HouseReport check_house(const Structure &p_structure);
// 局部坐标这一点在不在一间完整的屋里（地坪以上、头顶以下）
bool inside_complete_room(const Structure &p_structure, const HouseReport &p_report, double p_lx, double p_ly, double p_lz);
