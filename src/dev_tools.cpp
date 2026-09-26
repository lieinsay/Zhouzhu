#include "dev_tools.h"

#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include "atmosphere.h"
#include "input_actions.h"
#include "player.h"
#include "expedition.h"
#include "weather.h"
#include "world_clock.h"

using namespace godot;

namespace {

constexpr double NOTICE_SECONDS = 4.0;
constexpr int UI_LAYER = 100; // 压在各场景自己的 HUD 上面

String hour_text(double p_hour) {
    const int minutes = int(std::floor(std::fmod(p_hour + 24.0, 24.0) * 60.0 + 0.5)) % (24 * 60);
    return vformat("%02d:%02d", minutes / 60, minutes % 60);
}

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
    ClassDB::bind_method(D_METHOD("set_time_lapse", "enabled"), &DevTools::set_time_lapse);
    ClassDB::bind_method(D_METHOD("is_time_lapse"), &DevTools::is_time_lapse);
    ClassDB::bind_method(D_METHOD("switch_to_index", "index"), &DevTools::switch_to_index);
    ClassDB::bind_method(D_METHOD("switch_to", "path"), &DevTools::switch_to);
    ClassDB::bind_method(D_METHOD("is_menu_open"), &DevTools::is_menu_open);
    ClassDB::bind_method(D_METHOD("get_notice"), &DevTools::get_notice);
    ClassDB::bind_method(D_METHOD("set_weather_open", "open"), &DevTools::set_weather_open);
    ClassDB::bind_method(D_METHOD("is_weather_open"), &DevTools::is_weather_open);
    ClassDB::bind_method(D_METHOD("step_hours", "hours"), &DevTools::step_hours);
    ClassDB::bind_method(D_METHOD("shift_days", "days"), &DevTools::shift_days);
    ClassDB::bind_method(D_METHOD("toggle_pause"), &DevTools::toggle_pause);
    ClassDB::bind_method(D_METHOD("set_speed", "scale"), &DevTools::set_speed);
    ClassDB::bind_method(D_METHOD("set_weather", "kind"), &DevTools::set_weather);
    ClassDB::bind_method(D_METHOD("get_weather"), &DevTools::get_weather);
    ClassDB::bind_method(D_METHOD("set_weather_temp", "offset"), &DevTools::set_weather_temp);
    ClassDB::bind_method(D_METHOD("get_weather_temp"), &DevTools::get_weather_temp);
    ClassDB::bind_method(D_METHOD("get_weather_text"), &DevTools::get_weather_text);
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

    build_weather_panel(layer);
}

