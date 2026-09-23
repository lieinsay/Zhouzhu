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

class Player;

// 全局开发者工具（autoload，见 game/dev_tools.tscn），哪张地图里都在：
//   F2 开合「换地图」清单（maps：场景路径 → 名字），点一项或按数字键就换过去；
//   F3 开关 god 模式——当前场景里的 Player 能飞、飞得快（Player::set_god_mode），换了地图照样开着；
//   F4 开关时间快进——全局的世界时钟（WorldClock）按它的 fast_scale 倍走，近处远处的村子、农田都跟着快；
//   没有岛群地图的场景里按 M，提示去 F2 换一张有地图的。
// 当前场景的根节点有 dev_tools 属性且为假时，这些键都不理。
// 「god 开没开」「清单开没开」只在这里；它不认识各场景的编排，只找场景里的 Player。
class DevTools : public godot::Node {
    GDCLASS(DevTools, godot::Node)

    godot::Dictionary maps; // 场景路径 → 显示名，按插入顺序列

    bool god = false;
    bool menu_open = false;
    bool blocked_before_menu = false;
    godot::ObjectID player_id;
    godot::Node *player_scene = nullptr; // 找到 player_id 时的当前场景；场景换了就重找

    godot::Label *badge = nullptr;
    godot::Label *notice = nullptr;
    godot::PanelContainer *menu = nullptr;
    godot::VBoxContainer *menu_rows = nullptr;
    std::vector<godot::Button *> menu_buttons;
    double notice_time = 0.0;

    void build_ui();
    void refresh_menu();
    void set_menu_open(bool p_open);
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
    godot::String get_notice() const; // 眼下的提示（给测试看）

    void set_maps(const godot::Dictionary &p_value) { maps = p_value; }
    godot::Dictionary get_maps() const { return maps; }
};
