---
name: planet-generator-split-out
description: "The planet generator left the Zhouzhu repo on 2026-09-20 and is now its own repo, 空岛行星生成器 / skyisle-gen, at D:\\Project\\My\\skyisle-gen"
metadata: 
  node_type: memory
  type: project
  originSessionId: fc3a931f-efbf-481d-8e59-191d767f5ba4
  modified: 2026-09-27T17:38:54.688Z
---

On 2026-09-20 the planet & civilization generator was moved out of the Zhouzhu monorepo
(`generator/`) into a standalone git repo, then renamed: **空岛行星生成器 / `skyisle-gen`**,
at `D:\Project\My\skyisle-gen`. It carries all 53 original commits (`git subtree split`).
Zhouzhu commit `712e31d` removed `generator/`; in the new repo `22a0f34` is the split-out
commit and `e23d84a` the rename.

Names now: package `skyisle_gen` (was `zhouzhu_gen`), CLI `skyisle` (was `zhouzhu`), console
title 「空岛 · 行星操作台」. `docs/spec/` holds snapshots of the two specs it depends on
(`11-世界总图.md`, `12-扩散模型.md`, taken at Zhouzhu `7c499bc`) — snapshots, not synced. The
other upstream docs (01 铁律, 02 世界与地理, 04 社会, 07 核心循环, 08 地区设计规程) stayed in
Zhouzhu; `docs/0X-…` paths in this repo's docs mean that repo, and capitalized "Zhouzhu" in the
text is a deliberate reference to it. The 719M `out/` (seed7 / seed42 / seed2026) came along,
still gitignored.

Remote: `origin` = `git@github.com:lieinsay/skyisle-gen.git`, private, pushed 2026-09-20 (55
commits). SSH to GitHub authenticates as `lieinsay` without a proxy; `gh` is NOT installed here
and GitHub cannot create repos over SSH, so a new repo has to be made in the browser first. ME Pro still has the old install
(`~/.venvs/zhouzhu`, `~/.local/bin/zhouzhu`, `ZHOUZHU-DEV-ENV` in `~/.bashrc`) and needs
reinstalling under the new name; `/srv/git/zhouzhu.git` is still there as of 2026-09-20.

Branches: day-to-day work is on `island-rivers-resources-settle`; the standalone clone has no
local `main`. On 2026-09-28 the user asked to bring GitHub `main` up to date — fast-forwarded
`e23d84a..8a2f12b` via `git push origin island-rivers-resources-settle:main` (no force). From
then on `main` always follows on every push — see [[feedback-skyisle-main-follows]].
In Zhouzhu it is the submodule `extern/skyisle-gen` (remotes `origin` + `local` → the standalone
clone). `submodule update` leaves it on a detached HEAD, which TortoiseGit's log shows as "no
branch"; `git checkout island-rivers-resources-settle` + `merge --ff-only` fixes that without
changing the gitlink. Stale TortoiseGit overlays (no green check) → kill `TGitCache.exe`.

**Why:** the user judged Zhouzhu full of uncontrolled design and wants to restart the game side,
keeping the generator as the one finished result — so it must not depend on Zhouzhu's churn,
nor keep its name.

**How to apply:** generator work happens in `skyisle-gen`, not in Zhouzhu. Run it per
[[zhouzhu-console-utf8]]. Game-side decisions in [[zhouzhu-core-architecture]] are about the
repo being restarted, not this one.
