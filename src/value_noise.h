#pragma once

#include <vector>

#include "rng.h"

// 局部（非周期）值噪声，skyisle-gen island/grid.py 的移植。
// 格点随机值 + 平滑双线性插值，可在任意连续坐标（米）上取样——域扭曲需要这一点。

class LatticeNoise {
    double cell = 1.0;
    double x0 = 0.0, y0 = 0.0;
    int nx = 0, ny = 0;
    std::vector<double> lattice;

public:
    LatticeNoise(Rng &p_rng, double p_x0, double p_y0, double p_x1, double p_y1, double p_cell);

    double sample(double p_x, double p_y) const;
};

// 多倍频值噪声。特征尺度以米给出（与栅格分辨率无关），归一到约 [-1, 1]。
class FractalNoise {
    struct Layer {
        double amp;
        LatticeNoise noise;
    };
    std::vector<Layer> layers;
    double total = 1.0;

public:
    FractalNoise(Rng &p_rng, double p_x0, double p_y0, double p_x1, double p_y1, double p_feature,
            int p_octaves = 4, double p_persistence = 0.5, double p_lacunarity = 2.0);

    double sample(double p_x, double p_y) const;
};
