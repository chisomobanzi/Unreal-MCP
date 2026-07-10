"""Core tools — execute_python, health_check, wait_until_ready."""

import json
import time

from unrealmcp._bridge import mcp, _send
from unrealmcp._tcp_bridge import _tcp_send_raw


@mcp.tool()
def execute_python(code: str) -> str:
    """Execute arbitrary Python code inside the running Unreal Editor.

    The `unreal` module is pre-imported. Set a variable named `result`
    to return structured data back to Claude Code. The code runs in a
    single namespace, so comprehensions and nested functions see your
    top-level variables.
    """
    return _send(code)


@mcp.tool()
def health_check() -> str:
    """Check if the UE5 editor bridge is running and responsive.

    Includes editor readiness state when the bridge provides it:
    `ready` (world loaded + asset registry scan finished), `world`,
    `is_loading_assets`, and `pie_active`.
    """
    try:
        resp = _tcp_send_raw("health_check")
        if resp.get("status") == "error":
            return f"Error: {resp.get('error', 'Editor not reachable')}"
        return json.dumps(resp.get("result", resp), indent=2)
    except Exception as exc:
        return f"Error: {exc}"


@mcp.tool()
def set_unattended_mode(enabled: bool = True, dismiss_modals: bool = True) -> str:
    """Toggle the bridge's persistent non-interactive mode.

    While enabled, GIsRunningUnattendedScript stays set between commands
    (dialogs raised OUTSIDE a command window — PIE-start prompts, deferred
    imports — take their default answer instead of blocking the editor), and
    a Slate-tick sentinel auto-dismisses any modal window that appears
    anyway, recording a `modal_dismissed` bridge event.

    This is a fast-path command: it works even while the game thread is
    already blocked by a modal, so enabling it mid-hang un-wedges the editor.

    Leave OFF when a human is using the editor interactively — unattended
    mode silently answers their dialogs too.
    """
    try:
        resp = _tcp_send_raw("set_unattended_mode",
                             {"enabled": enabled, "dismiss_modals": dismiss_modals})
        if resp.get("status") == "error":
            return f"Error: {resp.get('error', 'bridge not reachable')}"
        return json.dumps(resp.get("result", resp), indent=2)
    except Exception as exc:
        return f"Error: {exc}"


@mcp.tool()
def get_bridge_events(since: float = 0, clear: bool = False) -> str:
    """Drain the bridge's structured event buffer (fast-path).

    Events: `modal_dismissed` (title of an auto-dismissed dialog),
    `command_timeout` (a command's 120s game-thread wait expired — the usual
    sign of a hung/modal game thread), `unattended_mode_changed`.

    Readable even while the game thread is hung — use it to find out WHY the
    editor stopped answering. `since` filters to events with seq greater than
    it; `clear=True` empties the buffer after reading.
    """
    try:
        resp = _tcp_send_raw("get_bridge_events", {"since": since, "clear": clear})
        if resp.get("status") == "error":
            return f"Error: {resp.get('error', 'bridge not reachable')}"
        return json.dumps(resp.get("result", resp), indent=2)
    except Exception as exc:
        return f"Error: {exc}"


@mcp.tool()
def get_resource_stats() -> str:
    """Editor resource telemetry: RAM, VRAM, frame time, PIE state.

    Returns process/system memory (MB), dedicated/allocated VRAM when the
    RHI reports it, average FPS / frame ms, and whether PIE is running.
    Use to spot memory pressure before an OOM kills a long batch.
    """
    try:
        resp = _tcp_send_raw("get_resource_stats")
        if resp.get("status") == "error":
            return f"Error: {resp.get('error', 'bridge not reachable')}"
        return json.dumps(resp.get("result", resp), indent=2)
    except Exception as exc:
        return f"Error: {exc}"


@mcp.tool()
def wait_until_ready(timeout_seconds: float = 600.0, poll_interval: float = 3.0) -> str:
    """Block until the editor is up AND ready for commands, or time out.

    Polls health_check until the bridge answers and reports `ready: true`
    (world loaded, asset registry scan complete). Use after launching the
    editor — commands issued during boot are rejected with a retryable
    'editor_not_ready' error, and this tool absorbs that wait for you.

    Bridges older than the readiness gate don't report `ready`; any
    successful health_check is then treated as ready (back-compat).
    """
    deadline = time.monotonic() + max(1.0, timeout_seconds)
    last_status = "no response yet"
    while time.monotonic() < deadline:
        try:
            resp = _tcp_send_raw("health_check")
            if resp.get("status") != "error":
                result = resp.get("result", resp)
                if not isinstance(result, dict) or result.get("ready", True):
                    return json.dumps(
                        {"ready": True, "health": result}, indent=2,
                    )
                last_status = (
                    f"bridge up, not ready: "
                    f"{result.get('not_ready_reason', 'still initializing')}"
                )
            else:
                last_status = resp.get("error", "bridge not reachable")
        except Exception as exc:
            last_status = str(exc)
        time.sleep(max(0.5, poll_interval))

    return json.dumps({
        "ready": False,
        "error": f"Editor not ready after {timeout_seconds:.0f}s "
                 f"(last status: {last_status})",
    }, indent=2)
