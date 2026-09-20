#include "build_info.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void BuildInfo::_bind_methods() {
    ClassDB::bind_method(D_METHOD("describe"), &BuildInfo::describe);
}

String BuildInfo::describe() const {
    return String("州渚 GDExtension 已加载 · 编译于 ") + __DATE__ + " " + __TIME__;
}

void BuildInfo::_ready() {
    UtilityFunctions::print(describe());
}
