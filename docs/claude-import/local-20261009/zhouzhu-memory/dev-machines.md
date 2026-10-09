---
name: dev-machines
description: Zhouzhu 的三台开发机（家里 Windows / 公司电脑 10.8.0.2 / ME Pro）各自的路径、工具与连法；从公司电脑同步来的记忆里的路径要换算
metadata:
  type: project
---

2026-09-24 起项目已「重开」：Godot 4.4.1 + C++ GDExtension（CMake、godot-cpp 子模块），旧 Python generator 迁出为 skyisle-gen。
家里旧 main 的 61 个提交留在本地分支 `backup/pre-reset-2026-09-24`。本目录下 zhouzhu-*/commit-*/godot-* 等记忆是 2026-09-24 从公司电脑拷来的。

- **家里 Windows**：仓库 `D:\Project\Zhouzhu`（公司电脑的记忆里写的 `D:\Project\My\Zhouzhu` 在这里要换成这个；skyisle-gen 在 `D:\Project\skyisle-gen`，FeatureMap 在 `D:\Project\featuremap`）。
  VS 2022 Community 在 `D:\Program Files\Microsoft Visual Studio\2022\Community`；CMake 4.4 与 Ninja 用 winget 装；
  Godot 4.4.1 在 `D:\Program Files\Godot\`（`D:\Godot` 的 4.6.2 mono 是别的项目的，别用）；Python 3.12 带 numpy+scipy。
  Rider 2026.2.1 走 Toolbox，装在 `D:\JetBrains\apps\Rider`（`C:\Program Files\JetBrains\Rider` 是残壳）；MSVC 工具链已写进
  `%APPDATA%\JetBrains\Rider2026.2\options\windows\toolchains.xml`。Rider 的 CMake 配置（cmake-build-debug，Ninja + MSVC）已跑通；`.run/zhouzhu.run.xml` 已改成与路径无关（2026-09-24 本地提交）。
- **公司电脑 10.8.0.2**（DESKTOP-F8PI1CS，用户 `desktop-f8pi1cs\15357`，用户主要在这里用 Rider 干活）：仓库 `D:\Project\My\Zhouzhu`，
  Rider 2026.1.3，CMake+Ninja。SSH 只能用密码（家里公钥没装过去），用户愿意时再问密码，别写进记忆。
- **ME Pro**（Debian 13，`ssh liein@10.8.0.12`，先连 WireGuard）：hub `/srv/git/zhouzhu.git`、`/srv/git/featuremap.git`。
- 仓库 remote：`origin`=GitHub `lieinsay/Zhouzhu`，`mepro`=hub。

**Why:** 用户在多台机器间切换开发，要求各机环境与公司电脑一致。

**How to apply:** 构建 `cmake -B build && cmake --build build --config Debug`；验证 `ctest -C Debug`（在 build/）+
`Godot_v4.4.1-stable_win64_console.exe --headless --path game --fixed-fps 60 res://tests/<x>_e2e.tscn`。
相关：[[zhouzhu-mepro-dev]]、[[godot-rider-debug-setup]]、[[user-language]]
