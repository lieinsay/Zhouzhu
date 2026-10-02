"""把 skyisle-gen 的一个行星（十步管线的 run 目录 + 第三层粗版）转成游戏的行星包（docs/PLAN-PLANET.md P0）。

    python tools/export_planet.py <run 目录> <输出目录> [--lod-res 2000] [--near <开局那群> --near-res 500 --near-radius 320] [--generator <skyisle-gen 根>] [--fine <岛群包目录>]
    python tools/export_planet.py D:/Project/skyisle-gen/out/seed42 game/planet/seed42 --near 6329 --near-radius 320 --fine game/planet/seed42/groups

开局周围那一圈 500 m 的粗版（D11；开局配置在 src/story_start.h）也能不经 Python、只换那一圈：
    build/Debug/zhouzhu_gen.exe near-lods --config game/planet/seed42/config.tsv --seed 42 --lods game/planet/seed42/lods.bin --near 6329 --near-radius 320
（地貌与聚落 P0 就这么把 2051 周围 600 km 换成 6329 周围 320 km；拿 --near 2051 --near-radius 600 验过与这里导的逐字节相同）

--fine：有细版的群（<目录>/<节点>/weather.tsv）拿来与粗版的逐日天气对一遍，报类型逐日相同的比例（行星计划 P5）。
--generator：记生成器提交号用（缺省按 run 目录的上上级，即 skyisle-gen/out/<run>；新建世界的 run 在游戏的存档目录里，要给）。
run 目录里没有粗版（新建世界：只算了行星层）照样导：lods.bin 是 0 条，lod.* 列是 NaN，游戏里远处先画替身（计划 4.8）。
单群的粗版（游戏按需生成的，tools/zhouzhu_gen.py 调 write_lods）写成同格式、只有一条的 lods/<节点>.bin。

粗版先用生成器批跑：skyisle island lod --run out/seed42 --lod-res 2000,1000（全行星），
                   skyisle island lod --run out/seed42 --lod-res 500 --near 6329 --radius 320（近邻）。
只依赖 numpy。游戏运行时不需要它，也不读 skyisle-gen 的 out/。
行星计划 P6e：游戏进程内生成用的是这里的 C++ 版（src/gen_export.*，连同生成器 Python 前端在中间做的那一步），两边写出的文件逐字节相同；
改这里的格式或写法，C++ 那边要一起改（单测 gen_seed42 与 tools/zhouzhu_gen.cpp 的对照会报出来）。
两份文件都是小端、不压缩（纯 C++ 读，不引 zlib）：

planet.bin —— 行星层：
    char[4] "ZZPL"
    u32  版本 = 1
    u32  seed, 节点数 N, 列数, 场数, 头文本字节数
    头文本（UTF-8）：一行一条「键 值…」——radius_km、rotation_hr、tilt_deg、calendar（year_days seasons months_per_season days_per_month solstice_n）、
                     bands（eq trades calm west 的带顶纬度）、g_storm（lat lon radius_deg）、source（run 目录名、生成器提交号）、
                     vertical（海面、云底、云顶、岛底净空 m，基准压强 atm、标高 km、氧份额）；缺省头仍读旧版默认值
    列（每个节点一个值）：u8 名字长、名字（UTF-8）、u8 类型（0 f32、1 i32、2 i16、3 u8）、N 个值
        名字 = 来源.字段：s03.lat、s03.area_km2……（islands.npz 的逐节点数组）、s04.precip……（climate_islands.npz）、
        s09.pop / s09.state / s09.polity（polity.npz）、lod.peak_m / lod.rim_m（粗版量出的主岛峰高、岸缘）、
        lod.violation_km / lod.constrained（势力范围：越界多少 km、布局重摆过没有）、lod.extent_km（群心到最远的陆地格心）
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

weather.bin —— 各岛群一年的逐日天气（行星计划 P5：行星天气场以各群当天的天气为真值）。生成器的粗版顺带出了第 0 年的逐日天气
    （islands_lod/<格距>/<节点>.npz 的 weather_<列> 与 weather_meta，与细版的 weather_y0.csv 逐行相同）；无损，读回与 weather.tsv 逐位相同：
    char[4] "ZZWX"
    u32  版本 = 1
    u32  一年几天 D、条数 N、头文本字节数、压缩数据字节数、解压后字节数
    头文本（UTF-8）：一行一条「键 值…」——types（逗号分隔的类型名，下标 = type）、calendar（seasons months_per_season days_per_month solstice_n）、
                     year、precip_formula（④ 的相对降水 → 年降水 mm：min max exp）、season_names <k> <逗号分隔>（第 k 套季名）、source
    索引（每条，不压缩）：u32 节点、u8 季名套号、u8 0、u16 0、f32 ref_m lapse rim_m fog_rise_m snow_temp precip_mm precip_climate_mm
        （台面、直减率 °C/km、主岛岸缘、云海漫多高、雪温、年降水（weather.tsv 头那个）、气候年降水）
    数据：zlib（RFC 1950）压一整块，里面按列、条目在外日在内：
        N·D 个 u8：type | sailable << 4
        湿日（type ≥ 2）的雨量：u16（0.1 mm），先全部低字节再全部高字节
        风暴日（type 风暴 / 暴风雪）的风暴号：u8
        N·D 个 u16 的 zigzag(当天 − 前一天)：气温（0.1 °C）、风从哪来（度，整数、0–360 原样）、风速（0.1 m/s）——各一列，每列先低字节再高字节；每条第 0 天减 0
    单群的（游戏按需生成的，tools/zhouzhu_gen.py 调 write_weather）写成同格式、只有一条的 lods/<节点>.wx。

config.tsv —— 生成器展平后的配置（行星计划 P6e：游戏进程内的 C++ 生成器新建世界、生成岛群都读它，不经 Python）：
    run 的 config.resolved.toml 经生成器自己的展平函数（engine.planet_config、island/engine.flat_config——C++ 核心库的 Config 就是这张扁平键值表）
    UTF-8，# 开头是注释，一行一条：表<TAB>类型<TAB>键<TAB>值
        表：planet（行星层 ①–⑨ 读的 shared / skeleton / s01–s09 / 槽位表 / 手工特征表）、island（第三层的 [island] 段）、engine（[engine]）
        类型：num（数；布尔写 1.0 / 0.0）、vec（逗号分隔的数，可空）、str（字符串；反斜杠、制表符、换行按 C 的写法转义）
        数写成 Python 的 repr（读回与 Python 的浮点逐位相同），次序照展平时的次序
    --config-only：只写这一份（仓库里的 seed42 行星包就这么补上，别的文件不动）。需要能 import 生成器（--generator 或 run 的上上级）。
"""

