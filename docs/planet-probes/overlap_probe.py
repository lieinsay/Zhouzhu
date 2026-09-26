"""邻群陆地会不会叠在一起：1 km 粗版地形（崩了退 500 / 250 m），按群心方位等距投影放到球面上，查两群陆地格的最近距离。
抽样：全部密接群（dense）+ 随机 300 个中疏群，各自与 6 个最近邻比。约 5 分钟。

行星计划（docs/PLAN-PLANET.md）定稿时的探针；P0 做完势力范围约束后用它复查。在 skyisle-gen 仓库根下跑：
    PYTHONPATH=. PYTHONUTF8=1 python <本文件>
2026-09-26 的结果：密接 1506 对里 612 对陆地相距 < 2 km、中疏 1800 对里 227 对。"""
import math
import sys
import time

import numpy as np

from skyisle_gen.cli import _ctx_from_run
from skyisle_gen import island as isl

ctx = _ctx_from_run("out/seed42")
c = isl.island_config(ctx)
quiet = lambda *a, **k: None
z = ctx.load_npz(3, "islands")
R = 6371.0
lat, lon, cls = z["lat"], z["lon"], z["cls"]
phi, lam = np.radians(lat), np.radians(lon)
xyz = np.stack([np.cos(phi) * np.cos(lam), np.cos(phi) * np.sin(lam), np.sin(phi)], 1)

cache = {}
FAILS = []


def land_xyz(node):
    """该群陆地格心的球面单位向量（方位等距：x 东、y 北，km）。"""
    if node in cache:
        return cache[node]
    inp = isl._node_inputs(ctx, node)
    g = None
    for res_try in (1000.0, 500.0, 250.0):
        try:
            g = isl.build_terrain(ctx, node, c, inp, res_m=res_try, log=quiet)
            break
        except ValueError:
            FAILS.append((node, res_try))
    J = g["json"]["raster"]
    res = J["res_m"] / 1000.0
    x0, y0 = J["origin_km"]
    rr, cc = np.where(g["island_id"] >= 0)
    x = x0 + (cc + 0.5) * res
    y = y0 - (rr + 0.5) * res
    d = np.hypot(x, y) / R  # 角距
    az = np.arctan2(x, y)  # 方位（北起、东正）
    p0, l0 = phi[node], lam[node]
    # 方位等距逆投影
    plat = np.arcsin(np.sin(p0) * np.cos(d) + np.cos(p0) * np.sin(d) * np.cos(az))
    plon = l0 + np.arctan2(np.sin(az) * np.sin(d) * np.cos(p0), np.cos(d) - np.sin(p0) * np.sin(plat))
    v = np.stack([np.cos(plat) * np.cos(plon), np.cos(plat) * np.sin(plon), np.sin(plat)], 1)
    cache[node] = (v, float(np.max(np.hypot(x, y))))
    return cache[node]


rng = np.random.default_rng(1)
dense = np.where(cls == 0)[0]
medium = rng.choice(np.where(cls == 1)[0], 300, replace=False)
t0 = time.perf_counter()
rows = []
for kind, nodes in (("dense", dense), ("medium", medium)):
    n_pairs = n_touch = n_overlap = 0
    worst = []
    for a in nodes:
        dist = R * np.arccos(np.clip(xyz @ xyz[a], -1, 1))
        dist[a] = 1e18
        for b in np.argsort(dist)[:6]:
            va, ra = land_xyz(int(a))
            vb, rb = land_xyz(int(b))
            if dist[b] > ra + rb + 4:
                n_pairs += 1
                continue
            # 两群陆地格的最近距离（km）
            best = 1e18
            for i in range(0, len(va), 4096):
                dots = va[i:i + 4096] @ vb.T
                best = min(best, float(R * np.arccos(np.clip(dots.max(), -1, 1))))
            n_pairs += 1
            if best < 2.0:
                n_overlap += 1
                worst.append((best, int(a), int(b), float(dist[b])))
            elif best < 10.0:
                n_touch += 1
    worst.sort()
    print("%s: %d 群 × 6 近邻 = %d 对；陆地相距 < 2 km（算叠上）%d 对，2–10 km %d 对" % (kind, len(nodes), n_pairs, n_overlap, n_touch))
    for w in worst[:5]:
        print("   叠上：%d–%d 群心距 %.0f km，陆地最近 %.1f km" % (w[1], w[2], w[3], w[0]))
    sys.stdout.flush()
print("粗版失败（换更细重试）%d 次，例：%s" % (len(FAILS), FAILS[:6]))
print("用时 %.0f s，生成了 %d 个群的粗版" % (time.perf_counter() - t0, len(cache)))
