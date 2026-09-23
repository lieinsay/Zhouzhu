#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "world_grid.h"

// 岛群地图面板：铺满屏幕，底图是栅格的晕渲（一格一像素），上面标岛号、邑治、村、码头、玩家。
// 滚轮以鼠标处为中心缩放，右键 / 中键拖动平移，悬停读出该处的坐标、岛、高程、地表。
// 左键点一下只发 teleport_requested(世界坐标)——瞬不瞬移、许不许瞬移（开发者工具）是上层的事；
// 上层用 set_teleport_enabled 告诉它要不要把「左键瞬移」写进提示、要不要发这个信号。
// 自己不读游戏状态：玩家位置由上层每帧 set_player 递进来；码头能不能靠、实际在哪（岸缘点）、哪些码头与村子已经铺出来了，
// 也由上层递进来（set_dock / set_built_sites）——地图上的点与 3D 里的一一对应。
class WorldMap : public godot::Control {
    GDCLASS(WorldMap, godot::Control)

    struct Marker {
        enum Kind {
            ISLAND,
            SEAT,
            VILLAGE,
            DOCK,
            MAIN_DOCK,
        } kind;
        double x, z; // 世界坐标
        std::string label;
        double area_km2 = 0.0; // 岛：按面积决定多小时还标
        int id = -1; // 村号 / 码头号
        bool berthable = true; // 码头：能靠泊
        bool built = false; // 码头 / 村：3D 里已经铺出来了
    };

    const WorldGrid *grid = nullptr;
    std::vector<std::string> landcover_names;
    std::vector<Marker> markers;
    godot::Ref<godot::ImageTexture> texture;
    godot::String title;

    // 视图：屏幕中心对着底图的哪个像素、一个底图像素画多大
    godot::Vector2 center;
    double zoom = 1.0;
    bool fitted = false;
    bool dragging = false;
    godot::Vector2 hover = godot::Vector2(-1, -1);
    bool teleport_enabled = false;
    int destination_dock = -1;

    godot::Vector3 player_position;
    double player_yaw = 0.0; // 面朝方向，Player 的 look_yaw 约定（0 = 朝 −z 即北）

    void fit();
    godot::Vector2 map_to_screen(const godot::Vector2 &p_map) const;
    godot::Vector2 screen_to_map(const godot::Vector2 &p_screen) const;
    void zoom_at(const godot::Vector2 &p_screen, double p_factor);

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    WorldMap();
    void _gui_input(const godot::Ref<godot::InputEvent> &p_event) override;

    // p_info 是 world.json（World::get_info）；p_grid 须比本面板活得久
    void setup(const WorldGrid &p_grid, const godot::Dictionary &p_info);
    void set_player(const godot::Vector3 &p_position, double p_yaw);
    void set_teleport_enabled(bool p_enabled);
    bool is_teleport_enabled() const { return teleport_enabled; }
    // 码头的实际位置（岸缘点，世界坐标）与能不能靠泊
    void set_dock(int p_id, bool p_berthable, const godot::Vector3 &p_position);
    void set_destination_dock(int p_id);
    // 哪些码头 / 村（号）已在 3D 里铺出来了
    void set_built_sites(const godot::PackedInt32Array &p_docks, const godot::PackedInt32Array &p_villages);
    // 给测试：某码头 / 村标在哪（世界 xz，y = 0）、是否标为已铺；没有这个号返回 false
    bool get_marker(const godot::String &p_kind, int p_id, godot::Vector3 &r_position, bool &r_built) const;
    godot::Vector3 get_dock_marker(int p_id) const;
    bool is_village_marked_built(int p_id) const;
    bool is_dock_marked_berthable(int p_id) const;
    // 视图对准某处（世界 xz），p_meters_across = 屏幕宽对应多少米；≤ 0 则不改缩放
    void look_at_world(double p_x, double p_z, double p_meters_across);

    // 给测试驱动器：世界 xz ↔ 面板里的屏幕坐标
    godot::Vector2 world_to_screen(double p_x, double p_z) const;
    godot::Vector2 screen_to_world(const godot::Vector2 &p_screen) const;
    double get_zoom() const { return zoom; }
};
