---
name: ue-mcp-verify
description: Verify a change actually rendered/works in Unreal via the MCP bridge — the screenshot + PIE loop done right (force redraw, downscale, game-view, runtime-vs-editor). Use after any material/lighting/actor/BP change you need to SEE, or when a screenshot "looks stale/wrong".
---

# Verifying UE changes over MCP

The #1 mistake is trusting a stale frame. Editor viewports render on demand; a change made via `execute_python`/`set_actor_property` may not be in the last frame.

## The still-image loop

1. **Frame the subject** — `frame_actor(actor=...)` or `set_viewport_camera(location=..., look_at=...)`. Don't rely on wherever the viewport happens to be.
2. **Capture** — `take_screenshot(max_width=1280)`. The bridge already forces `RedrawLevelEditingViewports + FlushRenderingCommands` before reading pixels (since the P0 batch), so the frame is fresh. **Always pass `max_width`** (~1280) — full-viewport PNGs are a large image-token cost for no extra clarity.
3. **Read it** — `Read` the returned `file_path` and judge with your eyes. Never assert "it worked" from a success status alone.

If an image still looks like the *old* scene after a python-side mutation, the render proxy was stale: nudge the camera (`set_viewport_camera` a few units) which forces a full redraw, then re-capture.

## Editor preview vs runtime (critical)

Many effects only apply at **BeginPlay**, not in the editor viewport — e.g. a Blueprint that swaps a material/HDRI on begin play. The editor still shows the *authored* state. To verify the real thing:

1. `start_pie(simulate=True)` — Simulate-in-Editor keeps the editor camera (no pawn possession), ideal for looking.
2. Poll `get_pie_status()` until `pie_active: true` (usually ~1 tick / 2s).
3. `take_screenshot(max_width=1280)` — now capturing the running world.
4. `stop_pie()`, then poll `get_pie_status()` until it clears.

## Clean shots

For anything you'll show a human (marketing, review), use `game_view=True` (the default on `capture_from_camera`/`contact_sheet`) to hide gizmos/grid/selection outlines.

## Runtime behavior: verify from STATE, not pixels

For gameplay/BeginPlay-driven behavior, a screenshot proves "it looked right once";
structured readback proves it works. In PIE:
- `get_runtime_state(actor_filter=...)` — actor locations + widget-component
  hit-testability (collision enabled + blocks the interaction channel).
- `get_widget_tree_runtime(actor=...)` — live widget tree with visibility/enabled/
  **text**: assert the panel actually shows the new value.
- Drive the flow like a player first (`teleport_to_room`, `stand_before`,
  `hand_ray_select`, `interact`) — see `ue-mcp-playtest` for the full agent-as-player
  loop and the scripted `playthrough()` macro.

## Gotchas
- A green `success` from a mutating tool means "the command ran", not "the result is correct". Screenshot.
- `health_check` returning ok ~1s before a crash is possible — it's liveness, not a guarantee the next command survives.
- Don't screenshot during the asset-registry scan right after boot; `wait_until_ready()` first.

Related: `ue-mcp-shot-capture` (multi-angle grids), `ue-mcp-look-dev` (material/lighting iteration).
