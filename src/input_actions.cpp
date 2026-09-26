#include "input_actions.h"

#include <initializer_list>

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

namespace {

void bind(const char *p_action, std::initializer_list<Key> p_keys) {
    InputMap *map = InputMap::get_singleton();
    const StringName action(p_action);
    if (map->has_action(action)) {
        return;
    }
    map->add_action(action);
    for (Key key : p_keys) {
        Ref<InputEventKey> event;
        event.instantiate();
        event->set_physical_keycode(key); // 按键位而不是字符：换键盘布局不受影响
        map->action_add_event(action, event);
    }
}

} // namespace

void input_actions::register_all() {
    bind(MOVE_FORWARD, { KEY_W, KEY_UP });
    bind(MOVE_BACK, { KEY_S, KEY_DOWN });
    bind(MOVE_LEFT, { KEY_A, KEY_LEFT });
    bind(MOVE_RIGHT, { KEY_D, KEY_RIGHT });
    bind(JUMP, { KEY_SPACE });
    bind(SPRINT, { KEY_SHIFT });
    bind(ASCEND, { KEY_SPACE });
    bind(DESCEND, { KEY_SHIFT, KEY_CTRL });
    bind(INTERACT, { KEY_E });
    bind(INVENTORY, { KEY_TAB, KEY_I });
    bind(REMOVE, { KEY_X, KEY_BACKSPACE, KEY_DELETE });
    bind(KIND_1, { KEY_1, KEY_KP_1 });
    bind(KIND_2, { KEY_2, KEY_KP_2 });
    bind(KIND_3, { KEY_3, KEY_KP_3 });
    bind(KIND_4, { KEY_4, KEY_KP_4 });
    bind(KIND_5, { KEY_5, KEY_KP_5 });
    bind(BUILD_MODE, { KEY_B, KEY_F1 });
    bind(ROTATE_LEFT, { KEY_Q });
    bind(ROTATE_RIGHT, { KEY_R });
    bind(SAVE, { KEY_F5 });
    bind(MAP, { KEY_M });
    bind(SWITCH_MAP, { KEY_F2 });
    bind(GOD_MODE, { KEY_F3 });
    bind(TIME_LAPSE, { KEY_F4 });
    bind(WEATHER_PANEL, { KEY_F6 });
    bind(FLY_DOWN, { KEY_CTRL, KEY_C });
    bind(NEXT_STYLE, { KEY_V });
    bind(PREV_STYLE, { KEY_Z });
    bind(EDIT_INTERIOR, { KEY_T });
    bind(SAVE_AS, { KEY_N });
    bind(GEAR_UP, { KEY_R, KEY_PAGEUP });
    bind(GEAR_DOWN, { KEY_F, KEY_PAGEDOWN });
    const Key digits[9] = { KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
    const Key pads[9] = { KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9 };
    for (int k = 0; k < 9; k++) {
        bind(HOTBAR[k], { digits[k], pads[k] });
    }
    bind(SPLIT, { KEY_Q });
    bind(QUICK_MOVE, { KEY_F });
}
