#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "skyisle/island/generate.hpp"
#include "skyisle/island/types.hpp"
#include "skyisle/planet/civ.hpp"
#include "skyisle/planet/planet.hpp"

// 从生成器的产物到游戏文件的转换（行星计划 P6e）：tools/export_skyisle.py 与 tools/export_planet.py 的纯 C++ 版，
// 连同生成器 Python 前端在中间做的那一步（island/engine.py 拼 island.json、decode.py 把代码译回中文、写 npz / json / csv 时的舍入与类型），
// 直接从 C++ 核心库的结果（island::Group、planet::World / Society）写出与 Python 那条路**逐字节相同**的文件。
// 格式见两个脚本开头的说明。不含 Godot 头；只做内存里的转换，写盘在 gen_core。
namespace gen_export {

// 一群的包要的、Group 里没有的
struct GroupMeta {
    int64_t node = 0;
    uint64_t seed = 0;
    std::string run; // 生成器 run 目录名（seed<N>）：world.json 的 source.run
    int cls = 1; // ③ 的类：0 密接、1 中疏、2 稀疏、3 孤悬
    double tilt_deg = 34.0; // ① 的黄赤交角
    double lapse_c_per_km = 6.0; // [s04.climate] lapse_c_per_km
    double snow_temp_c = 0.5; // [island.weather] snow_temp_c
    double fog_surface_max_m = 800.0; // [island.weather] fog_surface_max_m
};

// 岛群包的五份（export_skyisle.export）
struct GroupFiles {
    std::string terrain_bin, world_json, sites_tsv, resources_tsv, weather_tsv;
};
GroupFiles group_files(const skyisle::island::Group &p_group, const GroupMeta &p_meta);

// ---------------------------------------------------------------- 粗版（lods.bin 的一条：export_planet.encode_lod / write_lods）
struct LodEntry {
    int64_t node = 0;
    double res_m = 0.0; // 原生格距 × 块边长
    double origin_x = 0.0, origin_y = 0.0; // island.json 的 origin_km（km，x 东 y 北）
    int rows = 0, cols = 0, n_islands = 0;
    std::string blob; // 岛表 + 格子的行程编码
};
// p_group：水系之后（岛表按 island.json 的值）；p_block：它的块降采样
LodEntry lod_entry(int64_t p_node, const skyisle::island::Group &p_group, const skyisle::island::LodBlock &p_block);
// 粗版的块边长：max(1, round(2000 / 原生格距))（lod.build_lod）
int lod_factor(double p_native_res_m, double p_lod_res_m = 2000.0);
std::string lods_bin(const std::vector<LodEntry> &p_entries);

// ---------------------------------------------------------------- 逐日天气（weather.bin 的一条：export_planet.write_weather，数据同粗版 npz 的 weather_<列>）
struct WeatherEntry {
    int64_t node = 0;
    std::vector<std::string> season_names; // 中文季名
    int seasons = 4, months_per_season = 3, year = 0;
    double days_per_month = 28.0, day_offset_solstice_n = 126.0;
    double ref_m = 0, lapse = 0, rim_m = 0, fog_rise_m = 0, snow_temp_c = 0, precip_mm = 0, precip_climate_mm = 0;
    std::vector<int> type, sailable, storm_event, wind_from_deg;
    std::vector<double> precip_day, temp_c, wind_ms; // 已按 csv 舍到 0.1
};
WeatherEntry weather_entry(int64_t p_node, const skyisle::island::Group &p_group, const GroupMeta &p_meta);
// p_precip_formula：[island.climate] precip_mm_min / max / exp；p_source 空 = 不写 source 行
std::string weather_bin(const std::vector<WeatherEntry> &p_entries, const std::array<double, 3> &p_precip_formula, const std::string &p_source);

// ---------------------------------------------------------------- 行星包 planet.bin（export_planet.export_planet；新建世界时没有粗版：lod.* 列是 NaN / 0）
std::string planet_bin(const skyisle::planet::World &p_world, const skyisle::planet::Society &p_society, uint64_t p_seed, const std::string &p_source);

// ---------------------------------------------------------------- 小件（单测用）
// 按生成器的汇流面积反推 D8 流向（export_skyisle.flow_directions_from_acc）：返回流向码
std::vector<uint8_t> flow_directions_from_acc(const std::vector<double> &p_height, const std::vector<int32_t> &p_island, const std::vector<double> &p_acc,
        int p_h, int p_w, double p_res_m);
// 赋存区的长度与走向（resources._shape：np.cov + np.linalg.eigh）；r_has_axis = false 时 Python 写 None
void occurrence_shape(const std::vector<int32_t> &p_cells, int p_w, double p_res_km, double &r_length, double &r_axis, bool &r_has_axis);

} // namespace gen_export
