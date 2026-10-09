---
name: zhouzhu-extend-scale
description: "2026-09-22 rescale pass on slice-2-farming (in main and pushed since 2026-09-22): buildings 11×6.5 etc., ship cells 7×7×3.8, yard 60×28 with keel blocks; the user's then-rule that buildings/ship stay non-enterable props (overturned by extend-interiors and extend-ship-cabins); e2e routing lessons around the yard"
metadata: 
  node_type: memory
  type: project
  originSessionId: a5c83887-4766-4777-b167-28525ed22772
  modified: 2026-09-22T07:13:45.464Z
---

On 2026-09-22 the user said「建筑比例不合理，然后先做不能使用，玩家不能进去，包括船的比例和其模块也是同理」right after slice 4
([[zhouzhu-slice4-settlement]]). I read it as: proportions relative to the 1.8 m player were wrong (screenshots showed hut-sized
houses and a cramped deck), fix them; buildings and ship modules stay **decorative props — no interiors, not usable, not enterable**
for now. Landed on `slice-2-farming` as topical commits; in main and pushed since 2026-09-22. Mellos page `extend-scale`; fmap events extend `buildings` /
`ship-layout`.

What changed (the numbers the next work must respect):
- `src/buildings.cpp`: 民居 11×6.5 (plinth 0.5, wall 3.3, roof 2.6), 堂屋 17×9.5, 仓 6×6 (plinth 1.8), 望楼 6×6 (wall 12).
  Settlement/BuildOverlay: plinth margin 0.6, eaves 0.9, door 1.3×2.5. Rule: "按人的尺度定、比真实略大一成" — real-scale looks
  like huts in third person.
- `ship_layout.h`: CELL 7×7×3.8, BOW_M 3.4. `ship.cpp`: hull 5.2×2.8, deck 0.7, rail 1.15, helm aft 1.7, seat aft 2.9,
  ladder along −2.8, masts 7.2, sails 4.6×4.2, chase camera 26/stern+16.
- `island_field`: BERTH_HALF_LENGTH 30, yard 60×28. `shipyard`: CRADLE_LIFT 4.2, CONTACT_V 0 (ship seaward, walkway landward),
  **7 keel blocks** (0.8×6 m, pitch 7 m, ±21) instead of two continuous beams, land gantry posts at v=12.5 (outside the gangway
  foot), gantry 20 m high, gangway run 8.4.
- `island_river.cpp`: trail earthwork release is (2, 7) m from the pad so the pad's 2 m rim stays graded (test_yard asserts it).

Non-obvious lessons:
- Continuous keel beams (or blocks reaching the pad ends) block anyone walking from the apron onto the pad; e2e bots push
  head-on into a face and never slide. Keep the near-end 9 m free, keep blocks seaward of the walkway, and route e2e walks
  via `bench + cradle.basis.x * 6` (the landward side) both ways.
- A wider berth scan lengthens the pier, so respawn_e2e's "near the dock" check now measures distance to the spawn point.
- A wider house makes "on the apron" probes fall off the jagged shore: probe 6 m further inland (build_e2e TO_APRON phase) and
  pick the "too close" offset by surveying four directions first.
- SceneTree `_process` scripts must not `await` (a coroutine return quits the loop); screenshot on a later frame instead.

**Why:** any further building/ship work (interiors, cargo, bridges) starts from these dimensions, and the user explicitly
deferred usability/enterability.

**How to apply:** verify with `ctest` (339) + five headless e2e; `seed_villages.gd` re-lays the villages when sizes change.

2026-09-23 / 09-28：「只做摆设、进不去」已被 [[zhouzhu-extend-interiors]]（房子能进去）和 [[zhouzhu-extend-ship-cabins]]（船舱能进去）推翻；上面的尺寸是当时的数字，改之前先看代码。
