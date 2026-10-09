---
name: zhouzhu-extend-ship-3d
description: "Extension landed 2026-09-22 on slice-2-farming (9 commits d8a3060..fmap, NOT pushed): ship layout is a 3D grid (x/y/z, ≤12), refit happens in an in-world assembly view (RefitOverlay), ladders between levels, berth/cradle are contact lines; non-obvious ladder/e2e lessons"
metadata: 
  node_type: memory
  type: project
  originSessionId: 42a1b4ee-9cc3-4e18-8cdc-2b67c94bda88
  modified: 2026-09-22T03:33:36.077Z
---

On 2026-09-22 the "ship extends up/down/left/right + designed 3D-assembly refit UI" extension landed on
branch **`slice-2-farming`** right after slice 3 ([[zhouzhu-slice3-shipyard]]): 9 topical commits
`d8a3060`..`dd08e21` + fmap commit, **not pushed**. Mellos page `extend-ship-3d`; fmap event e5
(extend of `ship-layout`, new node `refit-overlay`, chain `shipyard` rewritten).

What changed (conventions the next ship work builds on):
- `ship_layout` is a 3D grid: x∈[-1,1], y∈[0,2], z∈[-3,3], ≤12 modules, ids `"x,y,z:kind"`. Rules and
  Chinese refusal reasons live there; Voyage/panel only relay them. Gate row (z=0) must be the widest and its
  end columns height 1 (gangway meets deck). Ladders are derived (`layout_ladders`), candidates too.
- Covered modules are closed cabins; top modules are decks. Ladders = plank + rungs + `Area3D` meta "ladder",
  collision layer 8, local +Z = outward from the wall, offset `LADDER_ALONG = -2` along the wall so it clears
  the helm post and crates.
- Player ladder rule: facing the wall W = up, back-to-wall W = down; when airborne on a ladder the outward
  horizontal component is cancelled (otherwise the player drifts off mid-climb); standing on the top deck and
  wanting down = ordinary walking off the rail gap.
- Berth and cradle are **contact lines** (`Transform3D`, X toward pier/gangway); `Ship::place_at(contact,
  keep_heading)` puts the facing edge on the line, so any beam docks. Yard 44×20 m, berth half-length 22 m.
- Assembly view: `RefitOverlay` (orbit camera, ghost candidates, cursor box yellow/red/white, Label3D
  船头/船尾/左舷/右舷), `RefitPanel` redesigned. Keys: W/S z, A/D x, Space/Shift y, 1/2/3 kind, E place/set,
  X remove, Tab close, mouse orbit.

Non-obvious lessons:
- E2E waypoint following must check height too, or a player hanging on a ladder "reaches" the waypoint
  under their feet and stops pressing forward (hovers forever, since holding still on a ladder is intended).
- Windowed screenshot runs: `$TMP` in Git Bash is `C:\Users\15357\AppData\Local\Temp`, NOT the scratchpad;
  pass the scratchpad path explicitly to `--shots=` or the images you read are stale.
- Two-module default ship has no cabins/ladders; the e2e's six-section two-level layout is what exercises them.

**Why:** floating shipyard, cargo hold contents and refit costs are the likely follow-ups and all sit on the
3D layout + contact-line conventions.

**How to apply:** verify with `ctest` (302) + four headless e2e scenes (recipe in [[zhouzhu-slice1-voyage]]);
`shipyard_e2e` windowed with `-- --shots=<dir>` gives REFIT_open / REFIT_stacked / LADDER_top screenshots.
