#pragma once

#include <cstdint>

// 云团噪声：天上的云与地上的雨用同一张场，所以抬头看见的乌云底下正在下雨。不含 Godot 头。
// 整数哈希的值噪声（五次平滑）叠四层 fbm，输入单位是「格」（云团的特征尺度 = 一格）。整数格按 WX_PERIOD 循环，
// 所以漂了多远都能对周期取模，着色器里的 float 不失精度。
// game/shaders/weather.gdshaderinc 是同一个式子（哈希常数、平滑、各层偏移、分位表），改一处要改两处。

constexpr int WX_PERIOD = 1024;

uint32_t wx_hash(int p_x, int p_y);
// 格点上的随机数 [0, 1]
double wx_rand(int p_x, int p_y);
// 值噪声 [0, 1]
double wx_value(double p_x, double p_y);
// 四层 fbm，[0, 1]（大多落在 0.2–0.8）
double wx_fbm(double p_x, double p_y);
// 覆盖率 → 门槛：fbm ≥ 门槛 的面积约占 p_coverage（0 → 1.01，谁都不在里面；1 → −0.01，处处都在）
double wx_threshold(double p_coverage);
// 分位表本身（WX_QUANTILES 个，覆盖率 0、1/16、…、1 的门槛），单测拿它对照重新取样
constexpr int WX_QUANTILES = 17;
const double *wx_quantile_table();
