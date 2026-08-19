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
def call_subsystem(
    class_name: str,
    scope: str = "game_instance",
    function: str = "",
    args: dict | None = None,
) -> str:
    """Reach any UE subsystem through C++ reflection — no Python binding needed.

    scope: "engine" | "editor" | "game_instance" | "world" (game_instance/world
    resolve against the PIE world and error clearly when PIE isn't running).
    With no `function`, returns the subsystem's reflected properties (a read
    probe). With `function`, calls that UFUNCTION with `args` (a dict of
    param-name -> value; struct/object coercion follows the same rules as
    property-setting tools) and returns its return value and out-params.
    Use when a subsystem or method has no Python/Blueprint exposure —
    e.g. services that unreal.* cannot see at all.
    """
    params: dict = {"class_name": class_name, "scope": scope}
    if function:
        params["function"] = function
    if args:
        params["args"] = args
    resp = _tcp_send_raw("call_subsystem", params)
    if resp.get("status") == "error":
        return f"Error: {resp.get('error', 'call_subsystem failed')}"
    return json.dumps(resp.get("result", resp), indent=2)


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
def get_editor_state() -> str:
    """Classify the editor's state and the recommended recovery action.

    States: running_ready / running_booting / hung_modal (a modal window is
    up — engine tick blocked) / crashed (fresh crash dir or crash-handler
    remnants) / not_running. Includes pids, port, crash summary, and a
    `plan` field (none / unwedge_modal / rewrite_port_file / relaunch /
    refuse-during-cook). Diagnostic only — nothing is touched.
    """
    from unrealmcp._watchdog import get_editor_state as _state
    try:
        return json.dumps(_state(), indent=2)
    except Exception as exc:
        return f"Error: {exc}"


@mcp.tool()
def recover_editor(wait_timeout_seconds: float = 600.0) -> str:
    """Detect a crashed/hung/missing editor and recover it automatically.

    Executes the planned action: dismiss a hung modal over the fast path,
    rewrite a cook-deleted port.txt, or kill crash remnants (CrashReport
    Client/apport) and relaunch via launch-editor.sh, waiting until the
    bridge is ready. REFUSES to touch anything while a cook/UAT/package
    process is running (shared editor guardrail). Returns a structured
    crashed_recovered event with downtime and the crash assertion.
    """
    from unrealmcp._watchdog import recover_editor as _recover
    try:
        return json.dumps(_recover(wait_timeout_s=wait_timeout_seconds), indent=2)
    except Exception as exc:
        return f"Error: {exc}"


@mcp.tool()
def run_batch(command: str, items_json: str, batch_id: str,
              checkpoint: bool = True) -> str:
    """Run a bridge command once per item, crash-safe and resumable.

    `items_json` is a JSON array of params objects — one call of `command`
    per element. After every item the checkpoint
    (Saved/UnrealMCP/batches/<batch_id>.json) is rewritten, so an editor
    crash mid-batch loses at most the in-flight item: the watchdog
    (recover_editor) relaunches the editor and the item is retried once.
    Re-invoking with the same batch_id resumes from the checkpoint instead
    of redoing finished items. Tool-level errors are recorded per item and
    do not stop the batch.
    """
    from unrealmcp._batch import run_batch as _run
    try:
        items = json.loads(items_json)
        if not isinstance(items, list):
            return "Error: items_json must be a JSON array of params objects"
        return json.dumps(_run(command, items, batch_id, checkpoint=checkpoint),
                          indent=2, default=str)
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
