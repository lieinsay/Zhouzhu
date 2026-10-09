---
name: zhouzhu-refactor-world-2051
description: "2026-09-22 real-scale island-group scene world_2051.tscn (skyisle node 2051) with streamed quadtree terrain, M map, dev teleport; commits 051d5e3..77ad0fb on slice-2-farming, fast-forwarded into main (77ad0fb) and pushed to origin + mepro on 2026-09-22 — slices 1–5 are in main too; old features not migrated"
metadata: 
  node_type: memory
  type: project
  originSessionId: 24fe8bfb-71bf-4056-bd28-a35dcb92fcbc
  modified: 2026-09-22T09:43:16.810Z
---

On 2026-09-22 the user decided the old islands (7–12 ha) were far too small against skyisle-gen's scale (its smallest islet is 0.3 km²;
node 2051's main island is 1,794 km²) — buildings were fine, the islands were the problem. They asked for a "重构": a real-scale game scene
from generator node 2051 (seed42), a UI map, and developer teleport via the map. Their choices: **new scene, main.tscn and slice 1–5
features untouched (migrate later)**; ground = generator 100 m grid bicubic + deterministic detail noise; data **exported into the repo**
(`tools/export_skyisle.py` → `game/worlds/2051/terrain.bin` + `world.json`), no runtime Python / skyisle out/ dependency.

Landed on `slice-2-farming`, commits 051d5e3..121a58b (incl. README + fmap chain `real-scale-world`); fast-forwarded into main (77ad0fb) and pushed the same day. Mellos page
`refactor-world-2051`.

Non-obvious facts for the next work:
- Coordinates: x east, z south, origin = group centre, y = height above cloud-band top (heights ~100–380 m); skyisle km (x, y-north) → game (x·1000, −y·1000).
- Headless Godot root viewport is tiny: e2e must set `get_tree().root.size` before clicking the map.
- Screenshots need ~3 frames after moving the player / adding tiles, else you capture the previous frame.
- Debug-build tile cost ~13 ms (Release 2.5 ms); ~274 tiles / 2.6 M tris at the seat; 3 worker threads.
- Underside skirts were added but never visually verified (camera can't pitch up; below y −40 the kill-respawn fires).
- Known gaps: ground looks flat, no trees, streams only colour, map lacks hamlets/outposts/bridges, float precision at ±50 km.

Follow-up same day (commits 155924a..77ad0fb): global dev tools — autoload **`DeveloperTools`** (class `DevTools`; an autoload
named the same as its class breaks GDScript typed vars with "Internal script error Opcode 28"). F2 map-switch list, F3 god
(`Player::set_god_mode`, 30 m/s ×5 Shift, Space up / Ctrl-C down), M in main.tscn hints to use F2. Test lessons: events from
`Input.parse_input_event` are handled next frame — assert a few frames later; to test `change_scene_to_file`, the driver adds
main.tscn to root and makes *it* `current_scene`, so the driver survives the switch. Startup scene is still main.tscn.

**Why:** next steps (migrating farm/shipyard/settlement/villagers/voyage onto World) start from these contracts.
**How to apply:** verify with ctest (436) + seven e2e incl. `world_e2e.tscn`; see [[zhouzhu-slice5-villagers]], [[planet-generator-split-out]].
