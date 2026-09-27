#include "planet_map.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include "group_provider.h"

using namespace godot;

namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;
constexpr double EARTH_R = 6376600.0;
const Color OUTLINE(0.0f, 0.0f, 0.0f, 0.8f);
const char *const CLASS_NAMES[4] = { "密接", "中疏", "稀疏", "孤悬" };

double dot(const PlanetVec &a, const PlanetVec &b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Color hashed_color(int p_key) {
    uint32_t h = uint32_t(p_key) * 2654435761u;
    h ^= h >> 15;
    return Color::from_hsv(float(h % 997) / 997.0f, 0.55f + 0.3f * float((h >> 10) % 100) / 100.0f, 0.85f);
}

String population_text(double p_pop) {
    if (p_pop >= 1e8) {
        return String::num(p_pop / 1e8, 2) + String(U" 亿");
    }
    if (p_pop >= 1e4) {
        return String::num(p_pop / 1e4, 1) + String(U" 万");
    }
    return itos(int64_t(std::lround(p_pop)));
}

String latlon_text(double p_lat, double p_lon) {
    return vformat(String(U"%s %s° %s %s°"), p_lat < 0.0 ? String(U"南纬") : String(U"北纬"), String::num(std::fabs(p_lat), 2),
            p_lon < 0.0 ? String(U"西经") : String(U"东经"), String::num(std::fabs(p_lon), 2));
}

} // namespace

void PlanetMap::_bind_methods() {
    ClassDB::bind_method(D_METHOD("look_at_node", "node"), &PlanetMap::look_at_node);
    ClassDB::bind_method(D_METHOD("look_at_latlon", "lat", "lon"), &PlanetMap::look_at_latlon);
    ClassDB::bind_method(D_METHOD("get_hovered"), &PlanetMap::get_hovered);
    ClassDB::bind_method(D_METHOD("get_selected"), &PlanetMap::get_selected);
    ClassDB::bind_method(D_METHOD("select", "node"), &PlanetMap::select);
    ClassDB::bind_method(D_METHOD("pick", "screen"), &PlanetMap::pick);
    ClassDB::bind_method(D_METHOD("node_screen", "node"), &PlanetMap::node_screen);
    ClassDB::bind_method(D_METHOD("get_readout", "node"), &PlanetMap::get_readout);
    ClassDB::bind_method(D_METHOD("set_color_mode", "mode"), &PlanetMap::set_color_mode);
    ClassDB::bind_method(D_METHOD("get_color_mode"), &PlanetMap::get_color_mode);
    ClassDB::bind_method(D_METHOD("request_jump"), &PlanetMap::request_jump);
    ClassDB::bind_method(D_METHOD("request_course"), &PlanetMap::request_course);
    ADD_SIGNAL(MethodInfo("jump_requested", PropertyInfo(Variant::INT, "node")));
    ADD_SIGNAL(MethodInfo("course_requested", PropertyInfo(Variant::INT, "node")));
    BIND_ENUM_CONSTANT(COLOR_CLASS);
    BIND_ENUM_CONSTANT(COLOR_CLIMATE);
    BIND_ENUM_CONSTANT(COLOR_STATE);
}

PlanetMap::PlanetMap() {
    set_focus_mode(FOCUS_ALL);
    set_mouse_filter(MOUSE_FILTER_STOP);
}

void PlanetMap::set_planet(const std::shared_ptr<const PlanetData> &p_planet) {
    planet = p_planet;
    dirs.clear();
    max_area = 1.0;
    if (!planet) {
        return;
    }
    dirs.reserve(size_t(planet->node_count()));
    for (int k = 0; k < planet->node_count(); k++) {
        const PlanetNode &n = planet->node(k);
        dirs.push_back(planet_unit(n.lat, n.lon));
        max_area = std::max(max_area, n.area_km2);
    }
    available.assign(dirs.size(), 0);
    available_age = 1e9;
    queue_redraw();
}

