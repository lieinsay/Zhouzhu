#include "pause_menu.h"

#include <godot_cpp/classes/center_container.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "dev_manual.h"

using namespace godot;

namespace {

constexpr int SCROLL_STEP = 48;

String utf8(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

Label *make_label(int p_font_size, const Color &p_color) {
    Label *label = memnew(Label);
    label->add_theme_font_size_override("font_size", p_font_size);
    label->add_theme_color_override("font_color", p_color);
    label->add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85f));
    label->add_theme_constant_override("outline_size", 3);
    label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    return label;
}

PanelContainer *make_panel(const Vector2 &p_min_size) {
    PanelContainer *panel = memnew(PanelContainer);
    Ref<StyleBoxFlat> box;
    box.instantiate();
    box->set_bg_color(Color(0.07f, 0.07f, 0.08f, 0.94f));
    box->set_border_color(Color(0.6f, 0.52f, 0.34f, 0.95f));
    box->set_border_width_all(2);
    box->set_corner_radius_all(8);
    box->set_content_margin_all(22);
    panel->add_theme_stylebox_override("panel", box);
    panel->set_custom_minimum_size(p_min_size);
    return panel;
}

Button *make_button(const String &p_text) {
    Button *b = memnew(Button);
    b->set_text(p_text);
    b->add_theme_font_size_override("font_size", 20);
    b->set_custom_minimum_size(Vector2(300, 48));
    return b;
}

// 当前场景开没开开发者工具（根节点的 dev_tools 属性，同 DevTools::allowed）
bool scene_dev_tools(SceneTree *p_tree) {
    const Node *scene = p_tree ? p_tree->get_current_scene() : nullptr;
    if (!scene) {
        return false;
    }
    const Variant flag = scene->get("dev_tools");
    return flag.get_type() != Variant::BOOL || bool(flag);
}

} // namespace

void PauseMenu::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open_menu"), &PauseMenu::open_menu);
    ClassDB::bind_method(D_METHOD("close_menu"), &PauseMenu::close_menu);
    ClassDB::bind_method(D_METHOD("is_open"), &PauseMenu::is_open);
    ClassDB::bind_method(D_METHOD("show_main"), &PauseMenu::show_main);
    ClassDB::bind_method(D_METHOD("show_dev_help"), &PauseMenu::show_dev_help);
    ClassDB::bind_method(D_METHOD("get_page"), &PauseMenu::get_page);
    ClassDB::bind_method(D_METHOD("get_dev_help_titles"), &PauseMenu::get_dev_help_titles);
    ClassDB::bind_method(D_METHOD("get_dev_help_text"), &PauseMenu::get_dev_help_text);
    ClassDB::bind_method(D_METHOD("scroll_help", "pixels"), &PauseMenu::scroll_help);
    ClassDB::bind_method(D_METHOD("get_help_scroll"), &PauseMenu::get_help_scroll);
}

void PauseMenu::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process_input(false);
        set_process_unhandled_input(false);
        return;
    }
    set_process_mode(PROCESS_MODE_ALWAYS);
    set_layer(LAYER);
    build_ui();
    set_visible(false);
}

void PauseMenu::build_ui() {
    shade = memnew(ColorRect);
    Object::cast_to<ColorRect>(shade)->set_color(Color(0, 0, 0, 0.55f));
    shade->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    shade->set_mouse_filter(Control::MOUSE_FILTER_STOP); // 暂停时点不到底下的东西
    add_child(shade);

    CenterContainer *center = memnew(CenterContainer);
    center->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    center->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    add_child(center);

    // 菜单
    PanelContainer *menu = make_panel(Vector2(380, 0));
    menu->set_name("PauseMain");
    center->add_child(menu);
    main_panel = menu;
    VBoxContainer *rows = memnew(VBoxContainer);
    rows->add_theme_constant_override("separation", 14);
    menu->add_child(rows);
    Label *title = make_label(30, Color(0.97f, 0.92f, 0.78f));
    title->set_text(String(U"暂停"));
    title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    rows->add_child(title);
    resume_button = make_button(String(U"返回游戏"));
    resume_button->connect("pressed", Callable(this, "close_menu"));
    rows->add_child(resume_button);
    help_button = make_button(String(U"开发者工具说明"));
    help_button->connect("pressed", Callable(this, "show_dev_help"));
    rows->add_child(help_button);
    Label *hint = make_label(14, Color(0.62f, 0.62f, 0.58f));
    hint->set_text(String(U"↑↓ 选 · 回车 / 点一下 · Esc 返回游戏"));
    hint->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    rows->add_child(hint);

    // 开发者工具说明
    PanelContainer *help = make_panel(Vector2(860, 0));
    help->set_name("PauseDevHelp");
    center->add_child(help);
    help_panel = help;
    VBoxContainer *column = memnew(VBoxContainer);
    column->add_theme_constant_override("separation", 10);
    help->add_child(column);
    Label *help_title = make_label(26, Color(0.97f, 0.92f, 0.78f));
    help_title->set_text(String(U"开发者工具说明"));
    column->add_child(help_title);
    help_status = make_label(15, Color(0.75f, 0.9f, 0.7f));
    column->add_child(help_status);
    help_scroll = memnew(ScrollContainer);
    help_scroll->set_name("HelpScroll");
    help_scroll->set_custom_minimum_size(Vector2(816, 440));
    help_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
    column->add_child(help_scroll);
    fill_help();
    back_button = make_button(String(U"返回"));
    back_button->connect("pressed", Callable(this, "show_main"));
    column->add_child(back_button);
    Label *help_hint = make_label(14, Color(0.62f, 0.62f, 0.58f));
    help_hint->set_text(String(U"滚轮 / ↑↓ / PgUp PgDn 翻 · Esc 回到菜单"));
    column->add_child(help_hint);
}