import json
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

VERSION = 1
WEATHER_VERSION = 1
DTYPES = {np.dtype(np.float32): 0, np.dtype(np.int32): 1, np.dtype(np.int16): 2, np.dtype(np.uint8): 3}
WEATHER_TYPES = ["晴", "多云", "小雨", "大雨", "云海漫顶", "风暴", "小雪", "大雪", "暴风雪"]  # 生成器 island/weather.py 的 TYPES
STORM_TYPES = (5, 8)


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


def generator_commit(run: Path, generator: Path | None = None) -> str:
    try:
        root = generator if generator is not None else run.parent.parent
        return subprocess.check_output(["git", "-C", str(root), "rev-parse", "--short", "HEAD"], text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "?"


def lod_entries(run: Path, res: float, nodes=None):
    d = run / "islands_lod" / str(int(round(res)))
    if not d.is_dir():
        return
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


def write_lods(path: Path, entries) -> int:
    """entries：[(节点, meta, arr)] → lods.bin 格式写到 path（先写 .tmp 再改名）；返回字节数。"""
    blobs = [encode_lod(meta, arr) for _node, meta, arr in entries]
    index_size = 12 + len(entries) * (4 + 4 * 3 + 2 * 4 + 8 + 4)
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "wb") as f:
        f.write(b"ZZLD" + struct.pack("<II", VERSION, len(entries)))
        off = index_size
        for (node, meta, arr), blob in zip(entries, blobs):
            r = meta["raster"]
            f.write(struct.pack("<I3f4HQI", node, r["res_m"], r["origin_km"][0], r["origin_km"][1], r["rows"], r["cols"],
                                len(meta["islands"]), 0, off, len(blob)))
            off += len(blob)
        for blob in blobs:
            f.write(blob)
    tmp.replace(path)
    return path.stat().st_size


