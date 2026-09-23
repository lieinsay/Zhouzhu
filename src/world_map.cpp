#include "world_map.h"

#include <algorithm>
#include <cmath>

#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include "map_image.h"

using namespace godot;

namespace {

const Color BACKDROP(0.05f, 0.06f, 0.09f, 0.94f);
const Color TEXT(0.95f, 0.92f, 0.82f);
const Color TEXT_DIM(0.66f, 0.66f, 0.62f);
const Color OUTLINE(0, 0, 0, 0.9f);
const Color SEAT(0.95f, 0.35f, 0.25f);
const Color VILLAGE(0.98f, 0.86f, 0.55f);
const Color DOCK(0.45f, 0.75f, 0.98f);
const Color DOCK_OFF(0.45f, 0.47f, 0.50f); // 靠不了的码头
const Color DESTINATION(1.0f, 0.82f, 0.25f);
const Color BUILT_RING(1.0f, 1.0f, 1.0f);
const Color PLAYER(1.0f, 1.0f, 1.0f);
const Color PLAYER_EDGE(0.85f, 0.15f, 0.1f);

constexpr double ZOOM_MIN = 0.3; // 屏幕像素 / 底图像素（一底图像素 = 100 m）
constexpr double ZOOM_MAX = 60.0;
constexpr double VILLAGE_NAMES_ZOOM = 5.0; // 放到这么大才写村名
constexpr double DOCKS_ZOOM = 2.0;

String to_godot(const std::string &p_text) {
    return String::utf8(p_text.c_str());
}

std::string to_std(const String &p_text) {
    return std::string(p_text.utf8().get_data());
}

} // namespace

WorldMap::WorldMap() {
    set_mouse_filter(MOUSE_FILTER_STOP);
    set_texture_filter(TEXTURE_FILTER_LINEAR);
}

void WorldMap::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_teleport_enabled", "enabled"), &WorldMap::set_teleport_enabled);
    ClassDB::bind_method(D_METHOD("is_teleport_enabled"), &WorldMap::is_teleport_enabled);
    ClassDB::bind_method(D_METHOD("look_at_world", "x", "z", "meters_across"), &WorldMap::look_at_world);
    ClassDB::bind_method(D_METHOD("world_to_screen", "x", "z"), &WorldMap::world_to_screen);
    ClassDB::bind_method(D_METHOD("screen_to_world", "screen"), &WorldMap::screen_to_world);
    ClassDB::bind_method(D_METHOD("get_zoom"), &WorldMap::get_zoom);
    ClassDB::bind_method(D_METHOD("get_dock_marker", "id"), &WorldMap::get_dock_marker);
    ClassDB::bind_method(D_METHOD("is_village_marked_built", "id"), &WorldMap::is_village_marked_built);
    ClassDB::bind_method(D_METHOD("is_dock_marked_berthable", "id"), &WorldMap::is_dock_marked_berthable);
    ClassDB::bind_method(D_METHOD("set_built_sites", "docks", "villages"), &WorldMap::set_built_sites);
    ADD_SIGNAL(MethodInfo("teleport_requested", PropertyInfo(Variant::VECTOR3, "position")));
}

void WorldMap::setup(const WorldGrid &p_grid, const Dictionary &p_info) {
    grid = &p_grid;
    const MapImage img = build_map_image(p_grid);
    PackedByteArray bytes;
    bytes.resize(int64_t(img.rgba.size()));
    std::copy(img.rgba.begin(), img.rgba.end(), bytes.ptrw());
    texture = ImageTexture::create_from_image(Image::create_from_data(img.width, img.height, false, Image::FORMAT_RGBA8, bytes));

    const Dictionary source = p_info.get("source", Dictionary());
    title = vformat(String(U"岛群 #%d · %s · 人口 %d"), int64_t(double(source.get("node", 0))), source.get("cls_zh", ""), int64_t(double(p_info.get("population", 0))));

    landcover_names.clear();
    const Array classes = Dictionary(p_info.get("landcover", Dictionary())).get("classes", Array());
    for (int64_t k = 0; k < classes.size(); k++) {
        landcover_names.push_back(to_std(classes[k]));
    }

    markers.clear();
    auto pos_of = [](const Dictionary &p_row, double &r_x, double &r_z) {
        const Array pos = p_row.get("pos", p_row.get("center", Array()));
        r_x = pos.size() > 0 ? double(pos[0]) : 0.0;
        r_z = pos.size() > 1 ? double(pos[1]) : 0.0;
    };
    const Array islands = p_info.get("islands", Array());
    for (int64_t k = 0; k < islands.size(); k++) {
        const Dictionary row = islands[k];
        Marker m{ Marker::ISLAND, 0, 0, "#" + std::to_string(int(row.get("id", 0))) };
        m.area_km2 = double(row.get("area_km2", 0.0));
        pos_of(row, m.x, m.z);
        markers.push_back(m);
    }
    const Array villages = p_info.get("villages", Array());
    for (int64_t k = 0; k < villages.size(); k++) {
        const Dictionary row = villages[k];
        const bool seat = bool(row.get("seat", false));
        Marker m{ seat ? Marker::SEAT : Marker::VILLAGE, 0, 0, to_std(row.get("name", "")) };
        m.id = int(row.get("id", -1));
        if (seat) {
            m.label = to_std(String(U"邑治 · ")) + m.label;
        }
        pos_of(row, m.x, m.z);
        markers.push_back(m);
    }
    const Array docks = p_info.get("docks", Array());
    for (int64_t k = 0; k < docks.size(); k++) {
        const Dictionary row = docks[k];
        const bool main = bool(row.get("main", false));
        Marker m{ main ? Marker::MAIN_DOCK : Marker::DOCK, 0, 0, main ? to_std(String(U"主码头")) : "" };
        m.id = int(row.get("id", -1));
        pos_of(row, m.x, m.z);
        markers.push_back(m);
    }
    fitted = false;
    queue_redraw();
}