void DevTools::build_weather_panel(Node *p_layer) {
    weather_panel = memnew(PanelContainer);
    weather_panel->set_name("WeatherPanel");
    Ref<StyleBoxFlat> box;
    box.instantiate();
    box->set_bg_color(Color(0.06f, 0.07f, 0.09f, 0.88f));
    box->set_border_color(Color(0.4f, 0.5f, 0.62f, 0.9f));
    box->set_border_width_all(2);
    box->set_corner_radius_all(6);
    box->set_content_margin_all(14);
    weather_panel->add_theme_stylebox_override("panel", box);
    weather_panel->set_anchors_preset(Control::PRESET_TOP_RIGHT);
    weather_panel->set_h_grow_direction(Control::GROW_DIRECTION_BEGIN);
    weather_panel->set_offset(SIDE_RIGHT, -24);
    weather_panel->set_offset(SIDE_TOP, 110);
    weather_panel->set_custom_minimum_size(Vector2(600, 0));
    weather_panel->set_visible(false);
    p_layer->add_child(weather_panel);

    VBoxContainer *rows = memnew(VBoxContainer);
    rows->add_theme_constant_override("separation", 6);
    weather_panel->add_child(rows);
    Label *title = make_label(20, Color(0.85f, 0.92f, 1.0f));
    title->set_text(String(U"时间与天气（开发者）"));
    rows->add_child(title);
    weather_text = make_label(15, Color(0.92f, 0.92f, 0.88f));
    rows->add_child(weather_text);

    auto row = [&](const String &p_label) {
        HBoxContainer *line = memnew(HBoxContainer);
        line->add_theme_constant_override("separation", 4);
        Label *name = make_label(15, Color(0.7f, 0.74f, 0.8f));
        name->set_text(p_label);
        name->set_custom_minimum_size(Vector2(44, 0));
        line->add_child(name);
        rows->add_child(line);
        return line;
    };
    auto button = [](HBoxContainer *p_line, const String &p_text, const Callable &p_call) {
        Button *b = memnew(Button);
        b->set_text(p_text);
        b->add_theme_font_size_override("font_size", 15);
        b->set_focus_mode(Control::FOCUS_NONE);
        b->connect("pressed", p_call);
        p_line->add_child(b);
    };
    HBoxContainer *hours = row(String(U"钟点"));
    button(hours, String(U"−1 时"), Callable(this, "step_hours").bind(-1.0));
    button(hours, String(U"+1 时"), Callable(this, "step_hours").bind(1.0));
    button(hours, String(U"−6 时"), Callable(this, "step_hours").bind(-6.0));
    button(hours, String(U"+6 时"), Callable(this, "step_hours").bind(6.0));
    HBoxContainer *days = row(String(U"日子"));
    button(days, String(U"−1 天"), Callable(this, "shift_days").bind(-1));
    button(days, String(U"+1 天"), Callable(this, "shift_days").bind(1));
    button(days, String(U"−1 月"), Callable(this, "shift_days").bind(-28));
    button(days, String(U"+1 月"), Callable(this, "shift_days").bind(28));
    button(days, String(U"−1 季"), Callable(this, "shift_days").bind(-84));
    button(days, String(U"+1 季"), Callable(this, "shift_days").bind(84));
    HBoxContainer *speed = row(String(U"钟"));
    button(speed, String(U"暂停 / 继续"), Callable(this, "toggle_pause"));
    for (double scale : { 1.0, 30.0, 120.0, 480.0 }) {
        button(speed, vformat(String(U"×%d"), int(scale)), Callable(this, "set_speed").bind(scale));
    }
    HBoxContainer *kinds[2] = { row(String(U"天气")), row(String()) };
    button(kinds[0], String(U"自动"), Callable(this, "set_weather").bind(String()));
    for (int k = 0; k < int(WeatherKind::COUNT); k++) {
        const WeatherKindDef &def = weather_kinds::at(WeatherKind(k));
        button(kinds[k < 4 ? 0 : 1], String::utf8(def.name), Callable(this, "set_weather").bind(String(def.id)));
    }
    HBoxContainer *temps = row(String(U"气温"));
    for (double t : { -15.0, -5.0, 0.0, 5.0, 15.0 }) {
        button(temps, t == 0.0 ? String(U"不偏") : vformat(String(U"%+d°"), int(t)), Callable(this, "set_weather_temp").bind(t));
    }
    Label *hint = make_label(14, Color(0.62f, 0.64f, 0.66f));
    hint->set_text(String(U"← / → 拨钟点（Shift 六个钟头）· ↑ / ↓ 拨日子 · PgUp / PgDn 拨月 · Home / End 拨季 · P 暂停\n"
                          U"0 自动、1–9 换天气 · − / = 偏气温（Shift 五度）· F6 / Esc 合上"));
    rows->add_child(hint);
}

void DevTools::set_weather_open(bool p_open) {
    if (weather_open == p_open || !weather_panel) {
        return;
    }
    if (p_open && menu_open) {
        set_menu_open(false);
    }
    weather_open = p_open;
    weather_panel->set_visible(weather_open);
    Player *player = find_player();
    if (weather_open) {
        refresh_weather_panel();
        blocked_before_weather = player && player->is_input_blocked();
        if (player) {
            player->set_input_blocked(true);
        }
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
    } else {
        if (player) {
            player->set_input_blocked(blocked_before_weather);
        }
        Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
    }
}

void DevTools::step_hours(double p_hours) {
    if (WorldClock *clock = WorldClock::get_singleton()) {
        clock->step_hours(p_hours);
        show_notice(vformat(String(U"拨钟：%s%s 个钟头 → %s"), p_hours >= 0.0 ? "+" : "", String::num(p_hours, 2), clock->get_clock_text()));
    }
}

