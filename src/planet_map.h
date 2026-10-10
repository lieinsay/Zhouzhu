#pragma once

#include <memory>
#include <vector>

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "planet_data.h"
#include "planet_frame.h"

// 行星地图（行星计划 P4，第五节 P4 第 4 条）：一个地球仪面板（正射投影），8000 个岛群一群一个点——
//   按类（密接 / 中疏 / 稀疏 / 孤悬）、气候（台面年均温）或邦着色（C 换）；点的大小按陆地；
//   悬停读数：节点号、陆地、经纬度、离你多远、邦与人口、气候、有没有缓存（仓库 / 生成过（生成器提交号）/ 没生成过）；
//   点一下选中：T（或双击）开发者瞬移过去（没生成过的当场生成，底下报进度），H 设为航向；
//   拖动转、滚轮缩放、方向键转；你在哪（红点）、此刻的锚群（黄圈）、航向（虚线大圆）、生成过的群（白圈）都标着。
// 自己不读游戏状态：行星包、GroupProvider（问缓存）、你在哪、航向、瞬移的进度由上层（DevTools）递进来；点了只发信号。
class PlanetMap : public godot::Control {
    GDCLASS(PlanetMap, godot::Control)

public:
    enum ColorMode {
        COLOR_CLASS,
        COLOR_CLIMATE,
        COLOR_STATE,
        COLOR_COUNT,
    };

private:
    std::shared_ptr<const PlanetData> planet;
    std::vector<PlanetVec> dirs; // 每群群心的单位向量（行星系）
    double max_area = 1.0;
    double view_lat = 0.0, view_lon = 0.0;
    double zoom = 1.0;
    int hover = -1, selected = -1;
    int color_mode = COLOR_CLASS;
    bool has_player = false;
    double player_lat = 0.0, player_lon = 0.0;
    int anchor_node = -1;
    int course_node = -1;
    int jump_node = -1;
    double jump_progress = 0.0;
    godot::String jump_text;
    godot::ObjectID provider_id;
    std::vector<uint8_t> available; // 1 = 有包（仓库 / 生成过）；隔一会儿问一次 GroupProvider
    double available_age = 1e9;
    bool dragging = false;
    godot::Vector2 last_mouse;
    bool teleport_enabled = true;
    bool compact = false; // M 地图壳负责标题、详情与操作按钮；旧开发者面板沿用完整绘制。

    struct View {
        PlanetVec f, e, n; // 朝着看的方向（屏幕里）、东、北
        godot::Vector2 center;
        double radius = 1.0;
    };
    View view() const;
    bool project(const View &p_view, const PlanetVec &p_dir, godot::Vector2 &r_screen) const;
    godot::Color node_color(int p_node) const;
    void refresh_available();
    godot::String readout(int p_node);
    void draw_globe();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    PlanetMap();
    void _gui_input(const godot::Ref<godot::InputEvent> &p_event) override;
    void _process(double p_delta) override;

    void set_planet(const std::shared_ptr<const PlanetData> &p_planet);
    void set_provider(godot::Object *p_provider);
    void set_player(double p_lat, double p_lon, int p_anchor_node);
    void set_course(int p_node);
    void set_jump(int p_node, double p_progress, const godot::String &p_text);
    void set_teleport_enabled(bool p_value) { teleport_enabled = p_value; }
    void set_compact(bool p_value);
    void zoom_by(double p_factor);

    // 给脚本（测试）
    void look_at_node(int p_node); // 把这一群转到正中
    void look_at_latlon(double p_lat, double p_lon);
    int get_hovered() const { return hover; }
    int get_selected() const { return selected; }
    void select(int p_node);
    int pick(const godot::Vector2 &p_screen) const; // 屏幕上这一点最近的群（10 像素内）；没有 −1
    godot::Vector2 node_screen(int p_node) const; // 这一群在屏幕（面板）上哪儿；在背面返回 (−1, −1)
    godot::String get_readout(int p_node) { return readout(p_node); }
    void set_color_mode(int p_mode);
    int get_color_mode() const { return color_mode; }
    void request_jump(); // 选中的那一群：发 jump_requested
    void request_course();
};

VARIANT_ENUM_CAST(PlanetMap::ColorMode);
