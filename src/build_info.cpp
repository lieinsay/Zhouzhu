#include "build_info.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void BuildInfo::_bind_methods() {
    ClassDB::bind_method(D_METHOD("describe"), &BuildInfo::describe);
}

String BuildInfo::describe() const {
    // ⚠️ 中文字面量一律用 U"..."。godot-cpp 的 String(const char *) 走的是
    // string_new_with_latin1_chars —— 会把 UTF-8 字节逐个当 Latin-1 字符，
    // 「州渚」会变成「å·æ¸」。U"..." 是编译期 UTF-32，不经过执行字符集。
    // （CMakeLists 里的 /utf-8 管的是另一头：让 MSVC 别把源文件按 GBK 读。两者都要。）
    return String(U"州渚 GDExtension 已加载 · 编译于 ") + __DATE__ + " " + __TIME__;
}

void BuildInfo::_ready() {
    UtilityFunctions::print(describe());
}
