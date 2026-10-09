---
name: work-on-main
description: Zhouzhu 以后一律直接在 main 上干活，新任务不开功能分支；提交照旧按主题拆、做完验过就提交
metadata:
  node_type: memory
  type: feedback
  originSessionId: ee40d672-b3d8-4335-a3dd-a9417375c584
  modified: 2026-09-26T10:39:42.475Z
---

Zhouzhu 的新工作（切片、拓展、重构）直接在 main 上做、在 main 上提交，不再为每个任务拉 slice-* / extend-* / refactor-* 分支，也就没有「并入 main」这一步。

**Why:** 用户 2026-09-26 说「后面都在main分支工作，没必要每次做新工作都创建新分支」（当天刚把 15 个提交快进并入 main、删光了已并入的功能分支）。

**How to apply:** 开工前确认在 main 上（`git branch --show-current`），别再 `checkout -b`。提交规矩不变：做完并验过就提交（[[commit-without-asking]]），按主题拆（[[commit-split-by-topic]]）。**推送**（origin 与 mepro）仍等用户开口。旧记忆里写的「并入 main 前问用户」已不适用。`backup/pre-reset-2026-09-24` 是推倒重开前的旧历史备份（61 个 main 没有的提交），不是功能分支，别删。
