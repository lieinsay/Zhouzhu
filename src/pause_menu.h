#pragma once

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

// 暂停菜单（全局 autoload，见 game/pause_menu.tscn，名字 GameMenu）：哪张地图里都在。
//   Esc（场景里没有地图、背包、面板要它合上，开发者工具的清单也没开着时）：整棵树暂停（世界钟、村子、人、船都停）、鼠标放出来，
//   屏幕中间一个菜单——「返回游戏」「开发者工具说明」；说明页照 dev_manual 列出每一样开发者工具（键、名字、在哪儿能用、怎么用），可滚动。
//   说明页里 Esc / 「返回」回到菜单；菜单里 Esc / 「返回游戏」合上、取消暂停、鼠标照原样收回去。
// 自己 PROCESS_MODE_ALWAYS；暂停时开发者工具（DevTools，也是 ALWAYS）不理键。
// Esc 的先后：场景（Expedition：地图、背包、面板）→ DevTools（换地图清单、时间与天气、行星地图）→ 这里——autoload 排在 DeveloperTools 前面，
// 未处理的输入先到后加的节点，所以它最后才轮到。
class PauseMenu : public godot::CanvasLayer {
    GDCLASS(PauseMenu, godot::CanvasLayer)

public:
    enum Page {
        PAGE_MAIN,
        PAGE_DEV_HELP,
    };

private:
    bool open = false;
    Page page = PAGE_MAIN;
    bool mouse_was_captured = false;
    godot::Control *shade = nullptr;
    godot::Control *main_panel = nullptr;
    godot::Button *resume_button = nullptr;
    godot::Button *help_button = nullptr;
    godot::Control *help_panel = nullptr;
    godot::Label *help_status = nullptr;
    godot::ScrollContainer *help_scroll = nullptr;
    godot::Button *back_button = nullptr;

    void build_ui();
    void fill_help();
    void show_page(Page p_page);

protected:
    static void _bind_methods();

public:
    static constexpr int LAYER = 120; // 压在开发者工具的 HUD（100）上面

    void _ready() override;
    void _input(const godot::Ref<godot::InputEvent> &p_event) override;
    void _unhandled_input(const godot::Ref<godot::InputEvent> &p_event) override;

    void open_menu();
    void close_menu();
    bool is_open() const { return open; }
    void show_main() { show_page(PAGE_MAIN); }
    void show_dev_help() { show_page(PAGE_DEV_HELP); }
    // 测试查：此刻哪一页（"main" / "dev_help"，没开为空）；说明页列了哪些工具（「键 名字」）；说明页的整段字
    godot::String get_page() const;
    godot::PackedStringArray get_dev_help_titles() const;
    godot::String get_dev_help_text() const;
    void scroll_help(int p_pixels);
    int get_help_scroll() const;
};
