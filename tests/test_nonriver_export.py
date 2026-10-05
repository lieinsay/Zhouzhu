"""隔离检查 Python 导出端的地貌二进制契约，不生成游戏世界。"""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import export_skyisle as exporter


class LandscapeExport(unittest.TestCase):
    def test_source_fields_and_missing_field_rejection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            src, dst = root / "source", root / "game"
            src.mkdir()
            shape, n = (5, 7), 35
            zero = np.zeros(shape, dtype=np.uint8)
            data = dict(height=np.full(shape, 100.0), island_id=np.zeros(shape, dtype=np.int32),
                        flowacc_km2=np.arange(1, n + 1).reshape(shape), landcover=zero + 2,
                        stream=zero, w_ch_m=zero, d_ch_m=zero, terrain_zone=zero + 4,
                        res_field=np.arange(7*n, dtype=np.uint8).reshape(7, *shape),
                        patch_id=np.full(shape, -1), coast_dist_m=np.full(shape, 2.375),
                        lith=zero + 3, rockwall_dir=zero + 255, rockwall_m=zero.astype(float))
            info = dict(raster=dict(res_m=20, rows=5, cols=7, origin_km=[0, 0]),
                        meta=dict(run="fixture", node=1, seed=42, generator="fixture", cls_zh="测试", lat=0, lon=0),
                        islands=[], links=[], landcover=dict(classes=[], palette=[]),
                        landforms=[dict(kind="fixture", area_km2=0.002)])
            (src / "island.json").write_text(json.dumps(info), encoding="utf-8")
            (src / "settlements.json").write_text("{}", encoding="utf-8")
            (src / "resources.json").write_text(json.dumps(dict(fields=dict(kinds=exporter.FIELD_KINDS))), encoding="utf-8")
            np.savez(src / "terrain.npz", **data)
            with patch.object(exporter, "write_weather"), patch.object(exporter, "write_resources"), contextlib.redirect_stdout(io.StringIO()):
                exporter.export(src, dst)
                binary = (dst / "terrain.bin").read_bytes()
                self.assertEqual(struct.unpack_from("<III", binary, 4), (8, 5, 7))
                self.assertEqual(len(binary), 40 + n * 36)
                np.testing.assert_array_equal(np.frombuffer(binary, "<f4", n, 40 + n*4), data["coast_dist_m"].ravel())
                np.testing.assert_array_equal(np.frombuffer(binary, "u1", n, 40 + n*30), data["lith"].ravel())
                np.testing.assert_array_equal(np.frombuffer(binary, "u1", n, 40 + n*31), data["rockwall_dir"].ravel())
                np.testing.assert_array_equal(np.frombuffer(binary, "<f4", n, 40 + n*32), data["rockwall_m"].ravel())
                self.assertEqual(json.loads((dst / "world.json").read_text(encoding="utf-8"))["landscape"]["landforms"], info["landforms"])
                del data["lith"]
                np.savez(src / "terrain.npz", **data)
                with self.assertRaisesRegex(ValueError, "地貌字段不完整"):
                    exporter.export(src, dst)
                self.assertEqual((dst / "terrain.bin").read_bytes(), binary)


if __name__ == "__main__":
    unittest.main()
