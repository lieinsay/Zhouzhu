#include "interior_edit.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <godot_cpp/classes/input.hpp>

#include "building_parts.h"
#include "input_actions.h"
#include "template_library.h"

using namespace godot;

namespace {

BoxPart ghost_box(double p_sx, double p_sy, double p_sz, double p_x, double p_y, double p_z, double p_yaw = 0.0) {
    BoxPart b;
    b.sx = p_sx, b.sy = p_sy, b.sz = p_sz;
    b.x = p_x, b.y = p_y, b.z = p_z;
    b.yaw = p_yaw;
    return b;
}

BoxPart rect_box(const InteriorRect &p_rect, double p_y0, double p_h) {
    return ghost_box(p_rect.x1 - p_rect.x0, p_h, p_rect.z1 - p_rect.z0, 0.5 * (p_rect.x0 + p_rect.x1), p_y0 + 0.5 * p_h, 0.5 * (p_rect.z0 + p_rect.z1));
}

// 外墙上开口的样子：墙厚里一块，宽 p_width，高按门 / 窗
BoxPart opening_box(const BuildingDef &p_def, WallSide p_side, double p_at, double p_width, bool p_window, double p_y0) {
    Opening o;
    o.side = p_side;
    o.at = p_at;
    o.width = p_width;
    o.window = p_window;
    double x = 0.0, z = 0.0, y0 = 0.0, y1 = 0.0;
    opening_point(p_def, o, -0.5 * house_dims::WALL_T, x, z);
    opening_span(p_def, o, y0, y1);
    const bool along_x = p_side == WallSide::FRONT || p_side == WallSide::BACK;
    return ghost_box(along_x ? p_width : house_dims::WALL_T + 0.1, y1 - y0, along_x ? house_dims::WALL_T + 0.1 : p_width, x, p_y0 + 0.5 * (y0 + y1), z);
}

std::string strip_digits(std::string p_text) {
    while (!p_text.empty() && std::isdigit(static_cast<unsigned char>(p_text.back()))) {
        p_text.pop_back();
    }
    return p_text.empty() ? std::string("custom") : p_text;
}

} // namespace

void InteriorEdit::setup(InteriorPanel *p_panel, InteriorOverlay *p_overlay) {
    panel = p_panel;
    overlay = p_overlay;
}

Transform3D InteriorEdit::building_transform() const {
    return target->get_building_transform(index);
}

bool InteriorEdit::open_on(Settlement *p_settlement, int p_index, const Say &p_say) {
    TemplateLibrary *library = TemplateLibrary::get_singleton();
    if (!library) {
        p_say(String(U"没有模板库（autoload BuildingTemplates）：改不了房屋模板"));
        return false;
    }
    if (!p_settlement || p_index < 0 || p_index >= p_settlement->get_count()) {
        p_say(String(U"光标下没有房子：站到房子里，或在布置模式里把光标挪到房子上，再按 T"));
        return false;
    }
    Building b;
    b.kind = play::to_std(p_settlement->get_building_kind(p_index));
    b.style = play::to_std(p_settlement->get_building_style(p_index));
    def = buildings::by_id(b.kind);
    if (!def) {
        return false;
    }
    if (!def->house) {
        p_say(String::utf8(def->name) + String(U"不是房屋，没有屋里可改"));
        return false;
    }
    // 自动挑的要按位置挑，得带上位置：直接问聚落这座实际用的哪套
    const HouseTemplate *used = library->find(play::to_std(p_settlement->get_building_template(p_index)));
    work = used ? *used : bare_template(*def);
    open = true;
    target = p_settlement;
    index = p_index;
    cursor_x = 0.0;
    cursor_z = 0.0;
    level = 0;
    tool = 0;
    quarter = 0;
    wall_started = false;
    report = interior_report(work, *def);
    target->set_cutaway(index, level);
    panel->set_visible(true);
    overlay->set_visible(true);
    overlay->show_at(building_transform(), get_cursor_global());
    overlay->get_camera()->make_current();
    refresh();
    p_say(String(U"改「") + String::utf8(work.name.c_str()) + String(U"」：用这套模板的房子都跟着变"));
    return true;
}