void PauseMenu::fill_help() {
    VBoxContainer *list = memnew(VBoxContainer);
    list->set_name("HelpList");
    list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
    list->add_theme_constant_override("separation", 4);
    help_scroll->add_child(list);
    for (const std::string &line : dev_manual_intro()) {
        Label *l = make_label(15, Color(0.8f, 0.8f, 0.76f));
        l->set_text(utf8(line));
        l->set_custom_minimum_size(Vector2(790, 0));
        list->add_child(l);
    }
    for (const DevManualEntry &e : dev_manual()) {
        Label *head = make_label(19, Color(1.0f, 0.82f, 0.45f));
        head->set_name("Tool");
        head->set_text(utf8(e.keys) + String(U"　") + utf8(e.title) + String(U"　· ") + utf8(e.where));
        head->set_custom_minimum_size(Vector2(790, 0));
        list->add_child(head);
        for (const std::string &line : e.lines) {
            Label *l = make_label(15, Color(0.9f, 0.9f, 0.86f));
            l->set_text(String(U"　· ") + utf8(line));
            l->set_custom_minimum_size(Vector2(790, 0));
            list->add_child(l);
        }
    }
}

void PauseMenu::show_page(Page p_page) {
    page = p_page;
    if (!main_panel) {
        return;
    }
    main_panel->set_visible(page == PAGE_MAIN);
    help_panel->set_visible(page == PAGE_DEV_HELP);
    if (page == PAGE_DEV_HELP) {
        help_status->set_text(scene_dev_tools(get_tree()) ? String(U"这张地图里开发者工具开着。") : String(U"这张地图关了开发者工具（场景根的 dev_tools 为假），下面这些键此刻都不理。"));
        help_scroll->set_v_scroll(0);
        back_button->grab_focus();
    } else if (open) {
        resume_button->grab_focus();
    }
}

void PauseMenu::open_menu() {
    if (open || !is_inside_tree()) {
        return;
    }
    open = true;
    Input *input = Input::get_singleton();
    mouse_was_captured = input->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED;
    input->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    get_tree()->set_pause(true);
    set_visible(true);
    show_page(PAGE_MAIN);
}

void PauseMenu::close_menu() {
    if (!open) {
        return;
    }
    open = false;
    set_visible(false);
    get_tree()->set_pause(false);
    if (mouse_was_captured) {
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

void PauseMenu::_input(const Ref<InputEvent> &p_event) {
    // 说明页：↑↓ PgUp PgDn 翻页（在按钮拿走焦点导航之前）
    if (!open || page != PAGE_DEV_HELP) {
        return;
    }
    const Ref<InputEventKey> key = p_event;
    if (!key.is_valid() || !key->is_pressed()) {
        return;
    }
    const Key code = key->get_physical_keycode();
    int step = 0;
    if (code == KEY_UP) {
        step = -SCROLL_STEP;
    } else if (code == KEY_DOWN) {
        step = SCROLL_STEP;
    } else if (code == KEY_PAGEUP) {
        step = -int(help_scroll->get_size().y * 0.9f);
    } else if (code == KEY_PAGEDOWN) {
        step = int(help_scroll->get_size().y * 0.9f);
    }
    if (step != 0) {
        scroll_help(step);
        get_viewport()->set_input_as_handled();
    }
}

void PauseMenu::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!p_event->is_action_pressed("ui_cancel") || p_event->is_echo()) {
        return;
    }
    if (!open) {
        open_menu();
    } else if (page == PAGE_DEV_HELP) {
        show_main();
    } else {
        close_menu();
    }
    get_viewport()->set_input_as_handled();
}

String PauseMenu::get_page() const {
    if (!open) {
        return String();
    }
    return page == PAGE_DEV_HELP ? String("dev_help") : String("main");
}

PackedStringArray PauseMenu::get_dev_help_titles() const {
    PackedStringArray out;
    for (const DevManualEntry &e : dev_manual()) {
        out.push_back(utf8(e.keys) + String(" ") + utf8(e.title));
    }
    return out;
}

String PauseMenu::get_dev_help_text() const {
    String text;
    const Node *list = help_scroll ? help_scroll->get_node_or_null("HelpList") : nullptr;
    if (!list) {
        return text;
    }
    for (int k = 0; k < list->get_child_count(); k++) {
        if (const Label *l = Object::cast_to<Label>(list->get_child(k))) {
            text += l->get_text() + String("\n");
        }
    }
    return text;
}

void PauseMenu::scroll_help(int p_pixels) {
    if (help_scroll) {
        help_scroll->set_v_scroll(std::max(0, help_scroll->get_v_scroll() + p_pixels));
    }
}

int PauseMenu::get_help_scroll() const {
    return help_scroll ? help_scroll->get_v_scroll() : 0;
}
