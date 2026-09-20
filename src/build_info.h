#pragma once

#include <godot_cpp/classes/node.hpp>

// 脚手架：只用来证明 C++ 这一侧确实被 Godot 加载了。
// 第一个切片落地后可以删掉。
class BuildInfo : public godot::Node {
    GDCLASS(BuildInfo, godot::Node)

protected:
    static void _bind_methods();

public:
    void _ready() override;

    godot::String describe() const;
};
