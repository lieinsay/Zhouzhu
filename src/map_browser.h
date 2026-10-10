#pragma once

#include <memory>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/core/object_id.hpp>
#include "planet_data.h"

namespace godot {
class Button;
class Label;
class LineEdit;
class PanelContainer;
class BoxContainer;
class TextureRect;
class VBoxContainer;
}
class WorldMap;
class PlanetMap;

// M 地图的两个尺度：当前岛群的细图、全行星的群心和选中群的已有粗图。
// 只管浏览和选择；跳转、航向与开合交给 Expedition。
class MapBrowser : public godot::Control {
    GDCLASS(MapBrowser, godot::Control)

    std::shared_ptr<const PlanetData> planet;
    godot::ObjectID provider_id;
    WorldMap *local_map = nullptr;
    PlanetMap *planet_map = nullptr;
    godot::PanelContainer *toolbar = nullptr, *target_card = nullptr;
    godot::Control *planet_tools = nullptr;
    godot::Button *local_tab = nullptr, *planet_tab = nullptr, *jump_button = nullptr;
    godot::Label *current = nullptr, *target_title = nullptr, *target_kind = nullptr;
    godot::Label *details = nullptr, *cache = nullptr, *error = nullptr, *preview_caption = nullptr, *footer = nullptr;
    godot::LineEdit *search = nullptr;
    godot::BoxContainer *target_body = nullptr;
    godot::VBoxContainer *preview_box = nullptr;
    godot::TextureRect *preview = nullptr;
    bool planet_view = false, can_jump = false;
    int anchor = -1, preview_node = -2;
    double player_lat = 0.0, player_lon = 0.0, refresh_age = 0.0;
    uint64_t preview_generation = 0;

    void build_ui();
    void layout();
    void refresh_target(int p_node);
    void refresh_preview(int p_node);
    void submit_lookup();
    void submitted(const godot::String &p_text);
    void request_jump();
    void request_course(int p_node);
    void request_close();
    void locate_player();
    void set_color_mode(int p_mode);

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    MapBrowser();
    void setup(WorldMap *p_local, const std::shared_ptr<const PlanetData> &p_planet, godot::Object *p_provider);
    void set_context(double p_lat, double p_lon, int p_anchor, bool p_can_jump);
    void set_course(int p_node);
    void set_jump(int p_node, double p_progress, const godot::String &p_text);
    void open_local();
    void show_local();
    void show_planet();
    bool is_planet_view() const { return planet_view; }
    PlanetMap *get_planet_map() const { return planet_map; }
    bool lookup_node(const godot::String &p_text); // 只定位和选中，不跳转。
    void show_error(const godot::String &p_text);
    int get_selected_node() const;
    godot::String get_target_text() const;
    bool has_preview() const;
    void _process(double p_delta) override;
};
