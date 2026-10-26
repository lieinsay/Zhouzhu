# 州渚 —— GDExtension（Godot 4.4 + C++）
#
#   cmake -B build                    # 配置（默认 template_debug）
#   cmake --build build --config Debug
#
# 产物落在 game/bin/，名字带 godot-cpp 的平台后缀，
# 与 game/bin/zhouzhu.gdextension 里登记的路径一一对应。
# Rider / CLion / VS / VS Code 都能直接打开本文件当项目。

cmake_minimum_required(VERSION 3.22)

project(zhouzhu LANGUAGES C CXX)

add_subdirectory(extern/godot-cpp)

# zlib 1.3.1（子模块 extern/zlib，与 Python 3.12 带的同一版）：只编压缩要的几个文件。
# 进程内生成要写的逐日天气（weather.bin、lods/<节点>.wx）是 zlib.compress(…, 9) 压的，要与 Python 那条路逐字节相同就得用同一份 deflate；
# 读仍是自己写的解码（src/inflate.*）。
add_library(zhouzhu_zlib STATIC
    extern/zlib/adler32.c
    extern/zlib/compress.c
    extern/zlib/crc32.c
    extern/zlib/deflate.c
    extern/zlib/trees.c
    extern/zlib/zutil.c
)
target_include_directories(zhouzhu_zlib PUBLIC extern/zlib)
if(MSVC)
    target_compile_definitions(zhouzhu_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(zhouzhu_zlib PRIVATE /W1)
endif()
set_target_properties(zhouzhu_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 生成器的提交号（写进行星包的 source、岛群包的 stamp.json）：配置时读子模块的 HEAD（子模块换了提交会重新配置）
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/extern/skyisle-gen" rev-parse --short HEAD
    OUTPUT_VARIABLE ZHOUZHU_GENERATOR_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT ZHOUZHU_GENERATOR_COMMIT)
    set(ZHOUZHU_GENERATOR_COMMIT "?")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/modules/extern/skyisle-gen/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.git/modules/extern/skyisle-gen/HEAD")
endif()
# HEAD 在分支快进时内容不变；同时监视实际分支引用，避免产物沿用旧提交戳。
execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/extern/skyisle-gen" symbolic-ref -q HEAD
    OUTPUT_VARIABLE ZHOUZHU_GENERATOR_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(ZHOUZHU_GENERATOR_REF)
    execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/extern/skyisle-gen" rev-parse --git-path "${ZHOUZHU_GENERATOR_REF}"
        OUTPUT_VARIABLE ZHOUZHU_GENERATOR_REF_PATH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    get_filename_component(ZHOUZHU_GENERATOR_REF_PATH "${ZHOUZHU_GENERATOR_REF_PATH}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/skyisle-gen")
    if(EXISTS "${ZHOUZHU_GENERATOR_REF_PATH}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ZHOUZHU_GENERATOR_REF_PATH}")
    endif()
endif()

# 生成器的 C++ 核心库（行星计划 P6e，D16）：skyisle-gen 作子模块（extern/skyisle-gen），只编它的 core/——静态库 skyisle_core，
# 不要 Python 扩展、不要它的自检。浮点照它自己的 CMakeLists（MSVC 默认 /fp:precise、不开 /arch:AVX2：乘加不合并成 FMA，与 numpy 逐位对得上），
# 这边不给它加任何会改浮点结果的选项。
# 进程内生成的转换与编排（静态库 zhouzhu_gen，cmake/zhouzhu_gen：生成器的结果 → 游戏文件，与 tools/export_skyisle.py、export_planet.py 逐字节相同）
# 同它放在一块：Debug 配置下这两样也开 /O2（/fp:precise 不变，Debug 与 Release 写出的文件逐字节相同，验过）——
# /Od 下生成一群要慢几倍，e2e（跑的是 Debug 的扩展）等不起。运行库照全工程的 CMAKE_MSVC_RUNTIME_LIBRARY（/MD），与别的目标一致。
set(SKYISLE_PYTHON OFF CACHE BOOL "skyisle-gen：不编 Python 扩展" FORCE)
set(SKYISLE_TESTS OFF CACHE BOOL "skyisle-gen：不编自检" FORCE)
set(ZHOUZHU_SAVED_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "/Zi /O2 /Ob2")
endif()
add_subdirectory(extern/skyisle-gen/core EXCLUDE_FROM_ALL)
add_subdirectory(cmake/zhouzhu_gen)
set(CMAKE_CXX_FLAGS_DEBUG "${ZHOUZHU_SAVED_CXX_FLAGS_DEBUG}")
# 核心库的编译选项（/permissive- /W3 …）是 PUBLIC 的：只留给它自己，别传到游戏的代码上
set_target_properties(skyisle_core PROPERTIES INTERFACE_COMPILE_OPTIONS "")

# 不含 Godot 头的纯算法文件：扩展与单元测试各编一份。
set(ZHOUZHU_CORE_SOURCES
    src/almanac.cpp
    src/almanac.h
    src/build_pieces.cpp
    src/build_pieces.h
    src/building_mesh.cpp
    src/building_mesh.h
    src/building_parts.cpp
    src/building_parts.h
    src/buildings.cpp
    src/buildings.h
    src/cloud_band.cpp
    src/cloud_band.h
    src/cover_blend.cpp
    src/cover_blend.h
    src/crops.cpp
    src/crops.h
    src/day_clock.cpp
    src/day_clock.h
    src/deposit_kinds.cpp
    src/deposit_kinds.h
    src/dev_manual.cpp
    src/dev_manual.h
    src/dock_site.cpp
    src/dock_site.h
    src/earthworks.cpp
    src/earthworks.h
    src/far_group_mesh.cpp
    src/far_group_mesh.h
    src/farm_model.cpp
    src/farm_model.h
    src/field_book.cpp
    src/field_book.h
    src/flat_ground.cpp
    src/flat_ground.h
    src/freight.cpp
    src/freight.h
    src/god_works.cpp
    src/god_works.h
    src/group_memory.cpp
    src/group_memory.h
    src/ground_works.cpp
    src/ground_works.h
    src/flight_model.cpp
    src/flight_model.h
    src/ground_query.h
    src/house_template.cpp
    src/house_template.h
    src/inflate.cpp
    src/inflate.h
    src/interior_rules.cpp
    src/interior_rules.h
    src/inventory.cpp
    src/inventory.h
    src/item_slots.cpp
    src/item_slots.h
    src/island_field.cpp
    src/island_field.h
    src/island_ground.cpp
    src/island_ground.h
    src/island_river.cpp
    src/island_river.h
    src/job_pool.cpp
    src/job_pool.h
    src/land_berth.cpp
    src/land_berth.h
    src/local_season.cpp
    src/local_season.h
    src/map_ground.cpp
    src/map_ground.h
    src/map_image.cpp
    src/map_image.h
    src/mesh_kit.h
    src/piece_mesh.cpp
    src/piece_mesh.h
    src/placeables.cpp
    src/placeables.h
    src/planet_data.cpp
    src/planet_data.h
    src/planet_frame.cpp
    src/planet_frame.h
    src/planet_sky.cpp
    src/planet_sky.h
    src/planet_surface.cpp
    src/planet_surface.h
    src/planet_weather.cpp
    src/planet_weather.h
    src/recipes.cpp
    src/recipes.h
    src/refit_pick.cpp
    src/refit_pick.h
    src/resource_scatter.cpp
    src/resource_scatter.h
    src/resource_sites.cpp
    src/resource_sites.h
    src/rng.h
    src/river_mesh.cpp
    src/river_mesh.h
    src/sea_state.cpp
    src/sea_state.h
    src/settlement_model.cpp
    src/settlement_model.h
    src/ship_cabins.cpp
    src/ship_cabins.h
    src/ship_layout.cpp
    src/ship_layout.h
    src/stream_meter.cpp
    src/stream_meter.h
    src/story_start.h
    src/structure_styles.cpp
    src/structure_styles.h
    src/template_set.cpp
    src/template_set.h
    src/terrain_edits.cpp
    src/terrain_edits.h
    src/terrain_tile.cpp
    src/terrain_tile.h
    src/value_noise.cpp
    src/value_noise.h
    src/village_plan.cpp
    src/village_plan.h
    src/village_prep.cpp
    src/village_prep.h
    src/village_sim.cpp
    src/village_sim.h
    src/village_state.cpp
    src/village_state.h
    src/villager_model.cpp
    src/villager_model.h
    src/weather.cpp
    src/weather.h
    src/weather_noise.cpp
    src/weather_noise.h
    src/wild_landing.cpp
    src/wild_landing.h
    src/world_grid.cpp
    src/world_grid.h
    src/world_ledger.cpp
    src/world_ledger.h
    src/world_river.cpp
    src/world_river.h
    src/world_ground.cpp
    src/world_ground.h
    src/world_sites.cpp
    src/world_sites.h
    src/world_terrain.cpp
    src/world_terrain.h
    src/yard_site.cpp
    src/yard_site.h
)

add_library(zhouzhu SHARED)

target_sources(
    zhouzhu
    PRIVATE
        ${ZHOUZHU_CORE_SOURCES}
        src/atmosphere.cpp
        src/atmosphere.h
        src/build_mode.cpp
        src/build_mode.h
        src/build_overlay.cpp
        src/build_overlay.h
        src/build_panel.cpp
        src/build_panel.h
        src/curved_material.cpp
        src/curved_material.h
        src/far_groups.cpp
        src/far_groups.h
        src/planet_view.cpp
        src/planet_view.h
        src/planet_globe.cpp
        src/planet_globe.h
        src/sea_surface.cpp
        src/sea_surface.h
        src/planet_host.cpp
        src/planet_host.h
        src/planet_jobs.cpp
        src/planet_jobs.h
        src/orbit_camera.cpp
        src/orbit_camera.h
        src/pause_menu.cpp
        src/pause_menu.h
        src/dev_tools.cpp
        src/dev_works_panel.cpp
        src/dev_works_panel.h
        src/dig_hand.cpp
        src/dig_hand.h
        src/dev_tools.h
        src/dock_builder.cpp
        src/dock_builder.h
        src/expedition.cpp
        src/expedition.h
        src/farm.cpp
        src/farm.h
        src/farm_hand.cpp
        src/farm_hand.h
        src/freight_hand.cpp
        src/freight_hand.h
        src/gather_hand.cpp
        src/gather_hand.h
        src/gen_job.cpp
        src/gen_job.h
        src/gen_process.cpp
        src/gen_process.h
        src/group_provider.cpp
        src/group_provider.h
        src/group_stream.cpp
        src/planet_map.cpp
        src/planet_map.h
        src/world_session.cpp
        src/world_session.h
        src/story_start_api.cpp
        src/story_start_api.h
        src/group_stream.h
        src/flat_map.cpp
        src/flat_map.h
        src/ground_host.h
        src/helm.cpp
        src/helm.h
        src/hinterland.cpp
        src/hinterland.h
        src/hotbar_bar.cpp
        src/hotbar_bar.h
        src/input_actions.cpp
        src/input_actions.h
        src/interior_edit.cpp
        src/interior_edit.h
        src/interior_overlay.cpp
        src/interior_overlay.h
        src/interior_panel.cpp
        src/interior_panel.h
        src/inventory_panel.cpp
        src/item_art.cpp
        src/item_art.h
        src/art_surface.cpp
        src/art_surface.h
        src/inventory_panel.h
        src/island.cpp
        src/island.h
        src/island_map.cpp
        src/island_map.h
        src/island_mesh.cpp
        src/island_mesh.h
        src/aim.h
        src/map_host.cpp
        src/map_host.h
        src/map_browser.cpp
        src/map_browser.h
        src/pack_hand.cpp
        src/pack_hand.h
        src/piece_hand.cpp
        src/piece_hand.h
        src/place_hand.cpp
        src/place_hand.h
        src/placed_props.cpp
        src/placed_props.h
        src/play_notice.h
        src/player.cpp
        src/player.h
        src/refit_bench.cpp
        src/refit_bench.h
        src/refit_overlay.cpp
        src/refit_overlay.h
        src/refit_panel.cpp
        src/refit_panel.h
        src/register_types.cpp
        src/register_types.h
        src/scatter_meshes.cpp
        src/scatter_meshes.h
        src/scatter_stream.cpp
        src/scatter_stream.h
        src/settlement.cpp
        src/settlement.h
        src/ship.cpp
        src/ship.h
        src/shared_looks.cpp
        src/shared_looks.h
        src/shipyard.cpp
        src/shipyard.h
        src/site_stream.cpp
        src/site_stream.h
        src/style_panel.cpp
        src/style_panel.h
        src/template_library.cpp
        src/template_library.h
        src/villagers.cpp
        src/villagers.h
        src/weather_view.cpp
        src/weather_view.h
        src/works_view.cpp
        src/works_view.h
        src/world.cpp
        src/world.h
        src/world_clock.cpp
        src/world_clock.h
        src/world_map.cpp
        src/world_map.h
        src/world_resource.cpp
        src/world_resource.h
        src/world_village.cpp
        src/world_village.h
)

target_include_directories(zhouzhu PRIVATE src)

# MSVC 默认按系统代码页（这台机器是 GBK）读源文件，源码里的中文字面量会在编译期就被转坏，
# 运行时打出乱码。/utf-8 同时指定源字符集与执行字符集为 UTF-8，与 Godot 的 String 一致。
if(MSVC)
    target_compile_options(zhouzhu PRIVATE /utf-8)
endif()
target_link_libraries(zhouzhu PRIVATE godot-cpp zhouzhu_gen)

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
# 有的测试读仓库里的真资产（game/planet/seed42/…：开局那群与它的邻群的细版，见 src/story_start.h）
target_compile_definitions(zhouzhu_core_tests PRIVATE ZHOUZHU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(zhouzhu_core_tests PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
target_link_libraries(zhouzhu_core_tests PRIVATE zhouzhu_gen)
if(MSVC)
    target_compile_options(zhouzhu_core_tests PRIVATE /utf-8)
endif()
add_test(NAME core COMMAND zhouzhu_core_tests)

# 进程内生成的命令行版（行星计划 P6e）：与游戏同一份代码，不要 Godot、不要 Python——逐字节对照、量速度、离线补包（见 tools/zhouzhu_gen.cpp）
add_executable(zhouzhu_gen_cli tools/zhouzhu_gen.cpp)
target_link_libraries(zhouzhu_gen_cli PRIVATE zhouzhu_gen)
set_target_properties(zhouzhu_gen_cli PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF OUTPUT_NAME zhouzhu_gen)
if(MSVC)
    target_compile_options(zhouzhu_gen_cli PRIVATE /utf-8)
endif()

# 生成器核心库自己的自检（行星计划 P6e：随机流对 numpy 的参考值、栅格公共件的小例子；源码在子模块里）
add_executable(skyisle_core_selftest extern/skyisle-gen/core/tests/selftest.cpp)
target_link_libraries(skyisle_core_selftest PRIVATE skyisle_core)
set_target_properties(skyisle_core_selftest PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(skyisle_core_selftest PRIVATE /utf-8)
endif()
add_test(NAME skyisle_core_selftest COMMAND skyisle_core_selftest)

# 岛群体检：把开局那群（src/story_start.h）的全部码头与村落离线铺一遍（见 tools/site_survey.cpp）
add_executable(zhouzhu_site_survey ${ZHOUZHU_CORE_SOURCES} tools/site_survey.cpp)
target_include_directories(zhouzhu_site_survey PRIVATE src)
set_target_properties(zhouzhu_site_survey PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(zhouzhu_site_survey PRIVATE /utf-8)
endif()
target_compile_definitions(zhouzhu_site_survey PRIVATE ZHOUZHU_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME site_survey_start COMMAND zhouzhu_site_survey --start)
# 行星包体检：摘要、邻群表、坐标自检、每群粗版对陆地（邻群叠没叠要 --overlap，手动跑）
add_test(NAME site_survey_planet COMMAND zhouzhu_site_survey --planet ${CMAKE_CURRENT_SOURCE_DIR}/game/planet/seed42)
# 行星天气场（行星计划 P5）：粗细两版天气对照、一年里全行星的云量与降水与 ④ 的气候场对账（5° 的格、一天两个时刻；细一点的用 --grid 2 --steps 4）
add_test(NAME site_survey_planet_weather COMMAND zhouzhu_site_survey --planet-weather ${CMAKE_CURRENT_SOURCE_DIR}/game/planet/seed42)