def weather_of(arr) -> dict | None:
    """粗版 npz（或 build_lod 给的 arrays）里的逐日天气：{"meta": weather_meta 的 dict, 列名: 数组}；没带天气返回 None。"""
    if "weather_meta" not in arr or "weather_type" not in arr:
        return None
    wx = {k[len("weather_"):]: np.asarray(arr[k]) for k in arr if k.startswith("weather_") and k != "weather_meta"}
    wx["meta"] = json.loads(str(arr["weather_meta"]))
    return wx


def weather_entries(run: Path, res: float, nodes=None):
    """run 的粗版目录里带天气的群：[(节点, 天气)]（按节点号排）。"""
    out = []
    d = run / "islands_lod" / str(int(round(res)))
    if not d.is_dir():
        return out
    for p in sorted(d.glob("*.npz"), key=lambda p: int(p.stem)):
        node = int(p.stem)
        if nodes is not None and node not in nodes:
            continue
        with np.load(p) as z:
            if "weather_meta" not in z.files:
                continue
            wx = weather_of({k: z[k] for k in z.files if k.startswith("weather_")})
        if wx is not None:
            out.append((node, wx))
    return out


def _zigzag(a):
    a = np.asarray(a, dtype=np.int64)
    return np.where(a >= 0, 2 * a, -2 * a - 1)


def _u16_planes(a) -> bytes:
    a = np.asarray(a, dtype=np.int64)
    assert a.size == 0 or (a.min() >= 0 and a.max() < 65536), "超出 u16"
    b = a.astype("<u2").view(np.uint8).reshape(-1, 2)
    return b[:, 0].tobytes() + b[:, 1].tobytes()


def _tenths(a, what):
    v = np.round(np.asarray(a, dtype=np.float64) * 10.0).astype(np.int64)
    assert np.allclose(v / 10.0, np.asarray(a, dtype=np.float64), atol=1e-9), f"{what} 不是 0.1 的整数倍（生成器按 csv 舍过的才能无损存）"
    return v


