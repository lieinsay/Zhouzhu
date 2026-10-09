---
name: godot-cpp-chinese-literals
description: "In the Zhouzhu GDExtension, Chinese string literals need BOTH /utf-8 in CMake and U\"...\" literals — fixing only one looks identical to fixing neither"
metadata: 
  node_type: memory
  type: project
  originSessionId: fc3a931f-efbf-481d-8e59-191d767f5ba4
  modified: 2026-09-20T10:34:06.461Z
---

In `D:\Project\My\Zhouzhu` (Godot 4.4 + C++ GDExtension, MSVC), a Chinese literal printed as
`å·æ¸ GDExtension å·²å è½½`. Two independent layers were broken and they produce the
*same* symptom, so fixing one changes nothing visible:

1. **MSVC reads sources in the system codepage** (GBK on this machine), so UTF-8 Chinese in the
   `.cpp` is corrupted at compile time → `target_compile_options(zhouzhu PRIVATE /utf-8)` in
   `CMakeLists.txt` (sets source *and* execution charset).
2. **godot-cpp's `String(const char *)` calls `string_new_with_latin1_chars`** — it widens each
   UTF-8 byte into its own codepoint, so 州 (`E5 B7 9E`) becomes three characters → write
   Chinese literals as `U"..."` (compile-time UTF-32, bypasses the execution charset) or
   `String::utf8("...")`.

Diagnosing this needs **byte-level checking**, not console output: PowerShell decodes a child
process's stdout with the console codepage and re-encodes it, so a correct string can still look
wrong on screen, and `Get-Content -Encoding UTF8` does not undo it. Redirect to a file from Bash
(raw bytes) and decode in Python. Correct output starts with `e5 b7 9e`.

**Why:** the mojibake is double-encoding in one layer and mis-widening in the other; both render
as the same Latin-1-looking garbage, which sent the first fix attempt down the wrong path.

**How to apply:** keep `/utf-8` in CMakeLists and use `U"..."` for every non-ASCII literal in
`src/`. When Godot output looks wrong, check the bytes before changing code. See
[[zhouzhu-repo-restarted]] for the project's current state.
