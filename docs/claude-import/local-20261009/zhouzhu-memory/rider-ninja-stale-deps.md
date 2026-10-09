---
name: rider-ninja-stale-deps
description: "Rider 的 cmake-build-debug（Ninja）出现「只改头文件不重编」导致链接失败时，查 ninja -t deps 的 #deps 0 与 .ninja_deps 里重复的路径记录；别和 Rider 同时构建这个目录"
metadata:
  node_type: memory
  type: project
  originSessionId: 544aca92-32b6-4f23-a9d9-2b323fdedbd4
  modified: 2026-09-25T16:27:59.919Z
---

2026-09-26 家里：切片十把 `Inventory::count` 改成虚函数后，Rider 链接失败——`villagers.cpp.obj` 还引用旧的非虚 `Inventory::count`（LNK2019）。`build/`（VS 生成器）一直编得过。

原因两层：
1. `cmake-build-debug` 里 1051 个 .obj（48 个扩展 + 1003 个 godot-cpp）是 2026-09-24 首建时编的，Ninja 没给它们记下任何头文件依赖（`ninja -t deps` 显示 `#deps 0`），只改头文件时永远不重编。多半是那次构建的控制台代码页不对，/showIncludes 的中文前缀没对上（rules.ninja 里 msvc_deps_prefix 是 GBK 的「注意: 包含文件:」）。Rider 自己后来编的 .obj 依赖都正常。
2. 我在命令行修它时 Rider 也在构建：两个 Ninja 同时往 `.ninja_deps` 追加，出现重复的路径记录（编号对不上），之后每次读都报 `premature end of file; recovering`，后面几百条依赖丢掉。

修法：确认没有 ninja / cl 进程 → 删掉 `cmake-build-debug/.ninja_deps` → 用 vcvars64 的 cmd 单独 `cmake --build cmake-build-debug` 全量重编（约 1100 个 .obj）→ `ninja -t deps` 应当没有 `#deps 0`、没有 premature。

**Why:** 这个坑的症状像代码问题（链接找不到符号），其实是构建缓存的依赖记录坏了。
**How to apply:** Rider 报链接找不到刚改过签名的符号、而 `build/` 编得过时，先查 deps；在命令行构建 `cmake-build-debug` 前先确认 Rider 没在构建（命令行验证优先用 `build/`）。命令行进 vcvars 要写成 .bat 用 `cmd //c` 跑。相关 [[dev-machines]]、[[godot-rider-debug-setup]]。