void WorldMap::set_player(const Vector3 &p_position, double p_yaw) {
    player_position = p_position;
    player_yaw = p_yaw;
    if (is_visible_in_tree()) {
        queue_redraw();
    }
}

void WorldMap::set_teleport_enabled(bool p_enabled) {
    teleport_enabled = p_enabled;
    queue_redraw();
}

void WorldMap::fit() {
    if (!grid || !texture.is_valid()) {
        return;
    }
    const Vector2 size = get_size();
    if (size.x <= 0 || size.y <= 0) {
        return;
    }
    zoom = 0.92 * std::min(size.x / texture->get_width(), size.y / texture->get_height());
    center = Vector2(texture->get_width(), texture->get_height()) * 0.5f;
    fitted = true;
}

void WorldMap::look_at_world(double p_x, double p_z, double p_meters_across) {
    if (!grid) {
        return;
    }
    double u, v;
    world_to_map(*grid, p_x, p_z, u, v);
    center = Vector2(real_t(u), real_t(v));
    Vector2 size = get_size();
    if (size.x <= 0 && get_viewport()) {
        size = get_viewport()->get_visible_rect().size;
    }
    if (p_meters_across > 0 && size.x > 0) {
        zoom = std::clamp(size.x / (p_meters_across / grid->res), ZOOM_MIN, ZOOM_MAX);
    }
    fitted = true;
    queue_redraw();
}

Vector2 WorldMap::map_to_screen(const Vector2 &p_map) const {
    return get_size() * 0.5f + (p_map - center) * real_t(zoom);
}

Vector2 WorldMap::screen_to_map(const Vector2 &p_screen) const {
    return center + (p_screen - get_size() * 0.5f) / real_t(zoom);
}

Vector2 WorldMap::world_to_screen(double p_x, double p_z) const {
    if (!grid) {
        return Vector2();
    }
    double u, v;
    world_to_map(*grid, p_x, p_z, u, v);
    return map_to_screen(Vector2(real_t(u), real_t(v)));
}

Vector2 WorldMap::screen_to_world(const Vector2 &p_screen) const {
    if (!grid) {
        return Vector2();
    }
    const Vector2 m = screen_to_map(p_screen);
    double x, z;
    map_to_world(*grid, m.x, m.y, x, z);
    return Vector2(real_t(x), real_t(z));
}

void WorldMap::zoom_at(const Vector2 &p_screen, double p_factor) {
    const Vector2 before = screen_to_map(p_screen);
    zoom = std::clamp(zoom * p_factor, ZOOM_MIN, ZOOM_MAX);
    center += before - screen_to_map(p_screen); // 鼠标下那一点不动
    queue_redraw();
}

void WorldMap::_gui_input(const Ref<InputEvent> &p_event) {
    const Ref<InputEventMouseButton> button = p_event;
    if (button.is_valid()) {
        const MouseButton which = button->get_button_index();
        if (button->is_pressed() && which == MOUSE_BUTTON_WHEEL_UP) {
            zoom_at(button->get_position(), 1.25);
        } else if (button->is_pressed() && which == MOUSE_BUTTON_WHEEL_DOWN) {
            zoom_at(button->get_position(), 0.8);
        } else if (which == MOUSE_BUTTON_RIGHT || which == MOUSE_BUTTON_MIDDLE) {
            dragging = button->is_pressed();
        } else if (which == MOUSE_BUTTON_LEFT && button->is_pressed() && teleport_enabled) {
            const Vector2 w = screen_to_world(button->get_position());
            emit_signal("teleport_requested", Vector3(w.x, 0, w.y));
        }
        accept_event();
        return;
    }
    const Ref<InputEventMouseMotion> motion = p_event;
    if (motion.is_valid()) {
        hover = motion->get_position();
        if (dragging) {
            center -= motion->get_relative() / real_t(zoom);
        }
        queue_redraw();
        accept_event();
    }
}

