#include "dev_tools.h"

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "input_actions.h"
#include "player.h"
#include "world_scene.h"

using namespace godot;

namespace {

constexpr double NOTICE_SECONDS = 4.0;
constexpr int UI_LAYER = 100; // 压在各场景自己的 HUD 上面

Label *make_label(int p_font_size, const Color &p_color) {
    Label *label = memnew(Label);
    label->add_theme_font_size_override("font_size", p_font_size);
    label->add_theme_color_override("font_color", p_color);
    label->add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85f));
    label->add_theme_constant_override("outline_size", 6);
    return label;
}

} // namespace

void DevTools::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_god", "enabled"), &DevTools::set_god);
    ClassDB::bind_method(D_METHOD("is_god"), &DevTools::is_god);
    ClassDB::bind_method(D_METHOD("toggle_god"), &DevTools::toggle_god);
    ClassDB::bind_method(D_METHOD("switch_to_index", "index"), &DevTools::switch_to_index);
    ClassDB::bind_method(D_METHOD("switch_to", "path"), &DevTools::switch_to);
    ClassDB::bind_method(D_METHOD("is_menu_open"), &DevTools::is_menu_open);
    ClassDB::bind_method(D_METHOD("get_notice"), &DevTools::get_notice);
    ClassDB::bind_method(D_METHOD("set_maps", "value"), &DevTools::set_maps);
    ClassDB::bind_method(D_METHOD("get_maps"), &DevTools::get_maps);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "maps"), "set_maps", "get_maps");
}

void DevTools::_ready() {
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        set_process_unhandled_input(false);
        return;
    }
    input_actions::register_all();
    set_process_mode(PROCESS_MODE_ALWAYS);
    build_ui();
}

void DevTools::build_ui() {
    CanvasLayer *layer = memnew(CanvasLayer);
    layer->set_name("DevToolsHud");
    layer->set_layer(UI_LAYER);
    add_child(layer);

    badge = make_label(18, Color(1.0f, 0.85f, 0.35f));
    badge->set_anchors_preset(Control::PRESET_TOP_RIGHT);
    badge->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
    badge->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    badge->set_offset(SIDE_RIGHT, -24);
    badge->set_offset(SIDE_TOP, 52);
    badge->set_visible(false);
    layer->add_child(badge);

    notice = make_label(24, Color(1, 1, 1));
    notice->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
    notice->set_anchors_preset(Control::PRESET_CENTER_TOP);
    notice->set_h_grow_direction(Control::GROW_DIRECTION_BOTH);
    notice->set_offset(SIDE_TOP, 90);
    layer->add_child(notice);

    menu = memnew(PanelContainer);
    menu->set_name("MapMenu");
    Ref<StyleBoxFlat> box;
    box.instantiate();
    box->set_bg_color(Color(0.08f, 0.07f, 0.06f, 0.9f));
    box->set_border_color(Color(0.55f, 0.48f, 0.32f, 0.9f));
    box->set_border_width_all(2);
    box->set_corner_radius_all(6);
    box->set_content_margin_all(16);
    menu->add_theme_stylebox_override("panel", box);
    menu->set_anchors_preset(Control::PRESET_CENTER);
    menu->set_h_grow_direction(Control::GROW_DIRECTION_BOTH);
    menu->set_v_grow_direction(Control::GROW_DIRECTION_BOTH);
    menu->set_custom_minimum_size(Vector2(420, 0));
    menu->set_visible(false);
    layer->add_child(menu);

    menu_rows = memnew(VBoxContainer);
    menu_rows->add_theme_constant_override("separation", 8);
    menu->add_child(menu_rows);
    Label *title = make_label(22, Color(0.95f, 0.9f, 0.75f));
    title->set_text(String(U"换地图（开发者）"));
    menu_rows->add_child(title);
    Label *hint = make_label(15, Color(0.66f, 0.66f, 0.62f));
    hint->set_text(String(U"点一项或按数字键 · F2 / Esc 合上"));
    menu_rows->add_child(hint);
}

void DevTools::refresh_menu() {
    for (Button *b : menu_buttons) {
        b->queue_free();
    }
    menu_buttons.clear();
    const Node *scene = get_tree()->get_current_scene();
    const String current = scene ? scene->get_scene_file_path() : String();
    const Array paths = maps.keys();
    for (int64_t k = 0; k < paths.size(); k++) {
        const String path = paths[k];
        Button *button = memnew(Button);
        String text = vformat("%d   %s", k + 1, maps[path]);
        if (path == current) {
            text += String(U"   · 当前");
            button->set_disabled(true);
        }
        button->set_text(text);
        button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
        button->add_theme_font_size_override("font_size", 19);
        button->connect("pressed", Callable(this, "switch_to_index").bind(k));
        menu_rows->add_child(button);
        menu_buttons.push_back(button);
    }
}

