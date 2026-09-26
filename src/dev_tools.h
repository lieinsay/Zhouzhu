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

class Atmosphere;
class Player;

// 全局开发者工具（autoload，见 game/dev_tools.tscn），哪张地图里都在：
//   F2 开合「换地图」清单（maps：场景路径 → 名字），点一项或按数字键就换过去；
//   F3 开关 god 模式——当前场景里的 Player 能飞、飞得快（Player::set_god_mode），换了地图照样开着；
//   F4 开关时间快进——全局的世界时钟（WorldClock）按它的 fast_scale 倍走，近处远处的村子、农田都跟着快；
//   F6 开合「时间与天气」面板——读出此时此地的日期、日出日落、天气、雪线；拨钟点（往回拨是日历退一天、钟往前走）、拨日子 / 月 / 季、
//      暂停与快进档；天气默认「自动」（此时此地该有的），可换成晴、多云、小雨……并偏气温——改在当前场景的天与光（Atmosphere）上，
//      按它的 override_fade 渐入渐出，村子、庄稼跟着变；换了地图照样生效；
//   没有岛群地图的场景里按 M，提示去 F2 换一张有地图的。
// 当前场景的根节点有 dev_tools 属性且为假时，这些键都不理。
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
    godot::Node *player_scene = nullptr; // 找到 player_id 时的当前场景；场景换了就重找

    godot::Label *badge = nullptr;
    godot::Label *notice = nullptr;
    godot::PanelContainer *menu = nullptr;
    godot::VBoxContainer *menu_rows = nullptr;
    godot::PanelContainer *weather_panel = nullptr;
    godot::Label *weather_text = nullptr;
    std::vector<godot::Button *> menu_buttons;
    double notice_time = 0.0;

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

    void set_time_lapse(bool p_enabled); // 开关快进（改的是 WorldClock）
    bool is_time_lapse() const;
    void set_god(bool p_enabled);
    bool is_god() const { return god; }
    void toggle_god() { set_god(!god); }
    // 换到清单里的第 p_index 张（0 起）/ 指定路径的场景
    void switch_to_index(int p_index);
    void switch_to(const godot::String &p_path);
    bool is_menu_open() const { return menu_open; }

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
