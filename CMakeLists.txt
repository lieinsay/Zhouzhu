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

# 不含 Godot 头的纯算法文件：扩展与单元测试各编一份。
set(ZHOUZHU_CORE_SOURCES
    src/buildings.cpp
    src/buildings.h
    src/crops.cpp
    src/crops.h
    src/farm_model.cpp
    src/farm_model.h
    src/flight_model.cpp
    src/flight_model.h
    src/inventory.cpp
    src/inventory.h
    src/island_field.cpp
    src/island_field.h
    src/island_river.cpp
    src/island_river.h
    src/rng.h
    src/settlement_model.cpp
    src/settlement_model.h
    src/ship_layout.cpp
    src/ship_layout.h
    src/value_noise.cpp
    src/value_noise.h
    src/villager_model.cpp
    src/villager_model.h
)

add_library(zhouzhu SHARED)

target_sources(
    zhouzhu
    PRIVATE
        ${ZHOUZHU_CORE_SOURCES}
        src/backpack_panel.cpp
        src/backpack_panel.h
        src/build_overlay.cpp
        src/build_overlay.h
        src/build_panel.cpp
        src/build_panel.h
        src/farm.cpp
        src/farm.h
        src/input_actions.cpp
        src/input_actions.h
        src/island.cpp
        src/island.h
        src/island_mesh.cpp
        src/island_mesh.h
        src/player.cpp
        src/player.h
        src/refit_overlay.cpp
        src/refit_overlay.h
        src/refit_panel.cpp
        src/refit_panel.h
        src/register_types.cpp
        src/register_types.h
        src/settlement.cpp
        src/settlement.h
        src/ship.cpp
        src/ship.h
        src/shipyard.cpp
        src/shipyard.h
        src/villagers.cpp
        src/villagers.h
        src/voyage.cpp
        src/voyage.h
)

target_include_directories(zhouzhu PRIVATE src)

# MSVC 默认按系统代码页（这台机器是 GBK）读源文件，源码里的中文字面量会在编译期就被转坏，
# 运行时打出乱码。/utf-8 同时指定源字符集与执行字符集为 UTF-8，与 Godot 的 String 一致。
if(MSVC)
    target_compile_options(zhouzhu PRIVATE /utf-8)
endif()
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

# 单元测试：只编纯算法文件，不链 godot-cpp。
#   cmake --build build --config Debug --target zhouzhu_core_tests
#   ctest --test-dir build -C Debug --output-on-failure
enable_testing()
add_executable(zhouzhu_core_tests ${ZHOUZHU_CORE_SOURCES} tests/core_tests.cpp)
target_include_directories(zhouzhu_core_tests PRIVATE src)
set_target_properties(zhouzhu_core_tests PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(zhouzhu_core_tests PRIVATE /utf-8)
endif()
add_test(NAME core COMMAND zhouzhu_core_tests)
