"""把 skyisle-gen 的一个岛群（第三层产物）转成游戏资产。

    python tools/export_skyisle.py <岛群目录> <输出目录>
    python tools/export_skyisle.py D:/Project/My/skyisle-gen/out/seed42/islands/2051 game/worlds/2051
    python tools/export_skyisle.py --sites game/worlds/2051      # 只按已有的 world.json 重写 sites.tsv

读 terrain.npz / island.json / settlements.json / weather_y0.csv，写四份文件：

terrain.bin —— 栅格，小端，行主序（行号随 z 增大，即自北向南）：
    char[4]  "ZZWG"
    u32      版本 = 2（1 没有最后两段水文；游戏两版都读）
    u32      rows, cols
    f64      res_m（格距）
    f64      x0, z0（格 (0, 0) 格心的游戏坐标，米）
    u16[rows·cols]  高程：云带顶以上的米数 × 100；虚空格填最近陆地格的值（插值到岸外不跳变）
    i16[rows·cols]  岸距：到岸线的有符号距离（米），陆地为正、虚空为负
    u8 [rows·cols]  最近的岛号（陆地即本岛；虚空取最近陆地格的岛）
    u8 [rows·cols]  地表类（island.json landcover.classes 的下标，0 = 虚空）
    u8 [rows·cols]  1 = 溪涧
    u8 [rows·cols]  D8 流向（v2）：0 = 不流（虚空格），1..8 = 流向邻格 (dr, dc) = FLOW_OFFSETS[k − 1]；
                    或上 0x10 = 那个邻格是虚空（河口：水从岸缘跌下去）
    f32[rows·cols]  汇流面积（v2）：平方公里，含本格；虚空格为 0

    流向与汇流按 skyisle-gen island/terrain.py 的 priority_fill + d8 + accumulate 在导出的高程上逐岛重算
    （导出的高程已经是它抬过洼地之后的，填平面不变，所以与生成器的 flowacc_km2 一致，导出时会对照）。

weather.tsv —— 一年的逐日天气（skyisle-gen 的 weather_y0.csv），给河的水情：
    # year_days <一年几天>  precip_mm <年降水>
    day  precip_mm  temp_c  type

world.json —— 岛表与点位，坐标全换成游戏的米：x 向东、z 向南，原点 = 群心。

sites.tsv —— 同一份岛表与点位的平铺版，给不读 JSON 的纯 C++（src/world_sites.*：游戏里铺码头与村落、批量体检工具）。
    UTF-8，制表符分隔，# 开头是注释，一行一条：
    island   id  rim_m  cliff_m  keel_m  max_shore_m
    dock     id  island  x  z  main(0/1)
    village  id  island  x  z  households  seat(0/1)  name
    hamlet   id  island  x  z  households  0           name

只依赖 numpy + scipy。游戏运行时不需要它，也不读 skyisle-gen 的 out/。
"""

import json
import struct
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage

VERSION = 2
HEIGHT_SCALE = 100.0


# D8 流向的邻格（行、列偏移），下标 + 1 即 terrain.bin 里的流向码
FLOW_OFFSETS = [(-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)]
FLOW_TO_VOID = 0x10
FILL_EPS_M = 0.001  # skyisle-gen hydro.fill_eps_m


def priority_fill(h, mask, eps=FILL_EPS_M):
    """洼地填平（Barnes 优先泛洪），同 skyisle-gen island/terrain.py：掩膜外 = 出口；填后每格严格高于其下游。"""
    import heapq
    H, W = h.shape
    edge = mask & ~ndimage.binary_erosion(mask, structure=np.ones((3, 3)), border_value=0)
    hl = np.where(mask, h, -np.inf).tolist()
    closed = (~mask | edge).tolist()
    res = [row[:] for row in hl]
    heap = [(hl[i][j], int(i), int(j)) for i, j in zip(*np.where(edge))]
    heapq.heapify(heap)
    while heap:
        z, i, j = heapq.heappop(heap)
        for di, dj in FLOW_OFFSETS:
            a, b = i + di, j + dj
            if a < 0 or b < 0 or a >= H or b >= W or closed[a][b]:
                continue
            closed[a][b] = True
            zn = max(hl[a][b], z + eps)
            res[a][b] = zn
            heapq.heappush(heap, (zn, a, b))
    return np.where(mask, np.array(res), np.nan)


