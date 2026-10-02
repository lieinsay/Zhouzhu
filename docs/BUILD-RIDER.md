# Rider 编译与调试

2026-10-02 根据用户已成功使用的 `.run/zhouzhu.run.xml`、Rider 的 Debug Profile 和本机 CMake 缓存核对。Windows 后续开发沿用这套配置；路径和工具版本是本机记录，机器配置改变时先核对实际设置。

## 在 Rider 中使用

首次克隆后，先在项目根取得子模块与地形数据。岛群 `terrain.bin` 已由 Git LFS 跟踪；GitHub 的普通 Git 单文件限制无法容纳当前 #6329 地形包。

```text
git submodule update --init --recursive
git lfs install --local
git lfs pull
```

确认 `terrain.bin` 是实际二进制文件，而不是只有几行 `version https://git-lfs...` 的指针文本，再启动游戏。

1. 打开项目根 `D:\Project\Zhouzhu` 的 CMake 项目，等待 CMake 加载完成。
2. 选择 CMake Profile **Debug** 和运行配置 **zhouzhu**。
3. 在游戏 C++ 代码处设置断点，点击该配置的 **Debug**。配置已经启用 Build Before Run，会先构建 `zhouzhu`，随后启动 Godot 并调试其加载的扩展。
4. 构建失败先查看 Build 输出；断点未绑定先核对下表中的 DLL/PDB、Godot 路径和工作目录。

| 项目 | 已核对的值 |
|---|---|
| 共享运行配置 | `.run/zhouzhu.run.xml`，类型 `CMakeRunConfiguration` |
| Profile / Target | `Debug` / `zhouzhu` |
| 构建目录 / 生成器 | `cmake-build-debug` / `Ninja` |
| 编译器 | VS 2022 Community，MSVC x64；当前工具目录 `14.44.35207`，编译器版本 `19.44.35228.0` |
| MSVC 环境入口 | `D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat` |
| Windows SDK | 当前缓存使用 `10.0.26100.0` |
| CMake | `C:\Program Files\CMake\bin\cmake.exe`，当前 4.4.3 |
| Ninja | 以缓存 `CMAKE_MAKE_PROGRAM` 为准；当前为 WinGet 安装的 Ninja |
| 调试启动程序 | `D:/Program Files/Godot/Godot_v4.4.1-stable_win64.exe` |
| 程序参数 / 工作目录 | `--path game` / `$PROJECT_DIR$`（项目根） |
| Debug DLL | `game/bin/zhouzhu.windows.template_debug.x86_64.dll` |
| Debug PDB | `game/bin/zhouzhu.windows.template_debug.x86_64.pdb` |
| Godot 加载声明 | `game/bin/zhouzhu.gdextension` 的 `windows.debug.x86_64` |

`.idea` 是本机工作状态，保持忽略；共享的运行配置在 `.run`。`GODOTCPP_TARGET=template_debug`、`GODOTCPP_DEBUG_CRT=OFF`，目前 Debug 构建使用 `/MD`；不要自行改成 `/MDd`。生成器核心在 Debug 下也按项目 CMake 设置启用 `/O2`，因此单步生成器代码时部分局部变量可能被优化掉，这不等于调试配置失效。

## 命令行复现同一构建

先读取 `CMakeCache.txt` 的 `CMAKE_BUILD_TYPE`、`CMAKE_GENERATOR`、`CMAKE_CXX_COMPILER`、`CMAKE_MAKE_PROGRAM`，确认仍是上表配置。**不要直接套用 README 历史切片里的 `cmake -B build`，也不要复用另一目录的旧二进制。**

核对 `cmake-build-debug/CMakeFiles/rules.ninja` 的 `msvc_deps_prefix`。2026-10-02 当前值是中文 `注意: 包含文件:`（还带分隔空格），对应 `VSLANG=2052`。只有实际值是英文 `Note: including file:` 时才用 `1033`。如果前缀缺失、乱码或为其他语言，先查编译器探测记录，不套用下面的语言设置。