void PlanetMap::set_provider(Object *p_provider) {
    provider_id = p_provider ? ObjectID(p_provider->get_instance_id()) : ObjectID();
    available_age = 1e9;
}

void PlanetMap::set_player(double p_lat, double p_lon, int p_anchor_node) {
    const bool changed = !has_player || std::fabs(p_lat - player_lat) > 1e-3 || std::fabs(p_lon - player_lon) > 1e-3 || p_anchor_node != anchor_node;
    has_player = true;
    player_lat = p_lat;
    player_lon = p_lon;
    anchor_node = p_anchor_node;
    if (changed) {
        queue_redraw();
    }
}

void PlanetMap::set_course(int p_node) {
    if (p_node != course_node) {
        course_node = p_node;
        queue_redraw();
    }
}

void PlanetMap::set_jump(int p_node, double p_progress, const String &p_text) {
    if (p_node != jump_node || std::fabs(p_progress - jump_progress) > 1e-3 || p_text != jump_text) {
        jump_node = p_node;
        jump_progress = p_progress;
        jump_text = p_text;
        queue_redraw();
    }
}

void PlanetMap::look_at_latlon(double p_lat, double p_lon) {
    view_lat = std::clamp(p_lat, -89.0, 89.0);
    view_lon = p_lon;
    queue_redraw();
}

void PlanetMap::look_at_node(int p_node) {
    if (planet && p_node >= 0 && p_node < planet->node_count()) {
        look_at_latlon(planet->node(p_node).lat, planet->node(p_node).lon);
    }
}

void PlanetMap::select(int p_node) {
    selected = planet && p_node >= 0 && p_node < planet->node_count() ? p_node : -1;
    queue_redraw();
}

void PlanetMap::set_color_mode(int p_mode) {
    color_mode = ((p_mode % COLOR_COUNT) + COLOR_COUNT) % COLOR_COUNT;
    queue_redraw();
}

void PlanetMap::request_jump() {
    if (selected >= 0 && teleport_enabled) {
        emit_signal("jump_requested", selected);
    }
}

void PlanetMap::request_course() {
    if (selected >= 0) {
        emit_signal("course_requested", selected);
    }
}

PlanetMap::View PlanetMap::view() const {
    View v;
    v.f = planet_unit(view_lat, view_lon);
    v.e = PlanetVec(-std::sin(view_lon * DEG), std::cos(view_lon * DEG), 0.0); // 东
    v.n = PlanetVec(-std::sin(view_lat * DEG) * std::cos(view_lon * DEG), -std::sin(view_lat * DEG) * std::sin(view_lon * DEG), std::cos(view_lat * DEG)); // 北
    const Vector2 size = get_size();
    v.center = size * 0.5f;
    v.radius = 0.42 * double(std::min(size.x, size.y)) * zoom;
    return v;
}

bool PlanetMap::project(const View &p_view, const PlanetVec &p_dir, Vector2 &r_screen) const {
    if (dot(p_dir, p_view.f) <= 0.0) {
        return false;
    }
    r_screen = p_view.center + Vector2(real_t(dot(p_dir, p_view.e) * p_view.radius), real_t(-dot(p_dir, p_view.n) * p_view.radius));
    return true;
}

int PlanetMap::pick(const Vector2 &p_screen) const {
    if (!planet) {
        return -1;
    }
    const View v = view();
    int best = -1;
    double best_d = 10.0 * 10.0;
    for (size_t k = 0; k < dirs.size(); k++) {
        Vector2 s;
        if (!project(v, dirs[k], s)) {
            continue;
        }
        const double d = double((s - p_screen).length_squared());
        if (d < best_d) {
            best_d = d;
            best = int(k);
        }
    }
    return best;
}

Vector2 PlanetMap::node_screen(int p_node) const {
    Vector2 s(-1, -1);
    if (planet && p_node >= 0 && p_node < int(dirs.size()) && project(view(), dirs[size_t(p_node)], s)) {
        return s;
    }
    return Vector2(-1, -1);
}

