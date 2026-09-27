"""州渚 × skyisle-gen 的生成包装（行星计划 P4，docs/PLAN-PLANET.md 第 4.7、4.8 节）：游戏在开发机上按需调生成器（它的 Python 前端，默认 C++ 后端）。
游戏只经这一个脚本调生成器（计划第八节的退路：统一经一个包装脚本调用、PYTHONUTF8=1、超时就杀）；P6e 换成进程内的 C++ 核心时，换掉的就是这一层。

    python -X utf8 tools/zhouzhu_gen.py group --generator <skyisle-gen 根> --run <run 目录> --node N --out <岛群包目录>
                                             [--lod-dir <粗版目录>] --status <状态文件>
    python -X utf8 tools/zhouzhu_gen.py lods  --generator <根> --run <run 目录> --nodes a,b,c --lod-dir <粗版目录> --status <状态文件>
    python -X utf8 tools/zhouzhu_gen.py world --generator <根> --seed N --world <世界目录> --status <状态文件>

group：生成器第三层（与 skyisle island <节点> 同一份 island.generate，只是不画预览图）→ tools/export_skyisle.py 的五份文件；
       先写到 <out>.tmp、写好 stamp.json 再改名成 <out>（游戏读不到半截的包）。
       --lod-dir 给了就顺带出这一群 2 km 的粗版（与 skyisle island lod 同一份 build_lod）→ <lod-dir>/<节点>.bin（lods.bin 同格式、一条），
       与它第 0 年的逐日天气 → <lod-dir>/<节点>.wx（weather.bin 同格式、一条；行星计划 P5 的行星天气场读它）。
       stamp.json 是缓存键（计划 4.7）：生成器提交号、run 的 config.resolved.toml 哈希、[island] 段哈希、导出版本（terrain.bin 版本 + 导出脚本哈希）。
       同一个世界里已经生成过的不因换版本重生成——游戏只读它、记下来；要强制重生成就删掉那一群的目录。
lods：一批群的粗版（各带 .wx 天气），照给的次序（游戏按由近及远排好）一群一群做，写好一份报一次进度；做不出来的记进状态、接着做下一群。
world：新建世界（生存模式，计划 4.8、D15）：
       行星层 ①–⑨（skyisle run --upto 9，不出图）→ <world>/gen/seed<N>/；
       tools/export_planet.py → <world>/planet/{planet.bin, lods.bin}（这时没有粗版：lods.bin 0 条，远处先画替身）；
       出生的岛群 = 生成器 ⑨ 开局候选 A（边陲小邦）第一个邦的都城（D1：世界设计由生成器定；没有就挑人口最多的中疏群）→ 同 group，外加它的粗版；
       最后写 <world>/world.json（种子、出生群、生成器版本、run 与行星包的相对路径）。

状态文件：一行一个 JSON，只追加——{"stage": 名, "progress": 0..1, "text": 说明}；做完一行 {"done": true, ...}，出错一行 {"error": 原因}。
游戏每 0.2 s 读一次，只认以换行结尾的最后一行（没写完的那半行不认）。生成器自己的输出与出错时的 traceback 进 <状态文件>.log。
依赖同生成器（numpy；导出另要 scipy）。游戏运行时不需要它：发布版没有 Python（P6e）。
"""

from __future__ import annotations

import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import time
import traceback
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.dont_write_bytecode = True  # import 本仓库的导出脚本时别在 tools/ 下留 __pycache__
LOD_RES = 2000.0  # 游戏里按需生成的粗版只要 2 km 一档（行星包里全行星也是 2 km，计划 D11）

_status_file = None


def status(**fields) -> None:
    fields.setdefault("time", round(time.time(), 3))
    _status_file.write(json.dumps(fields, ensure_ascii=False) + "\n")
    _status_file.flush()


def progress(stage: str, value: float, text: str, **extra) -> None:
    status(stage=stage, progress=round(max(0.0, min(1.0, value)), 3), text=text, **extra)
    print(f"[{stage}] {value:.2f} {text}", flush=True)


