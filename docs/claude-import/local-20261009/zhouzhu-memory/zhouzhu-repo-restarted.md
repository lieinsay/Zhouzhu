---
name: zhouzhu-repo-restarted
description: On 2026-09-20 the Zhouzhu game repo was wiped and restarted from scratch — only a one-paragraph README remains; the old design is archived on the remotes
metadata: 
  node_type: memory
  type: project
  originSessionId: fc3a931f-efbf-481d-8e59-191d767f5ba4
  modified: 2026-09-20T10:08:15.519Z
---

On 2026-09-20 the user restarted the Zhouzhu game project: "当前项目存在了很多不可控的设计",
so everything was deleted and a fresh git repo was initialized in place at
`D:\Project\My\Zhouzhu` (they asked for the folder to be lowercase `zhouzhu`; the case rename
was blocked because this session's shell held the directory — do it when nothing is rooted
there). First commit `2acb9e5 州渚：重开`, containing **only `README.md`, one paragraph**:
floating-island management / exploration / social game, 宗法分封 → 编户齐民, second son of a
border polity running cargo on a small airship, theme 「便利与失去是同一件事」, no supernatural
element but 浮石. Since then (same day into 2026-09-21) the engine was decided and a skeleton built: **Godot 4.4.1 +
C++ GDExtension** (godot-cpp submodule, branch 4.4), single project, no engine-independent core;
**CMake + Ninja** (SCons dropped for IDE support), Rider as IDE with breakpoints working. Layout:
src/ (C++), game/ (Godot project, 3D Forward+), extern/godot-cpp, .run/ (Rider config).
src/build_info.* is scaffolding to delete once slice 1 lands. HEAD 9175a74. Next step: slice
work, starting in a new session. See [[godot-rider-debug-setup]], [[godot-cpp-chinese-literals]].

Deleted: `docs/` (14 设定文档 incl. DECISIONS with 76 已定项), `core/` (6 底层设计稿, 86 待拍板
口径, zero code), `design/`. All of it is archived as tag **`archive/v1`** (= old main `712e31d`) on both
`origin` = `github.com/lieinsay/Zhouzhu` and `mepro` = `ssh://liein@10.8.0.12/srv/git/zhouzhu.git`.
On 2026-09-21 the new history was force-pushed as `main` to both (with lease on 712e31d), so the
same repo name now carries the restarted game; `git checkout archive/v1` shows the old design.

The user considers the planet generator the one salvaged result and said it has little to do
with this project — do not tie the restarted game to it unprompted. See
[[planet-generator-split-out]].

**Why:** the design had outgrown what could be kept under control — documents referencing each
other with no implementation underneath.

**How to apply:** treat pre-2026-09-20 Zhouzhu design decisions as archived, not current;
[[zhouzhu-core-architecture]] in particular is a record of the discarded version. Ask before
carrying any of it forward.
