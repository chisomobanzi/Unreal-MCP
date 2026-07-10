# Closed-Loop VR Playtest Harness — Design (2026-07-10)

Goal: an AI agent can build a feature and autonomously play-test it in PIE end-to-end —
unattended, immune to dialogs, window focus/throttling, and crashes. Two pillars:
**(1) reliability layer** (the bridge never blocks, never dies silently), **(2) agent-as-player**
(pawn control + simulated VR input + structured state readback), capped by a `playthrough()` macro.

Companion audit: `AUDIT-2026-07.md`. Architecture recon below is stamped against HEAD `dfa9bc4`.

---

## 0. Recon results (Phase 0)

### Where the game thread blocks and the socket "dies"

- Every non-fast-path command marshals to the game thread via `AsyncTask(GameThread)` +
  `TPromise/TFuture`; the socket thread blocks in `Future.WaitFor(120s)`
  (`EpicUnrealMCPBridge.cpp:671-981`). A modal Slate loop on the game thread therefore stalls
  *every* queued command to serial 120 s timeouts. `ping`/`health_check`/`set_mcp_debug`/
  `get_mcp_token_stats` answer from the socket thread regardless (`:544-619`).
- The only modal defense is **per-command** `TGuardValue<bool>(GIsRunningUnattendedScript, true)`
  (`:710`). **Gap:** `start_pie` is async (`RequestPlaySession` → PIE starts next tick,
  `EpicUnrealMCPEditorCommands.cpp:1367`), so PIE-startup dialogs (BP compile errors, map dirty
  prompts) fire *outside* the guard. Same for async loads, user-triggered dialogs, deferred
  imports. This is the root cause of "bridge hung, needed xdotool".
- The socket itself doesn't die on modals — the client (`_tcp_bridge.py`) waits 130 s
  (> server 120 s), gets a timeout error, and post-send failures are correctly not retried
  (`PostSendError`). Socket death = editor crash; classification already exists
  (`_diagnose_connection_failure`, `_tcp_bridge.py:175`).

### Live Coding spike — verdict: NOT AVAILABLE, do not build on it

No `LiveCoding` module ships in the Linux 5.6.1 binary install (Live Coding is Live++,
Windows-only). Only deprecated `HotReload` exists — and stale hot-reload `.so`s are this
project's top boot-crash signature (launch-editor.sh purges them). **Mitigation instead:**
implement as much of the harness as possible Python-side (wrappers over `execute_python`,
the same pattern as niagara.py/statetree.py) so iteration needs no editor restart; batch the
unavoidable C++ into few `editor-cycle.sh` rebuilds.

### Existing base to build on (don't re-do)

- Screenshot freshness: `RedrawLevelEditingViewports(true)` + synchronous `Viewport->Draw()` +
  `FlushRenderingCommands()` before `ReadPixels` (`EditorCommands.cpp:1284-1293`).
- Background throttle: bridge already disables `bThrottleCPUWhenNotForeground` on start (`:211`).
- Readiness cache ticker (1 s, `FTSTicker`) + port-file re-assert every 30 s (`:221-233`).
  NOTE: `FTSTicker` does NOT fire inside modal loops — the modal sentinel must hook
  `FSlateApplication` pre-tick, which does.
- Crash-aware client errors, no-double-exec retry policy, `wait_until_ready` (Python-side poll).
- PIE control (start/stop/status), `capture_from_camera`, `contact_sheet`.
- Zero input injection / PIE-world readback today (all actor queries deliberately use the
  *editor* world). `execute_python` is the only current path into the PIE world.

### Phase-2 feasibility probe (why mostly-Python works)

- PIE world from Python: `unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()`.
- Nav-projected move: `unreal.NavigationSystemV1.project_point_to_navigation` (BP-exposed) —
  same mechanism as the shipped teleport fix (`RoomNavWingWidget::ExecuteTeleport`, project
  commit 0c6ee6a5). Pawn = `BP_UserVRPawn` (Epic VR template).
- Hand-ray: the pawn's `WidgetInteractionComponent` (+ project `VRRayVisualizerComponent`);
  `press_pointer_key`/`release_pointer_key`/`set_world_rotation` are all BP-exposed → Python.
  Pod hittability contract: collision enabled + blocks the interaction trace channel
  (project commit eb568f91).

---

## 1. Reliability layer (Phase 1)

### 1a. Non-interactive mode (C++)

- `set_unattended_mode(enabled, dismiss_modals=true)` — persistently sets
  `GIsRunningUnattendedScript = true` (covers everything `FApp::IsUnattended()` checks,
  including dialogs raised outside command windows). Sticky until turned off; state reported
  by `health_check`. Optional default-on via `[UnrealMCP] UnattendedMode=True` in DefaultEngine.ini.
- **Modal sentinel** — hook Slate pre-tick (fires inside modal pumping, unlike FTSTicker):
  if `FSlateApplication::Get().GetActiveModalWindow()` is valid and unattended mode is on,
  record `{title, type, time}` and dismiss via `RequestDestroyWindow()` (yields the dialog's
  default/cancel result). Every dismissal appends a structured event.