def load_generator(root: Path):
    """把 skyisle-gen 与本仓库的 tools 放上 sys.path（生成器按它的约定在仓库根下跑）。"""
    for p in (str(root), str(TOOLS)):
        if p not in sys.path:
            sys.path.insert(0, p)


def sha1_bytes(b: bytes) -> str:
    return hashlib.sha1(b).hexdigest()


def generator_version(root: Path) -> dict:
    def git(*args):
        try:
            return subprocess.check_output(["git", "-C", str(root), *args], text=True, stderr=subprocess.DEVNULL).strip()
        except Exception:
            return None
    commit = git("rev-parse", "--short", "HEAD") or "?"
    dirty = git("status", "--porcelain", "--untracked-files=no")
    return {"root": str(root), "commit": commit, "dirty": bool(dirty)}


def run_keys(run: Path) -> dict:
    """run 的配置哈希（计划 4.7）：config.resolved.toml 整份、其中 [island] 段（按键排好的 JSON）。"""
    import tomllib
    raw = (run / "config.resolved.toml").read_bytes()
    cfg = tomllib.loads(raw.decode("utf-8"))
    island = json.dumps(cfg.get("island", {}), sort_keys=True, ensure_ascii=False, default=str).encode("utf-8")
    manifest = json.loads((run / "manifest.json").read_text(encoding="utf-8")) if (run / "manifest.json").exists() else {}
    return {"dir": str(run), "seed": manifest.get("seed"), "config_sha1": sha1_bytes(raw), "island_sha1": sha1_bytes(island)}


def engine_backend(ctx) -> str:
    try:
        from skyisle_gen.island.engine import backend
        return str(backend(ctx))
    except Exception:
        return "?"


def export_version() -> dict:
    import export_skyisle
    return {"terrain_version": export_skyisle.VERSION, "script_sha1": sha1_bytes((TOOLS / "export_skyisle.py").read_bytes())}


def write_lod_file(ctx, node: int, lod_dir: Path, run: Path | None = None) -> float:
    """一群 2 km 的粗版 → <lod_dir>/<节点>.bin（lods.bin 同格式、一条）；顺带出的第 0 年逐日天气 → <lod_dir>/<节点>.wx
    （weather.bin 同格式、一条，行星计划 P5：生存世界的行星天气场也以各群当天的天气为真值）。返回用时。"""
    from skyisle_gen import island as isl
    from skyisle_gen.island.lod import build_lod
    import export_planet
    t0 = time.perf_counter()
    c = isl.island_config(ctx, [])
    built = build_lod(ctx, node, c, [LOD_RES], weather=True)
    arr, meta = built[float(LOD_RES)]
    lod_dir.mkdir(parents=True, exist_ok=True)
    wx = export_planet.weather_of(arr)
    if wx is not None:
        # 天气先写：游戏看到 <节点>.bin 就去读 .wx，别让它读到半截
        export_planet.write_weather(lod_dir / f"{node}.wx", [(node, wx)], export_planet.precip_formula_of(run) if run else (150.0, 4000.0, 1.3))
    export_planet.write_lods(lod_dir / f"{node}.bin", [(node, meta, arr)])
    return time.perf_counter() - t0


