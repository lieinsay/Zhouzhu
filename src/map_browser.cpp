#include "map_browser.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <godot_cpp/classes/box_container.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include "group_provider.h"
#include "planet_frame.h"
#include "planet_map.h"
#include "world_map.h"

using namespace godot;

namespace {
Label *label(const String &p_text, int p_size, const Color &p_color) {
    Label *l = memnew(Label);
    l->set_text(p_text);
    l->add_theme_font_size_override("font_size", p_size);
    l->add_theme_color_override("font_color", p_color);
    l->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    return l;
}

Button *button(const String &p_text, const StringName &p_name, Node *p_parent, const Callable &p_callback, bool p_primary = false) {
    Button *b = memnew(Button);
    b->set_name(p_name);
    b->set_text(p_text);
    b->add_theme_font_size_override("font_size", 17);
    b->set_custom_minimum_size(Vector2(0, 38));
    Ref<StyleBoxFlat> normal;
    normal.instantiate();
    normal->set_bg_color(p_primary ? Color(0.73f, 0.57f, 0.29f) : Color(0.10f, 0.17f, 0.22f));
    normal->set_corner_radius_all(5);
    normal->set_content_margin_all(10);
    b->add_theme_stylebox_override("normal", normal);
    Ref<StyleBoxFlat> hover = normal->duplicate();
    hover->set_bg_color(p_primary ? Color(0.85f, 0.69f, 0.39f) : Color(0.16f, 0.27f, 0.33f));
    b->add_theme_stylebox_override("hover", hover);
    b->add_theme_stylebox_override("pressed", hover);
    if (p_primary) {
        b->add_theme_color_override("font_color", Color(0.07f, 0.10f, 0.12f));
        b->add_theme_color_override("font_hover_color", Color(0.07f, 0.10f, 0.12f));
        b->add_theme_color_override("font_pressed_color", Color(0.07f, 0.10f, 0.12f));
    }
    b->connect("pressed", p_callback);
    p_parent->add_child(b);
    return b;
}

void panel_style(PanelContainer *p_panel) {
    Ref<StyleBoxFlat> style;
    style.instantiate();
    style->set_bg_color(Color(0.055f, 0.10f, 0.14f, 0.98f));
    style->set_border_color(Color(0.18f, 0.28f, 0.33f));
    style->set_border_width_all(1);
    style->set_corner_radius_all(8);
    style->set_content_margin_all(14);
    p_panel->add_theme_stylebox_override("panel", style);
}
}

void MapBrowser::_bind_methods() {
    ClassDB::bind_method(D_METHOD("show_local"), &MapBrowser::show_local);
    ClassDB::bind_method(D_METHOD("show_planet"), &MapBrowser::show_planet);
    ClassDB::bind_method(D_METHOD("is_planet_view"), &MapBrowser::is_planet_view);
    ClassDB::bind_method(D_METHOD("get_planet_map"), &MapBrowser::get_planet_map);
    ClassDB::bind_method(D_METHOD("lookup_node", "text"), &MapBrowser::lookup_node);
    ClassDB::bind_method(D_METHOD("get_selected_node"), &MapBrowser::get_selected_node);
    ClassDB::bind_method(D_METHOD("get_target_text"), &MapBrowser::get_target_text);
    ClassDB::bind_method(D_METHOD("has_preview"), &MapBrowser::has_preview);
    ClassDB::bind_method(D_METHOD("_selected", "node"), &MapBrowser::refresh_target);
    ClassDB::bind_method(D_METHOD("_submit_lookup"), &MapBrowser::submit_lookup);
    ClassDB::bind_method(D_METHOD("_submitted", "text"), &MapBrowser::submitted);
    ClassDB::bind_method(D_METHOD("_request_jump"), &MapBrowser::request_jump);
    ClassDB::bind_method(D_METHOD("_request_course", "node"), &MapBrowser::request_course);
    ClassDB::bind_method(D_METHOD("_close"), &MapBrowser::request_close);
    ClassDB::bind_method(D_METHOD("_locate_player"), &MapBrowser::locate_player);
    ClassDB::bind_method(D_METHOD("_color_mode", "mode"), &MapBrowser::set_color_mode);
    ADD_SIGNAL(MethodInfo("close_requested"));
    ADD_SIGNAL(MethodInfo("jump_requested", PropertyInfo(Variant::INT, "node")));
    ADD_SIGNAL(MethodInfo("course_requested", PropertyInfo(Variant::INT, "node")));
}

