---
name: zhouzhu-export-package
description: 打 Windows 游戏包（分享用）的配方：Release DLL 在 build-release/、导出模板只装了 windows_release、导出版开局 boot.tscn 直进剧情 seed42、非资源数据靠 include_filter
metadata:
  node_type: memory
  type: project
  originSessionId: 77ffe3aa-7e6d-4586-ba3a-1a3a0fbad9a2
  modified: 2026-09-30T12:43:25.441Z
---

2026-09-30 用户要「打出一个游戏包分享」。做法（家里机器）：

- **Release DLL**：`cmake -B build-release -G "Visual Studio 17 2022" -A x64 -DGODOTCPP_TARGET=template_release -DGODOTCPP_USE_STATIC_CPP=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`
  （静态 CRT，不然 DLL 依赖 MSVCP140/VCRUNTIME140，没装 VC++ 运行库的机器加载不了；godot-cpp 的 linux.cmake 先把同名开关登记成 OFF，所以得显式给；验：dumpbin /dependents 只剩 KERNEL32），
  `cmake --build build-release --config Release --target zhouzhu --parallel`（Git Bash 里别写 `-- /m`，会被转成 `M:/`）→ `game/bin/zhouzhu.windows.template_release.x86_64.dll`。
- **导出模板**：`%APPDATA%\Godot\export_templates\4.4.1.stable\` 里只有 `windows_release_x86_64.exe` + `version.txt`，是按 HTTP Range 从 1.2 GB 的 tpz 里只抽出来的（33 MB）；要别的平台得另下。
- **导出**：`Godot_v4.4.1-stable_win64_console.exe --headless --path game --export-release "Windows Desktop" D:/Project/Zhouzhu/export/Zhouzhu/Zhouzhu.exe`（预设在 `game/export_presets.cfg`，`export/` 被 gitignore）。
  产物 exe 97 MB + pck 140 MB + DLL 5 MB，zip 约 92 MB。
- `.txt/.tsv/.bin/.json` 不是 Godot 资源，靠预设的 include_filter 才进 pck；tests/、tools/ 排除。
- **开局**：project.godot 的 `run/main_scene.template="res://boot.tscn"`，只在导出版生效：boot.gd 调 DeveloperTools.switch_to 进 world_seed42（剧情、存档 user://worlds/seed42）。编辑器 / Rider 跑照旧是 main.tscn（甲乙两岛老图）。
- **导出版里 res:// 在 pck 里**：`globalize_path("res://…")` 再走 DirAccess/std::ifstream 会失效（GroupProvider::available_nodes 就踩过，已改）。新写读 res:// 的代码一律经 FileAccess/DirAccess 直接用 res:// 路径。
- 验导出版：exe 旁放 `override.cfg`（`config/use_custom_user_dir=true`、`custom_user_dir_name="ZhouzhuExportTest"`）隔开存档，`--headless --quit-after N` 看 `%APPDATA%\ZhouzhuExportTest\logs\godot.log`；验完删 override.cfg。

**Why:** 开发者工具（F2 等）都还在包里；布置模式 F5、房屋模板另存会写 res://，导出版里写不了（只在开发时用）。

**How to apply:** 以后再打包照上面重来（先重编 Release DLL）；别开窗口验，见 [[feedback-generator-before-game]]。
