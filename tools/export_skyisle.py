"""把 skyisle-gen 的一个岛群（第三层产物）转成游戏资产。

    python tools/export_skyisle.py <岛群目录> <输出目录>
    python tools/export_skyisle.py D:/Project/My/skyisle-gen/out/seed42/islands/2051 game/worlds/2051
    python tools/export_skyisle.py --sites game/worlds/2051      # 只按已有的 world.json 重写 sites.tsv
    python tools/export_skyisle.py --terrain-only <岛群目录> game/worlds/2051
                                   # 只换地形（terrain.bin / weather.tsv / world.json 的岛表与栅格）：村、码头等点位沿用已有的 world.json

读 terrain.npz / island.json / settlements.json / weather_y0.csv，写四份文件：

terrain.bin —— 栅格，小端，行主序（行号随 z 增大，即自北向南）：
    char[4]  "ZZWG"
    u32      版本 = 3（1 没有水文；2 没有最后三段河道与地形区；游戏三版都读）
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
    f32[rows·cols]  汇流面积（v2）：平方公里，含本格；虚空格为 0（v3 起就是生成器的 flowacc_km2）
    u16[rows·cols]  河宽（v3）：分米；生成器 river.py 的水力几何（夸张后的数；溪涧为湿季值），非河道格为 0
    u16[rows·cols]  水深（v3）：厘米，同上。河道格的高程是河床（生成器已下切），水面 = 河床 + 水深
    u8 [rows·cols]  地形区（v3）：resources.py 的 ZONE_NAMES 下标（0 虚空 1 高山 2 山地 3 丘陵 4 台地平原 5 河谷 6 崖缘 7 水域）

    流向由生成器的汇流面积（flowacc_km2）反推：挨着虚空的格同生成器一律流出去（河口）；其余格按汇流守恒配给下游——生成器的 D8 走在它自己的「路由面」上（填平面 + 弯曲噪声 + 朝岸缘微倾，
    河道据此下切），导出的高程上重算的 D8 会把整片流域改道。下游格的汇流 = 本格 + 各上游格的汇流，所以按汇流从大到小，
    每格挑一个汇流比自己大、「剩余容量」还装得下自己的同岛邻格，装完剩得最少的优先（best fit）。反推之后重算汇流与生成器对照。

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

VERSION = 3
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


def flow_directions_from_acc(height, island, ref_acc, res):
    """按生成器的汇流面积反推 D8 流向：返回 (流向码 u8, 汇流面积 km²)，规则见文件头。"""
    H, W = height.shape
    land = island >= 0
    cell_km2 = (res / 1000.0) ** 2
    eps = 0.25 * cell_km2  # 汇流是 float32 存的格数 × 格面积
    flow = np.zeros((H, W), dtype=np.uint8)
    isl = island.ravel().tolist()
    A = ref_acc.ravel().tolist()
    hh = height.ravel().tolist()
    # 每格还能收多少上游：自己的汇流减本格
    room = [a - cell_km2 if isl[q] >= 0 else 0.0 for q, a in enumerate(A)]
    fl = [0] * (H * W)
    idx = np.where(land.ravel())[0]
    order = idx[np.argsort(-ref_acc.ravel()[idx], kind="stable")].tolist()
    for q in order:
        i, j = divmod(q, W)
        void_n = 0
        cands, spill = [], None
        for n, (di, dj) in enumerate(FLOW_OFFSETS):
            a_, b_ = i + di, j + dj
            if a_ < 0 or b_ < 0 or a_ >= H or b_ >= W or isl[a_ * W + b_] < 0:
                if not void_n:
                    void_n = n + 1
                continue
            r = a_ * W + b_
            if isl[r] != isl[q]:
                if not void_n:
                    void_n = n + 1  # 别的岛同虚空（生成器逐岛算，岛外都是出口）
                continue
            if A[r] > A[q]:
                if room[r] >= A[q] - eps:
                    cands.append((room[r] - A[q], hh[r], n + 1, r))
                elif spill is None or room[r] > spill[0]:
                    spill = (room[r], 0.0, n + 1, r)
        if void_n:
            fl[q] = void_n | FLOW_TO_VOID  # 同生成器的 d8：挨着虚空就流出去
            continue
        if not cands:
            # 坡面上的小格分配有歧义，前面配错了就没有正好装得下的：交给剩得最多的那个（误差只在坡面的零头上）
            if spill is not None:
                fl[q] = spill[2]
                room[spill[3]] -= A[q]
            continue
        cands.sort()  # 最合适：收下这一格后剩得最少的；再按低的优先
        _, _, code, r = cands[0]
        fl[q] = code
        room[r] -= A[q]
    flow = np.array(fl, dtype=np.uint8).reshape(H, W)
    # 按反推的流向重算汇流（从汇流小的往大的累加）
    Acc = np.where(land, 1.0, 0.0).ravel()
    for q in order[::-1]:
        c = fl[q]
        if c and not c & FLOW_TO_VOID:
            di, dj = FLOW_OFFSETS[(c & 0x0F) - 1]
            Acc[q + di * W + dj] += Acc[q]
    return flow, np.where(land, Acc.reshape(H, W) * cell_km2, 0.0)


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


def export(src: Path, dst: Path, terrain_only: bool = False) -> None:
    terrain = np.load(src / "terrain.npz")
    info = json.loads((src / "island.json").read_text(encoding="utf-8"))
    settle = json.loads((src / "settlements.json").read_text(encoding="utf-8")) if not terrain_only else {}

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

    ref = terrain["flowacc_km2"].astype(np.float64)
    flow, acc = flow_directions_from_acc(height, island, ref, res)
    stuck = land & (flow == 0)
    print(f"流向：河口 {int((land & (flow & FLOW_TO_VOID > 0)).sum())} 格，无处可流 {int(stuck.sum())} 格")
    off = np.abs(acc - ref)[land]
    big = (ref >= 1.0)[land]
    print(f"汇流对照 skyisle-gen：最大差 {off.max():.3f} km²，差 > 0.05 km² 的格 {int((off > 0.05).sum())} / {int(land.sum())}；"
          f"河道格（≥ 1 km²）差 > 1% 的 {int((big & (off > 0.01 * ref[land])).sum())} / {int(big.sum())}")

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
        f.write(np.where(land, ref, 0.0).astype("<f4").tobytes())  # 写生成器的汇流：河道格与它的溪涧 / 河一一对应；反推重算的只拿来对照
        f.write(np.clip(np.round(np.where(land, terrain["river_width_m"], 0) * 10.0), 0, 65535).astype("<u2").tobytes())
        f.write(np.clip(np.round(np.where(land, terrain["river_depth_m"], 0) * 100.0), 0, 65535).astype("<u2").tobytes())
        f.write(np.where(land, terrain["terrain_zone"], 0).astype(np.uint8).tobytes())

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
    if terrain_only:
        # 点位沿用已有的：游戏里的码头、村落、存档与测试路线都认它们（生成器的聚落层另有演进，换点位是另一件事）
        old = json.loads((dst / "world.json").read_text(encoding="utf-8"))
        for key in ("villages", "hamlets", "docks", "outposts", "bridgeheads", "population"):
            world[key] = old.get(key)
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
    elif len(sys.argv) == 4 and sys.argv[1] == "--terrain-only":
        export(Path(sys.argv[2]), Path(sys.argv[3]), terrain_only=True)
    elif len(sys.argv) == 3:
        export(Path(sys.argv[1]), Path(sys.argv[2]))
    else:
        sys.exit(__doc__)
