#!/usr/bin/env python3
"""Live validation suite for the playtest-harness reliability layer (Phase 1).

TDD companion to docs/PLAYTEST-HARNESS-DESIGN.md — written BEFORE the
implementation; run red against an old editor build, green after rebuild.

Talks raw TCP to the C++ bridge (same framing as validate_bridge.py). Needs a
running editor. Sections:
  1x  set_unattended_mode / get_bridge_events / health_check surface
  2x  dialog-suppression drill (deferred FMessageDialog outside any command)
  3x  modal-sentinel drill (start_pie with a compile-error BP in the level)
  4x  get_resource_stats
Cleans up after itself and restores unattended mode to OFF.
"""
import glob
import json
import os
import socket
import struct
import sys
import time


def _find_port_file() -> str:
    env_dir = os.environ.get("UNREAL_MCP_PROJECT_DIR")
    if env_dir:
        return os.path.join(env_dir, "Saved", "UnrealMCP", "port.txt")
    d = os.getcwd()
    while True:
        if glob.glob(os.path.join(d, "*.uproject")):
            return os.path.join(d, "Saved", "UnrealMCP", "port.txt")
        parent = os.path.dirname(d)
        if parent == d:
            return os.path.join("Saved", "UnrealMCP", "port.txt")
        d = parent


PORT_FILE = _find_port_file()
BP_DIR = os.environ.get("UNREAL_MCP_VALIDATE_DIR", "/Game/Dev")
BP_NAME = "BP_MCPHarnessBroken"
BP_PATH = f"{BP_DIR}/{BP_NAME}"


def call(cmd, params=None, timeout=120, conn_timeout=15):
    port = int(open(PORT_FILE).read().strip())
    s = socket.create_connection(("127.0.0.1", port), timeout=conn_timeout)
    s.settimeout(timeout)
    payload = json.dumps({"type": cmd, "params": params or {}}).encode()
    s.sendall(struct.pack(">I", len(payload)) + payload)

    def recvn(n):
        buf = b""
        while len(buf) < n:
            chunk = s.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("socket closed")
            buf += chunk
        return buf

    ln = struct.unpack(">I", recvn(4))[0]
    data = json.loads(recvn(ln).decode())
    s.close()
    return data


def result_of(resp):
    if resp.get("status") == "error":
        return {"success": False, "error": resp.get("error", "unknown")}
    return resp.get("result", resp)


PASS, FAIL = 0, 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"PASS  {name}")
    else:
        FAIL += 1
        print(f"FAIL  {name}  ->  {str(detail)[:400]}")


def bridge_responsive(within=5.0):
    """The core reliability assertion: fast path answers within `within` s."""
    t0 = time.monotonic()
    try:
        r = call("ping", timeout=within, conn_timeout=within)
        return (time.monotonic() - t0) <= within and r.get("status") != "error"
    except Exception:
        return False


def events_since(seq):
    r = result_of(call("get_bridge_events", {"since": seq}))
    return r.get("events", []), r.get("last_seq", seq)


