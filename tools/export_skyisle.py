"""把 skyisle-gen 的一个岛群（第三层产物）转成游戏资产。

    python tools/export_skyisle.py <岛群目录> <输出目录>
    python tools/export_skyisle.py D:/Project/My/skyisle-gen/out/seed42/islands/2051 game/worlds/2051
    python tools/export_skyisle.py --sites game/worlds/2051      # 只按已有的 world.json 重写 sites.tsv

读 terrain.npz / island.json / settlements.json，写三份文件：

terrain.bin —— 栅格，小端，行主序（行号随 z 增大，即自北向南）：
    char[4]  "ZZWG"
    u32      版本 = 1
    u32      rows, cols
    f64      res_m（格距）
    f64      x0, z0（格 (0, 0) 格心的游戏坐标，米）
    u16[rows·cols]  高程：云带顶以上的米数 × 100；虚空格填最近陆地格的值（插值到岸外不跳变）
    i16[rows·cols]  岸距：到岸线的有符号距离（米），陆地为正、虚空为负
    u8 [rows·cols]  最近的岛号（陆地即本岛；虚空取最近陆地格的岛）
    u8 [rows·cols]  地表类（island.json landcover.classes 的下标，0 = 虚空）
    u8 [rows·cols]  1 = 溪涧

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

VERSION = 1
HEIGHT_SCALE = 100.0


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
