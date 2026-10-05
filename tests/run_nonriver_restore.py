"""核对保留的旧包与当前生成器导出；仅在临时目录写配置，不改游戏数据。

使用能导入生成器依赖的 Python：
    python tests/run_nonriver_restore.py cmake-build-debug/zhouzhu_core_tests.exe
"""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "extern/skyisle-gen"), str(ROOT / "tools")]
from skyisle_gen.config import load_config
from skyisle_gen.engine import flatten, planet_config
from skyisle_gen.island.engine import flat_config
from export_planet import write_config

if __name__ == "__main__":
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    executable = Path(sys.argv[1]).resolve()
    config = load_config()
    with tempfile.TemporaryDirectory(prefix="zhouzhu-nonriver-") as temporary:
        path = Path(temporary) / "config.tsv"
        write_config(path, {"planet": planet_config(config), "island": flat_config(config["island"]), "engine": flatten(config.get("engine", {}))})
        env = dict(os.environ, ZHOUZHU_RESTORE_CONFIG=str(path))
        selected = sys.argv[2:] or ["nonriver_restore"]
        result = subprocess.run([str(executable), *selected], cwd=ROOT, env=env, timeout=300)
    raise SystemExit(result.returncode)