void InteriorEdit::close(Player *p_player) {
    if (!open) {
        return;
    }
    open = false;
    if (target) {
        target->set_cutaway(-1, -1);
    }
    target = nullptr;
    index = -1;
    panel->set_visible(false);
    overlay->set_visible(false);
    p_player->get_camera()->make_current();
}

void InteriorEdit::orbit(double p_dx, double p_dy) {
    overlay->orbit(-p_dx * MOUSE_SENSITIVITY, p_dy * MOUSE_SENSITIVITY);
}

void InteriorEdit::zoom(bool p_in) {
    overlay->zoom(p_in ? ZOOM_STEP : 1.0 / ZOOM_STEP);
}

double InteriorEdit::snapped_x() const {
    return std::round(cursor_x / SNAP) * SNAP;
}

double InteriorEdit::snapped_z() const {
    return std::round(cursor_z / SNAP) * SNAP;
}

Vector3 InteriorEdit::get_cursor_global() const {
    if (!target) {
        return Vector3();
    }
    return building_transform().xform(Vector3(real_t(snapped_x()), real_t(level_floor(*def, level)), real_t(snapped_z())));
}

void InteriorEdit::commit() {
    report = interior_report(work, *def);
    TemplateLibrary::get_singleton()->put(work); // 聚落收到 changed 就地重画用到它的房子
}

void InteriorEdit::browse(double p_delta, bool p_interact, const Say &p_say) {
    Input *input = Input::get_singleton();
    auto pressed = [input](const char *p_action) { return input->is_action_just_pressed(p_action); };
    TemplateLibrary *library = TemplateLibrary::get_singleton();

    // 挪光标：按相机的水平朝向，换算到建筑自己的坐标里
    const double fb = double(input->is_action_pressed(input_actions::MOVE_FORWARD)) - double(input->is_action_pressed(input_actions::MOVE_BACK));
    const double lr = double(input->is_action_pressed(input_actions::MOVE_RIGHT)) - double(input->is_action_pressed(input_actions::MOVE_LEFT));
    if (fb != 0.0 || lr != 0.0) {
        const Vector3 dir = (overlay->get_forward() * real_t(fb) + overlay->get_right() * real_t(lr)).normalized();
        const Vector3 local = building_transform().basis.xform_inv(dir);
        const double speed = CURSOR_SPEED * (input->is_action_pressed(input_actions::SPRINT) ? CURSOR_SPRINT : 1.0);
        cursor_x = std::clamp(cursor_x + local.x * speed * p_delta, -0.5 * def->width_m, 0.5 * def->width_m);
        cursor_z = std::clamp(cursor_z + local.z * speed * p_delta, -0.5 * def->depth_m, 0.5 * def->depth_m);
    }
    const int turn = int(pressed(input_actions::NEXT_STYLE)) - int(pressed(input_actions::PREV_STYLE));
    if (turn != 0) {
        const int n = interior_tool_count();
        tool = ((tool + turn) % n + n) % n;
        wall_started = false;
    }
    quarter = (quarter + int(pressed(input_actions::ROTATE_RIGHT)) - int(pressed(input_actions::ROTATE_LEFT)) + 4) % 4;
    const int climb = int(pressed(input_actions::JUMP)) - int(pressed(input_actions::FLY_DOWN));
    if (climb != 0) {
        const int to = std::clamp(level + climb, 0, def->levels - 1);
        if (to == level) {
            p_say(def->levels == 1 ? String(U"这座只有一层") : (climb > 0 ? String(U"已经是顶层") : String(U"已经是底层")));
        } else {
            level = to;
            wall_started = false;
            target->set_cutaway(index, level);
            p_say(String(U"改") + String::utf8(level_label(level)));
        }
    }

    const double x = snapped_x(), z = snapped_z();
    const InteriorHit hit = interior_find(work, *def, x, z, level);
    if (pressed(input_actions::REMOVE)) {
        if (wall_started) {
            wall_started = false;
            p_say(String(U"不画这道隔墙了"));
        } else if (hit.kind != InteriorHit::NONE) {
            const String name = String::utf8(interior_hit_name(work, hit).c_str());
            interior_remove(work, hit);
            commit();
            p_say(String(U"拆掉了") + name);
        } else {
            p_say(String(U"光标下没有东西"));
        }
    } else if (p_interact) {
        const InteriorTool t = interior_tool(tool);
        std::string reason;
        if (t.kind == InteriorToolKind::WALL) {
            if (!wall_started) {
                wall_started = true;
                wall_x = x;
                wall_z = z;
                p_say(String(U"隔墙起点定了：挪到终点再按 E（X 取消）"));
            } else {
                reason = interior_place_wall(work, *def, wall_x, wall_z, x, z, level);
                if (reason.empty()) {
                    wall_started = false;
                    commit();
                    p_say(String(U"砌了一道隔墙"));
                } else {
                    p_say(String::utf8(reason.c_str()));
                }
            }
        } else {
            reason = interior_place_tool(work, *def, tool, x, z, quarter, level);
            if (reason.empty()) {
                commit();
                p_say(String(U"放了") + get_tool_name());
            } else {
                p_say(String::utf8(reason.c_str()));
            }
        }
    }
    if (pressed(input_actions::SAVE_AS)) {
        const std::string shell = work.shell();
        const std::string style = library->unused_style(shell, strip_digits(work.style()));
        work.id = shell + "/" + style;
        work.name = work.name + " " + style.substr(strip_digits(style).size());
        work.auto_pick = false;
        library->put(work);
        target->set_building_style(index, String::utf8(style.c_str())); // 这座改用新的；整村重画，剖视照旧
        report = interior_report(work, *def);
        p_say(String(U"另存为 ") + String::utf8(work.id.c_str()) + String(U"（不参与自动挑），这座改用它：F5 把模板和清单都存下"));
    }
    if (pressed(input_actions::SAVE)) {
        const bool saved = library->save(String::utf8(work.id.c_str()));
        const bool layout_saved = target->is_dirty() && target->save();
        if (saved) {
            p_say(String(U"模板存到 ") + library->path_of(String::utf8(work.id.c_str())) + (layout_saved ? String(U"，清单存到 ") + target->get_layout_path() : String()));
        } else {
            p_say(String(U"模板存不了"));
        }
    }
}