void DevTools::set_menu_open(bool p_open) {
    if (menu_open == p_open) {
        return;
    }
    menu_open = p_open;
    menu->set_visible(menu_open);
    Player *player = find_player();
    if (menu_open) {
        refresh_menu();
        blocked_before_menu = player && player->is_input_blocked();
        if (player) {
            player->set_input_blocked(true);
        }
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    } else {
        if (player) {
            player->set_input_blocked(blocked_before_menu);
        }
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

String DevTools::get_notice() const {
    return notice ? notice->get_text() : String();
}

void DevTools::show_notice(const String &p_text) {
    notice->set_text(p_text);
    notice_time = NOTICE_SECONDS;
}

bool DevTools::allowed() const {
    const Node *scene = get_tree()->get_current_scene();
    if (!scene) {
        return false;
    }
    const Variant flag = scene->get("dev_tools");
    return flag.get_type() != Variant::BOOL || bool(flag);
}

Player *DevTools::find_player() {
    Node *scene = get_tree()->get_current_scene();
    if (!scene) {
        return nullptr;
    }
    Player *cached = Object::cast_to<Player>(ObjectDB::get_instance(player_id));
    if (cached && scene == player_scene) {
        return cached;
    }
    player_scene = scene;
    player_id = ObjectID();
    const TypedArray<Node> found = scene->find_children("*", "Player", true, false);
    Player *player = found.is_empty() ? nullptr : Object::cast_to<Player>(found[0]);
    if (player) {
        player_id = player->get_instance_id();
    }
    return player;
}

void DevTools::set_god(bool p_enabled) {
    god = p_enabled;
    Player *player = find_player();
    if (player) {
        player->set_god_mode(god);
    }
    show_notice(god ? String(U"god 模式：开——空格升、Ctrl 降、Shift 加速") : String(U"god 模式：关"));
}

void DevTools::switch_to_index(int p_index) {
    const Array paths = maps.keys();
    if (p_index >= 0 && p_index < paths.size()) {
        switch_to(paths[p_index]);
    }
}

void DevTools::switch_to(const String &p_path) {
    set_menu_open(false);
    const Error err = get_tree()->change_scene_to_file(p_path);
    if (err != OK) {
        show_notice(vformat(String(U"换不过去：%s（错误 %d）"), p_path, int(err)));
        return;
    }
    show_notice(vformat(String(U"换到「%s」"), maps.get(p_path, p_path)));
}

void DevTools::_unhandled_input(const Ref<InputEvent> &p_event) {
    if (!allowed()) {
        return;
    }
    if (p_event->is_action_pressed(input_actions::SWITCH_MAP)) {
        set_menu_open(!menu_open);
        get_viewport()->set_input_as_handled();
        return;
    }
    if (menu_open) {
        if (p_event->is_action_pressed("ui_cancel")) {
            set_menu_open(false);
            get_viewport()->set_input_as_handled();
            return;
        }
        const Ref<InputEventKey> key = p_event;
        if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
            const int64_t code = int64_t(key->get_physical_keycode());
            if (code >= KEY_1 && code <= KEY_9) {
                switch_to_index(int(code - KEY_1));
                get_viewport()->set_input_as_handled();
            }
        }
        return;
    }
    if (p_event->is_action_pressed(input_actions::GOD_MODE)) {
        toggle_god();
        get_viewport()->set_input_as_handled();
        return;
    }
    // 岛群场景自己管 M；别的场景没有地图，告诉开发者去哪儿找
    if (p_event->is_action_pressed(input_actions::MAP) && !get_tree()->get_first_node_in_group(WorldScene::GROUP)) {
        show_notice(String(U"这张地图没有岛群地图——按 F2 换到有地图的「岛群 #2051」"));
    }
}

void DevTools::_process(double p_delta) {
    Player *player = find_player();
    // 换了地图：新场景里的人也接着飞
    if (player && player->is_god_mode() != god) {
        player->set_god_mode(god);
    }
    badge->set_visible(god && player);
    if (god && player) {
        badge->set_text(vformat(String(U"GOD · %d m/s（Shift ×%s）· F3 关"), int(player->get_god_speed()), String::num(player->get_god_boost(), 1)));
    }
    if (notice_time > 0.0) {
        notice_time -= p_delta;
        if (notice_time <= 0.0) {
            notice->set_text(String());
        }
    }
}
