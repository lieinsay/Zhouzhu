# 进程内的生成（行星计划 P6e）：生成器核心库的结果 → 游戏文件（与 tools/export_skyisle.py、export_planet.py 逐字节相同）、
# 新建世界与按需生成的编排。不含 Godot 头：扩展、单元测试、命令行工具 zhouzhu_gen 共用这一个静态库。
# 单放一个目录只为编译选项：顶层在这之前把 Debug 的选项换成 /O2（与生成器的核心库一样，见顶层 CMakeLists.txt）。
set(ZHOUZHU_SRC "${PROJECT_SOURCE_DIR}/src")
add_library(zhouzhu_gen STATIC
    ${ZHOUZHU_SRC}/gen_config.cpp
    ${ZHOUZHU_SRC}/gen_config.h
    ${ZHOUZHU_SRC}/gen_core.cpp
    ${ZHOUZHU_SRC}/gen_core.h
    ${ZHOUZHU_SRC}/gen_export.cpp
    ${ZHOUZHU_SRC}/gen_export.h
    ${ZHOUZHU_SRC}/py_format.cpp
    ${ZHOUZHU_SRC}/py_format.h
    ${ZHOUZHU_SRC}/scipy_edt.cpp
    ${ZHOUZHU_SRC}/scipy_edt.h
)
target_include_directories(zhouzhu_gen PUBLIC ${ZHOUZHU_SRC})
target_link_libraries(zhouzhu_gen PUBLIC skyisle_core zhouzhu_zlib)
target_compile_definitions(zhouzhu_gen PRIVATE ZHOUZHU_GENERATOR_COMMIT="${ZHOUZHU_GENERATOR_COMMIT}")
set_target_properties(zhouzhu_gen PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(zhouzhu_gen PRIVATE /utf-8)
endif()
