#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/string.hpp>

#include "aim.h"
#include "build_pieces.h"
#include "map_host.h"
#include "pack_hand.h"
#include "play_notice.h"
#include "player.h"
#include "structure_styles.h"

class WorksView;

// 搭建（切片十一）：手上拿着一样搭建件，准星附近出一个吸附好的虚影（摆得下绿、摆不下红，提示说为什么），Q/R 转（新开一座按 15°、坡顶换往哪边低、梯子换靠哪边），
// 按 E 摆下（从选中那格扣一件）；准星对着摆好的一件按 X 拆回背包（上面还有靠它撑着的不让拆），对着门按 E 开关（往离人远的那面开）。
// 整座样式：样式面板（G）里挑一个拿在手上，准星处出整座的虚影，按 E 一次摆下（背包里得有全部的件，缺什么说什么），摆完放下样式。
// 吸附与支撑、完整的屋在 build_pieces.h（纯算法）；账在营造账（GroundWorks，map_works_at 给，宿主的局部坐标），实物由 WorksView 画；
// 房子、树、船、别的一座挡不挡问物理（虚影那一件的外包盒）。god 模式不扣件、拆了也不还。与场景无关的玩法部件（不是节点），编排持有、步行时每帧推一下。
class PieceHand {
public:
    struct Step {
        bool used = false;
    };

private:
    WorksSite site;
    WorksView *view = nullptr;
    PieceRules rules;
    int turns = 0;
    std::string held_item;
    PiecePlan plan;
    bool ghost_on = false;
    bool ghost_ok = false;
    std::string ghost_why;
    // 整座样式
    std::vector<StructureStyle> styles;
    int style_index = -1;
    StylePlan style_plan;
    // 看着的那一件
    int look_structure = -1, look_piece = -1;
    // 虚影
    godot::Node3D *ghost_parent = nullptr;
    godot::Node3D *ghost = nullptr;
    uint64_t ghost_signature = 0;
    godot::Ref<godot::StandardMaterial3D> ghost_good, ghost_bad;
    // 完整的屋（按座缓存，账变了重算）
    int report_revision = -1;
    std::vector<std::pair<int, HouseReport>> reports;

    godot::Transform3D frame_xform() const;
    void refresh_reports();
    const HouseReport *report_of(int p_structure);
    std::string obstacle(Player *p_player, const Structure &p_frame, const Piece &p_piece, int p_structure) const;
    void show_ghost(const Structure &p_frame, const std::vector<Piece> &p_pieces, bool p_ok);
    void hide_ghost();
    void load_styles();

public:
    void setup(godot::Node3D *p_ghost_parent);
    // 手上拿着搭建件，或拿着样式。p_held：快捷栏选中那格的物品，或开发者营造（god 下 F8 面板）挑的那件——都由编排给
    bool holds(const std::string &p_held) const;
    // 每个物理帧（步行、没开面板、没抱货箱时）：看着哪一件、手上拿着的出虚影
    void look(const WorksSite &p_site, MapHost *p_map, Player *p_player, const std::string &p_held, const AimHit &p_aim, bool p_free);
    // 这一下 E（p_interact）/ X（p_remove）归不归它
    bool claims(const std::string &p_held, bool p_interact, bool p_remove) const;
    // p_free（god）：不扣件、拆了也不还，背包不动
    Step act(Player *p_player, PackHand &p_pack, const std::string &p_held, bool p_interact, bool p_remove, bool p_free, const Say &p_say);
    void stand_down();

    // 整座样式：几套、第几套叫什么、要几件什么；拿起第几套（−1 放下）；把看着的那一座另存成样式（写进 user://structures/），返回新样式的下标
    int get_style_count() const { return int(styles.size()); }
    const StructureStyle &get_style(int p_index) const { return styles[size_t(p_index)]; }
    int get_style_index() const { return style_index; }
    void set_style(int p_index);
    int save_looked_as_style(godot::String &r_message);
    int get_look_structure() const { return look_structure; }
    int get_look_piece() const { return look_piece; }

    // 人站在一座完整的屋里（HUD 报；p_global 是人的位置）
    bool inside_house(const godot::Vector3 &p_global);
    // 底部提示；无事可说返回空
    godot::String prompt(const std::string &p_held);
    // 测试查：虚影 { on, ok, why, structure（−1 新开）, slot, x, z, level, tier, dir, snap（全局）}
    godot::Dictionary ghost_state() const;
    // 测试查：某座的完整度 { complete, missing, rooms }
    godot::Dictionary house_state(int p_structure);
};