Color PlanetMap::node_color(int p_node) const {
    const PlanetNode &n = planet->node(p_node);
    switch (color_mode) {
        case COLOR_CLIMATE: {
            // 台面年均温：−10 °C 蓝 → 5 °C 青 → 15 °C 黄绿 → 25 °C 橙红
            const double t = n.temp_c;
            const Color stops[4] = { Color(0.35f, 0.5f, 1.0f), Color(0.35f, 0.9f, 0.95f), Color(0.75f, 0.9f, 0.35f), Color(1.0f, 0.45f, 0.25f) };
            const double at[4] = { -10.0, 5.0, 15.0, 25.0 };
            if (t <= at[0]) {
                return stops[0];
            }
            for (int k = 0; k < 3; k++) {
                if (t <= at[k + 1]) {
                    return stops[k].lerp(stops[k + 1], float((t - at[k]) / (at[k + 1] - at[k])));
                }
            }
            return stops[3];
        }
        case COLOR_STATE:
            return n.state >= 0 ? hashed_color(n.state) : Color(0.55f, 0.55f, 0.55f);
        case COLOR_CLASS:
        default: {
            const Color c[4] = { Color(0.98f, 0.55f, 0.3f), Color(0.45f, 0.85f, 0.45f), Color(0.4f, 0.75f, 1.0f), Color(0.95f, 0.5f, 0.95f) };
            return c[std::clamp(n.cls, 0, 3)];
        }
    }
}

void PlanetMap::refresh_available() {
    GroupProvider *gp = Object::cast_to<GroupProvider>(ObjectDB::get_instance(provider_id));
    available.assign(dirs.size(), 0);
    if (!gp) {
        return;
    }
    const PackedInt32Array nodes = gp->available_nodes();
    for (int64_t k = 0; k < nodes.size(); k++) {
        if (nodes[k] >= 0 && nodes[k] < int(available.size())) {
            available[size_t(nodes[k])] = 1;
        }
    }
}

String PlanetMap::readout(int p_node) {
    if (!planet || p_node < 0 || p_node >= planet->node_count()) {
        return String();
    }
    const PlanetNode &n = planet->node(p_node);
    PackedStringArray lines;
    lines.push_back(vformat(String(U"群 #%d · %s · 陆地 %s km²（主岛 %s）"), p_node, String::utf8(CLASS_NAMES[std::clamp(n.cls, 0, 3)]), String::num(n.area_km2, 0),
            String::num(n.main_area_km2, 0)));
    String where = latlon_text(n.lat, n.lon);
    if (has_player) {
        const double d = great_circle_m(player_lat, player_lon, n.lat, n.lon, EARTH_R);
        where += vformat(String(U" · 离你 %s km、方位 %d°"), String::num(d / 1000.0, d < 100000.0 ? 1 : 0), int(std::lround(bearing_deg(player_lat, player_lon, n.lat, n.lon))));
    }
    lines.push_back(where);
    lines.push_back(vformat(String(U"%s · 人口 %s · 台面 %d m 年均 %s °C、温差 %s °C、降水 %s"), n.state >= 0 ? vformat(String(U"邦 %d"), n.state) : String(U"不属哪一邦（船团 / 部落）"),
            population_text(n.pop), int(std::lround(n.height_m)), String::num(n.temp_c, 1), String::num(n.season_range_c, 1), String::num(n.precip, 2)));
    GroupProvider *gp = Object::cast_to<GroupProvider>(ObjectDB::get_instance(provider_id));
    if (gp) {
        const Dictionary info = gp->get_group_info(p_node);
        const String source = info.get("source", String());
        const int state = int(info.get("state", 0));
        String cache;
        if (source == "repo") {
            cache = String(U"包：仓库里有（剧情出厂）");
        } else if (source == "cache") {
            cache = vformat(String(U"包：生成过（生成器 %s%s）"), String(info.get("commit", "?")), bool(info.get("stale", false)) ? String(U"，与这回的版本不同，照旧用") : String());
        } else if (state == GroupProvider::QUEUED || state == GroupProvider::RUNNING) {
            cache = vformat(String(U"包：在生成 %d%% · %s"), int(std::lround(double(info.get("progress", 0.0)) * 100.0)), String(info.get("text", String())));
        } else if (state == GroupProvider::FAILED) {
            cache = String(U"包：上回生成失败——") + String(info.get("error", String()));
        } else {
            cache = String(U"包：没生成过（瞬移过去会当场生成，两三秒）");
        }
        lines.push_back(cache);
    }
    if (p_node == anchor_node) {
        lines.push_back(String(U"你在这一群"));
    }
    return String("\n").join(lines);
}

