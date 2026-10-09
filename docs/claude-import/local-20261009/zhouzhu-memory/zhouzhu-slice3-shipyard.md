---
name: zhouzhu-slice3-shipyard
description: "Slice 3 (ground shipyard: fly ship into cradle, refit it into bridge/storage/power sections) landed 2026-09-22 on slice-2-farming, 8 topical commits 440ed3f..HEAD, NOT pushed; ship origin = boarding gate; yard pad carved in island_field; non-obvious e2e/terrain lessons"
metadata: 
  node_type: memory
  type: project
  originSessionId: 42a1b4ee-9cc3-4e18-8cdc-2b67c94bda88
  modified: 2026-09-22T02:36:34.681Z
---

On 2026-09-22 slice 3 of Zhouzhu landed on branch **`slice-2-farming`** (same branch as slice 2 and the
backpack extension, see [[zhouzhu-slice2-farming]]): 8 topical commits `440ed3f`..fmap commit, **not pushed**
as of that date. Mellos page `slice-shipyard`; fmap chain `shipyard` (event e4).

Pieces: `src/ship_layout.*` (pure: 驾驶区/储存区/动力区 modules, 1..5 sections, exactly one bridge, stats
from thrust/mass, edit rules with refusal reasons), `IslandYard` in `src/island_field.*` (36×16 m pad beside the
apron, 6 m from the dock centreline, on whichever side needs less inland depth; river/trail avoid it),
`Ship` rebuilt from a `layout` PackedStringArray property (`game/ship.tscn` is now just root + Visual + CameraRig),
`Shipyard` node (StaticBody3D child of Island: keel beams, gantries, gangway with collision, bench),
`RefitPanel` (draw-only), refit/docking orchestration in `Voyage`, `game/tests/shipyard_e2e.*`.

Key design decisions worth remembering:
- **The ship's origin is the boarding gate (舷门, z ∈ ±0.9 on both sides)**; sections are laid with
  `count/2` sections forward of it. Berth / cradle / gangway all target the gate, so ship length changes
  never move docks. Deck furniture keeps the centreline aisle (|x| < 0.85) clear so straight walks to the helm work.
- Refit edits apply live to the Ship (no draft); rules live in `ship_layout`, Voyage only relays reasons.
- No cost for refitting yet; hold capacity is only a number; layouts don't persist; the user asked for the
  ground shipyard first ("先只做地面上的"), so a floating/sky shipyard is a likely follow-up.

Non-obvious lessons:
- Walking from the yard straight to the pier end walks off the cliff: e2e routes must go via the spawn point.
- The gangway plank is a 0.7 m wall near its foot; the bench had to go on the dock side of it (BENCH_U = −4).
- `String + "literal"` is ambiguous in godot-cpp; wrap the literal in `String(...)`.
- A `Basis(u, up, v)` with a mirrored side is left-handed and renders boxes inside-out; use `u.cross(up)` for z.

**Why:** the next ship/yard work (sky shipyard, cargo hold, refit costs) builds on these conventions.

**How to apply:** verify with `ctest` + four headless e2e scenes (recipe in [[zhouzhu-slice1-voyage]]);
windowed `-- --shots=<dir>` run of shipyard_e2e gives usable screenshots of the yard and modular ship.