MapBrowser::MapBrowser() {
    set_mouse_filter(MOUSE_FILTER_STOP);
    set_clip_contents(true);
}

void MapBrowser::setup(WorldMap *p_local, const std::shared_ptr<const PlanetData> &p_planet, Object *p_provider) {
    local_map = p_local;
    planet = p_planet;
    provider_id = p_provider ? ObjectID(p_provider->get_instance_id()) : ObjectID();
    add_child(local_map);
    build_ui();
    open_local();
}

void MapBrowser::build_ui() {
    const Color white(0.91f, 0.94f, 0.92f), dim(0.61f, 0.72f, 0.76f);
    planet_map = memnew(PlanetMap);
    planet_map->set_name("PlanetOverview");
    planet_map->set_planet(planet);
    planet_map->set_provider(ObjectDB::get_instance(provider_id));
    planet_map->set_compact(true);
    planet_map->connect("selection_changed", Callable(this, "_selected"));
    planet_map->connect("jump_requested", Callable(this, "_request_jump").unbind(1));
    planet_map->connect("course_requested", Callable(this, "_request_course"));
    add_child(planet_map);

    toolbar = memnew(PanelContainer);
    toolbar->set_name("Toolbar");
    panel_style(toolbar);
    add_child(toolbar);
    HBoxContainer *row = memnew(HBoxContainer);
    row->set_name("ToolbarRow");
    row->add_theme_constant_override("separation", 8);
    toolbar->add_child(row);
    row->add_child(label(String(U"地图"), 23, white));
    local_tab = button(String(U"当前岛群"), "LocalTab", row, Callable(this, "show_local"));
    planet_tab = button(String(U"行星总览"), "PlanetTab", row, Callable(this, "show_planet"));
    current = label(String(), 16, Color(0.91f, 0.76f, 0.43f));
    current->set_h_size_flags(SIZE_EXPAND_FILL);
    current->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
    row->add_child(current);
    button(String(U"关闭 · M"), "CloseButton", row, Callable(this, "_close"));

    HBoxContainer *tools = memnew(HBoxContainer);
    tools->set_name("PlanetTools");
    tools->add_theme_constant_override("separation", 6);
    planet_tools = tools;
    add_child(tools);
    button(String(U"当前位置"), "LocatePlayer", tools, Callable(this, "_locate_player"));
    button("−", "ZoomOut", tools, Callable(planet_map, "zoom_by").bind(1.0 / 1.3));
    button("+", "ZoomIn", tools, Callable(planet_map, "zoom_by").bind(1.3));
    OptionButton *color_mode = memnew(OptionButton);
    color_mode->add_item(String(U"岛群类型"));
    color_mode->add_item(String(U"年均温"));
    color_mode->add_item(String(U"所属邦"));
    color_mode->connect("item_selected", Callable(this, "_color_mode"));
    tools->add_child(color_mode);

    target_card = memnew(PanelContainer);
    target_card->set_name("TargetCard");
    panel_style(target_card);
    add_child(target_card);
    VBoxContainer *content = memnew(VBoxContainer);
    content->set_name("Content");
    content->add_theme_constant_override("separation", 12);
    target_card->add_child(content);
    HBoxContainer *search_row = memnew(HBoxContainer);
    search_row->set_name("SearchRow");
    search_row->add_theme_constant_override("separation", 8);
    content->add_child(search_row);
    search = memnew(LineEdit);
    search->set_name("NodeSearch");
    search->set_placeholder(String(U"编号定位，如 3980"));
    search->set_h_size_flags(SIZE_EXPAND_FILL);
    search->set_keep_editing_on_text_submit(true);
    search->add_theme_font_size_override("font_size", 16);
    search->connect("text_submitted", Callable(this, "_submitted"));
    search_row->add_child(search);
    button(String(U"定位"), "LocateButton", search_row, Callable(this, "_submit_lookup"));

    target_body = memnew(BoxContainer);
    target_body->set_name("TargetBody");
    target_body->add_theme_constant_override("separation", 14);
    target_body->set_h_size_flags(SIZE_EXPAND_FILL);
    content->add_child(target_body);
    preview_box = memnew(VBoxContainer);
    preview_box->set_name("Preview");
    target_body->add_child(preview_box);
    preview = memnew(TextureRect);
    preview->set_name("CoarseTerrain");
    preview->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
    preview->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
    preview->set_texture_filter(TEXTURE_FILTER_NEAREST);
    preview->set_custom_minimum_size(Vector2(0, 140));
    preview_box->add_child(preview);
    preview_caption = label(String(U"选择岛群以查看地形"), 13, dim);
    preview_caption->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    preview_box->add_child(preview_caption);

    VBoxContainer *info = memnew(VBoxContainer);
    info->set_name("TargetInfo");
    info->set_h_size_flags(SIZE_EXPAND_FILL);
    info->add_theme_constant_override("separation", 8);
    target_body->add_child(info);
    target_title = label(String(U"选择目的岛群"), 25, white);
    info->add_child(target_title);
    target_kind = label(String(U"在行星图上点选，或按编号定位"), 15, dim);
    target_kind->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    info->add_child(target_kind);
    details = label(String(), 16, white);
    details->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    info->add_child(details);
    cache = label(String(), 14, dim);
    cache->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    info->add_child(cache);
    jump_button = button(String(U"跳转到此岛群"), "JumpButton", info, Callable(this, "_request_jump"), true);
    error = label(String(), 14, Color(0.98f, 0.68f, 0.44f));
    error->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    content->add_child(error);
    error->set_visible(false);

    footer = label(String(), 14, dim);
    footer->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
    add_child(footer);
    layout();
}