void PlanetMap::_process(double p_delta) {
    if (!is_visible_in_tree()) {
        return;
    }
    available_age += p_delta;
    if (available_age > 2.0) {
        available_age = 0.0;
        refresh_available();
        queue_redraw();
    }
}

void PlanetMap::_notification(int p_what) {
    if (p_what == NOTIFICATION_DRAW) {
        draw_globe();
    } else if (p_what == NOTIFICATION_VISIBILITY_CHANGED && is_visible_in_tree()) {
        available_age = 1e9;
        set_process(true);
    }
}

void PlanetMap::draw_globe() {
    const Vector2 size = get_size();
    draw_rect(Rect2(Vector2(), size), Color(0.03f, 0.04f, 0.07f, 0.94f));
    const Ref<Font> font = get_theme_default_font();
    auto text = [&](const Vector2 &p_at, const String &p_text, int p_size, const Color &p_color) {
        draw_multiline_string_outline(font, p_at, p_text, HORIZONTAL_ALIGNMENT_LEFT, -1, p_size, -1, 4, OUTLINE);
        draw_multiline_string(font, p_at, p_text, HORIZONTAL_ALIGNMENT_LEFT, -1, p_size, -1, p_color);
    };
    if (!planet) {
        text(Vector2(40, 60), String(U"这张地图没有行星数据"), 22, Color(1, 1, 1));
        return;
    }
    const View v = view();
    // 海（云带）与边
    draw_circle(v.center, real_t(v.radius), Color(0.08f, 0.14f, 0.26f));
    draw_arc(v.center, real_t(v.radius), 0.0f, real_t(2.0 * PI), 128, Color(0.45f, 0.65f, 0.95f, 0.8f), 2.0f, true);
    // 经纬网：每 30°
    const Color grid(0.4f, 0.5f, 0.7f, 0.25f);
    auto polyline = [&](auto p_point, int p_steps) {
        PackedVector2Array seg;
        for (int k = 0; k <= p_steps; k++) {
            Vector2 s;
            if (project(v, p_point(double(k) / p_steps), s)) {
                seg.push_back(s);
            } else if (seg.size() > 1) {
                draw_polyline(seg, grid, 1.0f, true);
                seg.clear();
            } else {
                seg.clear();
            }
        }
        if (seg.size() > 1) {
            draw_polyline(seg, grid, 1.0f, true);
        }
    };
    for (int lat = -60; lat <= 60; lat += 30) {
        polyline([&](double t) { return planet_unit(double(lat), -180.0 + 360.0 * t); }, 96);
    }
    for (int lon = -180; lon < 180; lon += 30) {
        polyline([&](double t) { return planet_unit(-89.0 + 178.0 * t, double(lon)); }, 64);
    }
    // 岛群：点的大小按陆地（开方），放大了跟着大一点
    const double px = std::clamp(std::sqrt(zoom), 1.0, 4.0);
    for (size_t k = 0; k < dirs.size(); k++) {
        Vector2 s;
        if (!project(v, dirs[k], s)) {
            continue;
        }
        const double r = px * (0.9 + 2.2 * std::sqrt(planet->node(int(k)).area_km2 / max_area));
        draw_rect(Rect2(s - Vector2(real_t(r), real_t(r)), Vector2(real_t(2 * r), real_t(2 * r))), node_color(int(k)));
        if (k < available.size() && available[k]) {
            draw_arc(s, real_t(r + 2.5), 0.0f, real_t(2.0 * PI), 16, Color(1, 1, 1, 0.85f), 1.2f, true);
        }
    }
    // 航向：你到那一群的大圆（虚线）
    if (has_player && course_node >= 0 && course_node < int(dirs.size())) {
        const PlanetVec a = planet_unit(player_lat, player_lon), b = dirs[size_t(course_node)];
        const double ang = std::acos(std::clamp(dot(a, b), -1.0, 1.0));
        Vector2 prev;
        bool have = false;
        for (int k = 0; k <= 64; k++) {
            const double t = double(k) / 64.0;
            const double s0 = std::sin((1.0 - t) * ang), s1 = std::sin(t * ang), sn = std::max(1e-9, std::sin(ang));
            const PlanetVec p((a.x * s0 + b.x * s1) / sn, (a.y * s0 + b.y * s1) / sn, (a.z * s0 + b.z * s1) / sn);
            Vector2 s;
            const bool vis = project(v, ang > 1e-6 ? p : a, s);
            if (vis && have && k % 2 == 1) {
                draw_line(prev, s, Color(1.0f, 0.85f, 0.3f, 0.9f), 2.0f, true);
            }
            prev = s;
            have = vis;
        }
        Vector2 s;
        if (project(v, b, s)) {
            draw_arc(s, 9.0f, 0.0f, real_t(2.0 * PI), 24, Color(1.0f, 0.85f, 0.3f), 2.0f, true);
        }
    }
    // 锚群、你
    Vector2 s;
    if (anchor_node >= 0 && anchor_node < int(dirs.size()) && project(v, dirs[size_t(anchor_node)], s)) {
        draw_arc(s, 12.0f, 0.0f, real_t(2.0 * PI), 24, Color(1.0f, 0.9f, 0.2f), 2.0f, true);
    }
    if (has_player && project(v, planet_unit(player_lat, player_lon), s)) {
        draw_circle(s, 4.0f, Color(1.0f, 0.25f, 0.2f));
        draw_arc(s, 6.0f, 0.0f, real_t(2.0 * PI), 16, Color(1, 1, 1), 1.5f, true);
    }
    if (jump_node >= 0 && jump_node < int(dirs.size()) && project(v, dirs[size_t(jump_node)], s)) {
        draw_arc(s, 15.0f, 0.0f, real_t(2.0 * PI * std::max(0.05, jump_progress)), 32, Color(0.4f, 1.0f, 0.6f), 3.0f, true);
    }
    if (selected >= 0 && project(v, dirs[size_t(selected)], s)) {
        draw_arc(s, 10.0f, 0.0f, real_t(2.0 * PI), 24, Color(0.4f, 1.0f, 1.0f), 2.5f, true);
    }
    if (hover >= 0 && hover != selected && project(v, dirs[size_t(hover)], s)) {
        draw_arc(s, 8.0f, 0.0f, real_t(2.0 * PI), 24, Color(1, 1, 1, 0.9f), 1.5f, true);
    }
    // 标题、图例、读数、选中与进度
    static const char *const MODES[3] = { "类（橙 密接 · 绿 中疏 · 蓝 稀疏 · 紫 孤悬）", "气候（台面年均温：蓝 冷 → 红 热）", "邦（一邦一色，灰 = 船团 / 部落）" };
    text(Vector2(24, 36), vformat(String(U"行星 · %d 个岛群 · 着色：%s"), planet->node_count(), String::utf8(MODES[color_mode])), 20, Color(0.95f, 0.9f, 0.75f));
    text(Vector2(24, 62), String(U"拖动转 · 滚轮缩放 · 点一下选中 · T / 双击 瞬移过去（开发者）· H 设为航向 · C 换着色 · 白圈 = 有包 · 黄圈 = 你在的群 · Esc / F2 合上"), 15,
            Color(0.7f, 0.72f, 0.7f));
    const int show = hover >= 0 ? hover : selected;
    if (show >= 0) {
        text(Vector2(24, real_t(size.y - 150)), readout(show), 17, Color(1, 1, 1));
    }
    if (selected >= 0) {
        text(Vector2(real_t(size.x * 0.55), real_t(size.y - 150)), vformat(String(U"选中群 #%d：T 瞬移过去%s · H 设为航向"), selected, teleport_enabled ? String() : String(U"（开发者工具关着）")), 18,
                Color(0.5f, 1.0f, 1.0f));
    }
    if (jump_node >= 0) {
        text(Vector2(real_t(size.x * 0.55), real_t(size.y - 110)), vformat(String(U"瞬移：%s · %d%%"), jump_text, int(std::lround(jump_progress * 100.0))), 18, Color(0.5f, 1.0f, 0.6f));
    }
}