def make_group(ctx, gen_root: Path, run: Path, node: int, out: Path, lod_dir: Path | None, p0: float = 0.0, p1: float = 1.0) -> dict:
    """生成一个岛群、导出成游戏的包；返回用时。进度在 [p0, p1] 里报。"""
    from skyisle_gen import island as isl
    import export_skyisle
    span = p1 - p0
    seconds = {}
    t0 = time.perf_counter()
    progress("generate", p0 + 0.05 * span, f"生成岛群 #{node}：布局、地形、水系、气候、天气、聚落、资源")
    g = isl.generate(ctx, node, write=False, log=print)
    seconds["generate"] = round(time.perf_counter() - t0, 2)
    # 产物：与 generate(write=True) 写的同一套（不画预览图），写到包旁边的临时目录，导出完就删
    t1 = time.perf_counter()
    progress("write", p0 + 0.45 * span, f"岛群 #{node}：写生成器的产物")
    raw = out.parent / f".raw_{node}"
    shutil.rmtree(raw, ignore_errors=True)
    from skyisle_gen.island.output import write_terrain
    write_terrain(raw, g)
    if "climate" in g:
        from skyisle_gen.island.climate import write_climate
        write_climate(raw, g, 0)
    if "settle" in g:
        from skyisle_gen.island.output import write_settlements
        write_settlements(raw, g)
    if "resources" in g:
        from skyisle_gen.island.resources import write_resources
        write_resources(raw, g)
    seconds["write"] = round(time.perf_counter() - t1, 2)
    t2 = time.perf_counter()
    progress("export", p0 + 0.6 * span, f"岛群 #{node}：导出游戏的包（terrain.bin、world.json、sites.tsv、weather.tsv、resources.tsv）")
    tmp = out.parent / f"{out.name}.tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    export_skyisle.export(raw, tmp)
    seconds["export"] = round(time.perf_counter() - t2, 2)
    if lod_dir is not None:
        progress("lod", p0 + 0.85 * span, f"岛群 #{node}：2 km 粗版")
        seconds["lod"] = round(write_lod_file(ctx, node, lod_dir, run), 2)
    stamp = {
        "node": node,
        "created": datetime.datetime.now().isoformat(timespec="seconds"),
        "generator": {**generator_version(gen_root), "backend": engine_backend(ctx)},
        "run": run_keys(run),
        "export": export_version(),
        "seconds": seconds,
    }
    (tmp / "stamp.json").write_text(json.dumps(stamp, ensure_ascii=False, indent=1), encoding="utf-8")
    shutil.rmtree(out, ignore_errors=True)
    tmp.replace(out)
    shutil.rmtree(raw, ignore_errors=True)
    seconds["total"] = round(time.perf_counter() - t0, 2)
    return seconds


def ctx_of(run: Path):
    from skyisle_gen.cli import _ctx_from_run
    return _ctx_from_run(str(run))


def cmd_group(a: dict) -> dict:
    gen_root, run, node = Path(a["--generator"]), Path(a["--run"]), int(a["--node"])
    load_generator(gen_root)
    ctx = ctx_of(run)
    lod_dir = Path(a["--lod-dir"]) if a.get("--lod-dir") else None
    out = Path(a["--out"])
    out.parent.mkdir(parents=True, exist_ok=True)
    seconds = make_group(ctx, gen_root, run, node, out, lod_dir)
    return {"node": node, "out": str(out), "seconds": seconds}


def cmd_lods(a: dict) -> dict:
    gen_root, run = Path(a["--generator"]), Path(a["--run"])
    load_generator(gen_root)
    ctx = ctx_of(run)
    lod_dir = Path(a["--lod-dir"])
    nodes = [int(x) for x in a["--nodes"].split(",") if x.strip()]
    done, failed = [], []
    for k, node in enumerate(nodes):
        progress("lod", k / max(1, len(nodes)), f"粗版 {k + 1} / {len(nodes)}：群 #{node}")
        try:
            sec = write_lod_file(ctx, node, lod_dir, run)
            done.append(node)
            status(stage="lod", progress=round((k + 1) / len(nodes), 3), text=f"群 #{node} 的粗版好了（{sec:.1f} s）", lod_done=node)
        except Exception as e:
            traceback.print_exc()
            failed.append(node)
            status(stage="lod", progress=round((k + 1) / len(nodes), 3), text=f"群 #{node} 的粗版做不出来：{e}", lod_failed=node)
    return {"done": done, "failed": failed}


