# 开发约定

## 原子提交

- 以足够原子、可独立说明和验证的功能或修复为单位；完成并通过相应验证后及时创建本地 Git 提交，不等整个大任务结束，不积攒大量 diff。
- 每次提交只包含该功能相关改动，避免夹带用户或其他工作的改动。推送遵循用户当次授权。
- Git 提交标题和说明正文使用中文；代码符号、路径和必要技术名词可保留原文。

## 必要范围内验证

- 减少非必要测试，只运行与本次改动直接相关、足以验证实际行为的检查；通过后不重复执行，不默认扩大成全量回归。
- 文档、记忆和提交说明等低影响改动，核对内容、引用、结构与 diff 即可，不为此编译整个项目或运行游戏回归。
- 只有新的代码改动、测试失败或尚未排除的具体问题才扩大验证；避免重复截图、重复启动游戏和重复运行已经通过的检查。

## Windows 编译与调试：沿用已成功的 Rider 配置

用户已通过 Rider 编译和调试本项目。后续会话开始编译或排查启动问题前，先读 [Rider 编译调试说明](docs/BUILD-RIDER.md)，核对 `.run/zhouzhu.run.xml` 和 `cmake-build-debug/CMakeCache.txt`。

- 岛群 `terrain.bin` 由 Git LFS 跟踪；首次克隆后按说明拉取子模块与 LFS 数据。若文件只是 `version https://git-lfs...` 指针，先恢复真实数据，不能拿指针文件启动游戏或修改读包代码来迁就它。

- 现有配置：CMake Profile `Debug`，目标 `zhouzhu`，生成器 **Ninja**，工具链 **Visual Studio 2022 MSVC x64**，构建目录 **`cmake-build-debug`**。
- Rider 先构建目标，再以项目根为工作目录运行 `D:/Program Files/Godot/Godot_v4.4.1-stable_win64.exe --path game`；用此配置的 Debug 按钮调试 C++ 扩展。
- 扩展产物是 `game/bin/zhouzhu.windows.template_debug.x86_64.dll`，Godot 按同目录 `zhouzhu.gdextension` 加载；对应 PDB 用于断点调试。
- 命令行验证必须先载入同一 `vcvars64.bat` 环境，并复用该构建目录。不要向现有缓存切换 MinGW、clang、Visual Studio 生成器、运行库或另一套 Godot，也不要拿其他目录的旧产物当作这次结果。
- **MSVC `/showIncludes` 的语言必须与 Ninja 实际记录的 `msvc_deps_prefix` 一致。** 以 `cmake-build-debug/CMakeFiles/rules.ninja` 为准；不能只看 CMakeCache，也不能盲目复制临时 `build/rider-verify.cmd`。目前检测到中文前缀，匹配 `VSLANG=2052`；英文前缀匹配 `VSLANG=1033`。完整核对与修复条件见说明。
- 遇到失败先读构建日志、核对缓存与依赖记录；只有证据确认缓存损坏时才在同一工具链下重配、完整重建。不要把反复换构建方式当作排错。
- 运行测试前先确认对应目标已构建。自动测试崩溃应记录退出码和日志，不启动 Visual Studio 即时调试器；不要终止用户自己的 Rider/Godot 会话来抢占 DLL。

## 数据与场景

- 游戏按生成器提供的几何、水文和时间序列还原；缺失数据要明确记录，不擅自补猜河宽、曲流、岛缘缺口等。
- FeatureMap 按实际任务说明切片、场景重构或拓展，复用已有功能节点，写清前后变化、验证范围和未完成项。
