---
name: commit-split-by-topic
description: "User wants commits split into clear topical commits, never one commit with the whole diff"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 31357e2f-f3d8-4394-881c-0f12198c2402
  modified: 2026-09-17T09:07:32.800Z
---

Split work into several clear commits by topic (e.g. UI rework / generator code+config+tests / world docs sync / new design doc), each with a Chinese message in the repo's `generator：…` / `docs：…` style. When one file mixes two topics, stage a topic-only version via `git hash-object -w` + `git update-index --cacheinfo` (interactive `add -p` is unavailable).

**Why:** user said「每个提交要比较清晰，别一个commit包括所有的diff」(2026-09-17).
**How to apply:** never use `git commit -a` when other unrelated changes are in the tree — it once swept world docs into a one-line comment commit and had to be undone with `reset --soft`. Stage explicit paths, check `git diff --cached --stat` before each commit.
