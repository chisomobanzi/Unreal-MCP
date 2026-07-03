---
name: ue-mcp-lifecycle
description: Launch / wait-for-ready / rebuild / relaunch the Unreal editor for MCP work, and recompile the C++ bridge plugin after editing it. Use when starting an MCP session, after editing plugin/game C++, or when the editor needs a clean restart.
---

# UE editor + bridge lifecycle

## Launch (always via the hardened script)
```bash
./launch-editor.sh            # single-instance, cleans stale port.txt, purges stale
                              # hot-reload *.so, auto-retries one boot crash
./launch-editor.sh --force    # kill an existing instance and relaunch
```
Then, from the MCP client, gate on readiness — **don't issue commands during boot** (asset-registry scan / world init; commands are rejected with a retryable `editor_not_ready`, and early level loads have crashed the editor):
```
wait_until_ready()            # polls health_check until ready:true
```
`health_check()` reports `ready`, `world`, `is_loading_assets`, `pie_active`.

## Recompile after C++ edits (bridge plugin OR game module)
The editor must be **closed** — the linker can't replace a loaded `.so`.
```bash
pkill -f 'Binaries/Linux/[U]nrealEditor.*<ProjectName>'; sleep 4
"$ENGINE/Engine/Build/BatchFiles/Linux/Build.sh" <Project>Editor Linux Development \
    -project="$PWD/<Project>.uproject" -waitmutex
./launch-editor.sh
```
Incremental builds of one changed `.cpp` are ~20-40s. `Result: Succeeded` = go.

## Python-side changes load on server restart
The MCP **Python** package is an editable install (`.venv/bin/pip install -e .`). New tools / changed wrappers appear after the **MCP server process** restarts (next client session) — no rebuild needed, but they won't show mid-session. C++ command changes are live as soon as the rebuilt editor is up.

## Manual launch over SSH (no desktop)
If `launch-editor.sh` isn't usable, discover the live display (it rotates `:0`/`:1` across logins — never hardcode): `ls /tmp/.X11-unix/` and lift `DISPLAY`/`XAUTHORITY` from a running `gnome-shell`'s `/proc/<pid>/environ`. A wrong DISPLAY silently falls back to SDL `offscreen` and the editor crashes ~8 min later. Use `setsid` so the editor detaches cleanly.

Related: `ue-mcp-crash-triage`, `ue-mcp-verify`.
