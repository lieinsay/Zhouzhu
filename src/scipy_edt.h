#pragma once

#include <cstdint>
#include <vector>

// scipy.ndimage 的几样（行星计划 P6e：tools/export_skyisle.py 用它们导出岛群包，进程内的 C++ 要写出逐字节相同的文件）。不含 Godot 头。
namespace scipy_nd {

// distance_transform_edt 的特征变换（2 维，sampling = None）：scipy 的 NI_EuclideanFeatureTransform（Maurer 的线性时间算法，
// 先逐列、再逐行做 Voronoi），并列时挑哪个特征与 scipy 逐位相同。p_input 非 0 的格不是特征（距离从它们量到最近的 0 格）；
// r_row / r_col：每格最近的特征格的行列（没有任何特征时为 −1）
void feature_transform(const uint8_t *p_input, int p_h, int p_w, std::vector<int32_t> &r_row, std::vector<int32_t> &r_col);

// distance_transform_edt(input)：每格到最近的 0 格的欧氏距离（格数；0 格为 0）= sqrt(dr² + dc²)
std::vector<double> distance_edt(const uint8_t *p_input, int p_h, int p_w);

// binary_erosion(mask, structure = 3 × 3 全 1, border_value = 0)：出界当 0
std::vector<uint8_t> binary_erosion_3x3(const uint8_t *p_mask, int p_h, int p_w);

} // namespace scipy_nd