void MapBrowser::layout() {
    if (!toolbar) return;
    const Vector2 size = get_size();
    const bool wide = size.x >= 980;
    toolbar->set_position(Vector2(16, 14));
    toolbar->set_size(Vector2(std::max(0.0f, size.x - 32), 62));
    current->set_visible(size.x >= 760);
    local_map->set_position(Vector2(0, 88));
    local_map->set_size(Vector2(size.x, std::max(0.0f, size.y - 88)));
    const real_t bottom_card = std::min(real_t(286), size.y * 0.45f);
    planet_map->set_position(Vector2(16, 88));
    planet_map->set_size(wide ? Vector2(size.x - 356, size.y - 140) : Vector2(size.x - 32, std::max(real_t(120), size.y - 148 - bottom_card)));
    target_card->set_position(wide ? Vector2(size.x - 324, 88) : Vector2(16, size.y - 52 - bottom_card));
    target_card->set_size(wide ? Vector2(308, size.y - 140) : Vector2(size.x - 32, bottom_card));
    target_body->set_vertical(wide);
    preview_box->set_custom_minimum_size(wide ? Vector2(0, 0) : Vector2(170, 0));
    preview->set_custom_minimum_size(Vector2(0, wide ? 140 : 112));
    planet_tools->set_position(Vector2(28, 98));
    footer->set_position(Vector2(24, size.y - 42));
    footer->set_size(Vector2(size.x - 48, 34));
    if (target_title) refresh_target(get_selected_node());
}

void MapBrowser::_notification(int p_what) {
    if (p_what == NOTIFICATION_RESIZED) layout();
    if (p_what == NOTIFICATION_DRAW) draw_rect(Rect2(Vector2(), get_size()), Color(0.025f, 0.05f, 0.075f, 0.98f));
}

void MapBrowser::set_context(double p_lat, double p_lon, int p_anchor, bool p_can_jump) {
    const bool changed = anchor != p_anchor || can_jump != p_can_jump;
    player_lat = p_lat;
    player_lon = p_lon;
    anchor = p_anchor;
    can_jump = p_can_jump;
    current->set_text(vformat(String(U"所在岛群 #%d"), anchor));
    planet_map->set_player(p_lat, p_lon, p_anchor);
    planet_map->set_teleport_enabled(p_can_jump);
    if (changed) refresh_target(get_selected_node());
}

void MapBrowser::set_course(int p_node) { planet_map->set_course(p_node); }
void MapBrowser::set_jump(int p_node, double p_progress, const String &p_text) { planet_map->set_jump(p_node, p_progress, p_text); }

void MapBrowser::open_local() {
    search->release_focus();
    error->set_visible(false);
    show_local();
}

