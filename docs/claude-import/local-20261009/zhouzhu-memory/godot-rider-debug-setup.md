---
name: godot-rider-debug-setup
description: "How C++ breakpoints work for the Zhouzhu GDExtension in Rider — never point the debugger at Godot's _console.exe, it is a launcher shell"
metadata: 
  node_type: memory
  type: project
  originSessionId: fc3a931f-efbf-481d-8e59-191d767f5ba4
  modified: 2026-09-21T08:10:43.552Z
---

Debug setup for `D:\Project\My\Zhouzhu` (Godot 4.4.1 + C++ GDExtension, CMake + Ninja, MSVC):

- Godot is installed at `D:\Program Files\Godot\` (downloaded 2026-09-20; there was none before).
- Rider is **2026.1.3**, even though its folder is still named
  `D:\Program Files\JetBrains\JetBrains Rider 2024.1.1` (updated in place — check
  `product-info.json`, not the folder name). Its bundled LLDB is at
  `plugins\cidr-debugger\bin\lldb\win\x64\bin\lldb.exe` and works from the command line.
- The shared run config is `.run/zhouzhu.run.xml` (committed): builds target `zhouzhu`, then runs
  Godot with `--path D:\Project\My\Zhouzhu\game`.

**The trap:** `Godot_v4.4.1-stable_win64_console.exe` is a 0.2 MB launcher that spawns the real
`Godot_v4.4.1-stable_win64.exe` (149 MB) as a *child process* and forwards its output. A debugger
attached to it sees the output but never the DLL — breakpoints show ⊘ and stay pending, even one on
the exported `zhouzhu_library_init`. Always point debuggers at the non-console exe. Its `print`
output still reaches Rider's console through the pipe (verified).

Earlier on the way there: Rider first said 「未指定调试器可执行文件」 because the C++ toolchain had
never been configured (the user fixed it in Settings → Toolchains); and it defaulted to the NMake
generator until switched to Ninja.

**Why:** the symptom (⊘ breakpoint, output visible) points at symbols or path mapping, which were
fine; the real cause is which process the debugger is in.

**How to apply:** to diagnose a breakpoint that won't bind, reproduce with that LLDB in `--batch`
mode — first break on an exported symbol; if even that stays pending, the DLL isn't in the
debugged process. See [[godot-cpp-chinese-literals]], [[zhouzhu-repo-restarted]].
