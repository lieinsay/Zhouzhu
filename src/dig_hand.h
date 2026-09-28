#pragma once

#include <string>

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/string.hpp>

#include "aim.h"
#include "earthworks.h"
#include "map_host.h"
#include "pack_hand.h"
#include "play_notice.h"
#include "player.h"

// 挖与垒（切片十一，方格台阶）：手上拿着木锹 / 石镐，准星落到的那一格描一个框（能挖绿、不能红，提示说为什么），按住 E 挖一级（0.5 m）：
// 土层里出土、以下出石料（只石镐挖得动），进背包；手上拿着土，准星那一格按一下 E 垒一级、扣一份土。
// 准星打在竖着的坎上：挖是挖坎后面那格（高的）、垒是垒坎前面那格（低的）——像 MC 里对着方块的面。
// 规矩在 earthworks.h（纯算法），地面的事实（压着什么、土层多厚、压不压着人）问地图宿主（map_earth_facts），改也是宿主改（map_set_earth_step：
// 账、地形取样、地面块、碰撞一起改，当场站得上去）。god 模式不扣土、不进背包。与场景无关的玩法部件（不是节点），编排持有、步行时每帧推一下。
// 开发者营造（god 下从 F8 面板挑的挖 / 垒，p_brush ≥ 1）：不读条，按一下 E 把以准星那一格为心的 n × n 格各改一级（每格照规矩查，不行的跳过），
// 按住隔 repeat_s 再来一下；一次只换一回账（map_set_earth_steps）。
class DigHand {
public:
    struct Rules {
        double reach_m = 3.6; // 准星落点离人（水平）多远以内够得着
        double reach_up_m = 3.0; // 高低差多少以内
        double repeat_s = 0.2; // 开发者营造：按住 E 隔多久再改一级
        EarthRules earth;
    };

private:
    Rules rules;
    bool target_on = false;
    int ti = 0, tj = 0; // 准星那一格
    bool raising = false; // 这一格是要垒（手上拿着土）
    EarthCheck check;
    std::string why; // 够不着、改不了地形这些（格本身没问题时为空）
    bool working = false;
    double work_time = 0.0;
    int dug = 0; // 这一回按住 E 挖了几级
    int brush = 0; // 开发者营造的笔刷（0 = 平常的挖垒）
    int brush_done = 0, brush_skipped = 0; // 上一下笔刷改了几格、跳过几格
    godot::Node3D *marker_parent = nullptr;
    godot::MeshInstance3D *marker = nullptr;
    godot::Ref<godot::StandardMaterial3D> marker_ok, marker_bad;

    void show_marker(MapHost *p_map, bool p_ok);
    void hide_marker();
    // 开发者营造：笔刷里每格照规矩改一级，一次交给宿主；返回改了几格，r_why 是头一个不行的格为什么
    int brush_apply(MapHost *p_map, const godot::Vector3 &p_player, const std::string &p_held, std::string &r_why);

public:
    void setup(godot::Node3D *p_marker_parent);
    // 手上拿着的（p_held：快捷栏选中那格，或开发者营造的工具当成的物品）是挖的家伙或土（这一下 E 可能归它）
    static bool holds(const std::string &p_held);
    // 每个物理帧（步行、没开面板、没抱货箱时）：p_aim 是这一帧的准星；p_free = god 模式；p_brush ≥ 1 = 开发者营造的笔刷（n × n，瞬间改）。
    // 这一下 E 归它（挖、垒，或说为什么不行）返回 true
    bool step(MapHost *p_map, const WorksSite &p_site, Player *p_player, PackHand &p_pack, const std::string &p_held, const AimHit &p_aim, bool p_free, int p_brush,
            bool p_interact, double p_delta, const Say &p_say);
    void stand_down();

    bool is_working() const { return working; }
    bool has_target() const { return target_on; }
    int target_i() const { return ti; }
    int target_j() const { return tj; }
    bool target_ok() const { return target_on && why.empty() && check.ok; }
    godot::String target_why() const;
    // 底部提示：准星那一格能干什么、干到哪了；手上不是挖垒的东西返回空
    godot::String prompt(const std::string &p_held) const;
    // 测试查：上一下笔刷改了几格、跳过几格
    int get_brush_done() const { return brush_done; }
    int get_brush_skipped() const { return brush_skipped; }
};
