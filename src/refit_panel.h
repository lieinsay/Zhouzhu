#pragma once

#include <vector>

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>

#include "ship_layout.h"

// 改装面板——船节点模块界面：只负责画，并回答屏幕上的一点落在哪张卡片 / 哪个钮上。自己不读输入、不改船。
// 画的是（铺满屏幕的一层，点击由编排转给改装部件）：
//   左边一列——每种节点一张卡片：名字、形状（单节 / 前后双节 / 上下双节 / 左右双节）、功能、规矩（只能在最上面……）、
//     眼下能放几处（没处放的卡片暗下去）；鼠标指着的描亮，拖着的描黄；
//   右上——船况（节数、质量、推力、货位、船舱、航速）、鼠标指着的地方能不能放及原因、选中的节点与「拆除」钮（拆不了写原因）；
//   拖着节点时，一张小卡片跟着鼠标。
class RefitPanel : public godot::Control {
    GDCLASS(RefitPanel, godot::Control)

public:
    enum HitType {
        HIT_NONE = -1,
        HIT_CARD = 0, // index = ModuleKind
        HIT_REMOVE = 1, // 拆除钮
        HIT_PANEL = 2, // 面板的底子（点了不落到船上）
    };
    struct Hit {
        int type = HIT_NONE;
        int index = -1;
    };
    enum Tone {
        TONE_DIM,
        TONE_OK,
        TONE_WARN,
    };
    struct View {
        std::vector<int> spots; // 每种节点眼下能放几处（按 ModuleKind 排）
        int hover_card = -1;
        int drag_kind = -1; // 拖着 / 拿着的节点；−1 没有
        bool sticky = false; // 点了一下卡片拿着的（不是按住拖的）
        godot::Vector2 mouse;
        godot::String hover_text; // 鼠标指着的地方：能不能放、为什么；指着的节点是什么
        int hover_tone = TONE_DIM;
        bool has_selected = false;
        godot::String selected_title; // 「储存区（单节）」
        godot::String selected_where; // 「中线 · 第 1 层 · 舷门后 1 排」
        godot::String selected_detail; // 功能
        bool can_remove = false;
        godot::String remove_reason; // 拆不了的原因
        bool hover_remove = false;
        ShipStats stats;
    };

private:
    View view;
    std::vector<godot::Rect2> card_rects; // 按 ModuleKind
    godot::Rect2 palette_rect, info_rect, remove_rect;
    bool remove_shown = false;
    real_t card_h = 56;

    void layout();
    void text(const godot::Vector2 &p_at, const godot::String &p_text, int p_size, const godot::Color &p_color, real_t p_width = -1,
            godot::HorizontalAlignment p_align = godot::HORIZONTAL_ALIGNMENT_LEFT);
    void draw_card(ModuleKind p_kind, const godot::Rect2 &p_rect, bool p_hover, bool p_dragged, int p_spots, float p_alpha);
    void draw_info();

protected:
    static void _bind_methods();

public:
    RefitPanel();
    void _draw() override;

    void refresh(const View &p_view);
    // 本控件坐标里的一点落在哪
    Hit hit(const godot::Vector2 &p_local) const;
    // 某张卡片 / 拆除钮的中心（本控件坐标；没有返回 (−1, −1)）——测试拿它点鼠标
    godot::Vector2 card_center(ModuleKind p_kind) const;
    godot::Vector2 remove_center() const;
    real_t palette_right() const { return palette_rect.get_end().x; } // 左边那列卡片的右边缘
};
