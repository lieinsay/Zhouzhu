---
name: skyisle-env-numpy
description: "skyisle-gen Python env — since 2026-09-30 use the dedicated venv %LOCALAPPDATA%\\venvs\\skyisle (numpy 2.5.2); the shared Python312 keeps numpy 1.26.4 and must not be touched"
metadata:
  node_type: memory
  type: project
  originSessionId: 8d8baaf7-c084-46cc-a18e-fc7eea98cd9d
  modified: 2026-09-30T12:00:00.000Z
---

Since 2026-09-30 skyisle-gen runs from its own venv `%LOCALAPPDATA%\venvs\skyisle` (Python 3.12 base, numpy **2.5.2**, matplotlib 3.11, nanobind 3.1.0, `pip install -e .`); full pytest 253/253 passes there. The shared `%LOCALAPPDATA%\Programs\Python\Python312` still has numpy **1.26.4** (2024 original, next to opencv/pandas/scipy) — running skyisle tests with it gives ~19 python-vs-cpp parity failures (p6c/p6d/sculpt_island), which are environmental. ME Pro uses `~/.venvs/skyisle` (numpy 2.5.2) — see skyisle-gen CLAUDE.md and DESIGN-NOTES 四点四十一.

**Why:** the C++ core mirrors numpy 2.5.2 bit-for-bit; the shared Python's numpy may be relied on by other projects, so it is left alone.

**How to apply:** always use the venv interpreter for skyisle-gen; if parity tests fail, first check which python ran them. A background pytest holds `_core.pyd` open — rebuilding then fails with LNK1104. Related: [[zhouzhu-console-utf8]], [[planet-generator-split-out]].
