"""量第三层岛群生成器在不同分辨率下的耗时（不写产物）：只跑布局 + 地形（第 1 步），粗的再加水系。

行星计划（docs/PLAN-PLANET.md）定稿时的探针。在 skyisle-gen 仓库根下跑：
    PYTHONPATH=. PYTHONUTF8=1 python <本文件>
2026-09-26 的结果：2051 地形 100 m 3.07 s / 500 m 0.38 s / 1 km 0.12 s / 2 km 0.06 s；1303 为 6.15 / 0.52 / 0.19 / 0.09 s。"""
import time

import numpy as np

from skyisle_gen.cli import _ctx_from_run
from skyisle_gen import island as isl

ctx = _ctx_from_run("out/seed42")
c = isl.island_config(ctx)
quiet = lambda *a, **k: None

for node in (2051, 1303):
    inp = isl._node_inputs(ctx, node)
    for res in (100.0, 500.0, 1000.0, 2000.0):
        t0 = time.perf_counter()
        g = isl.build_terrain(ctx, node, c, inp, res_m=res, log=quiet)
        t1 = time.perf_counter()
        land = g["island_id"] >= 0
        h = g["height"]
        print("node %d res %5.0f m: 地形 %.2f s，栅格 %dx%d，陆地 %.0f km2，主岛峰 %.0f m，岛数 %d" % (
            node, res, t1 - t0, h.shape[0], h.shape[1], land.sum() * (res / 1000) ** 2,
            np.nanmax(h[g["island_id"] == 0]), len(g["json"]["islands"])))
        if res >= 1000.0:
            from skyisle_gen.island.hydro import build_hydro
            t2 = time.perf_counter()
            try:
                build_hydro(ctx, node, c, g, log=quiet)
                print("   水系 %.2f s" % (time.perf_counter() - t2))
            except Exception as e:  # 粗分辨率未必支持
                print("   水系失败：%r" % e)
