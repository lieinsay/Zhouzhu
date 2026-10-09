---
name: zhouzhu-slice4-settlement
description: "Slice 4 (developer build mode: press B, place buildings on islands, Settlement node + game/settlements/*.txt layouts) landed 2026-09-22 on slice-2-farming, NOT pushed; conventions, seed tool, and terrain lessons (village belongs on the far bank)"
metadata: 
  node_type: memory
  type: project
  originSessionId: a5c83887-4766-4777-b167-28525ed22772
  modified: 2026-09-22T06:37:52.447Z
---

On 2026-09-22 slice 4 of Zhouzhu landed on branch **`slice-2-farming`** right after the ship-3D extension
([[zhouzhu-extend-ship-3d]]), as topical commits, **not pushed**. Mellos page `slice-settlement`; fmap chain `settlement`.

Pieces: `src/buildings.*` (pure table: house 民居 6×5, hall 堂屋 10×7, granary 仓 4.5×4.5, tower 望楼 4×4; plinth/wall/roof
heights), `src/settlement_model.*` (pure: `Building{kind,x,z,yaw}` in island-local coords, `survey_site` with 9 sample points,
refusal order wet → dock → yard → trail → overlap → steep (rise > `SiteRules::max_rise_m` = 2.5), text form `kind@x,z,yaw°`,
functions prefixed `settlement_*` because `ship_layout.h` already owns `layout_from_ids` etc.), `Settlement` node
(StaticBody3D child of Island; `layout_path` → `game/settlements/<island>.txt`, geometry = plinth + walls + PrismMesh gable roof
+ door, collision shapes must be direct children of the body), `BuildOverlay` (orbit camera around the cursor, ghost building
green/red/white) + `BuildPanel` (draw-only), build-mode orchestration in `Voyage` (`dev_tools` property; B/F1 open, WASD camera-
relative cursor, Q/R 15° turns, 1–4 kind, E place, X remove, F5 save, Tab close), `game/tools/seed_villages.gd` (SceneTree
script that lays out a starter village with `find_site`), `game/tests/build_e2e.*`.

Non-obvious lessons:
- Building yaw convention: local +x → (cos yaw, −sin yaw), local +z (door side) → (sin yaw, cos yaw); matches Godot
  `Basis(Vector3(0,1,0), yaw)`. `_facing(dir) = atan2(dir.x, dir.z)` gives the yaw whose door faces `dir`.
- On IslandA the trail end's own bank is a 2–3 m strip squeezed between the river, the trail margin and the yard pad /
  escarpment; the wide buildable plateau is the **far (west) bank**, so the village sits across the shallow river (no bridge yet,
  water has no collision). A text probe (`survey()` on a 2 m grid printed as characters) settled this in one run — do that
  before guessing anchors again.
- Godot shore masks are jagged: ground starts ~5 m inland of `dock.x/z`, so unit tests probing "the apron" go 14 m inland.
- e2e steering of the build cursor: read `get_build_camera_yaw()`, forward = (−sin, 0, −cos), right = (cos, 0, −sin), press
  W/S/A/D by dot products; the cursor snaps to a 0.5 m grid so compare the snapped cursor to a grid-aligned `find_site` result.
- `Input.action_press` scripts need 4 released frames between taps or `is_action_just_pressed` misses them (same as before).

**Why:** the next settlement work (bridges/paths between houses, NPCs living in them, farm plots respecting buildings,
persistence) builds on these conventions.

**How to apply:** verify with `ctest` (339) + five headless e2e scenes (recipe in [[zhouzhu-slice1-voyage]]);
`build_e2e` windowed with `-- --shots=<dir>` gives BUILD_ghost / BUILD_tower / BUILD_blocked screenshots. Re-run
`Godot --headless --path game --script res://tools/seed_villages.gd` when terrain changes make the shipped village fail
`survey_building` (build_e2e checks that at the end).