def main():
    # ---- 1. Tool surface -------------------------------------------------
    r = result_of(call("health_check"))
    check("1.1 health_check ok", r.get("status") == "ok", r)
    check("1.2 health_check reports unattended_mode", "unattended_mode" in r, r)
    check("1.3 health_check reports pending_events", "pending_events" in r, r)

    r = result_of(call("set_unattended_mode", {"enabled": True, "dismiss_modals": True}))
    check("1.4 set_unattended_mode succeeds", r.get("success") is True, r)
    check("1.5 ... reflects enabled", r.get("unattended_mode") is True, r)

    h = result_of(call("health_check"))
    check("1.6 health_check now unattended", h.get("unattended_mode") is True, h)

    r = result_of(call("get_bridge_events", {"since": 0}))
    check("1.7 get_bridge_events succeeds", r.get("success") is True, r)
    evs = r.get("events", [])
    check("1.8 unattended_mode_changed event recorded",
          any(e.get("type") == "unattended_mode_changed" for e in evs), evs)
    baseline_seq = r.get("last_seq", 0)

    # since-filter: nothing new expected beyond baseline
    evs2, _ = events_since(baseline_seq)
    check("1.9 since-filter returns no stale events", evs2 == [], evs2)

    # ---- 2. Dialog-suppression drill ------------------------------------
    # Schedule a modal message dialog to open OUTSIDE any command window
    # (post-tick callback fires after this execute_python returns). Without
    # persistent unattended mode this hangs the game thread ~forever; with it,
    # the dialog must take its default answer without ever blocking.
    code = r"""
import unreal

def _pop(dt):
    unreal.unregister_slate_post_tick_callback(_h)
    unreal.EditorDialog.show_message(
        "MCP harness drill", "This dialog must never block the bridge.",
        unreal.AppMsgType.YES_NO, unreal.AppReturnType.NO)

_h = unreal.register_slate_post_tick_callback(_pop)
result = "scheduled"
"""
    r = result_of(call("execute_python", {"code": code}))
    check("2.1 dialog drill scheduled", "scheduled" in str(r), r)
    time.sleep(3)  # give the deferred dialog time to fire (and be suppressed)
    check("2.2 bridge responsive after deferred dialog", bridge_responsive(5), )
    r = result_of(call("get_world_info", {}))  # game-thread command must round-trip
    check("2.3 game thread not hung (world info round-trips)",
          r.get("success", True) is not False and "error" not in r, r)

    # ---- 2b. Sentinel RECOVERY drill --------------------------------------
    # While unattended mode is ON, Slate itself cancels raw modals — nothing
    # to dismiss. The sentinel's job is recovery: a modal that opened while
    # ATTENDED (game thread genuinely hung) must be dismissed the moment the
    # agent flips set_unattended_mode(true) over the fast path.
    r = result_of(call("set_unattended_mode", {"enabled": False}))
    check("2.4 unattended off for hang setup", r.get("unattended_mode") is False, r)
    _, pre_modal_seq = events_since(0)
    r = result_of(call("debug_open_modal", {}))
    check("2.5 raw modal scheduled (game thread will hang)", r.get("success", False), r)
    time.sleep(3)
    # The modal blocks the ENGINE tick (PIE/timers), though AsyncTask-based
    # commands still run (Slate's modal loop services the game TaskGraph).
    # health_check's modal_active flag — maintained by the pre-tick, which
    # fires inside modal loops — is the proof the modal is really up.
    h = result_of(call("health_check"))
    check("2.6a modal genuinely present (health_check.modal_active)",
          h.get("modal_active") is True, h)
    check("2.6b fast path alive while modal up", bridge_responsive(5))
    r = result_of(call("set_unattended_mode", {"enabled": True}))
    check("2.7 set_unattended_mode answers mid-hang", r.get("unattended_mode") is True, r)
    time.sleep(3)
    r = result_of(call("get_world_info", {}, timeout=20))
    check("2.8 game thread un-wedged (world info round-trips)",
          r.get("success", True) is not False and "error" not in r, r)
    # The un-wedge has two racing mechanisms: Slate's own modal loop exits
    # once GIsRunningUnattendedScript is re-asserted, and the sentinel
    # dismisses the window. Either way the OUTCOME must hold: no modal left.
    h = result_of(call("health_check"))
    evs, _ = events_since(pre_modal_seq)
    dismissed = any(e.get("type") == "modal_dismissed" for e in evs)
    check("2.9 modal cleared after enabling unattended (event or slate-exit)",
          h.get("modal_active") is False, {"health": h, "events": evs})
    print(f"      (cleared by: {'sentinel event' if dismissed else 'slate modal-loop exit'})")

    # ---- 3. Modal-sentinel drill: PIE with a broken BP -------------------
    # A level-placed BP with compile errors makes start_pie raise a modal
    # prompt one tick later (outside start_pie's own unattended guard). The
    # persistent mode and/or sentinel must resolve it unattended.
    #
    # Runs in a dedicated scratch level so the user's real map is never
    # touched; the original map is restored afterwards (section 3.11).
    r = result_of(call("execute_python", {"code": r"""
import unreal
_w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if _w is None:
    _w = unreal.EditorLevelLibrary.get_editor_world()
_prev = _w.get_path_name()
_ls = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
_ls.new_level("/Game/Dev/L_MCPHarnessScratch")
# Save it: PIE inside a never-saved level + map switch afterwards crashed the
# editor once (TickTaskManager !LevelList.Contains assert during world GC).
_ls.save_current_level()
result = _prev.split(".")[0]
"""}))
    raw = r.get("result", r) if isinstance(r, dict) else r
    tokens = [t for t in str(raw).replace('"', " ").split() if t.startswith("/Game")]
    prev_map_pkg = tokens[0].split(".")[0] if tokens else ""
    check("3.0 scratch level loaded (original map recorded)",
          bool(prev_map_pkg), r)
    if not prev_map_pkg:
        # NEVER run the PIE drill in whatever map the user had open.
        print("ABORT  scratch level unavailable — refusing to drill in the live map")
        result_of(call("set_unattended_mode", {"enabled": False}))
        print(f"\n=== {PASS} passed, {FAIL} failed (aborted before drill) ===")
        sys.exit(1)

    call("delete_actor", {"name": BP_NAME})
    call("delete_asset", {"asset_path": f"{BP_PATH}.{BP_NAME}", "force": True})

    r = result_of(call("create_blueprint",
                       {"name": BP_NAME, "path": BP_DIR, "parent_class": "Actor"}))
    check("3.1 scratch BP created",
          r.get("success", True) is not False and "error" not in r, r)

    # Inject a guaranteed compile error: custom event exec wired into a
    # CallFunction on StaticMeshComponent with its Target pin left dangling
    # ("'Target' must have a connection" — a hard error, not a warning).
    ev_resp = result_of(call("add_custom_event_node",
                             {"blueprint_name": BP_PATH, "event_name": "HarnessEvt",
                              "pos_x": 0, "pos_y": 0}))
    ev_id = ev_resp.get("node_id", "") or ev_resp.get("node", {}).get("node_id", "")
    r = result_of(call("add_blueprint_node", {
        "blueprint_name": BP_PATH,
        "node_type": "CallFunction",
        "node_params": {"target_function": "SetStaticMesh",
                        "target_class": "StaticMeshComponent",
                        "pos_x": 400, "pos_y": 0}}))
    call_id = r.get("node_id", "")
    conn = result_of(call("connect_nodes", {
        "blueprint_name": BP_PATH,
        "source_node_id": ev_id, "source_pin_name": "then",
        "target_node_id": call_id, "target_pin_name": "execute"}))
    check("3.2 error-inducing graph built",
          bool(ev_id) and bool(call_id) and conn.get("success", False),
          {"ev": ev_resp, "call": call_id, "conn": conn})

    r = result_of(call("compile_blueprint", {"blueprint_name": BP_PATH}))
    compile_failed = r.get("compiled") is False or r.get("num_errors", 0) > 0
    check("3.3 BP has compile errors (precondition; needs honest compile_blueprint)",
          compile_failed, r)

    r = result_of(call("spawn_blueprint_actor",
                       {"blueprint_name": BP_PATH, "actor_name": BP_NAME,
                        "location": [0, 0, 200]}))
    check("3.4 broken BP placed in level",
          r.get("success", True) is not False and "error" not in r, r)

    _, pre_pie_seq = events_since(0)
    r = result_of(call("start_pie", {}))
    check("3.5 start_pie accepted", r.get("success", False) or r.get("requested", False), r)

    # Bridge must stay responsive the whole time the prompt would be up.
    responsive_during = all(bridge_responsive(5) or time.sleep(1)
                            for _ in range(3))
    check("3.6 bridge responsive during PIE-start prompt window", responsive_during)

    # Within 30s: either PIE is running (prompt auto-answered 'play anyway' /
    # suppressed) or PIE never started (default was 'cancel') — both are
    # acceptable unattended outcomes; a HUNG bridge is the failure.
    deadline = time.monotonic() + 30
    pie_state = None
    while time.monotonic() < deadline:
        try:
            st = result_of(call("get_pie_status", timeout=10))
            pie_state = st
            break
        except Exception as exc:
            pie_state = f"no answer: {exc}"
            time.sleep(2)
    check("3.7 pie_status answers (game thread alive)", isinstance(pie_state, dict), pie_state)

    evs, _ = events_since(pre_pie_seq)
    dismissed = [e for e in evs if e.get("type") == "modal_dismissed"]
    if isinstance(pie_state, dict) and pie_state.get("pie_active"):
        # Prompt either suppressed (unattended default) or dismissed by sentinel.
        check("3.8 PIE reached without human", True)
    else:
        check("3.8 prompt resolved unattended (sentinel event or clean refusal)",
              bool(dismissed) or isinstance(pie_state, dict),
              {"events": evs, "pie": pie_state})
    print(f"      (sentinel dismissals observed: {len(dismissed)})")

    call("stop_pie", {})
    pie_stopped = False
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        st = result_of(call("get_pie_status", timeout=10))
        if not st.get("pie_active"):
            pie_stopped = True
            break
        time.sleep(1)
    check("3.9 PIE stopped (verified)", pie_stopped)

    # Cleanup scratch actor + asset, restore the original map, drop the
    # scratch level without saving it.
    call("delete_actor", {"name": BP_NAME})
    r = result_of(call("delete_asset", {"asset_path": f"{BP_PATH}.{BP_NAME}", "force": True}))
    check("3.10 cleanup", r.get("success", True) is not False, r)

    if prev_map_pkg:
        # Let deferred PIE-world teardown finish before destroying the scratch
        # world, then force the GC so the map switch doesn't purge a level
        # whose tick tasks are still registered (crashed the editor once).
        time.sleep(5)
        call("execute_console_command", {"command": "obj gc"})
        time.sleep(3)
        # load_level's bool return is unreliable (observed False on successful
        # loads) — verify by reading the world back instead. Save the scratch
        # level first so no save-prompt (auto-defaulted under unattended) can
        # abort the switch.
        call("execute_python", {"code": f"""
import unreal
_ls = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
_ls.save_current_level()
_ls.load_level("{prev_map_pkg}")
result = "load issued"
""", }, timeout=300)
        # Verify from a separate command: the editor world pointer is briefly
        # null right after a load; get_world_info a moment later is reliable.
        time.sleep(3)
        w = result_of(call("get_world_info", {}, timeout=60))
        world_name = str(w.get("world", ""))
        check("3.11 original map restored (world verified)",
              world_name and prev_map_pkg.endswith(world_name), w)
    else:
        check("3.11 original map restored", False, "no original map recorded")

    # ---- 4. get_resource_stats -------------------------------------------
    r = result_of(call("get_resource_stats", {}))
    check("4.1 get_resource_stats succeeds", r.get("success", False), r)
    check("4.2 reports process memory", r.get("process_physical_mb", 0) > 100, r)
    check("4.3 reports frame timing", r.get("average_frame_ms", 0) > 0, r)
    check("4.4 reports pie flag", "pie_active" in r, r)

    # ---- restore ----------------------------------------------------------
    r = result_of(call("set_unattended_mode", {"enabled": False}))
    check("5.1 unattended mode restored OFF", r.get("unattended_mode") is False, r)
    h = result_of(call("health_check"))
    check("5.2 health_check reflects OFF", h.get("unattended_mode") is False, h)

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
