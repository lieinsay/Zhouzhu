"""把 skyisle-gen 的一个行星（十步管线的 run 目录 + 第三层粗版）转成游戏的行星包（docs/PLAN-PLANET.md P0）。

    python tools/export_planet.py <run 目录> <输出目录> [--lod-res 2000] [--near 2051 --near-res 500 --near-radius 600]
    python tools/export_planet.py D:/Project/skyisle-gen/out/seed42 game/planet/seed42

粗版先用生成器批跑：skyisle island lod --run out/seed42 --lod-res 2000,1000（全行星），
                   skyisle island lod --run out/seed42 --lod-res 500 --near 2051 --radius 600（近邻）。
只依赖 numpy。游戏运行时不需要它，也不读 skyisle-gen 的 out/。两份文件都是小端、不压缩（纯 C++ 读，不引 zlib）：

planet.bin —— 行星层：
    char[4] "ZZPL"
    u32  版本 = 1
    u32  seed, 节点数 N, 列数, 场数, 头文本字节数
    头文本（UTF-8）：一行一条「键 值…」——radius_km、rotation_hr、tilt_deg、calendar（year_days seasons months_per_season days_per_month solstice_n）、
                     bands（eq trades calm west 的带顶纬度）、g_storm（lat lon radius_deg）、source（run 目录名、生成器提交号）
    列（每个节点一个值）：u8 名字长、名字（UTF-8）、u8 类型（0 f32、1 i32、2 i16、3 u8）、N 个值
        名字 = 来源.字段：s03.lat、s03.area_km2……（islands.npz 的逐节点数组）、s04.precip……（climate_islands.npz）、
        s09.pop / s09.state / s09.polity（polity.npz）、lod.peak_m / lod.rim_m（粗版量出的主岛峰高、岸缘）、
        lod.violation_km / lod.constrained（势力范围：越界多少 km、布局重摆过没有）
    场（1° 网格，格心对齐，第 0 行在南）：u8 名字长、名字、u16 行、u16 列、f32 lat0 lon0 dlat dlon（第 0 行第 0 列的格心、步长，度）、
        u8 类型、行 × 列 个值。名字 = s03.plates.<键>、s04.climate_grid.<键>、s04.wind_local.<键>、s04.band_local.edges（8 × 360，行 = 带界）
    第三层（岛群生成器）要读的行星产物都在里面（P6 移植进游戏时不用再导）。

lods.bin —— 岛群粗版（远处看的低模）：
    char[4] "ZZLD"
    u32  版本 = 1
    u32  条数
    索引（每条）：u32 节点、f32 格距（米）、f32 栅格左上角 origin_x_km origin_y_km（相对群心，x 东 y 北）、u16 行、u16 列、u16 岛数、u16 0、u64 数据偏移、u32 数据字节
    数据（每条）：岛表（每岛 f32 center_x_km center_y_km rim_m keel_m peak_m surface_m）、再是格子的行程编码：
        u16 段头：最高位 1 = 陆地段、0 = 虚空段，低 15 位 = 格数；陆地段后面跟 格数 × { u16 高（米，块内陆地的最高点——远处的山看剪影）、
        u8 地表类 << 4 | 陆地占比（地表类是生成器 LANDCOVER_CLASSES 的下标；占比 1–15，×17 还原成 0–255）、u8 岛号 }——一格 4 字节
    同一个节点可以有几条（不同格距）：全行星 --lod-res，--near 节点周围 --near-radius km 内另加 --near-res。
"""

import json
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

VERSION = 1
DTYPES = {np.dtype(np.float32): 0, np.dtype(np.int32): 1, np.dtype(np.int16): 2, np.dtype(np.uint8): 3}


def as_column(a):
    a = np.asarray(a)
    if a.dtype == np.bool_:
        return a.astype(np.uint8)
    if a.dtype.kind == "f":
        return a.astype(np.float32)
    if a.dtype.kind in "iu":
        if a.min() >= 0 and a.max() <= 255 and a.dtype.itemsize == 1:
            return a.astype(np.uint8)
        if a.min() >= -32768 and a.max() <= 32767 and a.dtype.itemsize <= 2:
            return a.astype(np.int16)
        return a.astype(np.int32)
    raise ValueError(f"不认识的类型 {a.dtype}")


def name_bytes(s):
    b = s.encode("utf-8")
    assert len(b) < 256
    return struct.pack("<B", len(b)) + b


def generator_commit(run: Path) -> str:
    try:
        return subprocess.check_output(["git", "-C", str(run.parent.parent), "rev-parse", "--short", "HEAD"], text=True).strip()
    except Exception:
        return "?"


