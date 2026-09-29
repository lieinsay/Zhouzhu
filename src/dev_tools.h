#pragma once

#include <vector>

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "world_session.h"

class Atmosphere;
class Player;
class PlanetMap;

// 全局开发者工具（autoload，见 game/dev_tools.tscn），哪张地图里都在：
//   F2 开合「换地图」清单（maps：场景路径 → 名字），点一项或按数字键就换过去；
//   F3 开关 god 模式——当前场景里的 Player 能飞、飞得快（Player::set_god_mode），换了地图照样开着；
//   F4 开关时间快进——全局的世界时钟（WorldClock）按它的 fast_scale 倍走，近处远处的村子、农田都跟着快；
//   F6 开合「时间与天气」面板——读出此时此地的日期、日出日落、天气、雪线；拨钟点（往回拨是日历退一天、钟往前走）、拨日子 / 月 / 季、
//      暂停与快进档；天气默认「自动」（此时此地该有的），可换成晴、多云、小雨……并偏气温——改在当前场景的天与光（Atmosphere）上，
//      按它的 override_fade 渐入渐出，村子、庄稼跟着变；换了地图照样生效；
//   F7 开关轨道相机（行星计划 P2）——有行星数据的地图里，从当前相机的位姿起飞（OrbitCamera），一路升到轨道看整颗星；Esc / F7 落回人身上；
//   没有岛群地图的场景里按 M，提示去 F2 换一张有地图的。
// 行星计划 P4：F2 的清单里另有——
//   「行星」：地球仪面板（PlanetMap），8000 个岛群，悬停读数，点一下开发者瞬移（没生成过的当场生成，HUD 报进度）或设为航向；有行星数据的场景才能开；
//   「新建世界（随机种子）」：WorldMaker 在进程内用生成器的 C++ 核心库算行星层、出生的岛群（行星计划 P6e；项目设置切到 python 时调 tools/zhouzhu_gen.py world），
//     好了换进这个世界（生存模式，计划 4.8）；
//   「继续：世界 seedN」：user://worlds 下建过的生存世界，已生成的岛群、粗版不重生成；
//   「世界 seed42」是剧情模式（存档目录 user://worlds/seed42；出生在开局那群的主码头，开局配置见 story_start.h）。
//   换场景、关窗前让场景里的 GroupStream 把载入着的群的账存进世界存档目录。
// 当前场景的根节点有 dev_tools 属性且为假时，这些键都不理；暂停菜单开着（树暂停）时也不理。
// 每样工具的说明在 dev_manual.cpp（Esc 暂停菜单的「开发者工具说明」照它列）：加了新工具记得添一条。
// 「god 开没开」「清单开没开」只在这里；它不认识各场景的编排，只找场景里的 Player。
class DevTools : public godot::Node {
    GDCLASS(DevTools, godot::Node)

    godot::Dictionary maps; // 场景路径 → 显示名，按插入顺序列

    bool god = false;
    bool menu_open = false;
    bool weather_open = false;
    bool blocked_before_weather = false;
    godot::String weather_kind; // 覆盖成什么天气（id）；空 = 自动
    double weather_temp = 0.0; // 气温偏移
    godot::ObjectID atmosphere_id;
    godot::Node *atmosphere_scene = nullptr;
    godot::ObjectID pushed_to; // 覆盖推给过哪一个天与光：只在设定变了、或换了场景（新的天与光）时才推，别的地方（测试、工具）直接设的不被冲掉
    bool weather_dirty = false;
    bool blocked_before_menu = false;
    godot::ObjectID player_id;
    godot::ObjectID orbit_id; // 轨道相机（F7）
    godot::ObjectID orbit_prev_camera;
    bool blocked_before_orbit = false;
    godot::Node *player_scene = nullptr; // 找到 player_id 时的当前场景；场景换了就重找

    godot::Label *badge = nullptr;
    godot::Label *notice = nullptr;
    godot::PanelContainer *menu = nullptr;
    godot::VBoxContainer *menu_rows = nullptr;
    godot::PanelContainer *weather_panel = nullptr;
    godot::Label *weather_text = nullptr;
    std::vector<godot::Button *> menu_buttons;
    double notice_time = 0.0;