void DevTools::shift_days(int p_days) {
    if (WorldClock *clock = WorldClock::get_singleton()) {
        clock->shift_days(p_days);
        show_notice(vformat(String(U"拨日子：%+d 天 → %s"), p_days, String(clock->get_date()["text"])));
    }
}

void DevTools::toggle_pause() {
    if (WorldClock *clock = WorldClock::get_singleton()) {
        clock->set_paused(!clock->is_paused());
        show_notice(clock->is_paused() ? String(U"时间停了（村子、庄稼都停着）· P 继续") : String(U"时间照常走"));
    }
}

void DevTools::set_speed(double p_scale) {
    if (WorldClock *clock = WorldClock::get_singleton()) {
        clock->set_paused(false);
        if (p_scale <= 1.0) {
            clock->set_fast_forward(false);
            show_notice(String(U"时间照常走"));
        } else {
            clock->set_fast_scale(p_scale);
            clock->set_fast_forward(true);
            show_notice(vformat(String(U"时间快进 ×%d"), int(p_scale)));
        }
    }
}

void DevTools::set_weather(const String &p_kind) {
    const WeatherKindDef *def = p_kind.is_empty() ? nullptr : weather_kinds::by_id(std::string(p_kind.utf8().get_data()));
    weather_kind = def ? String(def->id) : String();
    weather_dirty = true;
    push_weather();
    show_notice(def ? vformat(String(U"天气换成「%s」（开发者，渐入）"), String::utf8(def->name)) : String(U"天气：自动（此时此地该有的）"));
}

void DevTools::set_weather_temp(double p_offset) {
    weather_temp = Math::clamp(p_offset, -40.0, 40.0);
    weather_dirty = true;
    push_weather();
    show_notice(weather_temp == 0.0 ? String(U"气温不偏") : vformat(String(U"气温偏 %+d °C"), int(std::lround(weather_temp))));
}

Atmosphere *DevTools::find_atmosphere() {
    Node *scene = get_tree()->get_current_scene();
    if (!scene) {
        return nullptr;
    }
    Atmosphere *cached = Object::cast_to<Atmosphere>(ObjectDB::get_instance(atmosphere_id));
    if (cached && scene == atmosphere_scene) {
        return cached;
    }
    atmosphere_scene = scene;
    atmosphere_id = ObjectID();
    Atmosphere *found = Object::cast_to<Atmosphere>(get_tree()->get_first_node_in_group(Atmosphere::GROUP));
    if (found) {
        atmosphere_id = found->get_instance_id();
    }
    return found;
}

void DevTools::push_weather() {
    Atmosphere *atmosphere = find_atmosphere();
    if (!atmosphere || (!weather_dirty && ObjectID(atmosphere->get_instance_id()) == pushed_to)) {
        return;
    }
    weather_dirty = false;
    pushed_to = ObjectID(atmosphere->get_instance_id());
    if (weather_kind.is_empty() && weather_temp == 0.0) {
        atmosphere->clear_weather_override();
    } else {
        atmosphere->set_weather_override(weather_kind, weather_temp);
    }
}

String DevTools::get_weather_text() const {
    return weather_text ? weather_text->get_text() : String();
}

