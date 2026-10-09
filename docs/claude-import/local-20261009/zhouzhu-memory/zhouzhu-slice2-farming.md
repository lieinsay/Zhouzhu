---
name: zhouzhu-slice2-farming
description: "Slice 2 (trail to river, hold E to till → sow → harvest) landed 2026-09-21 on slice-2-farming (pushed, not merged); 2026-09-22 extension: seeds chosen from a backpack (Tab), harvest into backpack, crops 粟/菽 — 9 more commits on the same branch, NOT pushed; non-obvious terrain lessons"
metadata: 
  node_type: memory
  type: project
  originSessionId: 54809177-546a-4165-9380-e93b6cee2f78
  modified: 2026-09-21T10:30:26.002Z
---

On 2026-09-21 slice 2 of Zhouzhu was implemented on branch **`slice-2-farming`** (branched from
`slice-1-voyage`, see [[zhouzhu-slice1-voyage]]) and committed as 8 topical commits `6f056df`..`cfb371d`
(the user gave standing permission to commit, see [[commit-without-asking]]); pushed to origin and mepro on 2026-09-21 (together with slice-1-voyage), not merged
into main. Mellos page: `slice-farming`. Feature-map chain: `farming`; `.fmap/map.json` is tracked
(its own `.fmap/.gitignore` keeps backups out), so re-commit it after fmap updates.

New pieces: `src/island_river.*` (river + dock→floodplain trail, pure), `src/farm_model.*` (pure),
`src/farm.*` (`Farm` node, child of `Island`), farming orchestration inside `Voyage`,
`game/tests/farm_e2e.*`. `Island.has_river` turns on both river and trail; IslandB has none.

Extension on 2026-09-22 (same branch, commits `886271f`..`bda6cb5`, **not pushed** as of that date): a backpack
(`src/inventory.*`, pure), item/crop table (`src/crops.*`, pure; 粟 and 菽, seed == product), `Plot.crop`
in the farm model, `BackpackPanel` (draw-only Control), `Player::set_input_blocked`, input action `inventory`
(Tab/I). All state (pack, held seed, panel open/cursor) stays in `Voyage`; `starting_pack` Dictionary property
on the scene. Mellos page: `extend-backpack-seeds`. fmap event e3 = `extend` on `voyage` (child `voyage-pack`).
The e2e picks the SECOND item (菽) on purpose so that "which seed" is actually verified.

Non-obvious things learned:
- The island rim is a ring of 35°+ escarpment on every age class; slice 1 never left the dock apron so
  nobody noticed. Anything the player must reach on foot needs a graded trail (`carve_dock_trail`);
  the first farm e2e got stuck halfway up the slope.
- Carving by "nearest point on a polyline" makes terrain cliffs on the inside of bends; the fix is the
  distance-scaled soft blend in `Polyline::near`. Any earthwork near the dock must fade out via
  `dock_release` or it leaves a step at the apron edge.
- Child `_ready` runs before the parent's: `Farm` waits for the `Island`'s `rebuilt` signal.

**Why:** the next slice builds on these, and the escarpment/trail issue will recur for any new island feature.

**How to apply:** verify with `ctest` + the three headless e2e scenes (recipe in [[zhouzhu-slice1-voyage]]);
for a look at terrain changes, a throwaway scene with a free `Camera3D` and `save_png` works windowed.
When patching files from Bash, write the Python patch script with the Write tool — heredocs containing
`'''` plus double quotes fail to parse in this harness.