// 每帧：光标、幽灵与面板。光标下有东西就描它（白）；没有就按选的工具画要放的那一样，能放绿、不能放红
void InteriorEdit::refresh() {
    const double x = snapped_x(), z = snapped_z();
    const double y0 = level_floor(*def, level), lh = level_height(*def);
    const InteriorHit hit = wall_started ? InteriorHit() : interior_find(work, *def, x, z, level);
    const InteriorTool t = interior_tool(tool);
    std::vector<BoxPart> ghost;
    String status;
    bool ok = true;
    InteriorOverlay::GhostState state = InteriorOverlay::GHOST_CAN_PLACE;
    if (hit.kind != InteriorHit::NONE) {
        state = InteriorOverlay::GHOST_SELECTED;
        status = String(U"这里是") + String::utf8(interior_hit_name(work, hit).c_str()) + String(U" · X 拆掉");
        switch (hit.kind) {
            case InteriorHit::ITEM: {
                const Furnishing &f = work.items[size_t(hit.index)];
                const FurnitureDef *fd = furniture::by_id(f.kind);
                ghost.push_back(rect_box(furnishing_rect(f), y0, fd ? std::max(0.1, fd->h) : 1.0));
            } break;
            case InteriorHit::LADDER:
                ghost.push_back(rect_box(ladder_board_rect(work.ladders[size_t(hit.index)]), y0, lh));
                break;
            case InteriorHit::DOORWAY: {
                const Doorway &d = work.doorways[size_t(hit.index)];
                ghost.push_back(ghost_box(house_dims::DOORWAY_W, house_dims::DOORWAY_H, house_dims::DOORWAY_W, d.x, y0 + 0.5 * house_dims::DOORWAY_H, d.z));
            } break;
            case InteriorHit::OPENING: {
                const Opening &o = work.openings[size_t(hit.index)];
                ghost.push_back(opening_box(*def, o.side, o.at, o.width, o.window, y0));
            } break;
            case InteriorHit::PARTITION:
                for (const InteriorRect &r : partition_pieces(work, hit.index)) {
                    ghost.push_back(rect_box(r, y0, lh - 0.3));
                }
                break;
            case InteriorHit::NONE:
                break;
        }
    } else {
        HouseTemplate trial = work;
        std::string reason;
        switch (t.kind) {
            case InteriorToolKind::WALL: {
                if (wall_started) {
                    reason = interior_place_wall(trial, *def, wall_x, wall_z, x, z, level);
                    double ax = 0.0, az = 0.0, bx = 0.0, bz = 0.0;
                    interior_wall_snap(*def, wall_x, wall_z, x, z, ax, az, bx, bz);
                    ghost.push_back(ghost_box(std::max(house_dims::PARTITION_T, bx - ax), lh - 0.3, std::max(house_dims::PARTITION_T, bz - az), 0.5 * (ax + bx), y0 + 0.5 * (lh - 0.3), 0.5 * (az + bz)));
                    status = reason.empty() ? String(U"E 砌到这里") : String::utf8(reason.c_str());
                } else {
                    ghost.push_back(ghost_box(house_dims::PARTITION_T, lh - 0.3, house_dims::PARTITION_T, x, y0 + 0.5 * (lh - 0.3), z));
                    status = String(U"E 定隔墙起点");
                }
            } break;
            case InteriorToolKind::DOOR:
            case InteriorToolKind::WINDOW: {
                reason = interior_place_tool(trial, *def, tool, x, z, quarter, level);
                WallSide side = WallSide::FRONT;
                double at = 0.0;
                const bool window = t.kind == InteriorToolKind::WINDOW;
                if (nearest_wall(*def, x, z, 0.8, side, at)) {
                    ghost.push_back(opening_box(*def, side, at, window ? 1.2 : 1.3, window, y0));
                } else {
                    ghost.push_back(ghost_box(window ? 1.2 : 1.0, window ? 1.3 : 2.3, 0.3, x, y0 + (window ? 1.65 : 1.15), z));
                }
                status = reason.empty() ? String(U"E 开") + get_tool_name() : String::utf8(reason.c_str());
            } break;
            case InteriorToolKind::LADDER: {
                reason = interior_place_tool(trial, *def, tool, x, z, quarter, level);
                const LadderSpot l{ x, z, quarter, level };
                ghost.push_back(rect_box(ladder_board_rect(l), y0, lh));
                ghost.push_back(rect_box(ladder_foot_rect(l), y0, 0.05));
                status = reason.empty() ? String(U"E 搭梯子（人站在亮的那一侧，Q/R 转）") : String::utf8(reason.c_str());
            } break;
            case InteriorToolKind::FURNITURE: {
                reason = interior_place_tool(trial, *def, tool, x, z, quarter, level);
                Furnishing f;
                f.kind = furniture::DEFS[t.furniture].id;
                f.x = x;
                f.z = z;
                f.quarter = quarter;
                f.level = level;
                std::vector<BoxPart> parts;
                furniture_parts(f, y0, 0, parts);
                for (const BoxPart &p : parts) {
                    if (p.visual) {
                        ghost.push_back(p);
                    }
                }
                status = reason.empty() ? String(U"E 放") + get_tool_name() : String::utf8(reason.c_str());
            } break;
        }
        ok = reason.empty();
        state = ok ? InteriorOverlay::GHOST_CAN_PLACE : InteriorOverlay::GHOST_CANNOT_PLACE;
    }
    overlay->refresh(building_transform(), get_cursor_global(), Vector3(real_t(x), real_t(y0), real_t(z)), ghost, state);

    InteriorPanel::View view;
    const TemplateLibrary *library = TemplateLibrary::get_singleton();
    view.name = String::utf8(work.name.c_str());
    view.id = String::utf8(work.id.c_str());
    view.level = level;
    view.levels = def->levels;
    view.x = x;
    view.z = z;
    view.status = status;
    view.ok = ok;
    for (int k = 0; k < interior_tool_count(); k++) {
        view.tools.push_back(String::utf8(interior_tool_name(k).c_str()));
    }
    view.tool = tool;
    view.quarter = quarter;
    view.drawing_wall = wall_started;
    for (const std::string &p : report.problems) {
        view.problems.push_back(String::utf8(p.c_str()));
    }
    view.path = library ? library->path_of(view.id) : String();
    view.dirty = library && library->is_dirty(work.id);
    panel->refresh(view);
}
