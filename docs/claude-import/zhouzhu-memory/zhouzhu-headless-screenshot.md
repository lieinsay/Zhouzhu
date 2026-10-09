---
name: zhouzhu-headless-screenshot
description: How to visually verify the Zhouzhu 3D console (globe.gl) without a real browser — headless Edge quirks and the delayed-load trick
metadata: 
  node_type: memory
  type: project
  originSessionId: fc5d935c-95b5-4ed9-8181-c740612278b4
  modified: 2026-09-10T03:31:38.433Z
---

Headless Edge (`"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --headless=new --disable-gpu --screenshot=... --window-size=1600,1000`) renders the globe.gl console fine, but:
- the screenshot is taken at the page `load` event; the single-file `viewer.html` embeds its texture as a data URL that hasn't decoded yet, so globe.gl's ready gate is closed and the shot shows a black area with one dot;
- `--virtual-time-budget` works for the live `serve` page but hangs forever on the single-file page;
- use a fresh `--user-data-dir` per run (a leftover profile makes Edge exit with no screenshot), and kill stray `msedge.exe` between runs;
- console output needs `--enable-logging=stderr --v=0` redirected to a file, then grep `CONSOLE`.

Working recipe (2026-09-10): serve the scratch dir with a tiny custom HTTP server that has a `/slow?10` endpoint sleeping 10 s, inject `<img src="/slow?10">` into a copy of the page so `load` is delayed, then screenshot with `--timeout=60000`. A canvas smaller than its container in the capture is the viewport-resize timing artifact, not a layout bug.

**Why:** the user cannot watch, and browser-only bugs (e.g. globe.gl wiping `#globe`'s children) were invisible to pytest and to a node harness.
**How to apply:** for any change to `zhouzhu_gen/web/static/index.html`, run the node harness on the extracted script (syntax + pure functions) and one delayed-load headless screenshot of both the live page and `viz web` output. See [[zhouzhu-console-utf8]] for running the CLI from Bash.

**Interaction testing (2026-09-17):** plain `--screenshot` can't click. Drive Edge via CDP instead: launch `msedge --headless=new --remote-debugging-port=9333 --user-data-dir=<fresh>`, fetch `http://127.0.0.1:9333/json`, open the page's `webSocketDebuggerUrl` with Node 24's built-in `WebSocket`, then `Runtime.evaluate` (clicks, read state) + `Page.captureScreenshot`. Wait for readiness by polling `#status` text starting with `就绪`. This works for both the live serve page and the file:// `viewer.html` (no /slow trick needed). The console accepts `#lens=geo|climate|routes|culture|polity` in the URL.

**Readiness gotcha (2026-09-18):** the footer `#status` reads `就绪` in the static HTML before any data has loaded, so polling it alone races the world fetch (`selectIsland` then throws `Cannot read properties of null (reading 'n_islands')`). Poll `typeof W !== 'undefined' && W && W.n_islands > 0` as well; once loaded the status reads `就绪：seed42`. Working CDP driver for the probe's 「岛群生成器」 fold lived at the session scratchpad `cdp_island.mjs` (navigate → wait → `selectIsland(n)` → open `details[data-fold="island"]` → click `#btnIsland` → wait for `<img` in `#islandBox` → screenshot).
