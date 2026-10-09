---
name: feedback-skyisle-main-follows
description: 推送 skyisle-gen 时 GitHub 的 main 一律跟着快进到工作分支，不用再问
metadata:
  node_type: memory
  type: feedback
  originSessionId: 4a21625e-356a-40df-8f1e-d8a8787704f7
  modified: 2026-09-27T17:38:53.089Z
---

每次推送 skyisle-gen（工作分支 `island-rivers-resources-settle`）时，同一次把 GitHub 的 `main` 也推到同一提交：
`git push origin island-rivers-resources-settle island-rivers-resources-settle:main`。

**Why:** 用户 2026-09-28 说「以后 main 都跟着一起推」——之前 `main` 停在改名那次（e23d84a），落后 44 个提交，在小乌龟里看着像没更新。

**How to apply:** 只做快进；推之前先 `git merge-base --is-ancestor origin/main island-rivers-resources-settle`，不是祖先（`main` 上有分支没有的提交）就停下问，别强推。推送本身仍要等用户开口（[[commit-without-asking]]、[[work-on-main]]），这条只管「推的时候 main 一起」。独立仓库没有本地 `main`，不必建。见 [[planet-generator-split-out]]。