    enum EntryKind {
        ENTRY_MAP, // 换到 maps 里的场景
        ENTRY_PLANET, // 行星地图
        ENTRY_NEW_WORLD, // 新建世界（随机种子）
        ENTRY_WORLD, // 继续一个生存世界
    };
    struct MenuEntry {
        godot::String text;
        EntryKind kind = ENTRY_MAP;
        godot::String arg;
        bool enabled = true;
    };
    std::vector<MenuEntry> entries;
    godot::String planet_scene = "res://world_seed42.tscn"; // 行星场景（剧情、生存都用它）
    PlanetMap *planet_map = nullptr;
    bool planet_open = false;
    bool blocked_before_planet = false;
    WorldMaker maker;
    bool making = false;
    bool maker_enter = true;
    void build_entries();
    void set_planet_open(bool p_open);
    void feed_planet_map();
    void on_planet_jump(int p_node);
    void on_planet_course(int p_node);
    void poll_maker();
    void save_scene_world();

    void build_ui();
    void build_weather_panel(godot::Node *p_layer);
    void refresh_menu();
    void set_menu_open(bool p_open);
    void refresh_weather_panel();
    void push_weather();
    Atmosphere *find_atmosphere();
    void show_notice(const godot::String &p_text);
    bool allowed() const;
    Player *find_player();

protected:
    static void _bind_methods();

public:
    void _ready() override;
    void _process(double p_delta) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;
    void _notification(int p_what);

    void set_time_lapse(bool p_enabled); // 开关快进（改的是 WorldClock）
    bool is_time_lapse() const;
    void set_god(bool p_enabled);
    bool is_god() const { return god; }
    // 轨道相机（F7）：开的时候从当前相机起飞，关的时候落回原来的相机（人身上）
    void set_orbit(bool p_enabled);
    bool is_orbit() const;
    godot::Node *get_orbit_camera() const;
    void toggle_god() { set_god(!god); }
    // 换到清单里的第 p_index 张（0 起）/ 指定路径的场景
    void switch_to_index(int p_index);
    void switch_to(const godot::String &p_path);
    bool is_menu_open() const { return menu_open; }
    void set_menu_open_for_tests(bool p_open) { set_menu_open(p_open); }
    // F2 清单的第 p_index 项（地图、行星、新建世界、继续某个世界）
    void menu_pick(int p_index);
    godot::PackedStringArray get_menu_entries(); // 清单每一项的字（给测试看）
    // 行星地图（F2「行星」）
    void open_planet_map();
    void close_planet_map() { set_planet_open(false); }
    bool is_planet_open() const { return planet_open; }
    PlanetMap *get_planet_map() const { return planet_map; }
    // 新建世界（生存模式）：p_seed ≤ 0 随机；开始了返回 true。p_enter：好了自动换进去（测试给假，自己实例化场景）；p_name 空 = seed<N>
    bool new_world(int p_seed, bool p_enter = true, const godot::String &p_name = godot::String());
    // 下一个实例化的行星场景用这个世界（不换场景；测试用）
    bool prepare_world(const godot::String &p_world_dir);
    // { making, seed, world_dir, progress, text, error, elapsed_s }
    godot::Dictionary get_new_world_state() const;
    // 换进一个世界（存档目录；剧情的也行）
    bool enter_world(const godot::String &p_world_dir);

    // 时间与天气（F6 面板上的按钮与键也走这些；测试直接调）
    void set_weather_open(bool p_open);
    bool is_weather_open() const { return weather_open; }
    void step_hours(double p_hours); // 拨钟点（可正可负）
    void shift_days(int p_days); // 拨日子（只动日历）
    void toggle_pause();
    void set_speed(double p_scale); // 1 = 照常；> 1 快进这么多倍
    void set_weather(const godot::String &p_kind); // 空 = 自动
    godot::String get_weather() const { return weather_kind; }
    void set_weather_temp(double p_offset);
    double get_weather_temp() const { return weather_temp; }
    godot::String get_weather_text() const; // 面板上那段读数（给测试看）
    godot::String get_notice() const; // 眼下的提示（给测试看）

    void set_maps(const godot::Dictionary &p_value) { maps = p_value; }
    godot::Dictionary get_maps() const { return maps; }
};