本次用同一 `vcvars64.bat`、`VSLANG=2052` 编译独立的 `#include <cstddef>` 探针，`/showIncludes` 输出与当前 Ninja 前缀逐字匹配。当前输出编码是 UTF-8，不要按 CP936 解码成乱码；探针没有修改项目缓存或游戏 DLL。

确认前缀仍为中文后，在 **cmd 或 `.cmd` 脚本**中运行：

```bat
cd /d D:\Project\Zhouzhu
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
set "VSLANG=2052"
"C:\Program Files\CMake\bin\cmake.exe" --build cmake-build-debug --target zhouzhu --parallel 8
```

需要运行核心检查时，将目标换成 `zhouzhu_core_tests`，再从项目根运行 `cmake-build-debug\zhouzhu_core_tests.exe`；只改文档无需运行游戏回归。完整 CTest 前要先构建其全部目标，用 `ctest --test-dir cmake-build-debug -N` 查看清单，不将“测试程序不存在”判为代码失败。

命令行启动游戏仍是同一 Godot、同一项目根、同一 `--path game`；需要交互断点时回 Rider 的 **zhouzhu / Debug** 配置。编译时如果 DLL 被占用，先确认是哪一个进程，不强制结束用户自己的游戏或 Rider。

## 已遇到的失败与排查顺序

1. **头文件依赖漏记，导致新旧对象 ABI 混用。** 本项目曾因 `/showIncludes` 前缀编码损坏出现这个问题。CMakeCache 中的 `-D...SHOWINCLUDES_PREFIX` 不一定是最终值，生成的 `CMakeCXXCompiler.cmake`/`CMakeCCompiler.cmake` 会影响 Ninja 的实际前缀。必须同时核对编译输出语言、`rules.ninja` 和 Ninja `-t deps` 中的头文件依赖，不能凭“增量编译成功”认定依赖有效。
2. **临时脚本不是长期构建入口。** 忽略目录中的 `build/rider-verify.cmd` 曾强制 `VSLANG=1033`，而当前 Rider 生成的 Ninja 前缀为中文；后续会话不得盲目运行它。配置、构建、后续自动重配要使用一致的语言环境。
3. **确认缓存损坏后才重建。** 保留日志，在同一 MSVC x64 + Ninja + Debug 环境重新配置 `cmake-build-debug`，检查实际依赖前缀后完整重建，确保头文件变更能触发依赖目标重编。不要通过换编译器、生成器、目录或未经检查删除用户缓存来绕过失败。
4. **自动测试崩溃不应弹即时调试窗口。** 核心测试入口已通过 `SetErrorMode` 关闭 Windows 的崩溃弹窗；测试应有超时、退出码与日志。出现 Win32 异常先保存结果、定位具体用例，不自动打开 Visual Studio JIT 调试器。

这份记录约束后续操作方式；它不表示任何一次未来构建已经通过。每次修改仍按实际影响范围编译、验证并记录结果。

## 提交说明与历史来源

后续提交标题和正文使用中文。2026-10-02 按用户要求，将本次尚未推送的 14 条英文提交说明改为中文，保留代码、作者与时间；原历史保存在本地 `codex/backup-before-chinese-commits-20261002` 分支。生成器提交映射为 `978cfee → 3a9761b`、`b038fa2 → 9accce7`、`cfb126c → aa6468c`，对应代码树完全相同。主仓库同步子模块引用，文档与 FeatureMap 使用新提交号；数据包内的历史生成来源不改写，仍可出现旧提交号。

随后因 GitHub 拒绝超过 100 MB 的地形文件，将未发布历史中的 `game/planet/seed42/groups/*/terrain.bin` 转为 Git LFS；远端已发布历史保持不变，本地原始数据逐份验证 SHA-256。迁移前历史另存于本地 `codex/backup-before-lfs-20261002`，文档中的主仓库提交号随迁移再次同步。