void PlanetMap::_gui_input(const Ref<InputEvent> &p_event) {
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        if (dragging) {
            const Vector2 d = motion->get_relative();
            const double deg_per_px = 90.0 / std::max(1.0, view().radius);
            view_lon = std::fmod(view_lon - double(d.x) * deg_per_px * 1.6 + 540.0, 360.0) - 180.0;
            view_lat = std::clamp(view_lat + double(d.y) * deg_per_px * 1.6, -89.0, 89.0);
            queue_redraw();
        } else {
            const int h = pick(motion->get_position());
            if (h != hover) {
                hover = h;
                queue_redraw();
            }
        }
        accept_event();
        return;
    }
    const Ref<InputEventMouseButton> button = p_event;
    if (button.is_valid()) {
        if (button->get_button_index() == MOUSE_BUTTON_WHEEL_UP && button->is_pressed()) {
            zoom = std::min(60.0, zoom * 1.2);
            queue_redraw();
        } else if (button->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN && button->is_pressed()) {
            zoom = std::max(0.8, zoom / 1.2);
            queue_redraw();
        } else if (button->get_button_index() == MOUSE_BUTTON_LEFT && button->is_pressed()) {
            const int p = pick(button->get_position());
            if (p >= 0) {
                select(p);
                if (button->is_double_click()) {
                    request_jump();
                }
            } else {
                dragging = true;
            }
        } else if (button->get_button_index() == MOUSE_BUTTON_RIGHT || button->get_button_index() == MOUSE_BUTTON_MIDDLE) {
            dragging = button->is_pressed();
        } else if (!button->is_pressed()) {
            dragging = false;
        }
        if (button->get_button_index() == MOUSE_BUTTON_LEFT && !button->is_pressed()) {
            dragging = false;
        }
        accept_event();
        return;
    }
    const Ref<InputEventKey> key = p_event;
    if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
        switch (key->get_physical_keycode()) {
            case KEY_C:
                set_color_mode(color_mode + 1);
                break;
            case KEY_T:
            case KEY_ENTER:
                request_jump();
                break;
            case KEY_H:
                request_course();
                break;
            case KEY_LEFT:
                look_at_latlon(view_lat, view_lon - 10.0 / zoom);
                break;
            case KEY_RIGHT:
                look_at_latlon(view_lat, view_lon + 10.0 / zoom);
                break;
            case KEY_UP:
                look_at_latlon(view_lat + 10.0 / zoom, view_lon);
                break;
            case KEY_DOWN:
                look_at_latlon(view_lat - 10.0 / zoom, view_lon);
                break;
            default:
                return;
        }
        accept_event();
    }
}