def write_weather(path: Path, entries, precip_formula=(150.0, 4000.0, 1.3), source: str = "") -> int:
    """entries：[(节点, 天气)]（weather_of 给的）→ weather.bin 格式写到 path（先写 .tmp 再改名）；返回字节数。"""
    import zlib
    entries = sorted(entries, key=lambda e: e[0])
    D = int(entries[0][1]["type"].size) if entries else 336
    season_sets = []
    index = bytearray()
    types, sails, precips, storms, temps, wdirs, wspeeds = [], [], [], [], [], [], []
    for node, wx in entries:
        m = wx["meta"]
        assert list(m["types"]) == WEATHER_TYPES, f"群 {node} 的天气类型表与生成器的不一样"
        assert wx["type"].size == D, f"群 {node} 的天数 {wx['type'].size} ≠ {D}"
        names = ",".join(m["season_names"])
        if names not in season_sets:
            season_sets.append(names)
        cl = m["climate"]
        index += struct.pack("<IBBH7f", node, season_sets.index(names), 0, 0, cl["ref_m"], cl["lapse_c_per_km"], cl["rim_m"],
                             round(float(cl["fog_rise_m"]), 1), cl["snow_temp_c"], float(m["precip_mm"]), float(m["precip_climate_mm"]))
        t = np.asarray(wx["type"], dtype=np.int64)
        types.append(t)
        sails.append(np.asarray(wx["sailable"]).astype(np.int64))
        p = _tenths(wx["precip_mm"], "雨量")
        assert (p[t < 2] == 0).all(), f"群 {node} 晴、多云的日子有雨量"
        precips.append(p[t >= 2])
        st = np.isin(t, STORM_TYPES)
        ev = np.asarray(wx["storm_event"], dtype=np.int64)
        assert (ev[~st] == 0).all() and ev.max(initial=0) < 256, f"群 {node} 的风暴号不在风暴日上或太大"
        storms.append(ev[st])
        temps.append(np.diff(_tenths(wx["temp_c"], "气温"), prepend=0))
        wdirs.append(np.diff(np.asarray(wx["wind_from_deg"], dtype=np.int64), prepend=0))
        wspeeds.append(np.diff(_tenths(wx["wind_ms"], "风速"), prepend=0))
    cat = lambda xs: np.concatenate(xs) if xs else np.zeros(0, np.int64)
    raw = (np.asarray(cat(types) | (cat(sails) << 4), dtype=np.uint8).tobytes() + _u16_planes(cat(precips)) +
           np.asarray(cat(storms), dtype=np.uint8).tobytes() + _u16_planes(_zigzag(cat(temps))) + _u16_planes(_zigzag(cat(wdirs))) +
           _u16_planes(_zigzag(cat(wspeeds))))
    packed = zlib.compress(raw, 9)
    cal = entries[0][1]["meta"]["calendar"] if entries else {"seasons": 4, "months_per_season": 3, "days_per_month": 28, "day_offset_solstice_n": 126}
    year = entries[0][1]["meta"].get("year", 0) if entries else 0
    head = [
        "types " + ",".join(WEATHER_TYPES),
        f"calendar {int(cal['seasons'])} {int(cal['months_per_season'])} {int(round(float(cal['days_per_month'])))} {float(cal['day_offset_solstice_n']):g}",
        f"year {int(year)}",
        f"precip_formula {precip_formula[0]:g} {precip_formula[1]:g} {precip_formula[2]:g}",
    ] + [f"season_names {k} {s}" for k, s in enumerate(season_sets)] + ([f"source {source}"] if source else [])
    head_b = ("\n".join(head) + "\n").encode("utf-8")
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "wb") as f:
        f.write(b"ZZWX" + struct.pack("<6I", WEATHER_VERSION, D, len(entries), len(head_b), len(packed), len(raw)))
        f.write(head_b)
        f.write(bytes(index))
        f.write(packed)
    tmp.replace(path)
    return path.stat().st_size


def precip_formula_of(run: Path):
    """run 的 [island.climate] precip_mm_min / max / exp（④ 的相对降水折成年降水 mm，生成器 hydro.precip_mm）。
    （P6e 前读的是 [island] 顶层的同名键——那里没有，一直落到默认值；默认值与 [island.climate] 的一样，写出来的不变。）"""
    try:
        import tomllib
        cfg = tomllib.loads((run / "config.resolved.toml").read_text(encoding="utf-8"))
        isl = cfg.get("island", {})
        cl = isl.get("climate", {}) if isinstance(isl.get("climate"), dict) else {}
        pick = lambda k, d: float(cl.get(k, isl.get(k, d)))
        return (0.0, float(cfg.get("s04", {}).get("climate", {}).get("precip_mm_ref", 4000.0)), 1.0)
    except Exception:
        return (0.0, 4000.0, 1.0)


