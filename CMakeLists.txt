# 州渚 —— GDExtension（Godot 4.4 + C++）
#
#   cmake -B build                    # 配置（默认 template_debug）
#   cmake --build build --config Debug
#
# 产物落在 game/bin/，名字带 godot-cpp 的平台后缀，
# 与 game/bin/zhouzhu.gdextension 里登记的路径一一对应。
# Rider / CLion / VS / VS Code 都能直接打开本文件当项目。

cmake_minimum_required(VERSION 3.22)

project(zhouzhu LANGUAGES CXX)

add_subdirectory(extern/godot-cpp)

add_library(zhouzhu SHARED)

target_sources(
    zhouzhu
    PRIVATE
        src/build_info.cpp
        src/build_info.h
        src/register_types.cpp
        src/register_types.h
)

target_include_directories(zhouzhu PRIVATE src)
target_link_libraries(zhouzhu PRIVATE godot-cpp)

# godot-cpp 算好的平台后缀，例如 .windows.template_debug.x86_64
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

if(NOT DEFINED GODOTCPP_SYMBOL_VISIBILITY)
    set(GODOTCPP_SYMBOL_VISIBILITY "default")
endif()

set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/game/bin")

set_target_properties(
    zhouzhu
    PROPERTIES
        CXX_STANDARD 17
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET ${GODOTCPP_SYMBOL_VISIBILITY}
        POSITION_INDEPENDENT_CODE ON
        BUILD_RPATH_USE_ORIGIN ON
        # 用生成器表达式包住，免得 MSVC 生成器再套一层 Debug/ Release/ 子目录
        LIBRARY_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"
        PDB_OUTPUT_DIRECTORY "$<1:${OUTPUT_DIR}>"
        PREFIX ""
        OUTPUT_NAME "zhouzhu${GODOTCPP_SUFFIX}"
)
