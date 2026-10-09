---
name: commit-without-asking
description: "Standing permission in Zhouzhu: commit finished, verified work on the working branch without asking first"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 54809177-546a-4165-9380-e93b6cee2f78
  modified: 2026-09-21T10:29:25.750Z
---

In the Zhouzhu repo, commit on my own once work is done and verified — do not end a task with an
uncommitted tree and a "shall I commit?" question. Still split by topic ([[commit-split-by-topic]]).

**Why:** user said (2026-09-21)「不需要经过我的同意，能提交的时候，你提交即可，已经通过分支来管理项目了，就别被提交卡着」—
branches already isolate the risk, so waiting on approval only stalls things.

**How to apply:** this covers local commits. Since 2026-09-26 all work happens directly on main ([[work-on-main]]), so these
are commits on main. It was not said about pushing or force operations — those still need an explicit ask.
