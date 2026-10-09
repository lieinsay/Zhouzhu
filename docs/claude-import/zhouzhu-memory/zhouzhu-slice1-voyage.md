---
name: zhouzhu-slice1-voyage
description: "Slice 1 (walk → board airship → fly island to island → moor) landed 2026-09-21 on branch slice-1-voyage (in main and pushed since 2026-09-22, branch deleted); how to build and verify it from the CLI, and what the user OK'd about skyisle-gen"
metadata: 
  node_type: memory
  type: project
  originSessionId: 83b9b9e4-0d6c-4d82-ae35-a41d86f41294
  modified: 2026-09-21T09:00:23.575Z
---

On 2026-09-21 slice 1 of the restarted Zhouzhu game was implemented and committed as 10 topical
commits on branch **`slice-1-voyage`** (`1ff9b1e`..`a3711d9`, on top of main `9175a74`); merged into main and pushed with
the world-2051 refactor on 2026-09-22, branch since deleted — see [[zhouzhu-refactor-world-2051]]. `.mellos/` is
gitignored by my choice. Contents: C++ classes `Island` / `Player` / `Ship` / `Voyage`,
scenes `game/main.tscn` + `ship.tscn` + `player.tscn`, tests in `tests/` (CTest) and `game/tests/`
(headless Godot). The mellos map page is `slice-voyage`.

The user explicitly said island generation "可以参考 skyisle-gen" — so porting its algorithms into
the game is sanctioned (this supersedes the "don't tie the game to the generator unprompted" note in
[[zhouzhu-repo-restarted]] *for island terrain*). What was done is a game-scale C++ port of
`island/terrain.py` (shape + age-based base form, no fluvial erosion), with **no runtime dependency**
on skyisle-gen or its exports.

Verifying from the CLI (Bash tool):
- Build with `cmake --build build --config Debug` — `build/` is the Visual Studio generator and needs
  no vcvars; Rider's `cmake-build-debug/` (Ninja) does. Both write the same DLL into `game/bin/`, and
  a running game (`Godot --path game`, no editor) presumably holds the DLL — I did not try linking
  against it; when the user had the game open I verified intermediate commits compile-only instead: `MSBuild build/zhouzhu.vcxproj -t:ClCompile -p:Configuration=Debug -p:Platform=x64`.
- `Godot_v4.4.1-stable_win64_console.exe --headless --path game --fixed-fps 60 res://tests/voyage_e2e.tscn`
  runs the whole voyage in ~3 s wall clock; add `-- --shots=<dir>` and drop `--headless` for screenshots.
- Redirect Godot output to a file and print it via Python as UTF-8 (see [[zhouzhu-console-utf8]]).

**Why:** the next slice will build on these pieces, and the verification recipe took several tries.

**How to apply:** extend the e2e drivers rather than testing by hand; in test drivers hold a key for a
few physics frames — press-then-release on consecutive frames is missed by `is_action_just_pressed`.
Vertex colours authored as sRGB need `FLAG_SRGB_VERTEX_COLOR` or the mesh renders washed out.