void DevTools::refresh_weather_panel() {
    if (!weather_text) {
        return;
    }
    const WorldClock *clock = WorldClock::get_singleton();
    Atmosphere *atmosphere = find_atmosphere();
    PackedStringArray lines;
    if (clock) {
        const Dictionary date = clock->get_date();
        lines.push_back(vformat(String(U"%s · 一年第 %d 天 · 钟：%s"), clock->get_clock_text(), int(date["day_of_year"]) + 1,
                clock->is_paused() ? String(U"停着") : (clock->is_fast_forward() ? vformat(String(U"快进 ×%d"), int(clock->get_fast_scale())) : String(U"照常"))));
    }
    if (atmosphere) {
        const Dictionary w = atmosphere->get_weather();
        lines.push_back(vformat(String(U"%s · 纬度 %s° · 日出 %s 日落 %s（昼 %s h）· 太阳高 %d° · 月亮亮 %d%%"), w["season_name"],
                String::num(double(w["latitude"]), 1), hour_text(w["sunrise"]), hour_text(w["sunset"]), String::num(double(w["day_length"]), 1),
                int(std::lround(double(w["sun_altitude"]))), int(std::lround(double(w["moon_lit"]) * 100.0))));
        if (bool(w.get("planet", false))) {
            // 行星计划 P1：日月按相机的经纬度，钟点是 2051（152.15°E）的地方时（D10）
            const double lat = w["sun_latitude"], lon = w["sun_longitude"], off = w["local_offset_h"];
            lines.push_back(vformat(String(U"相机：%s %s° %s %s° · 地方时 %s（比钟点%s %d 分）"), lat < 0.0 ? String(U"南纬") : String(U"北纬"),
                    String::num(std::abs(lat), 2), lon < 0.0 ? String(U"西经") : String(U"东经"), String::num(std::abs(lon), 2), hour_text(w["local_hour"]),
                    off < 0.0 ? String(U"晚") : String(U"早"), int(std::lround(std::abs(off) * 60.0))));
        }
        lines.push_back(vformat(String(U"此地：%s %s°C · %s %s m/s · 云 %d%% · 降水 %s mm/h（雪 %d%%）· 雾 %d%%"), w["kind_name"],
                String::num(double(w["temp_c"]), 1), w["wind_name"], String::num(double(w["wind_ms"]), 1), int(std::lround(double(w["cloud"]) * 100.0)),
                String::num(double(w["precip_mm_h"]), 1), int(std::lround(double(w["snow_frac"]) * 100.0)), int(std::lround(double(w["fog"]) * 100.0))));
        const double snow_line = w["snow_line"], ice_line = w["ice_line"];
        lines.push_back(vformat(String(U"今天群里：%s · 雪线 %s · 河冰线 %s · 地湿 %d%% · %s"), w["day_kind_name"],
                snow_line > 1e8 ? String(U"没有雪") : vformat(String(U"%d m"), int(snow_line)), ice_line > 1e8 ? String(U"不冻") : vformat(String(U"%d m"), int(ice_line)),
                int(std::lround(double(w["wet"]) * 100.0)), bool(w["sailable"]) ? String(U"能出航") : String(U"禁航")));
        lines.push_back(vformat(String(U"物候（此地）：绿 %d%% · 叶 %d%% · 秋色 %d%% · 作物 %d%% · 积雪 %s mm"), int(std::lround(double(w["green"]) * 100.0)),
                int(std::lround(double(w["leaf"]) * 100.0)), int(std::lround(double(w["autumn"]) * 100.0)), int(std::lround(double(w["crop"]) * 100.0)),
                String::num(double(w["snow_mm"]), 0)));
        const Dictionary o = w["override"];
        lines.push_back(vformat(String(U"天气：%s%s（渐入 %d%%）%s"), bool(o["active"]) ? o["kind_name"] : String(U"自动（此时此地该有的）"),
                weather_temp != 0.0 ? vformat(String(U" · 气温偏 %+d °C"), int(std::lround(weather_temp))) : String(),
                int(std::lround(double(o["blend"]) * 100.0)), bool(w["has_data"]) ? String() : String(U" · 这张地图没有气候数据（常年晴）")));
    } else {
        lines.push_back(String(U"这个场景没有天与光（Atmosphere）"));
    }
    weather_text->set_text(String("\n").join(lines));
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

void DevTools::set_time_lapse(bool p_enabled) {
    if (WorldClock *clock = WorldClock::get_singleton()) {
        clock->set_fast_forward(p_enabled);
        show_notice(p_enabled ? vformat(String(U"时间快进 ×%d（远近的村子、庄稼都跟着快）· F4 关"), int(clock->get_fast_scale())) : String(U"时间照常走"));
    }
}

bool DevTools::is_time_lapse() const {
    const WorldClock *clock = WorldClock::get_singleton();
    return clock && clock->is_fast_forward();
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
        set_weather_open(false);
        set_menu_open(!menu_open);
        get_viewport()->set_input_as_handled();
        return;
    }
    if (p_event->is_action_pressed(input_actions::WEATHER_PANEL)) {
        set_weather_open(!weather_open);
        get_viewport()->set_input_as_handled();
        return;
    }
    if (weather_open) {
        const Ref<InputEventKey> key = p_event;
        if (p_event->is_action_pressed("ui_cancel")) {
            set_weather_open(false);
            get_viewport()->set_input_as_handled();
            return;
        }
        if (key.is_valid() && key->is_pressed()) {
            const int64_t code = int64_t(key->get_physical_keycode());
            const bool shift = key->is_shift_pressed();
            bool used = true;
            if (code == KEY_LEFT || code == KEY_RIGHT) {
                step_hours((code == KEY_LEFT ? -1.0 : 1.0) * (shift ? 6.0 : 1.0));
            } else if (code == KEY_UP || code == KEY_DOWN) {
                shift_days(code == KEY_UP ? 1 : -1);
            } else if (code == KEY_PAGEUP || code == KEY_PAGEDOWN) {
                shift_days(code == KEY_PAGEUP ? 28 : -28);
            } else if (code == KEY_HOME || code == KEY_END) {
                shift_days(code == KEY_HOME ? 84 : -84);
            } else if (code == KEY_P && !key->is_echo()) {
                toggle_pause();
            } else if (code == KEY_0 && !key->is_echo()) {
                set_weather(String());
            } else if (code >= KEY_1 && code <= KEY_9 && !key->is_echo()) {
                const int k = int(code - KEY_1);
                if (k < int(WeatherKind::COUNT)) {
                    set_weather(String(weather_kinds::at(WeatherKind(k)).id));
                }
            } else if (code == KEY_MINUS || code == KEY_EQUAL) {
                set_weather_temp(weather_temp + (code == KEY_MINUS ? -1.0 : 1.0) * (shift ? 5.0 : 1.0));
            } else {
                used = false;
            }
            if (used) {
                get_viewport()->set_input_as_handled();
                return;
            }
        }
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
    if (p_event->is_action_pressed(input_actions::TIME_LAPSE)) {
        set_time_lapse(!is_time_lapse());
        get_viewport()->set_input_as_handled();
        return;
    }
    // 岛群场景自己管 M；别的场景没有地图，告诉开发者去哪儿找
    if (p_event->is_action_pressed(input_actions::MAP) && !get_tree()->get_first_node_in_group(Expedition::MAP_GROUP)) {
        show_notice(String(U"这张地图没有岛群地图——按 F2 换到有地图的「岛群 #2051」"));
    }
}

void DevTools::_process(double p_delta) {
    Player *player = find_player();
    // 换了地图：新场景里的人也接着飞
    if (player && player->is_god_mode() != god) {
        player->set_god_mode(god);
    }
    PackedStringArray lines;
    if (god && player) {
        lines.push_back(vformat(String(U"GOD · %d m/s（Shift ×%s）· F3 关"), int(player->get_god_speed()), String::num(player->get_god_boost(), 1)));
    }
    if (is_time_lapse()) {
        lines.push_back(vformat(String(U"快进 ×%d · F4 关"), int(WorldClock::get_singleton()->get_fast_scale())));
    }
    if (WorldClock::get_singleton() && WorldClock::get_singleton()->is_paused()) {
        lines.push_back(String(U"时间停着 · F6 面板里 P 继续"));
    }
    // 天气覆盖：换了地图也接着推给新场景的天与光
    push_weather();
    if (!weather_kind.is_empty() || weather_temp != 0.0) {
        String what = weather_kind.is_empty() ? String(U"自动") : String::utf8(weather_kinds::by_id(std::string(weather_kind.utf8().get_data()))->name);
        if (weather_temp != 0.0) {
            what += vformat(String(U"，气温偏 %+d °C"), int(std::lround(weather_temp)));
        }
        lines.push_back(String(U"天气（开发者）：") + what + String(U" · F6"));
    }
    if (weather_open) {
        refresh_weather_panel();
    }
    badge->set_visible(!lines.is_empty());
    badge->set_text(String("\n").join(lines));
    if (notice_time > 0.0) {
        notice_time -= p_delta;
        if (notice_time <= 0.0) {
            notice->set_text(String());
        }
    }
}