void WorldMap::_notification(int p_what) {
    if (p_what == NOTIFICATION_RESIZED && !fitted) {
        fit();
        return;
    }
    if (p_what != NOTIFICATION_DRAW) {
        return;
    }
    if (!fitted) {
        fit();
    }
    const Vector2 size = get_size();
    draw_rect(Rect2(Vector2(), size), BACKDROP);
    if (!grid || !texture.is_valid()) {
        return;
    }
    const Ref<Font> font = get_theme_default_font();
    auto text = [&](const Vector2 &p_at, const String &p_text, int p_size, const Color &p_color, HorizontalAlignment p_align = HORIZONTAL_ALIGNMENT_LEFT) {
        const real_t width = p_align == HORIZONTAL_ALIGNMENT_LEFT ? -1 : 600;
        const Vector2 at = p_align == HORIZONTAL_ALIGNMENT_CENTER ? p_at - Vector2(300, 0) : p_at;
        draw_string_outline(font, at, p_text, p_align, width, p_size, 4, OUTLINE);
        draw_string(font, at, p_text, p_align, width, p_size, p_color);
    };

    const Vector2 corner = map_to_screen(Vector2());
    draw_texture_rect(texture, Rect2(corner, Vector2(texture->get_width(), texture->get_height()) * real_t(zoom)), false);

    // 标记：岛号总写；村名放大了才写；码头放大了才画（主码头总画）
    for (const Marker &m : markers) {
        const Vector2 p = world_to_screen(m.x, m.z);
        if (p.x < -200 || p.y < -50 || p.x > size.x + 200 || p.y > size.y + 50) {
            continue;
        }
        switch (m.kind) {
            case Marker::ISLAND:
                if (m.area_km2 * zoom * zoom > 30.0) {
                    text(p + Vector2(0, 6), to_godot(m.label), m.area_km2 > 500 ? 22 : 16, TEXT_DIM, HORIZONTAL_ALIGNMENT_CENTER);
                }
                break;
            case Marker::SEAT: {
                if (m.built) {
                    draw_circle(p, 11.0f, BUILT_RING, false, 2.0f);
                }
                const real_t r = 7;
                const PackedVector2Array diamond({ p + Vector2(0, -r), p + Vector2(r, 0), p + Vector2(0, r), p + Vector2(-r, 0) });
                draw_colored_polygon(diamond, SEAT);
                text(p + Vector2(10, -8), to_godot(m.label), 18, TEXT);
                break;
            }
            case Marker::VILLAGE:
                if (m.built) {
                    draw_circle(p, 7.0f, BUILT_RING, false, 2.0f); // 已铺出来的村：白圈
                }
                draw_circle(p, zoom > VILLAGE_NAMES_ZOOM || m.built ? 4.0f : 2.5f, VILLAGE);
                if (zoom > VILLAGE_NAMES_ZOOM) {
                    text(p + Vector2(7, 5), to_godot(m.label), 14, TEXT);
                }
                break;
            case Marker::MAIN_DOCK:
            case Marker::DOCK: {
                const bool main = m.kind == Marker::MAIN_DOCK, destination = m.id == destination_dock;
                if (!main && !destination && zoom <= DOCKS_ZOOM) {
                    break;
                }
                const real_t r = main || destination ? 5 : 3;
                const Color fill = destination ? DESTINATION : (m.berthable ? DOCK : DOCK_OFF);
                if (m.berthable) {
                    draw_rect(Rect2(p - Vector2(r + 2, r + 2), Vector2(2 * r + 4, 2 * r + 4)), m.built ? BUILT_RING : OUTLINE); // 能靠泊：描边高亮
                }
                draw_rect(Rect2(p - Vector2(r, r), Vector2(2 * r, 2 * r)), fill);
                if (destination) {
                    text(p + Vector2(9, 16), String(U"目的码头 #") + String::num_int64(m.id), 16, DESTINATION);
                } else if (main) {
                    text(p + Vector2(9, 16), to_godot(m.label), 16, DOCK);
                }
                break;
            }
        }
    }

    // 玩家：箭头指向面朝方向（look_yaw 0 = −z = 北 = 屏幕上方）
    const Vector2 me = world_to_screen(player_position.x, player_position.z);
    const Vector2 fwd(real_t(-std::sin(player_yaw)), real_t(-std::cos(player_yaw)));
    const Vector2 side(-fwd.y, fwd.x);
    const PackedVector2Array arrow({ me + fwd * 13, me - fwd * 8 + side * 8, me - fwd * 4, me - fwd * 8 - side * 8 });
    draw_colored_polygon(arrow, PLAYER_EDGE);
    const PackedVector2Array inner({ me + fwd * 9, me - fwd * 5 + side * 5, me - fwd * 2, me - fwd * 5 - side * 5 });
    draw_colored_polygon(inner, PLAYER);

    // 比例尺：挑 1 / 2 / 5 × 10ⁿ 米里长约 140 像素的那个
    const double meters_per_px = grid->res / zoom;
    const double base = std::pow(10.0, std::floor(std::log10(140.0 * meters_per_px)));
    double bar_m = base;
    for (double k : { 2.0, 5.0 }) {
        if (base * k / meters_per_px <= 160.0) {
            bar_m = base * k;
        }
    }
    const real_t bar_px = real_t(bar_m / meters_per_px);
    const Vector2 bar_at(28, size.y - 36);
    draw_rect(Rect2(bar_at, Vector2(bar_px, 5)), TEXT);
    text(bar_at + Vector2(0, -8), bar_m >= 1000 ? vformat("%s km", String::num(bar_m / 1000.0, 1)) : vformat("%d m", int(bar_m)), 16, TEXT);

    // 标题与提示
    text(Vector2(28, 40), title, 24, TEXT);
    String hint = String(U"滚轮缩放 · 右键拖动 · M 合上");
    if (teleport_enabled) {
        hint = String(U"左键瞬移（开发者）· ") + hint;
    }
    text(Vector2(28, 66), hint, 16, TEXT_DIM);

    // 悬停读数
    if (hover.x >= 0 && hover.y >= 0) {
        const Vector2 w = screen_to_world(hover);
        const size_t cell = grid->nearest_cell(w.x, w.y);
        String line = vformat(String(U"东 %s km · 南 %s km"), String::num(w.x / 1000.0, 2), String::num(w.y / 1000.0, 2));
        if (grid->shore_at(w.x, w.y) > 0.0) {
            const int cls = grid->landcover[cell];
            const String cover = cls >= 0 && cls < int(landcover_names.size()) ? to_godot(landcover_names[size_t(cls)]) : String();
            line += vformat(String(U" · 岛 #%d · 高 %d m · %s"), int(grid->island[cell]), int(std::lround(grid->height_at(w.x, w.y))), cover);
        } else {
            line += String(U" · 云海");
        }
        text(Vector2(size.x * 0.5f, size.y - 30), line, 18, TEXT, HORIZONTAL_ALIGNMENT_CENTER);
    }
}

