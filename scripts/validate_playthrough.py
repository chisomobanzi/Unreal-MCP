#!/usr/bin/env python3
"""Phase 3 ACCEPTANCE RUN: closed-loop unattended VR playthrough.

One unattended run proving, per the harness goal:
  A. A real Speakeasy3D VR flow plays end-to-end via playthrough():
     ship map -> PIE -> Observatory -> Engine Room -> hand-ray a skill pod
     -> detail panel verified from STRUCTURED STATE (widget tree + log).
  B. A raw modal raised MID-FLOW is auto-resolved (Slate cancels it under
     unattended mode) without blocking any beat.
  C. The PIE capture beat works while the editor window is MINIMIZED
     (xdotool is used only to minimize/restore — never to drive the editor).
  D. A forced editor crash mid-flow auto-recovers (watchdog) and the flow
     replays to completion.

DESTRUCTIVE BY DESIGN (phase D SIGSEGVs the editor). Refuses during cooks.
"""
import json
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp import _playthrough, _watchdog  # noqa: E402
from unrealmcp._tcp_bridge import _tcp_send_raw  # noqa: E402

SHIP_MAP = "/Game/Environments/Voyager/Spaceship/INTERIOR/Int_Spaceship"
HOME_MAP = "/Game/Environments/Voyager/Onboarding/L_VoyagerOnboarding"

PASS, FAIL = 0, 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"PASS  {name}")
    else:
        FAIL += 1
        print(f"FAIL  {name}  ->  {str(detail)[:350]}")


def call(cmd, params=None):
    r = _tcp_send_raw(cmd, params)
    return r.get("result", r) if r.get("status") != "error" else {"error": r.get("error")}


def beat_report(res, action, nth=0):
    hits = [b for b in res.get("beats", []) if b.get("action") == action]
    return hits[nth] if len(hits) > nth else {}


def main():
    if _watchdog.collect_probes()["cook_pids"]:
        print("ABORT: cook running")
        sys.exit(2)
    st = _watchdog.get_editor_state()
    if st["state"] != "running_ready":
        print(f"ABORT: editor {st['state']}")
        sys.exit(2)
    if call("get_pie_status").get("pie_active"):
        print("ABORT: PIE already running")
        sys.exit(2)

    win = ""
    try:
        out = subprocess.run(["xdotool", "search", "--name", "Unreal Editor"],
                             capture_output=True, text=True, timeout=10)
        ids = out.stdout.split()
        win = ids[-1] if ids else ""
    except Exception:
        pass

    # ---- A+B+C: the real flow, with a mid-run modal and a minimized window
    if win:
        subprocess.run(["xdotool", "windowminimize", win], timeout=10)

    flow = {
        "name": "engine-room-detail-flow",
        "beats": [
            {"action": "load_map", "args": {"map": SHIP_MAP}},
            {"action": "start_pie", "args": {"settle": 6}},
            {"action": "assert_state", "args": {"pie_active": True}},
            {"action": "teleport_to_room", "args": {"room": "Observatory"}},
            {"action": "get_pawn_state"},
            {"action": "teleport_to_room", "args": {"room": "Engine"}},
            {"action": "wait", "args": {"seconds": 4}},
            # B: raise a raw modal mid-flow — must not block anything.
            {"action": "raw", "args": {"command": "debug_open_modal"}},
            {"action": "wait", "args": {"seconds": 2}},
            {"action": "assert_state", "args": {"log_contains": "was canceled"}},
            {"action": "stand_before", "args": {"actor": "BP_SkillPod_Fluency"}},
            {"action": "hand_ray_select", "args": {"target": "BP_SkillPod_Fluency"}},
            {"action": "wait", "args": {"seconds": 2}},
            {"action": "assert_state",
             "args": {"log_contains": "pod-click -> detail"}},
            {"action": "assert_state",
             "args": {"widget_text_contains":
                      {"actor": "BP_EngineRoomHub", "text": "Fluency"}}},
            # C: capture while minimized.
            {"action": "capture", "args": {"max_width": 800}},
            {"action": "stop_pie"},
        ],
    }
    t0 = time.monotonic()
    res = _playthrough.run_playthrough(flow)
    dt = time.monotonic() - t0

    if win:
        subprocess.run(["xdotool", "windowmap", win], timeout=10)
        subprocess.run(["xdotool", "windowactivate", win], timeout=10)

    check("A.1 flow completed unattended", res.get("ok") is True,
          {"failed_beat": res.get("failed_beat"),
           "beats": [b for b in res.get("beats", []) if not b.get("ok")]})
    check("A.2 every beat green", all(b.get("ok") for b in res.get("beats", [])),
          [b for b in res.get("beats", []) if not b.get("ok")])
    print(f"      (flow: {len(res.get('beats', []))} beats in {dt:.0f}s, "
          f"restarts={res.get('restarts')})")
    hr = beat_report(res, "hand_ray_select").get("detail", {})
    check("A.3 hand-ray hovered + clicked the pod",
          hr.get("clicked") and "SkillPod" in str(hr.get("hovered", "")), hr)
    check("A.4 outcome verified from structured state (log + widget text)",
          beat_report(res, "assert_state", 2).get("ok")
          and beat_report(res, "assert_state", 3).get("ok"),
          [beat_report(res, "assert_state", i) for i in (2, 3)])
    check("B.1 mid-flow modal auto-resolved (Slate canceled under unattended)",
          beat_report(res, "assert_state", 1).get("ok"),
          beat_report(res, "assert_state", 1))
    cap = beat_report(res, "capture").get("detail", {})
    cap_ok = cap.get("file") and os.path.exists(cap.get("file", ""))
    if cap_ok and win:
        size = os.path.getsize(cap["file"])
        cap_ok = size > 10000  # non-trivial PNG => not a black frame
    check("C.1 PIE capture while window minimized", bool(cap_ok), cap)

    # ---- D: forced crash mid-flow -> auto-recover -> replay completes ----
    flag = os.path.join(tempfile.gettempdir(), f"mcp_pt_crash_{int(time.time())}")
    crash_flow = {
        "name": "crash-recover-flow",
        "beats": [
            {"action": "start_pie", "args": {"settle": 5}},
            {"action": "exec", "args": {"code": f"""
import os
if not os.path.exists({flag!r}):
    open({flag!r}, "w").write("x")
    import ctypes
    ctypes.string_at(0)
result = "alive-after-replay"
"""}},
            {"action": "assert_state", "args": {"pie_active": True}},
            {"action": "stop_pie"},
        ],
    }
    res2 = _playthrough.run_playthrough(crash_flow)
    check("D.1 crash flow completed after recovery", res2.get("ok") is True, res2)
    check("D.2 exactly one watchdog restart", res2.get("restarts") == 1, res2)
    ev_types = [e.get("type") for e in res2.get("bridge_events", [])]
    print(f"      (bridge events during crash flow: {ev_types})")
    try:
        os.remove(flag)
    except OSError:
        pass

    st = _watchdog.get_editor_state()
    check("E.1 editor healthy at the end", st["state"] == "running_ready", st)

    # restore home map
    call("execute_python", {"code": f"""
import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("{HOME_MAP}")
result = "ok"
"""})
    time.sleep(3)
    check("E.2 home map restored",
          call("get_world_info").get("world") == "L_VoyagerOnboarding")

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
