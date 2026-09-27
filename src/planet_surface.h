#pragma once

#include <cstdint>
#include <vector>

#include "planet_data.h"

// 行星计划 P2：行星贴图（从粗版烘的六面立方体贴图，docs/PLAN-PLANET.md P2 第 2 条）。不含 Godot 头。
//
// 立方体的六个面（与着色器 game/shaders/planet_sky.gdshaderinc 的 planet_cube_uv 逐式相同，改一处要改两处）：
//   面 k 的法向 n、右 e_u、上 e_v（行星系，同 sphere.py 的轴）：
//     0 +X：n (1,0,0)  e_u (0,1,0)   e_v (0,0,1)      1 −X：n (−1,0,0) e_u (0,−1,0) e_v (0,0,1)
//     2 +Y：n (0,1,0)  e_u (−1,0,0)  e_v (0,0,1)      3 −Y：n (0,−1,0) e_u (1,0,0)  e_v (0,0,1)
//     4 +Z：n (0,0,1)  e_u (0,1,0)   e_v (−1,0,0)     5 −Z：n (0,0,−1) e_u (0,1,0)  e_v (1,0,0)
//   方向 d 落在 |分量| 最大的那一面：a = d·e_u / d·n、b = d·e_v / d·n ∈ [−1, 1]，再按 atan 拉匀（texel 的面积各处差不到一成半）：
//   纹理坐标 U = (atan(a)·4/π + 1) / 2、V = (atan(b)·4/π + 1) / 2；第 j 行 = V 从小到大（图的第 0 行 V 最小）。
//
// 每面两张 RGBA8（面宽 size，默认 2048，赤道上约 5 km 一像素）：
//   color：R G B = 陆地的地表色（sRGB，与岛群地皮同一份调色，按陆地面积平均），A = 陆地占这一像素的份额；
//   extra：R = 陆地的平均高（/ 8000 m）、G = 夜里的灯（⑨ 的人口按地表类撒到陆地上，密度开方）、B = 林的份额、A = 会枯黄的份额（草、田、灌丛）。
// 季相（绿、枯黄、积雪）不烘进去，着色器按纬度、一年里的第几天、这一点的高现算（与远处岛群的简化版海拔表一个意思）。
//
// 没有粗版的群画替身（4.8）：按行星包的数（群心、陆地、主岛、台面）撒一团程序化的岛（主岛一块 + 几块小岛），只给远处和轨道上看；
// seed42 每群都有粗版，替身只留着口子（生存模式 P4 / P6e 用）。

namespace planet_cube {
// 方向 → 面与纹理坐标 (u, v) ∈ [0, 1]
int face_of(const double p_dir[3], double &r_u, double &r_v);
// 面与纹理坐标 → 单位方向
void dir_of(int p_face, double p_u, double p_v, double r_dir[3]);
// 面宽 p_size 时，第 (i, j) 像素所占的立体角
double texel_solid_angle(int p_size, int p_i, int p_j);
} // namespace planet_cube

struct SurfaceFace {
    std::vector<uint8_t> color; // size² × 4
    std::vector<uint8_t> extra; // size² × 4
};

struct SurfaceBakeStats {
    int groups = 0; // 这一面烘进了几群
    int stand_ins = 0; // 其中几群是替身
    int land_texels = 0;
    double land_km2 = 0.0; // 按像素的陆地份额 × 像素面积加起来
    double lit_texels = 0; // 有灯的像素
    double ms = 0.0;
};

struct SurfaceBakeRules {
    int size = 2048;
    double lod_res_m = 2000.0; // 用哪一档粗版（挑不超过它里最粗的）
    double height_scale_m = 8000.0;
    double light_full_per_km2 = 400.0; // 人口密度到这里灯算满（开方）
    bool force_stand_ins = false; // 测试用：一律画替身
};

// 群 p_node 碰到哪几面（位掩码）
uint32_t surface_group_faces(const PlanetData &p_data, int p_node, const SurfaceBakeRules &p_rules = SurfaceBakeRules());
// 烘一面（面与面之间互不相干：可以六条线程各烘一面）
void bake_surface_face(const PlanetData &p_data, int p_face, const SurfaceBakeRules &p_rules, SurfaceFace &r_face, SurfaceBakeStats *r_stats = nullptr);
// 替身：没有粗版的群按行星包的数撒的一团岛（格距 p_res_m 的栅格，同 PlanetLod 的口径）
PlanetLod surface_stand_in(const PlanetData &p_data, int p_node, double p_res_m);

// BC3（DXT5）压缩：行星贴图六面两张 2048² 不压要 270 MB 显存，压了四分之一。Godot 运行时的 Image::compress 走 GPU 压缩器（Betsy），
// 要另开一个渲染设备、常驻几百兆，所以自己压：每 4×4 一块，颜色按包围盒的对角线取两端（565）、四级；alpha 取最大最小、八级。
// 一块 16 字节：alpha 8 字节（两端 + 16 × 3 位），颜色 8 字节（两端 565 + 16 × 2 位），块内按行、低位在前（同 Godot / DirectX）。
void bc3_encode_block(const uint8_t p_rgba[64], uint8_t r_block[16]);
void bc3_decode_block(const uint8_t p_block[16], uint8_t r_rgba[64]);
// 整张图（p_width × p_height 的 RGBA8，边上不足 4 的按最后一行一列补）→ 块，按块的行主序写进 r_out（ceil(w/4)·ceil(h/4)·16 字节）
void bc3_encode_image(const uint8_t *p_rgba, int p_width, int p_height, uint8_t *r_out);
