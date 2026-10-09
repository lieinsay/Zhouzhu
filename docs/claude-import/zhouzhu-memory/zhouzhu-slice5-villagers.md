---
name: zhouzhu-slice5-villagers
description: "Slice 5 (NPC villagers: clock, nav grid A*, jobs farmer/carrier/watchman/idler, Villagers node) landed 2026-09-22 on slice-2-farming, 7 commits 1ebfbe0..b570bd2, in main and pushed since 2026-09-22; conventions and lessons"
metadata: 
  node_type: memory
  type: project
  originSessionId: 5e43d6b8-6930-47d8-83d3-655cf1c23aa5
  modified: 2026-09-22T08:02:50.904Z
---

On 2026-09-22 the user asked「场景缺少npc，先支持能在场景中按照一定规则移动，工作，即可」. Landed on `slice-2-farming`
as 7 topical commits `1ebfbe0..b570bd2`; in main and pushed since 2026-09-22. Mellos page `slice-villagers`; fmap chain `villagers`.

Pieces: `src/villager_model.*` (pure: NavGrid 1.25 m, max_grade 0.6, building margin 0.9, water cost ×4, A* + greedy
line-of-sight pull; DayRules day 1200 s, work 6–18.5, lunch 11.5–12.5; `assign_residents`; `decide_task`), `src/villagers.*`
(`Villagers` node, child of Island **after Settlement** so its `rebuilt` handler runs after the settlement rebuilds; owns all
state, village store, claims), `Farm::get_model()` / `Settlement::get_model()` read-only getters, Voyage HUD clock,
`game/tests/villager_e2e.*` (headless ~5 s).

Non-obvious:
- The seeded village on IslandA sits right on the river bank (doors face the river, ~1 m of dry strip), so "fetch water and
  carry home" was a 1 m jitter — carriers now deliver to the **hall** (堂屋). Villagers wade along the shallow river between doors.
- Claims with owner −1 mark unreachable wild plots so farmers don't re-pick them forever.
- GDScript: `Farm.State.keys()` fails to parse for GDExtension enums; a parse error makes the headless test hang forever —
  always wrap Godot runs in `timeout`.
- `std::vector<int> order(size_t(N))` is a most-vexing-parse in MSVC.

**Why:** next NPC work (talking to player, collisions, interiors, day/night lighting, saving) builds on these.

**How to apply:** verify with ctest (391) + six headless e2e; windowed `villager_e2e -- --shots=<dir>` gives
VILLAGE_work / farmer / carrier / night. Known gaps: no NPC collision, no dialogue, no lighting for night, not saved,
HUD「田 N 块」counts villagers' plots too. See [[zhouzhu-slice4-settlement]].
