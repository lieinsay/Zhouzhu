"""岛群剖面体检：地形、地表、河网、哪些岛有人（PLAN-LAND 的基线与验收都用它）。

先生成岛群、量河网（在仓库根目录、Git Bash 里；OUT 换成草稿目录）：

    OUT=<草稿目录>; NODES=6615,6610,6329,6073,6072,5766,5498
    build/Debug/zhouzhu_gen.exe group --config game/planet/seed42/config.tsv --seed 42 --nodes $NODES --out-root "$OUT/groups"
    for n in ${NODES//,/ }; do
        build/Debug/zhouzhu_site_survey.exe "$OUT/groups/$n" --rivers "$OUT/riv_$n.tsv" > "$OUT/survey_$n.txt"
    done
    PYTHONUTF8=1 python docs/planet-probes/transect.py "$OUT" $NODES

仓库里带的岛群（game/worlds/<节点>）也能量：把 terrain.bin、world.json 拷进 "$OUT/groups/<节点>/" 再照上面跑 site_survey。
Release 版的 site_survey 要是报「terrain.bin 版本不对」，就用 Debug 版。
"""
import collections
import csv
import json
import re
import struct
import sys

import numpy as np

LC = ["虚空", "崖缘", "裸岩", "高山草甸", "林地", "灌丛", "草坡", "可耕地", "梯田", "湿地", "河道", "湖"]


def read_terrain(path):
    raw = open(path, "rb").read()
    assert raw[:4] == b"ZZWG", path
    ver, rows, cols = struct.unpack_from("<III", raw, 4)
    assert ver == 5, f"terrain.bin 版本 {ver}，本脚本按 5 读"
    res, _x0, _z0 = struct.unpack_from("<ddd", raw, 16)
    n = rows * cols
    o = 40
    h = np.frombuffer(raw, "<f4", n, o).reshape(rows, cols); o += 4 * n
    shore = np.frombuffer(raw, "<i2", n, o).reshape(rows, cols); o += 2 * n
    isl = np.frombuffer(raw, "u1", n, o).reshape(rows, cols); o += n
    lc = np.frombuffer(raw, "u1", n, o).reshape(rows, cols)
    return res, h, shore, isl, lc


def main():
    out, nodes = sys.argv[1], sys.argv[2].split(",")
    for n in nodes:
        g = f"{out}/groups/{n}"
        W = json.load(open(f"{g}/world.json", encoding="utf-8"))
        res, h, shore, isl, lc = read_terrain(f"{g}/terrain.bin")
        land = shore >= 0
        gy, gx = np.gradient(h.astype(np.float64), res)
        slope = np.degrees(np.arctan(np.hypot(gx, gy)))
        main_isl = land & (isl == 0)

        def frac(mask, cond):
            return (mask & cond).sum() / max(1, mask.sum())

        I = W["islands"]
        m = I[0]
        sv = open(f"{out}/survey_{n}.txt", encoding="utf-8").read()
        rmin = float(re.search(r"径流指数一年里 ([\d.]+)\.\.", sv).group(1))
        rain = re.search(r"年降水 (\d+) mm", sv).group(1)
        src = W["source"]
        print(f"=== #{n}  {src['cls_zh']}  北纬 {src['lat']:.1f} 东经 {src['lon']:.1f}  {rain} mm  岛 {len(I)} 座  陆地 {land.sum() * res * res / 1e6:.0f} km²  人口 {W['population']:.0f}  村 {len(W['villages'])}")
        print(f"  主岛 {m['area_km2']:.0f} km²  岸缘 {m['rim_m']:.0f}  台面 {m['surface_m']:.0f}  峰 {m['peak_m']:.0f}  起伏 {m['peak_m'] - m['rim_m']:.0f} m  岛龄 {m['age']}")
        print(f"  主岛坡 <2° {frac(main_isl, slope < 2):.0%}  <5° {frac(main_isl, slope < 5):.0%}  >15° {frac(main_isl, slope > 15):.0%}   全群 <2° {frac(land, slope < 2):.0%}")
        above = h - m["rim_m"]
        print(f"  主岛离岸缘高 <100 m {frac(main_isl, above < 100):.0%}  <300 m {frac(main_isl, above < 300):.0%}")
        cnt = collections.Counter(lc[land].tolist())
        tot = land.sum()
        print("  地表（全群）", "  ".join(f"{LC[k]} {v / tot:.1%}" for k, v in sorted(cnt.items(), key=lambda kv: -kv[1]) if v / tot >= 0.002))

        # 河网：site_survey --rivers 的逐点表；常年有水 = 渗漏门槛低于一年里最小的径流指数
        stems = collections.defaultdict(list)
        for r in csv.DictReader(open(f"{out}/riv_{n}.tsv", encoding="utf-8"), delimiter="\t"):
            stems[int(r["stem"])].append((float(r["s"]), float(r["width"]), float(r["acc_km2"]), float(r["dry"]), int(r["mouth"])))
        L = Lw20 = Lw40 = Lper = 0.0
        outs = []
        for pts in stems.values():
            for a, b in zip(pts, pts[1:]):
                d = b[0] - a[0]
                L += d
                Lw20 += d if b[1] >= 20 else 0.0
                Lw40 += d if b[1] >= 40 else 0.0
                Lper += d if b[3] < rmin else 0.0
            if pts[-1][4] == 1:
                outs.append((pts[-1][2], pts[-1][1]))
        outs.sort(reverse=True)
        print(f"  河 {len(stems)} 条  出岸缘 {len(outs)}  共 {L / 1000:.0f} km（每 km² 陆地 {L / 1000 / max(1.0, tot * res * res / 1e6):.2f} km）  宽≥20 m {Lw20 / 1000:.0f} km  宽≥40 m {Lw40 / 1000:.0f} km  常年有水 {Lper / max(L, 1):.0%}")
        print("  最大的出口 汇水 km² / 宽 m：", "  ".join(f"{a:.0f}/{w:.0f}" for a, w in outs[:8]))
        print(f"  汇水 ≥100 km² 的出口 {sum(1 for a, _ in outs if a >= 100)} 个  ≥300 km² {sum(1 for a, _ in outs if a >= 300)} 个")

        # 哪些岛有人（主岛除外）：村（名字不带专业前缀的）、专业聚落（有 special）、散户、前哨
        farm = collections.defaultdict(int)
        spec = collections.defaultdict(int)
        hamlet = collections.defaultdict(int)
        for v in W["villages"]:
            (spec if v.get("special") else farm)[v["island"]] += 1
        for v in W.get("hamlets", []):
            hamlet[v["island"]] += 1
        print("  小岛（主岛除外）  大小          座数  有人住  有村  只有专业聚落")
        for lo, hi in ((0, 10), (10, 30), (30, 100), (100, 1e9)):
            ids = [i["id"] for i in I if not i.get("main") and lo <= i["area_km2"] < hi]
            if not ids:
                continue
            k = len(ids)
            lived = sum(1 for i in ids if farm[i] or spec[i] or hamlet[i])
            vil = sum(1 for i in ids if farm[i])
            only = sum(1 for i in ids if spec[i] and not farm[i] and not hamlet[i])
            label = f"{lo:g}–{hi:g} km²" if hi < 1e9 else f"> {lo:g} km²"
            print(f"                    {label:<12}{k:>5}  {lived / k:>5.0%}  {vil / k:>4.0%}  {only / k:>6.0%}")
        print(f"  索桥 {sum(1 for l in W['links'] if l.get('kind') == 'bridge')}  桥头 {len(W.get('bridgeheads', []))}")


if __name__ == "__main__":
    main()
