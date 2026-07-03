---
name: ue-mcp-crash-triage
description: Diagnose why the Unreal MCP bridge stopped responding — distinguish "editor not running" vs "crashed" vs "still booting", extract the assertion + callstack, and classify it. Use when a tool returns "Cannot connect to C++ bridge" or "Failed after N attempts".
---

# UE MCP crash / disconnect triage

Since the P0 batch the client error is usually **self-explaining** — read it first. A `Failed after 4 attempts: ...` message now branches into "editor is not running", "editor appears to have CRASHED: <assertion> (see <dir>)", or "port file exists but nothing is listening". If it already told you, act on that. Otherwise:

## 1. Is a process alive?
```bash
pgrep -f 'Binaries/Linux/[U]nrealEditor.*<ProjectName>'   # bracket = don't self-match
ss -ltn | grep :55557                                      # bridge listening?
cat <project>/Saved/UnrealMCP/port.txt                     # the port the editor wrote
```
- No process + no port file → **not running**. Launch it (`./launch-editor.sh`).
- Process alive, port not listening → still booting or hung; give it time or check the log.
- Port file says a port nothing listens on → **stale port file** (the client now auto-probes the default, but confirm).

## 2. Did it crash? Pull the assertion + callstack.
```bash
D=$(ls -dt <project>/Saved/Crashes/crashinfo-*/ | head -1)
grep -am1 -E 'Assertion failed|Fatal error|SIGSEGV|SIGILL' "$D"/*.log
sed -n '/<CALLSTACK START>/,/<CALLSTACK END>/p' "$D/Diagnostics.txt" | grep -v 'libc\|_start' | head -8
```
The top **non-engine** frame (a path under your project/plugin) is the culprit.

## 3. Classify (Speakeasy crash-signature table — see plugins/UnrealMCP/docs/AUDIT-2026-07.md §2)
- `libcef.so` SIGILL → CEF/WebBrowser (Fab/Bridge/login UIs). Consider disabling `WebBrowserWidget` if unused.
- `UObjectGlobals.cpp:3646` CDO fatal at `PreInitPostStartupScreen`, or `ShaderParameterMetadata.cpp:482` → **stale hot-reload binaries** (`*-NNNN.so`). `launch-editor.sh` purges these; if you launched another way, delete them and relaunch.
- `RerunConstructionScripts` in the frame → a fragile BP construction script; `set_actor_property` now skips the rerun by default (opt in with `rerun_construction_script`).
- `TickTaskManager.cpp` assert right after boot → a command ran before the editor was ready; `wait_until_ready()` first.

## 4. Recover
`./launch-editor.sh` handles single-instance, stale-port cleanup, stale-binary purge, and one auto-retry on a boot crash. Then `wait_until_ready()` before issuing commands. Log any *new* MCP-caused crash as a row in the Notion "Unreal MCP" tracker.

Related: `ue-mcp-lifecycle` (launch/restart), `ue-mcp-verify`.
