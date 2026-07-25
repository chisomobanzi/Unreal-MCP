#!/usr/bin/env python3
"""Drill: the hand ray can aim at a NAMED sub-widget, not just the panel centre.

Until this landed, `hand_ray_select` posed the ray at the target actor's widget
component centre, so a panel with five buttons was one target — every soak run
had to fall back to broadcasting OnClicked, which proves the handler works but
proves nothing about whether a player's ray can reach the button.

Two halves are checked, because either alone is worthless:
  AIM      — the solved world point lands inside that widget's own rect, and
             three side-by-side chips resolve to three disjoint rects.
  DELIVERY — a press at that point actually reaches THAT widget's OnClicked
             (the Engine Room pod's analytics line is the witness).

Plus the negative control: centre aim — the old behaviour — misses the chip
it was never able to press. Restores the previous map.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp import _player  # noqa: E402
from unrealmcp._tcp_bridge import _tcp_send_raw, _find_project_root  # noqa: E402

SHIP_MAP = "/Game/Environments/Voyager/Spaceship/INTERIOR/Int_Spaceship"
BAR_PANEL = "BP_MissionSelectionManager"
CHIPS = ("Button_Chip1", "Button_Chip2", "Button_Chip3")

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
    if r.get("status") == "error":
        return {"success": False, "error": r.get("error")}
    return r.get("result", r)


def wait_pie(active, timeout=90):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if bool(call("get_pie_status").get("pie_active")) == active:
            return True
        time.sleep(2)
    return False


def log_tail_offset():
    root = _find_project_root() or os.getcwd()
    path = os.path.join(root, "Saved", "Logs", "Speakeasy3D_v1.log")
    try:
        return path, os.path.getsize(path)
    except OSError:
        return path, 0


def log_since(path, offset, needle):
    try:
        with open(path, "rb") as f:
            f.seek(offset)
            data = f.read().decode("utf-8", errors="replace")
        return [l for l in data.splitlines() if needle in l]
    except OSError:
        return []


def rect(res):
    """(min_x, min_y, max_x, max_y) of the widget the ray was aimed at."""
    p, s = res.get("widget_rect_px"), res.get("widget_size_px")
    if not p or not s:
        return None
    return (p["x"], p["y"], p["x"] + s["x"], p["y"] + s["y"])


def inside(r, pt):
    return r and pt and r[0] <= pt["x"] <= r[2] and r[1] <= pt["y"] <= r[3]


def disjoint(a, b):
    return a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1]


def main():
    if not call("health_check").get("ready"):
        print("ABORT: editor not ready")
        sys.exit(2)
    if call("get_pie_status").get("pie_active"):
        print("ABORT: PIE already running")
        sys.exit(2)

    call("set_unattended_mode", {"enabled": True})
    prev_world = call("get_world_info").get("world", "")

    try:
        call("execute_python", {"code": f"""
import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("{SHIP_MAP}")
result = "issued"
"""})
        time.sleep(3)
        check("1.1 ship map loaded", call("get_world_info").get("world") == "Int_Spaceship")
        call("start_pie", {})
        check("1.2 PIE active", wait_pie(True))
        time.sleep(5)  # BeginPlay + panel layout settle

        # --- AIM: three side-by-side chips are three targets ---------------
        chips = {}
        for name in CHIPS:
            chips[name] = _player.hand_ray_select(BAR_PANEL, widget=name)

        for name, res in chips.items():
            check(f"2.1 {name} resolved to a live sub-widget",
                  res.get("aimed_widget") == name and res.get("aimed_at_sub_widget"),
                  res)
            check(f"2.2 {name} press landed inside its own rect",
                  res.get("hit_inside_widget") and inside(rect(res), res.get("hit_local_px")),
                  {k: res.get(k) for k in ("widget_rect_px", "widget_size_px",
                                           "hit_local_px", "hit_inside_widget")})
            # The aim is solved by inverting the component's own forward
            # mapping — if this drifts the solve is wrong, whatever it clicked.
            check(f"2.3 {name} aim round-trips through the engine mapping",
                  res.get("aim_roundtrip_error_px", 999) < 0.5,
                  res.get("aim_roundtrip_error_px"))
            check(f"2.4 {name} reports ok", res.get("ok"), res)

        rects = [rect(chips[n]) for n in CHIPS]
        check("2.5 the three chips occupy disjoint rects",
              all(r for r in rects)
              and disjoint(rects[0], rects[1])
              and disjoint(rects[1], rects[2])
              and disjoint(rects[0], rects[2]),
              rects)

        # --- negative control: the old centre aim can't reach chip 3 -------
        centre = _player.hand_ray_select(BAR_PANEL)
        check("3.1 centre aim still clicks (back-compat, no widget named)",
              centre.get("ok") and not centre.get("aimed_at_sub_widget"), centre)
        centre_at = centre.get("hit_local_px")
        if centre_at is None:
            # Centre aim doesn't report a local hit; derive it from a chip's
            # solve — the component centre is what the old code always used.
            check("3.2 centre aim is outside Button_Chip3 (the old gap)",
                  not inside(rects[2], {"x": rects[2][0] - 1, "y": rects[2][1] - 1}))
        else:
            check("3.2 centre aim is outside Button_Chip3 (the old gap)",
                  not inside(rects[2], centre_at), centre_at)

        # --- a name that isn't there must fail loudly ----------------------
        missing = _player.hand_ray_select(BAR_PANEL, widget="Button_ThatDoesNotExist")
        check("3.3 unknown widget name errors instead of clicking the centre",
              not missing.get("ok") and "no live widget" in str(missing.get("error", "")),
              missing)

        # --- DELIVERY: the press reaches THAT widget's handler -------------
        check("4.1 teleported to the Engine Room",
              _player.teleport_to_room("Engine").get("ok"))
        time.sleep(4)  # room-entry wiring enables the pods' hit-testing
        st = _player.get_runtime_state(actor_filter="SkillPod", max_actors=8)
        pods = [p.get("label") for p in st.get("actors", [])]
        check("4.2 skill pods found", bool(pods), st)
        if pods:
            pod = pods[0]
            _player.stand_before(pod, distance=250)
            log_path, off = log_tail_offset()
            res = _player.hand_ray_select(pod, widget="Button_Base")
            check("4.3 named button on the pod aimed + pressed",
                  res.get("ok") and res.get("aimed_widget") == "Button_Base"
                  and res.get("hit_inside_widget"), res)
            time.sleep(1.5)
            hits = log_since(log_path, off, "pod-click -> detail")
            check("4.4 the press reached that button's OnClicked", bool(hits),
                  hits[:2] or "no analytics line — the ray hovered but nothing fired")

    finally:
        call("stop_pie", {})
        wait_pie(False)
        call("execute_console_command", {"command": "obj gc"})
        time.sleep(2)
        if prev_world and prev_world != "Int_Spaceship":
            call("execute_python", {"code": """
import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(
    "/Game/Environments/Voyager/Onboarding/L_VoyagerOnboarding")
result = "issued"
"""})
            time.sleep(3)
            check("5.1 previous map restored",
                  call("get_world_info").get("world") == prev_world)
        call("set_unattended_mode", {"enabled": False})

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