def config_tables(run: Path, generator: Path | None = None) -> dict:
    """run 的配置按生成器自己的展平函数展开：{"planet": …, "island": …, "engine": …}，各是 {"num": {}, "vec": {}, "str": {}}（config.tsv 的内容）。"""
    root = generator if generator is not None else run.parent.parent
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))
    from skyisle_gen import engine as E
    from skyisle_gen import island as isl
    from skyisle_gen.cli import _ctx_from_run
    from skyisle_gen.island.engine import flat_config
    ctx = _ctx_from_run(str(run))
    c = isl.island_config(ctx, [])      # default.toml ← run 的快照（同 island 生成时读的）；顺带把 [engine] 也合好
    return {"planet": E.planet_config(ctx.cfg), "island": {**flat_config(c), "str": {}}, "engine": E.flatten(ctx.cfg.get("engine", {}))}


def _esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def write_config(path: Path, tables: dict) -> int:
    """config.tsv（格式见文件头）；先写 .tmp 再改名。返回字节数。"""
    lines = ["# 生成器展平后的配置（行星计划 P6e；tools/export_planet.py 导出，格式见那个脚本开头），游戏进程内生成读它，别手改。",
             "# 表\t类型\t键\t值"]
    for t, d in tables.items():
        for k, v in d.get("num", {}).items():
            lines.append(f"{t}\tnum\t{_esc(k)}\t{float(v)!r}")
        for k, v in d.get("vec", {}).items():
            lines.append(f"{t}\tvec\t{_esc(k)}\t{','.join(repr(float(x)) for x in v)}")
        for k, v in d.get("str", {}).items():
            lines.append(f"{t}\tstr\t{_esc(k)}\t{_esc(str(v))}")
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    tmp.replace(path)
    return path.stat().st_size


def compare_fine(entries, fine_dir: Path) -> None:
    """有细版的群：weather.tsv 与粗版的逐日天气比（类型逐日相同的比例、各列一样不一样）。"""
    by_node = dict(entries)
    for d in sorted(p for p in fine_dir.iterdir() if (p / "weather.tsv").exists() and p.name.isdigit()):
        node = int(d.name)
        if node not in by_node:
            print(f"  细版 {node}：行星包里没有它的逐日天气")
            continue
        wx = by_node[node]
        rows = [ln.split("\t") for ln in (d / "weather.tsv").read_text(encoding="utf-8").splitlines() if ln and not ln.startswith("#")]
        n = min(len(rows), wx["type"].size)
        same_type = sum(1 for k in range(n) if rows[k][3] == WEATHER_TYPES[int(wx["type"][k])])
        same_all = sum(1 for k in range(n) if rows[k][1:8] == [str(float(wx["precip_mm"][k])), str(float(wx["temp_c"][k])), WEATHER_TYPES[int(wx["type"][k])],
                                                              str(int(wx["wind_from_deg"][k])), str(float(wx["wind_ms"][k])), "1" if wx["sailable"][k] else "0",
                                                              str(int(wx["storm_event"][k]))])
        print(f"  细版 {node}：类型逐日相同 {same_type}/{n}（{100.0 * same_type / max(1, n):.1f}%），整行相同 {same_all}/{n}")


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    opt = {"--lod-res": "2000", "--near": None, "--near-res": "500", "--near-radius": "600", "--generator": None, "--fine": None, "--config-only": None}
    i = 2
    while i < len(args):
        if args[i] == "--config-only":
            opt["--config-only"] = True
            i += 1
            continue
        opt[args[i]] = args[i + 1]
        i += 2
    if opt["--config-only"]:
        dst = Path(args[1])
        dst.mkdir(parents=True, exist_ok=True)
        n = write_config(dst / "config.tsv", config_tables(Path(args[0]), Path(opt["--generator"]) if opt["--generator"] else None))
        print(f"{dst / 'config.tsv'}: {n / 1e3:.1f} kB")
        return 0
    return export_planet(Path(args[0]), Path(args[1]), opt)