void MapBrowser::show_local() {
    planet_view = false;
    local_map->set_visible(true);
    planet_map->set_visible(false);
    target_card->set_visible(false);
    planet_tools->set_visible(false);
    local_tab->set_disabled(true);
    planet_tab->set_disabled(false);
    footer->set_visible(false); // 本地图保留自己的地表、资源与局部瞬移提示。
    search->release_focus();
}

void MapBrowser::show_planet() {
    if (!planet_view) {
        planet_map->look_at_node(anchor);
        planet_map->select(anchor);
    }
    planet_view = true;
    local_map->set_visible(false);
    planet_map->set_visible(true);
    target_card->set_visible(true);
    planet_tools->set_visible(true);
    local_tab->set_disabled(false);
    planet_tab->set_disabled(true);
    footer->set_text(String(U"拖动旋转 · 滚轮缩放 · 点选岛群后查看右侧目标　金圈：当前位置 · 青圈：目标 · 白圈：已准备"));
    footer->set_visible(true);
    planet_map->grab_focus();
    refresh_target(get_selected_node());
}

void MapBrowser::locate_player() { planet_map->look_at_node(anchor); planet_map->select(anchor); }
void MapBrowser::set_color_mode(int p_mode) { planet_map->set_color_mode(p_mode); }
void MapBrowser::request_close() { search->release_focus(); emit_signal("close_requested"); }
void MapBrowser::submit_lookup() { lookup_node(search->get_text()); }
void MapBrowser::submitted(const String &p_text) { lookup_node(p_text); }

void MapBrowser::show_error(const String &p_text) {
    error->set_text(p_text);
    error->set_visible(true);
}

bool MapBrowser::lookup_node(const String &p_text) {
    String text = p_text.strip_edges();
    if (text.begins_with("#")) text = text.substr(1).strip_edges();
    auto reject = [&](const String &p_error) {
        show_error(p_error);
        return false;
    };
    if (text.is_empty()) return reject(String(U"请输入岛群编号，如 3980"));
    for (int64_t k = 0; k < text.length(); k++) {
        if (text[k] < U'0' || text[k] > U'9') return reject(String(U"岛群编号须为非负整数"));
    }
    int node = 0;
    const int count = planet ? planet->node_count() : 0;
    for (int64_t k = 0; k < text.length(); k++) {
        const int64_t next = int64_t(node) * 10 + int(text[k] - U'0');
        if (next >= count) return reject(vformat(String(U"有效编号为 0–%d"), count - 1));
        node = int(next);
    }
    if (!planet_view) show_planet();
    error->set_visible(false);
    planet_map->select(node);
    planet_map->look_at_node(node);
    return true;
}

void MapBrowser::request_jump() {
    const int node = get_selected_node();
    if (can_jump && node >= 0 && node != anchor) emit_signal("jump_requested", node);
}

void MapBrowser::request_course(int p_node) { emit_signal("course_requested", p_node); }

int MapBrowser::get_selected_node() const { return planet_map ? planet_map->get_selected() : -1; }
String MapBrowser::get_target_text() const {
    return target_title->get_text() + "\n" + details->get_text() + "\n" + cache->get_text()
            + (error->is_visible() ? "\n" + error->get_text() : String());
}
bool MapBrowser::has_preview() const { return preview && preview->get_texture().is_valid(); }

