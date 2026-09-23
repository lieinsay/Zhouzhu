#pragma once

#include <functional>
#include <string>

#include <godot_cpp/variant/string.hpp>

// 玩法部件（PackHand、FarmHand、RefitBench、BuildMode）给玩家的一句提示：怎么显示、显示多久由编排定。
using Say = std::function<void(const godot::String &)>;

namespace play {

inline std::string to_std(const godot::String &p_string) {
    return std::string(p_string.utf8().get_data());
}

} // namespace play