def lod_entries(run: Path, res: float, nodes=None):
    d = run / "islands_lod" / str(int(round(res)))
    files = sorted(d.glob("*.npz"), key=lambda p: int(p.stem))
    for p in files:
        node = int(p.stem)
        if nodes is not None and node not in nodes:
            continue
        with np.load(p) as z:
            meta = json.loads(str(z["meta"]))
            yield node, meta, {k: z[k] for k in z.files if k != "meta"}


def encode_lod(meta, arr) -> bytes:
    isl = meta["islands"]
    out = bytearray()
    for i in isl:
        out += struct.pack("<6f", i["center_km"][0], i["center_km"][1], i["rim_m"], i["keel_m"], i["peak_m"], i["surface_m"])
    land = arr["land"].ravel()
    peak = np.nan_to_num(arr["peak"].ravel(), nan=0.0)
    hgt = np.clip(np.round(peak), 0, 65535).astype(np.uint16)
    lc = arr["landcover"].ravel().astype(np.uint8)
    ii = np.clip(arr["island"].ravel(), 0, 255).astype(np.uint8)
    is_land = land > 0
    # 地表类（12 类）与陆地占比各 4 位拼一个字节：占比 0–15（读的时候 ×17 还原成 0–255），有陆地的格至少 1
    land4 = np.where(is_land, np.clip(np.round(land / 17.0), 1, 15), 0).astype(np.uint8)
    packed = ((np.minimum(lc, 15) << 4) | land4).astype(np.uint8)
    n = land.size
    k = 0
    while k < n:
        v = bool(is_land[k])
        j = k
        while j < n and bool(is_land[j]) == v and j - k < 32767:
            j += 1
        cnt = j - k
        out += struct.pack("<H", (0x8000 if v else 0) | cnt)
        if v:
            rec = np.zeros(cnt, dtype=[("h", "<u2"), ("lc_land", "u1"), ("isl", "u1")])
            rec["h"], rec["lc_land"], rec["isl"] = hgt[k:j], packed[k:j], ii[k:j]
            out += rec.tobytes()
        k = j
    return bytes(out)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    run, dst = Path(args[0]), Path(args[1])
    opt = {"--lod-res": "2000", "--near": None, "--near-res": "500", "--near-radius": "600"}
    for i in range(2, len(args), 2):
        opt[args[i]] = args[i + 1]
    dst.mkdir(parents=True, exist_ok=True)

    planet = json.loads((run / "s01_planet" / "planet.json").read_text(encoding="utf-8"))
    bands = json.loads((run / "s02_wind" / "bands.json").read_text(encoding="utf-8"))
    manifest = json.loads((run / "manifest.json").read_text(encoding="utf-8")) if (run / "manifest.json").exists() else {}
    seed = int(manifest.get("seed", 0))
    isl = np.load(run / "s03_islands" / "islands.npz")
    cli = np.load(run / "s04_climate" / "climate_islands.npz")
    pol = np.load(run / "s09_polity" / "polity.npz")
    N = int(isl["lat"].size)

    # ---- 列
    columns = []
    for k in isl.files:
        a = isl[k]
        if a.ndim == 1 and a.size == N:
            columns.append(("s03." + k, as_column(a)))
    for k in cli.files:
        a = cli[k]
        if a.ndim == 1 and a.size == N:
            columns.append(("s04." + k, as_column(a)))
    for k in ("pop", "state", "polity", "kind", "realm", "circle", "fief"):
        if k in pol.files and pol[k].ndim == 1 and pol[k].size == N:
            columns.append(("s09." + k, as_column(pol[k])))
    peak = np.full(N, np.nan, np.float32)
    rim = np.full(N, np.nan, np.float32)
    viol = np.full(N, np.nan, np.float32)
    constrained = np.zeros(N, np.uint8)
    lod_res = [float(x) for x in opt["--lod-res"].split(",")]
    for node, meta, _arr in lod_entries(run, lod_res[0]):
        main = meta["islands"][0]
        peak[node], rim[node] = main["peak_m"], main["rim_m"]
        t = meta.get("territory", {})
        viol[node] = t.get("violation_km", -1e3)
        constrained[node] = 1 if t.get("constrained") else 0
    columns.append(("lod.peak_m", peak))
    columns.append(("lod.rim_m", rim))
    columns.append(("lod.violation_km", viol))       # 势力范围：陆地越过与邻群分界线多少 km（≤ 0 没越界）
    columns.append(("lod.constrained", constrained))  # 1 = 布局因势力范围重摆过

    # ---- 场
    grids = []
    for stage, name in (("s03_islands", "plates"), ("s04_climate", "climate_grid"), ("s04_climate", "wind_local"), ("s04_climate", "band_local")):
        z = np.load(run / stage / f"{name}.npz")
        lats = z["lats"] if "lats" in z.files else np.arange(-89.5, 90.0, 1.0)
        lons = z["lons"] if "lons" in z.files else np.arange(-179.5, 180.0, 1.0)
        for k in z.files:
            a = z[k]
            if a.ndim != 2 or k in ("lats", "lons"):
                continue
            if a.shape == (lats.size, lons.size):
                geo = (float(lats[0]), float(lons[0]), float(lats[1] - lats[0]), float(lons[1] - lons[0]))
            elif a.shape[1] == lons.size:
                geo = (0.0, float(lons[0]), 1.0, float(lons[1] - lons[0]))   # 带界 edges[8, 360]：行是第几条带界
            else:
                continue
            grids.append((f"{stage[:3]}.{name}.{k}", as_column(a), geo))

    commit = generator_commit(run)
    cal = planet.get("calendar", {})
    dps = float(cal.get("days_per_season", 84.0))
    mps = int(cal.get("months_per_season", 3))
    g = bands.get("G", {})
    head = [
        f"radius_km {planet['radius_km']}",
        f"rotation_hr {planet.get('rotation_period_hr', 24.0)}",
        f"tilt_deg {planet['axial_tilt_deg']}",
        # 北半球夏至 = 季 1 的季中（生成器 island/climate.py：day_offset_solstice_n = 1.5 × 每季天数）
        f"calendar {int(cal.get('year_days_solar', 336))} {int(cal.get('seasons', 4))} {mps} {int(round(dps / mps))} {1.5 * dps}",
        "bands " + " ".join(str(planet["bands"][k]) for k in ("eq_storm_top_deg", "trades_top_deg", "calm_top_deg", "westerlies_top_deg")),
        f"g_storm {g.get('lat', 37.5)} {g.get('lon', -10.0)} {g.get('radius_deg', 4.0)}",
        f"source {run.name} {commit}",
    ]
    head_b = ("\n".join(head) + "\n").encode("utf-8")

    with open(dst / "planet.bin", "wb") as f:
        f.write(b"ZZPL" + struct.pack("<IIIIII", VERSION, seed, N, len(columns), len(grids), len(head_b)))
        f.write(head_b)
        for name, a in columns:
            f.write(name_bytes(name) + struct.pack("<B", DTYPES[a.dtype]) + a.astype(a.dtype.newbyteorder("<")).tobytes())
        for name, a, geo in grids:
            f.write(name_bytes(name) + struct.pack("<HH4fB", a.shape[0], a.shape[1], *geo, DTYPES[a.dtype]))
            f.write(np.ascontiguousarray(a).astype(a.dtype.newbyteorder("<")).tobytes())

    # ---- 粗版
    entries = []
    for res in lod_res:
        entries += [(node, meta, arr) for node, meta, arr in lod_entries(run, res)]
    if opt["--near"] is not None:
        near = int(opt["--near"])
        R = float(planet["radius_km"])
        la, lo = np.radians(isl["lat"].astype(float)), np.radians(isl["lon"].astype(float))
        d = R * np.arccos(np.clip(np.sin(la[near]) * np.sin(la) + np.cos(la[near]) * np.cos(la) * np.cos(lo - lo[near]), -1, 1))
        near_set = set(int(k) for k in np.where(d <= float(opt["--near-radius"]))[0])
        for res in [float(x) for x in opt["--near-res"].split(",")]:
            entries += [(node, meta, arr) for node, meta, arr in lod_entries(run, res, near_set)]
    blobs = [encode_lod(meta, arr) for _node, meta, arr in entries]
    index_size = 12 + len(entries) * (4 + 4 * 3 + 2 * 4 + 8 + 4)
    with open(dst / "lods.bin", "wb") as f:
        f.write(b"ZZLD" + struct.pack("<II", VERSION, len(entries)))
        off = index_size
        for (node, meta, arr), blob in zip(entries, blobs):
            r = meta["raster"]
            f.write(struct.pack("<I3f4HQI", node, r["res_m"], r["origin_km"][0], r["origin_km"][1], r["rows"], r["cols"],
                                len(meta["islands"]), 0, off, len(blob)))
            off += len(blob)
        for blob in blobs:
            f.write(blob)

    missing = int(np.isnan(peak).sum())
    mb = lambda p: (dst / p).stat().st_size / 1e6
    print(f"{dst / 'planet.bin'}: {N} 群、{len(columns)} 列、{len(grids)} 张场，{mb('planet.bin'):.1f} MB；生成器 {commit}")
    print(f"{dst / 'lods.bin'}: {len(entries)} 条粗版（{', '.join(f'{int(r)} m' for r in lod_res)}"
          f"{'；近邻 ' + opt['--near-res'] + ' m' if opt['--near'] is not None else ''}），{mb('lods.bin'):.1f} MB；没有粗版的群 {missing}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