void WorldMap::set_dock(int p_id, bool p_berthable, const Vector3 &p_position) {
    for (Marker &m : markers) {
        if ((m.kind == Marker::DOCK || m.kind == Marker::MAIN_DOCK) && m.id == p_id) {
            m.berthable = p_berthable;
            m.x = p_position.x;
            m.z = p_position.z;
        }
    }
    queue_redraw();
}

void WorldMap::set_destination_dock(int p_id) {
    destination_dock = p_id;
    queue_redraw();
}

void WorldMap::set_built_sites(const PackedInt32Array &p_docks, const PackedInt32Array &p_villages) {
    for (Marker &m : markers) {
        if (m.kind == Marker::DOCK || m.kind == Marker::MAIN_DOCK) {
            m.built = p_docks.has(m.id);
        } else if (m.kind == Marker::VILLAGE || m.kind == Marker::SEAT) {
            m.built = p_villages.has(m.id);
        }
    }
    queue_redraw();
}

bool WorldMap::get_marker(const String &p_kind, int p_id, Vector3 &r_position, bool &r_built) const {
    const bool dock = p_kind == "dock";
    for (const Marker &m : markers) {
        const bool is_dock = m.kind == Marker::DOCK || m.kind == Marker::MAIN_DOCK;
        const bool is_village = m.kind == Marker::VILLAGE || m.kind == Marker::SEAT;
        if (m.id == p_id && ((dock && is_dock) || (!dock && is_village))) {
            r_position = Vector3(real_t(m.x), 0, real_t(m.z));
            r_built = m.built;
            return true;
        }
    }
    return false;
}

Vector3 WorldMap::get_dock_marker(int p_id) const {
    Vector3 position;
    bool built = false;
    get_marker("dock", p_id, position, built);
    return position;
}

bool WorldMap::is_village_marked_built(int p_id) const {
    Vector3 position;
    bool built = false;
    return get_marker("village", p_id, position, built) && built;
}

bool WorldMap::is_dock_marked_berthable(int p_id) const {
    for (const Marker &m : markers) {
        if ((m.kind == Marker::DOCK || m.kind == Marker::MAIN_DOCK) && m.id == p_id) {
            return m.berthable;
        }
    }
    return false;
}
