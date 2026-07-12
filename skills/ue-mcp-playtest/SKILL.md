---
name: ue-mcp-playtest
description: Autonomously play-test a UE feature in PIE via the MCP bridge — drive the pawn, click world-space UI with a simulated VR hand-ray, verify outcomes from structured runtime state, and run scripted playthrough flows that survive dialogs and editor crashes. Use when a change needs runtime/gameplay verification beyond a screenshot, or to regression-test a VR flow unattended.
---

# Closed-loop playtest over MCP

The harness lets you BE the player: build a feature, then play it in PIE and
assert the outcome — unattended. Design + findings: `docs/PLAYTEST-HARNESS-DESIGN.md`.

## Before you start

1. `wait_until_ready()` — then `set_unattended_mode(enabled=True)` for the session.
   While it's on, editor dialogs take their safe default (Slate cancels raw modals;
   FMessageDialogs auto-answer) — `start_pie` with BP compile errors can't hang you.
   **Turn it off when done** — it silently answers a human's dialogs too.
2. Check `get_pie_status()` — never start PIE over someone's live session (shared editor).

## Agent-as-player loop

1. `start_pie()` → poll `get_pie_status()` until active, then let BeginPlay settle ~5s.
2. Move: `teleport_to_room("Engine")` / `move_to(actor=...)` / `stand_before(actor=...)`.
   All destinations are navmesh-projected + capsule-corrected — you cannot reproduce
   the raise-the-pawn-half-a-body bug even with a wrong Z.
3. Interact: `hand_ray_select(target)` — poses the interaction ray like a raised VR
   hand at the target's widget face and pulls the trigger (real
   WidgetInteractionComponent pipeline). Non-VR fallback: `interact(target)`
   broadcasts a Button's OnClicked directly (bypasses hit-testing).
4. **Verify from state, not screenshots**: `get_pawn_state()`,
   `get_runtime_state(actor_filter=...)` (locations + widget hit-testability),
   `get_widget_tree_runtime(actor=...)` (live widget tree with text — assert
   "the panel now shows Fluency"). Screenshots are for humans; state is for asserts.
5. `stop_pie()` and verify it stopped before map switches (then `obj gc` + settle —
   switching maps with PIE teardown in flight has crashed the editor).

### Red flag: `needed_occluder_bypass: true`
`hand_ray_select` retried by making the target's own mesh ignore the trace. The click
landed, **but a real player's ray cannot reach that widget** (it's enclosed in
blocking mesh). That's a project bug — surface it, don't paper over it
(this exact class hid the unclickable Engine Room pods, fixed in `4a1617d1`).

## Scripted flows: `playthrough(flow_json)`

```json
{"name": "my-flow", "beats": [
  {"action": "load_map", "args": {"map": "/Game/..."}},
  {"action": "start_pie", "args": {"settle": 6}},
  {"action": "teleport_to_room", "args": {"room": "Engine"}},
  {"action": "hand_ray_select", "args": {"target": "BP_SkillPod_Fluency"}},
  {"action": "assert_state", "args": {"log_contains": "pod-click -> detail"}},
  {"action": "assert_state", "args": {"widget_text_contains":
      {"actor": "BP_EngineRoomHub", "text": "Fluency"}}},
  {"action": "capture", "args": {"max_width": 800}},
  {"action": "stop_pie"}]}
```
Asserts: `pie_active`, `pawn_near {x,y,z,tolerance}`, `widget_text_contains`,
`log_contains` (project log since the previous action). A crash mid-flow triggers the
watchdog and ONE full replay (PIE state can't checkpoint — resume = restart).

## When things break mid-run

- Bridge stops answering → `get_editor_state()` classifies it (crashed / wedged /
  hung_modal / booting) → `recover_editor()` executes the right recovery (dismiss the
  modal over the fast path, rewrite a cook-deleted port.txt, or kill crash remnants +
  relaunch + wait-ready). It refuses during cooks. `get_bridge_events()` tells you
  what happened while the game thread was busy (modal_dismissed / command_timeout).
- Long asset jobs: `run_batch(command, items_json, batch_id)` — per-item checkpoint,
  crash-recover-retry, resume by batch_id. `get_resource_stats()` to watch RAM/VRAM.

## Test suites (in `scripts/`, run from the project root)

`UNREAL_MCP_PROJECT_DIR=$PWD python3 Plugins/UnrealMCP/scripts/<suite>.py`
- `validate_bridge.py` (24) + `validate_harness.py` (37) — bridge + reliability regression.
- `validate_player.py` / `validate_interaction.py` — pawn control + hand-ray/readback.
- `validate_capture.py` — capture while the window is minimized.
- **DESTRUCTIVE** (kill/segfault the editor on purpose — never run while a human is
  mid-work): `validate_recovery.py`, `validate_batch.py`, `validate_playthrough.py`.

Related: `ue-mcp-verify` (screenshot loop), `ue-mcp-lifecycle` (launch/rebuild),
`ue-mcp-crash-triage` (manual diagnosis when the watchdog can't run).