def flow_directions(height, island, res):
    """逐岛填洼 + D8：返回 (流向码 u8, 汇流面积 km²)。规则同 skyisle-gen 的 d8：有虚空邻格就流出去（河口），否则取最陡的下坡邻格。"""
    H, W = height.shape
    flow = np.zeros((H, W), dtype=np.uint8)
    acc = np.zeros((H, W))
    cell_km2 = (res / 1000.0) ** 2
    for k in range(int(island.max()) + 1):
        m = island == k
        if not m.any():
            continue
        rr, cc = np.where(m)
        r0, r1, c0, c1 = max(rr.min() - 1, 0), min(rr.max() + 2, H), max(cc.min() - 1, 0), min(cc.max() + 2, W)
        mk = m[r0:r1, c0:c1]
        hf = priority_fill(height[r0:r1, c0:c1], mk)
        h, w = mk.shape
        best = np.full((h, w), -np.inf)
        code = np.zeros((h, w), dtype=np.uint8)
        hh = np.where(mk, hf, np.inf)
        for n, (di, dj) in enumerate(FLOW_OFFSETS):
            nb = np.full((h, w), np.nan)
            nm = np.zeros((h, w), dtype=bool)
            si, sj = slice(max(0, -di), h - max(0, di)), slice(max(0, -dj), w - max(0, dj))
            ti, tj = slice(max(0, di), h - max(0, -di)), slice(max(0, dj), w - max(0, -dj))
            nb[si, sj] = hf[ti, tj]
            nm[si, sj] = mk[ti, tj]
            dist = res * (2 ** 0.5 if di and dj else 1.0)
            with np.errstate(invalid="ignore"):
                drop = np.where(nm, (hh - nb) / dist, 1e6)  # 虚空邻格：视作出口
            better = mk & (drop > best)
            best = np.where(better, drop, best)
            code = np.where(better, (n + 1) | np.where(nm, 0, FLOW_TO_VOID), code).astype(np.uint8)
        code = np.where(mk & (best > 0), code, 0).astype(np.uint8)
        # 汇流：按填平面从高到低累加
        idx = np.where(mk.ravel())[0]
        order = idx[np.argsort(-hf.ravel()[idx], kind="stable")]
        A = np.where(mk, 1.0, 0.0).ravel()
        cl = code.ravel()
        for q in order.tolist():
            c = int(cl[q])
            if c and not c & FLOW_TO_VOID:
                di, dj = FLOW_OFFSETS[(c & 0x0F) - 1]
                A[q + di * w + dj] += A[q]
        sub = (slice(r0, r1), slice(c0, c1))
        flow[sub] = np.where(mk, code, flow[sub])
        acc[sub] = np.where(mk, A.reshape(h, w) * cell_km2, acc[sub])
    return flow, acc


def to_game(km):
    """skyisle 的 (x 东, y 北) 公里 → 游戏的 [x, z] 米"""
    return [round(km[0] * 1000.0, 1), round(-km[1] * 1000.0, 1)]


