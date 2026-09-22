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
}
