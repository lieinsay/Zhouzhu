"""Python 显式后端的打包阶段复用一次生成给出的 LOD 和天气，不另跑岛群。"""
import io
from pathlib import Path
import struct
import sys

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "extern" / "skyisle-gen"))
import zhouzhu_gen as wrapper


@pytest.mark.parametrize("resolutions", [None, [2000.0, 1000.0, 500.0]])
def test_group_export_reuses_native_capture(tmp_path, monkeypatch, resolutions):
    from skyisle_gen import island as isl
    from skyisle_gen.island import output, climate
    import export_skyisle

    requested = resolutions or [2000.0]
    g = {"json": {"raster": {"origin_km": [-2.0, 2.0]},
                  "islands": [{"center_km": [0.0, 0.0], "surface_m": 300.0, "rim_m": 300.0, "keel_m": 0.0, "peak_m": 300.0}],
                  "hydro": {"precip_mm": 1200.0}, "weather": {}},
         "inp": {"lat": 30.0, "lon": 10.0, "lapse_c_per_km": 6.0, "planet": {"axial_tilt_deg": 34.0}},
         "climate": {"season_names": ["春", "夏", "秋", "冬"], "season_type": "four", "season_type_zh": "四季",
                     "annual": {"temp_ref_height_m": 300.0, "precip_mm": 1200.0},
                     "calendar": {"seasons": 4, "months_per_season": 3, "days_per_month": 28.0, "days_per_season": 84.0,
                                  "year_days": 336, "day_offset_solstice_n": 126.0}},
         "weather": {"days": [dict(day=day, season=0, month=1, day_of_month=day + 1, type=kind, precip_mm=rain,
                                   temp_c=18.0, temp_rim_c=18.0, wind_from_deg=90, wind_ms=3.0, sailable=True, storm_event=0)
                                  for day, kind, rain in [(0, "晴", 0.0), (1, "小雨", 2.5)]]},
         "lods": {res: dict(land=np.full((2, 3), 255, np.uint8), peak=np.full((2, 3), 300.0, np.float32),
                            island=np.zeros((2, 3), np.int16), landcover=np.full((2, 3), 3, np.uint8)) for res in requested}}
    calls = []

    def generate(ctx, node, **kwargs):
        calls.append(kwargs)
        assert kwargs["lod_resolutions"] == requested
        return g

    def export(source, target, **kwargs):
        target.mkdir(parents=True)
        (target / "terrain.bin").write_bytes(b"fine fixture")

    monkeypatch.setattr(isl, "generate", generate)
    monkeypatch.setattr(isl, "island_config", lambda *a: {"weather": {"fog_surface_max_m": 500.0, "snow_temp_c": -1.0,
                                                                   "heavy_rain_mm": 25.0, "sail_wind_max_ms": 15.0}})
    monkeypatch.setattr(output, "write_terrain", lambda path, group: path.mkdir(parents=True))
    monkeypatch.setattr(climate, "write_climate", lambda *a: None)
    monkeypatch.setattr(export_skyisle, "export", export)
    monkeypatch.setattr(wrapper, "write_lod_file", lambda *a: pytest.fail("不应为粗版再次生成"))
    monkeypatch.setattr(wrapper, "generator_version", lambda *a: {"commit": "fixture", "dirty": False})
    monkeypatch.setattr(wrapper, "run_keys", lambda *a: {})
    monkeypatch.setattr(wrapper, "_status_file", io.StringIO())
    out, lods = tmp_path / "groups" / "6070", tmp_path / "lods"
    out.parent.mkdir()
    wrapper.make_group(object(), ROOT / "extern" / "skyisle-gen", tmp_path, 6070, out, lods, lod_resolutions=resolutions)
    assert len(calls) == 1 and (out / "terrain.bin").read_bytes() == b"fine fixture"
    assert (out / "stamp.json").is_file()
    bundle = (lods / "6070.bin").read_bytes()
    assert struct.unpack_from("<4sII", bundle) == (b"ZZLD", 1, len(requested))
    assert [struct.unpack_from("<If", bundle, 12 + k * 36) for k in range(len(requested))] == [(6070, res) for res in requested]
    weather = (lods / "6070.wx").read_bytes()
    assert struct.unpack_from("<4sIII", weather) == (b"ZZWX", 1, 2, 1)
    header_size = struct.unpack_from("<I", weather, 16)[0]
    assert struct.unpack_from("<I", weather, 28 + header_size)[0] == 6070
