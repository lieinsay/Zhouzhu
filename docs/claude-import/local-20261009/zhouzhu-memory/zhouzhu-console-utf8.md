---
name: zhouzhu-console-utf8
description: "Running zhouzhu_gen from the Bash tool: PYTHONUTF8=1 (GBK crash on ↔), use the Python312 exe explicitly (WindowsApps python3 alias silently eats heredoc scripts), never run pipeline alongside serve"
metadata:
  node_type: memory
  type: project
  originSessionId: bd2871f7-020b-412f-9eb3-91242cbd2804
  modified: 2026-09-11T06:56:41.131Z
---

In the planet generator (since 2026-09-20 its own repo, skyisle-gen, at D:\Project\My\skyisle-gen; it used to live at D:\Project\My\Zhouzhu\generator), when running `skyisle_gen.cli run` or `check` through the Bash tool with output piped, Python picks the GBK console codepage and dies with `UnicodeEncodeError ... '\u2194'` inside run_check (the SK-perm label contains "↔"). The pipeline itself is fine.

Also: `python3` on this machine resolves to the WindowsApps store alias, which exits 0 on `python3 - <<EOF` without running anything. A `python3 - <<EOF ... || py - <<EOF2` fallback therefore silently no-ops (patch scripts "succeed" but nothing changes). This bit me twice (2026-09-10 and 2026-09-11).

**Why:** Windows console codepage is GBK; the user's own PowerShell runs don't hit it, so the code was never changed. The store alias is a Windows quirk, not a project one.

**How to apply:** `export PYTHONUTF8=1` and always invoke `"$LOCALAPPDATA/Programs/Python/Python312/python.exe"` explicitly (write patch scripts to the scratchpad with Write, then run them with that exe; verify with grep afterwards). Do not "fix" check.py for this unless the user asks. Don't run the pipeline concurrently with `zhouzhu serve` (memory-killed before). See [[zhouzhu-headless-screenshot]] for verifying the console visually.
