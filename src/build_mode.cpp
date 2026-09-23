#include "build_mode.h"

#include <cmath>

#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/core/math.hpp>

#include "input_actions.h"

using namespace godot;

void BuildMode::setup(BuildPanel *p_panel, BuildOverlay *p_overlay) {
    panel = p_panel;
    overlay = p_overlay;
}

Settlement *BuildMode::settlement_of(GroundHost *p_host) {
    if (!p_host) {
        return nullptr;
    }
    const TypedArray<Node> children = p_host->host_node()->get_children();
    for (int64_t k = 0; k < children.size(); k++) {
        if (Settlement *settlement = Object::cast_to<Settlement>(children[k])) {
            return settlement;
        }
    }
    return nullptr;
}

bool BuildMode::open_on(GroundHost *p_host, const Vector3 &p_at, const Say &p_say) {
    Settlement *settlement = settlement_of(p_host);
    if (!settlement) {
        p_say(String(U"脚下没有聚落：在地面宿主下面挂一个 Settlement，或走到村子里再来布置"));
        return false;
    }
    open = true;
    host = p_host;
    target = settlement;
    cursor = p_at;
    yaw = 0.0;
    kind = 0;
    selected = -1;
    panel->set_visible(true);
    overlay->set_visible(true);
    overlay->show_at(cursor_snapped());
    overlay->get_camera()->make_current();
    refresh();
    return true;
}

void BuildMode::close(Player *p_player) {
    if (!open) {
        return;
    }
    open = false;
    host = nullptr;
    target = nullptr;
    selected = -1;
    panel->set_visible(false);
    overlay->set_visible(false);
    p_player->get_camera()->make_current();
}

void BuildMode::orbit(double p_dx, double p_dy) {
    overlay->orbit(-p_dx * MOUSE_SENSITIVITY, p_dy * MOUSE_SENSITIVITY);
}

void BuildMode::zoom(bool p_in) {
    overlay->zoom(p_in ? ZOOM_STEP : 1.0 / ZOOM_STEP);
}

Vector3 BuildMode::cursor_snapped() const {
    if (!target) {
        return cursor;
    }
    const Transform3D xf = host->host_node()->get_global_transform();
    Vector3 local = xf.affine_inverse().xform(cursor);
    local.x = real_t(std::round(local.x / SNAP) * SNAP);
    local.z = real_t(std::round(local.z / SNAP) * SNAP);
    Vector3 snapped = xf.xform(local);
    const double ground = target->get_ground_height(snapped);
    if (!std::isnan(ground)) {
        snapped.y = real_t(ground);
    }
    return snapped;
}

String BuildMode::get_kind() const {
    return String(buildings::at(kind).id);
}

double BuildMode::get_yaw_deg() const {
    return Math::rad_to_deg(yaw);
}

void BuildMode::browse(double p_delta, bool p_interact, const Say &p_say) {
    Input *input = Input::get_singleton();
    auto pressed = [input](const char *p_action) { return input->is_action_just_pressed(p_action); };

    const double fb = double(input->is_action_pressed(input_actions::MOVE_FORWARD)) - double(input->is_action_pressed(input_actions::MOVE_BACK));
    const double lr = double(input->is_action_pressed(input_actions::MOVE_RIGHT)) - double(input->is_action_pressed(input_actions::MOVE_LEFT));
    if (fb != 0.0 || lr != 0.0) {
        const Vector3 dir = (overlay->get_forward() * real_t(fb) + overlay->get_right() * real_t(lr)).normalized();
        const double speed = CURSOR_SPEED * (input->is_action_pressed(input_actions::SPRINT) ? CURSOR_SPRINT : 1.0);
        cursor += dir * real_t(speed * p_delta);
        const double ground = target->get_ground_height(cursor);
        if (!std::isnan(ground)) {
            cursor.y = real_t(ground);
        }
    }
    yaw += Math::deg_to_rad(YAW_STEP) * (double(pressed(input_actions::ROTATE_RIGHT)) - double(pressed(input_actions::ROTATE_LEFT)));
    yaw = std::fmod(yaw + 4.0 * Math_PI, 2.0 * Math_PI);
    const char *kind_keys[4] = { input_actions::KIND_1, input_actions::KIND_2, input_actions::KIND_3, input_actions::KIND_4 };
    for (int k = 0; k < 4 && k < buildings::COUNT; k++) {
        if (pressed(kind_keys[k])) {
            kind = k;
        }
    }

    const Vector3 spot = cursor_snapped();
    selected = target->find_at(spot);
    const BuildingDef &def = buildings::at(kind);
    if (pressed(input_actions::REMOVE)) {
        if (selected >= 0) {
            const String name = String::utf8(buildings::by_id(play::to_std(target->get_building_kind(selected)))->name);
            target->remove(selected);
            selected = -1;
            p_say(String(U"拆掉了一座") + name);
        } else {
            p_say(String(U"光标下没有建筑"));
        }
    } else if (p_interact) {
        if (selected >= 0) {
            p_say(String(U"这里已经有") + String::utf8(buildings::by_id(play::to_std(target->get_building_kind(selected)))->name) + String(U"了：X 拆掉再放"));
        } else {
            const String reason = target->place(String(def.id), spot, yaw);
            p_say(reason.is_empty() ? String(U"放了一座") + String::utf8(def.name) : reason);
        }
    }
    if (pressed(input_actions::SAVE)) {
        if (target->save()) {
            p_say(String(U"已存到 ") + target->get_layout_path());
        } else {
            p_say(String(U"没设 layout_path，存不了：在 Settlement 节点上填一个 res:// 路径"));
        }
    }
}

// 每帧把光标、幽灵与面板刷一遍：光标下有建筑就描它（白），没有就按能不能盖画幽灵（绿 / 红）
void BuildMode::refresh() {
    const Vector3 spot = cursor_snapped();
    const Vector3 local = host->host_node()->get_global_transform().affine_inverse().xform(spot);
    String status;
    bool ok = false;
    if (selected >= 0) {
        const BuildingDef *here = buildings::by_id(play::to_std(target->get_building_kind(selected)));
        status = String(U"这里是") + String::utf8(here->name) + String(U" · X 拆掉");
        ok = true;
        overlay->refresh(spot, *here, target->get_building_transform(selected), BuildOverlay::GHOST_SELECTED);
    } else {
        const BuildingDef &def = buildings::at(kind);
        Transform3D ghost;
        const SiteReport report = target->survey_site_global(def, spot, yaw, ghost);
        ok = report.ok();
        status = ok ? String(U"空地：E 放一座") + String::utf8(def.name) : String::utf8(report.reason.c_str());
        overlay->refresh(spot, def, ghost, ok ? BuildOverlay::GHOST_CAN_PLACE : BuildOverlay::GHOST_CANNOT_PLACE);
    }
    panel->refresh(host->host_node()->get_name(), local, get_yaw_deg(), status, ok, kind, target->get_count(), target->get_layout_path(), target->is_dirty());
}