def export(src: Path, dst: Path) -> None:
    terrain = np.load(src / "terrain.npz")
    info = json.loads((src / "island.json").read_text(encoding="utf-8"))
    settle = json.loads((src / "settlements.json").read_text(encoding="utf-8"))

    raster = info["raster"]
    res = float(raster["res_m"])
    rows, cols = int(raster["rows"]), int(raster["cols"])
    ox_km, oy_km = raster["origin_km"]
    x0 = ox_km * 1000.0 + 0.5 * res
    z0 = -oy_km * 1000.0 + 0.5 * res

    island = terrain["island_id"].astype(np.int32)
    land = island >= 0
    height = terrain["height"].astype(np.float64)
    assert height.shape == (rows, cols)

    flow, acc = flow_directions(height, island, res)
    ref = terrain["flowacc_km2"].astype(np.float64)
    off = np.abs(acc - ref)[land]
    print(f"汇流对照 skyisle-gen：最大差 {off.max():.3f} km²，差 > 0.05 km² 的格 {int((off > 0.05).sum())} / {int(land.sum())}")

    # 虚空格取最近陆地格：高程与岛号都外推
    _, (ri, ci) = ndimage.distance_transform_edt(~land, return_indices=True)
    height = height[ri, ci]
    nearest = island[ri, ci]

    # 有符号岸距：格心到另一类格心的距离减半格，陆地为正
    inside = ndimage.distance_transform_edt(land) * res
    outside = ndimage.distance_transform_edt(~land) * res
    shore = np.where(land, inside - 0.5 * res, -(outside - 0.5 * res))

    # 各岛最深处的岸距：岛底按「向内程度」垂下去要用
    max_shore = ndimage.maximum(inside, labels=island + 1, index=np.arange(1, island.max() + 2))

    dst.mkdir(parents=True, exist_ok=True)
    with open(dst / "terrain.bin", "wb") as f:
        f.write(b"ZZWG")
        f.write(struct.pack("<III", VERSION, rows, cols))
        f.write(struct.pack("<ddd", res, x0, z0))
        f.write(np.clip(np.round(height * HEIGHT_SCALE), 0, 65535).astype("<u2").tobytes())
        f.write(np.clip(np.round(shore), -32767, 32767).astype("<i2").tobytes())
        f.write(nearest.astype(np.uint8).tobytes())
        f.write(terrain["landcover"].astype(np.uint8).tobytes())
        f.write((terrain["stream"] > 0).astype(np.uint8).tobytes())
        f.write(flow.tobytes())
        f.write(acc.astype("<f4").tobytes())

    write_weather(src, info, dst)

    islands = []
    for isl in info["islands"]:
        islands.append({
            "id": isl["id"],
            "center": to_game(isl["center_km"]),
            "area_km2": isl["area_km2"],
            "age": isl["age"],
            "age_zh": isl["age_zh"],
            "main": bool(isl.get("is_main", isl["id"] == 0)),
            "rim_m": isl["rim_m"],
            "peak_m": isl["peak_m"],
            "cliff_m": isl["cliff_m"],
            "keel_m": isl["keel_m"],
            "max_shore_m": round(float(max_shore[isl["id"]]), 1),
        })

    def places(key, fields):
        out = []
        for item in settle.get(key) or []:
            row = {"id": item["id"], "island": item["island"], "pos": to_game(item["km"])}
            for name in fields:
                if name in item:
                    row[name] = item[name]
            out.append(row)
        return out

    world = {
        "source": {
            "run": info["meta"]["run"],
            "node": info["meta"]["node"],
            "seed": info["meta"]["seed"],
            "generator": info["meta"]["generator"],
            "cls_zh": info["meta"]["cls_zh"],
            "lat": info["meta"]["lat"],
            "lon": info["meta"]["lon"],
        },
        "grid": {"rows": rows, "cols": cols, "res_m": res, "x0": x0, "z0": z0, "height_scale": HEIGHT_SCALE},
        "landcover": {"classes": info["landcover"]["classes"], "palette": info["landcover"]["palette"]},
        "islands": islands,
        "links": [{"a": l["a"], "b": l["b"], "kind": l["kind"], "gap_km": l["gap_km"]} for l in info["links"]],
        "villages": places("villages", ["name", "households", "seat", "elev_m"]),
        "hamlets": places("hamlets", ["name", "households", "elev_m"]),
        "docks": places("docks", ["main", "serves"]),
        "outposts": places("outposts", ["settlement", "households"]),
        "bridgeheads": places("bridgeheads", ["to", "gap_km"]),
        "population": settle.get("population"),
    }
    (dst / "world.json").write_text(json.dumps(world, ensure_ascii=False, indent=1), encoding="utf-8")
    write_sites(world, dst)
    print(f"{dst}: {rows}×{cols} @ {res:g} m, {len(islands)} 岛, {len(world['villages'])} 村, {len(world['docks'])} 码头")


def write_weather(src: Path, info: dict, dst: Path) -> None:
    import csv
    climate = json.loads((src / "climate.json").read_text(encoding="utf-8"))
    year_days = int(climate["calendar"]["year_days"])
    lines = [f"# 由 tools/export_skyisle.py 从 weather_y0.csv 导出，别手改。year_days {year_days}  precip_mm {info['hydro']['precip_mm']:g}"]
    with open(src / "weather_y0.csv", encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            lines.append("\t".join((row["day"], row["precip_mm"], row["temp_c"], row["type"])))
    (dst / "weather.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_sites(world: dict, dst: Path) -> None:
    lines = ["# 由 tools/export_skyisle.py 从 world.json 导出，别手改。格式见那个脚本开头。"]
    for isl in world["islands"]:
        lines.append("\t".join(str(v) for v in ("island", isl["id"], isl["rim_m"], isl["cliff_m"], isl["keel_m"], isl["max_shore_m"])))
    for d in world["docks"]:
        lines.append("\t".join(str(v) for v in ("dock", d["id"], d["island"], d["pos"][0], d["pos"][1], 1 if d.get("main") else 0)))
    for kind, key in (("village", "villages"), ("hamlet", "hamlets")):
        for v in world[key]:
            row = (kind, v["id"], v["island"], v["pos"][0], v["pos"][1], v["households"], 1 if v.get("seat") else 0, v.get("name", ""))
            lines.append("\t".join(str(x) for x in row))
    (dst / "sites.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--sites":
        out = Path(sys.argv[2])
        write_sites(json.loads((out / "world.json").read_text(encoding="utf-8")), out)
        print(f"{out / 'sites.tsv'}")
    elif len(sys.argv) == 3:
        export(Path(sys.argv[1]), Path(sys.argv[2]))
    else:
        sys.exit(__doc__)