- **Event ring buffer** — `TArray` + critical section, appended on game thread, drained by a
  new **socket-thread fast-path** command `get_bridge_events` (works even while the game
  thread is hung — the whole point).
- Prove: BP with compile error + `start_pie` → dialog auto-dismissed, bridge responsive,
  event recorded. Zero xdotool.

### 1b. Focus/throttle-proof capture (mostly proof + targeted C++ fix)

- Test matrix: editor viewport / PIE viewport × focused / backgrounded (xdotool used ONLY to
  background the window in the test, never to drive the editor).
- If PIE capture is stale/black when backgrounded: draw the *active PIE viewport's* FViewport
  directly (not just level-editing viewports) + `FlushRenderingCommands()`; ensure
  `t.IdleWhenNotInForeground=0` while PIE runs under harness control.

### 1c. Crash watchdog + auto-recover (Python)

- `unrealmcp/_watchdog.py`: on connect-failure/timeout, reuse `_diagnose_connection_failure`
  classification → if crash (or hang beyond N serial timeouts with `health_check` also dead):
  check-shared-editor guard (`pgrep` cook/build; refuse if found), relaunch via project
  `launch-editor.sh`, poll `wait_until_ready`, emit `{event: "crashed→recovered", assertion,
  crash_dir, downtime_s}`.
- Exposed as `recover_editor()` tool + opt-in auto mode (`UNREAL_MCP_AUTORECOVER=1` or
  `set_auto_recover(true)`); manual-safe by default because the editor is shared.
- Hang (not crash) case: modal sentinel should prevent; if game thread still hung >120 s with
  no modal, report structured "game thread hung" with `get_bridge_events` tail — recovery for
  a true deadlock is restart via the same path.

### 1d. Resumable batch + resource telemetry

- `batch(items, per_item_command, checkpoint=True)` (Python): checkpoint JSON in
  `Saved/UnrealMCP/batches/<id>.json` after each item; on crash, watchdog recovers and the
  batch resumes from the checkpoint. Report per-item ok/fail/skipped.
- `get_resource_stats` (C++): `FPlatformMemory::GetStats()` (RAM), RHI texture/VRAM stats,
  `GAverageFPS`/frame-time, PIE-active flag. Fast-path where possible so it works under load.

## 2. Agent-as-player (Phase 2) — Python-first (`unrealmcp/tools/player.py`)

- `get_pawn_state()` — PIE world pawn: location/rotation, controller, HMD/camera, velocity.
- `move_to(x,y,z | actor)` — nav-project target to floor (`project_point_to_navigation`,
  never raw-Z — the ½-body-too-high bug), then teleport or stepwise walk; returns final state.
- `look_at(target)` — controller/camera yaw+pitch.
- `teleport_to_room(room)` — resolve via RoomHub/PlayerStart tags; nav-projected.
- `interact(target)` / `hand_ray_select(target)` — aim the pawn's WidgetInteractionComponent
  at target widget/actor, `press_pointer_key(LeftMouseButton)` + release; non-VR fallback:
  direct `OnClicked`/interface call. Falls back to C++ only if the BP surface proves
  insufficient (e.g. needing true key events via `FSlateApplication::ProcessKeyDownEvent`).
- `get_runtime_state(filter)` — compact PIE snapshot: actors (class/label/loc) + key
  components, active montages/morph targets, widget tree with visibility/hit-testability
  (the eb568f91 contract: collision + blocks interaction channel).

## 3. Closed loop (Phase 3)

`playthrough(flow)` — YAML/JSON flow of beats: `{action, args, assert, capture}`. Runs
unattended: start_pie → beats (move/look/interact) → per-beat `get_runtime_state` asserts +
optional screenshot → stop_pie → report `{beat results, events, captures}`. Ships with one
real flow (mission start → tutor loads → walk up → hand-ray a UI action → assert outcome).

## Test loop (repeatable, per-tool)

- Editor-less: pytest (`tests/`) for watchdog classification, batch checkpoint/resume logic.
- Live: extend `scripts/validate_bridge.py` with numbered checks per new tool (modal-dismiss
  drill, backgrounded capture, kill-editor drill, pawn drive, hand-ray select). Run after each
  editor-cycle rebuild; suite must stay green (regression gate for existing 24 checks).

## Rollout / rebuild batching

1. **C++ batch 1 (1a + events + 1d-stats):** unattended mode, modal sentinel, ring buffer +
   `get_bridge_events` fast path, `get_resource_stats`. One editor-cycle rebuild, then prove 1a.
2. **1b/1c/1d-batch:** Python-side; C++ only if backgrounded-PIE capture fails its proof.
3. **Phase 2:** Python `player.py` iterated live via execute_python (no rebuilds expected).
4. **Phase 3:** Python `playthrough` + flow assets + acceptance drill.

Guardrails: shared editor (check cooks/PIE before restart; editor-cycle.sh only), commit to
THIS repo by pathspec, existing tools regression-clean (validate_bridge.py), log tool gaps in
Notion "Unreal MCP" tracker.
