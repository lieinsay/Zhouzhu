"""2051 周围的岛群：大圆距离、面积；全行星的最近邻距离分布；各半径内的群数。

行星计划（docs/PLAN-PLANET.md）定稿时的探针；读 D:/Project/skyisle-gen/out/seed42，在哪跑都行：
    PYTHONUTF8=1 python <本文件>"""
import json

import numpy as np

run = r"D:\Project\skyisle-gen\out\seed42"
z = np.load(run + r"\s03_islands\islands.npz")
planet = json.load(open(run + r"\s01_planet\planet.json", encoding="utf-8"))
R = float(planet.get("radius_km", 6371.0))
lat, lon, area = z["lat"], z["lon"], z["area_km2"]
phi, lam = np.radians(lat), np.radians(lon)
xyz = np.stack([np.cos(phi) * np.cos(lam), np.cos(phi) * np.sin(lam), np.sin(phi)], 1)


def dist_from(k):
    c = np.clip(xyz @ xyz[k], -1, 1)
    return R * np.arccos(c)


print("planet keys", list(planet.keys()))
print("R", R)
d = dist_from(2051)
order = np.argsort(d)
print("2051 lat %.2f lon %.2f area %.0f" % (lat[2051], lon[2051], area[2051]))
for k in order[1:9]:
    print("  node %5d  %6.0f km  area %6.0f km2  lat %.2f lon %.2f cls %d" % (k, d[k], area[k], lat[k], lon[k], z["cls"][k]))
for r in (160, 300, 500, 1000, 2000, 5000):
    print("groups within %5d km: %d" % (r, int((d <= r).sum()) - 1))

# 全行星最近邻（抽 2000 个）
rng = np.random.default_rng(0)
nn = []
for k in rng.choice(len(lat), 2000, replace=False):
    dk = dist_from(k)
    dk[k] = 1e18
    nn.append(dk.min())
nn = np.array(nn)
print("nearest neighbour km: p10 %.0f median %.0f p90 %.0f max %.0f" % tuple(np.percentile(nn, [10, 50, 90, 100])))
# 可见半球里（离某点 90° 以内）平均多少群
print("groups per hemisphere ~ %d" % (len(lat) // 2))