def export_planet(run: Path, dst: Path, opt: dict) -> int:
    opt = {"--lod-res": "2000", "--near": None, "--near-res": "500", "--near-radius": "600", "--generator": None, "--fine": None, **opt}
    dst.mkdir(parents=True, exist_ok=True)
    # 生成器展平后的配置（P6e：游戏进程内生成读它）；import 不了生成器（只装了 numpy）就不写，照旧能导其余的
    try:
        write_config(dst / "config.tsv", config_tables(run, Path(opt["--generator"]) if opt["--generator"] else None))
    except ImportError as e:
        print(f"没写 config.tsv（import 不了生成器：{e}）")

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
    extent = np.full(N, np.nan, np.float32)
    constrained = np.zeros(N, np.uint8)
    lod_res = [float(x) for x in opt["--lod-res"].split(",")]
    for node, meta, arr in lod_entries(run, lod_res[0]):
        main = meta["islands"][0]
        peak[node], rim[node] = main["peak_m"], main["rim_m"]
        t = meta.get("territory", {})
        viol[node] = t.get("violation_km", -1e3)
        constrained[node] = 1 if t.get("constrained") else 0
        # 陆地铺多远：群心到最远的陆地格心（km）——行星天气场按它定一群的天气管多大一片（P5）
        r = meta["raster"]
        rr, cc = np.nonzero(arr["land"] > 0)
        if rr.size:
            res_km = float(r["res_m"]) / 1000.0
            x = r["origin_km"][0] + (cc + 0.5) * res_km
            y = r["origin_km"][1] - (rr + 0.5) * res_km
            extent[node] = float(np.sqrt(x * x + y * y).max())
    columns.append(("lod.peak_m", peak))
    columns.append(("lod.rim_m", rim))
    columns.append(("lod.violation_km", viol))       # 势力范围：陆地越过与邻群分界线多少 km（≤ 0 没越界）
    columns.append(("lod.constrained", constrained))  # 1 = 布局因势力范围重摆过
    columns.append(("lod.extent_km", extent))        # 群心到最远的陆地格心（km，粗版量的；没有粗版为 NaN）

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

    commit = generator_commit(run, Path(opt["--generator"]) if opt["--generator"] else None)
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
    vertical = planet.get("vertical")
    if vertical:
        head.append("vertical " + " ".join(str(float(vertical[k])) for k in ("sea_level_m", "cloud_base_m", "cloud_top_m", "keel_gap_m", "datum_pressure_atm", "scale_height_km", "o2_fraction")))
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
    write_lods(dst / "lods.bin", entries)

    # ---- 逐日天气（行星计划 P5）：粗版顺带出的第 0 年（与细版 weather.tsv 逐行相同）
    wx_entries = weather_entries(run, lod_res[0])
    write_weather(dst / "weather.bin", wx_entries, precip_formula_of(run), f"{run.name} {commit}")

    missing = int(np.isnan(peak).sum())
    mb = lambda p: (dst / p).stat().st_size / 1e6
    print(f"{dst / 'planet.bin'}: {N} 群、{len(columns)} 列、{len(grids)} 张场，{mb('planet.bin'):.1f} MB；生成器 {commit}")
    print(f"{dst / 'lods.bin'}: {len(entries)} 条粗版（{', '.join(f'{int(r)} m' for r in lod_res)}"
          f"{'；近邻 ' + opt['--near-res'] + ' m' if opt['--near'] is not None else ''}），{mb('lods.bin'):.1f} MB；没有粗版的群 {missing}")
    print(f"{dst / 'weather.bin'}: {len(wx_entries)} 群的逐日天气，{mb('weather.bin'):.1f} MB；没有天气的群 {N - len(wx_entries)}"
          f"；合计 {sum(mb(p) for p in ('planet.bin', 'lods.bin', 'weather.bin')):.1f} MB")
    if opt["--fine"]:
        compare_fine(wx_entries, Path(opt["--fine"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