def pick_spawn(run: Path) -> tuple[int, str]:
    """出生的岛群（生存模式）：⑨ 开局候选 A（边陲小邦）第一个邦的都城；没有就挑人口最多的中疏群。返回（节点，理由）。"""
    import numpy as np
    pol = json.loads((run / "s09_polity" / "polities.json").read_text(encoding="utf-8"))
    states = pol.get("polities", [])
    for s in pol.get("openings", {}).get("A_frontier_small_state", []) or []:
        if 0 <= int(s) < len(states) and int(states[int(s)].get("capital", -1)) >= 0:
            st = states[int(s)]
            return int(st["capital"]), f"开局候选 A（边陲小邦）{st.get('name', s)} 的都城"
    isl = np.load(run / "s03_islands" / "islands.npz")
    z = np.load(run / "s09_polity" / "polity.npz")
    pop = np.where(isl["cls"] == 1, z["pop"], -1)
    return int(pop.argmax()), "人口最多的中疏群"


def cmd_world(a: dict) -> dict:
    gen_root, seed, world = Path(a["--generator"]), int(a["--seed"]), Path(a["--world"])
    load_generator(gen_root)
    world.mkdir(parents=True, exist_ok=True)
    times = {}
    t0 = time.perf_counter()
    progress("planet", 0.02, f"种子 {seed}：行星层 ①–⑨（地形、风、岛群分布、气候、航路、文明、政治）")
    from skyisle_gen.config import load_config
    from skyisle_gen.pipeline import run as pipeline_run
    cfg = load_config([], [])
    run = Path(pipeline_run(cfg, seed, world / "gen", upto=9))
    times["planet"] = round(time.perf_counter() - t0, 2)
    t1 = time.perf_counter()
    progress("export_planet", 0.45, "导出行星包（planet.bin；还没有粗版，远处先画替身）")
    import export_planet
    export_planet.export_planet(run, world / "planet", {"--generator": str(gen_root)})
    times["export_planet"] = round(time.perf_counter() - t1, 2)
    spawn, why = pick_spawn(run)
    progress("spawn", 0.55, f"出生的岛群：#{spawn}（{why}）")
    ctx = ctx_of(run)
    seconds = make_group(ctx, gen_root, run, spawn, world / "groups" / str(spawn), world / "lods", 0.55, 0.98)
    times["spawn_group"] = seconds.get("total")
    info = {
        "name": world.name,
        "mode": "survival",
        "seed": seed,
        "spawn": spawn,
        "spawn_reason": why,
        "run": run.relative_to(world).as_posix(),
        "planet": "planet",
        "generator": generator_version(gen_root),
        "config_sha1": run_keys(run)["config_sha1"],
        "created": datetime.datetime.now().isoformat(timespec="seconds"),
        "seconds": times,
    }
    (world / "world.json").write_text(json.dumps(info, ensure_ascii=False, indent=1), encoding="utf-8")
    times["total"] = round(time.perf_counter() - t0, 2)
    return {"world": str(world), "seed": seed, "spawn": spawn, "seconds": times}


def main(argv: list[str]) -> int:
    global _status_file
    if len(argv) < 1 or argv[0] not in ("group", "lods", "world"):
        print(__doc__)
        return 2
    cmd = argv[0]
    a = {}
    for i in range(1, len(argv) - 1, 2):
        a[argv[i]] = argv[i + 1]
    status_path = Path(a.get("--status") or "zhouzhu_gen_status.jsonl")
    status_path.parent.mkdir(parents=True, exist_ok=True)
    _status_file = open(status_path, "a", encoding="utf-8")
    log = open(str(status_path) + ".log", "a", encoding="utf-8")
    sys.stdout = log
    sys.stderr = log
    print(f"==== {datetime.datetime.now().isoformat(timespec='seconds')} {' '.join(argv)}", flush=True)
    try:
        result = {"group": cmd_group, "lods": cmd_lods, "world": cmd_world}[cmd](a)
        status(done=True, progress=1.0, text="好了", result=result)
        print(f"好了：{json.dumps(result, ensure_ascii=False)}", flush=True)
        return 0
    except Exception as e:
        traceback.print_exc()
        status(error=f"{type(e).__name__}: {e}", text=f"出错：{e}")
        return 1
    finally:
        log.flush()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
