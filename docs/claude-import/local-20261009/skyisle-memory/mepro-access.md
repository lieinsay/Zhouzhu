---
name: mepro-access
description: "How to reach ME Pro and where skyisle-gen lives there (ssh alias, repo path, venv, how to start serve in background)"
metadata:
  node_type: memory
  type: reference
  originSessionId: 6db05abe-ac31-484f-a770-a02066d35fc7
  modified: 2026-09-30T12:15:18.327Z
---

ME Pro = ssh alias `mepro` (192.168.0.116, user liein; WireGuard 10.8.0.12), passwordless via ~/.ssh/id_ed25519.
- Repo: `~/dev/skyisle-gen` (not ~/skyisle); venv `~/.venvs/skyisle`, `~/.local/bin/skyisle` symlinks to it; env in `~/.skyisle-env.sh` (MPLBACKEND=Agg, proxy).
- As of 2026-09-30 the venv was already reinstalled under the new name and the C++ core `.so` is built there — CLAUDE.md's "needs reinstall / not compiled" note is stale.
- ME Pro's repo can be ahead of the local Windows checkout (other sessions commit there); check `git log` on both before assuming.
- Start the web console detached (plain `nohup … &` over ssh hangs the ssh client):
  `cd ~/dev/skyisle-gen && setsid nohup ~/.venvs/skyisle/bin/skyisle serve --host 192.168.0.116,10.8.0.12 --no-open > out/serve.log 2>&1 < /dev/null &`
  Never bind 0.0.0.0; don't run `pipeline` while serve is up.