void MapBrowser::refresh_target(int p_node) {
    const bool valid = planet && p_node >= 0 && p_node < planet->node_count();
    jump_button->set_disabled(!valid || !can_jump || p_node == anchor);
    if (!valid) {
        target_title->set_text(String(U"选择目的岛群"));
        target_kind->set_text(String(U"点选图上的岛群，或输入编号定位"));
        details->set_text(String());
        cache->set_text(String());
        refresh_preview(-1);
        return;
    }
    const PlanetNode &n = planet->node(p_node);
    const char *const names[] = { "密接", "中疏", "稀疏", "孤悬" };
    target_title->set_text(vformat(String(U"岛群 #%d"), p_node));
    target_kind->set_text(String::utf8(names[std::clamp(n.cls, 0, 3)]) + (n.state >= 0 ? vformat(String(U" · 邦 %d"), n.state) : String(U" · 船团 / 部落")));
    const double distance = great_circle_m(player_lat, player_lon, n.lat, n.lon, planet->radius_km * 1000.0);
    const bool wide = get_size().x >= 980;
    details->set_text(wide ? vformat(String(U"距当前位置 %s km · 方位 %d°\n%s %s° · %s %s°\n陆地 %s km² · 人口 %s\n台面年均温 %s °C"),
            String::num(distance / 1000.0, 1), int(std::lround(bearing_deg(player_lat, player_lon, n.lat, n.lon))),
            n.lat < 0 ? String(U"南纬") : String(U"北纬"), String::num(std::abs(n.lat), 2),
            n.lon < 0 ? String(U"西经") : String(U"东经"), String::num(std::abs(n.lon), 2),
            String::num(n.area_km2, 0), n.pop >= 10000 ? String::num(n.pop / 10000.0, 1) + String(U" 万") : String::num(n.pop, 0), String::num(n.temp_c, 1))
            : vformat(String(U"距当前位置 %s km · 方位 %d°\n陆地 %s km² · 年均温 %s °C"), String::num(distance / 1000.0, 1),
                      int(std::lround(bearing_deg(player_lat, player_lon, n.lat, n.lon))), String::num(n.area_km2, 0), String::num(n.temp_c, 1)));
    GroupProvider *provider = Object::cast_to<GroupProvider>(ObjectDB::get_instance(provider_id));
    const Dictionary state = provider ? provider->get_group_info(p_node) : Dictionary();
    const String source = state.get("source", String());
    cache->set_text(p_node == anchor ? String(U"当前位置所在的岛群")
                    : !can_jump ? String(U"步行时可用开发者跳转")
                    : source == "repo" || source == "cache" ? String(U"地形已准备 · 可以直接载入") : String(U"首次到访 · 跳转时准备详细地形"));
    jump_button->set_text(p_node == anchor ? String(U"已在此岛群") : vformat(String(U"跳转到 #%d"), p_node));
    refresh_preview(p_node);
}

void MapBrowser::refresh_preview(int p_node) {
    const uint64_t generation = planet ? planet->lod_generation() : 0;
    if (preview_node == p_node && preview_generation == generation) return;
    preview_node = p_node;
    preview_generation = generation;
    const PlanetLod lod = planet && p_node >= 0 ? planet->lod(p_node) : PlanetLod();
    if (lod.empty()) {
        preview->set_texture(Ref<Texture2D>());
        preview_caption->set_text(p_node < 0 ? String(U"选择岛群以查看地形") : String(U"此群暂无粗略地形图"));
        return;
    }
    // 直接绘制已有LOD的陆地占比、地表与高程，不补猜岛形或细节。
    const Color covers[] = { Color(0,0,0,0), Color(0.48f,0.44f,0.35f), Color(0.53f,0.52f,0.46f), Color(0.60f,0.65f,0.49f),
        Color(0.29f,0.48f,0.37f), Color(0.44f,0.57f,0.39f), Color(0.61f,0.66f,0.44f), Color(0.72f,0.67f,0.43f),
        Color(0.67f,0.64f,0.44f), Color(0.38f,0.56f,0.54f), Color(0.32f,0.59f,0.70f), Color(0.32f,0.59f,0.70f) };
    PackedByteArray bytes;
    bytes.resize(int64_t(lod.rows) * lod.cols * 4);
    for (size_t k = 0; k < lod.land.size(); k++) {
        const Color c = covers[std::min(size_t(lod.landcover[k]), std::size(covers) - 1)];
        const double light = 0.75 + 0.25 * std::min(1.0, double(lod.height[k]) / 3000.0);
        bytes.set(int64_t(k * 4), uint8_t(c.r * light * 255));
        bytes.set(int64_t(k * 4 + 1), uint8_t(c.g * light * 255));
        bytes.set(int64_t(k * 4 + 2), uint8_t(c.b * light * 255));
        bytes.set(int64_t(k * 4 + 3), lod.land[k]);
    }
    preview->set_texture(ImageTexture::create_from_image(Image::create_from_data(lod.cols, lod.rows, false, Image::FORMAT_RGBA8, bytes)));
    preview_caption->set_text(vformat(String(U"已有粗图 · 北 ↑ · 宽 %s km"), String::num(lod.cols * lod.res_m / 1000.0, 0)));
}

void MapBrowser::_process(double p_delta) {
    if (!is_visible_in_tree() || !planet_view) return;
    refresh_age += p_delta;
    if (refresh_age > 2.0) {
        refresh_age = 0.0;
        refresh_target(get_selected_node());
    }
}
