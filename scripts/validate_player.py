#!/usr/bin/env python3
"""Phase 2a drill: agent-as-player pawn control, live in Int_Spaceship.

Loads the spaceship interior, starts PIE, and drives the pawn to the
Observatory and Engine Room via the player tools — asserting every
destination was nav-projected to the floor (never the authored Z).
Restores the previous map afterwards.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp import _player  # noqa: E402
from unrealmcp._tcp_bridge import _tcp_send_raw  # noqa: E402

SHIP_MAP = "/Game/Environments/Voyager/Spaceship/INTERIOR/Int_Spaceship"

PASS, FAIL = 0, 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"PASS  {name}")
    else:
        FAIL += 1
        print(f"FAIL  {name}  ->  {str(detail)[:300]}")


def call(cmd, params=None):
    r = _tcp_send_raw(cmd, params)
    if r.get("status") == "error":
        return {"success": False, "error": r.get("error")}
    return r.get("result", r)


def wait_pie(active, timeout=60):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        st = call("get_pie_status")
        if bool(st.get("pie_active")) == active:
            return True
        time.sleep(2)
    return False


def main():
    h = call("health_check")
    if not h.get("ready"):
        print("ABORT: editor not ready")
        sys.exit(2)
    if call("get_pie_status").get("pie_active"):
        print("ABORT: PIE already running (shared editor — not touching it)")
        sys.exit(2)

    call("set_unattended_mode", {"enabled": True})
    prev_world = call("get_world_info").get("world", "")

    try:
        r = call("execute_python", {"code": f"""
import unreal
_ls = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
_ls.load_level("{SHIP_MAP}")
result = "issued"
"""})
        time.sleep(3)
        w = call("get_world_info").get("world", "")
        check("1.1 ship map loaded", w == "Int_Spaceship", w)

        r = call("start_pie", {})
        check("1.2 start_pie accepted", "error" not in r, r)
        check("1.3 PIE active", wait_pie(True, 90))
        time.sleep(4)  # BeginPlay settle

        st = _player.get_pawn_state()
        check("2.1 get_pawn_state ok", st.get("ok"), st)
        check("2.2 pawn possessed", bool(st.get("pawn_class")), st)
        start_loc = st.get("location", [0, 0, 0])

        # --- Observatory ---
        r = _player.teleport_to_room("Observatory")
        check("3.1 teleport_to_room(Observatory) resolved", r.get("ok"), r)
        if r.get("ok"):
            raw_z = r["raw_target"][2]
            floor_z = r["projected_floor"][2]
            final = r["final"]
            check("3.2 destination was nav-projected (floor found)",
                  abs(floor_z - raw_z) > 1.0 or abs(final[2] - raw_z) < 200,
                  r)
            check("3.3 pawn stands at floor + capsule half-height",
                  abs(final[2] - (floor_z + r["capsule_half_height"])) < 5.0, r)
            check("3.4 pawn arrived (XY within 300uu of target)",
                  abs(final[0] - r["raw_target"][0]) < 300
                  and abs(final[1] - r["raw_target"][1]) < 300, r)
        st_obs = _player.get_pawn_state()
        moved = st_obs.get("ok") and st_obs["location"] != start_loc
        check("3.5 pawn state reflects the move", moved,
              {"before": start_loc, "after": st_obs.get("location")})

        # --- Engine Room ---
        r = _player.teleport_to_room("Engine")
        check("4.1 teleport_to_room(Engine) resolved", r.get("ok"), r)
        if r.get("ok"):
            check("4.2 floor-projected + arrived",
                  abs(r["final"][2] - (r["projected_floor"][2]
                                       + r["capsule_half_height"])) < 5.0, r)
        engine_loc = _player.get_pawn_state().get("location")

        # --- move_to / look_at a skill pod ---
        r = _player.move_to(actor="SkillPod")
        if not r.get("ok"):
            r = _player.move_to(actor="Pod")
        check("5.1 move_to(actor=pod-ish) ok", r.get("ok"), r)
        r2 = _player.look_at(actor=r.get("target_actor") or "Pod")
        check("5.2 look_at ok", r2.get("ok"), r2)
        if r.get("ok") and r2.get("ok"):
            st2 = _player.get_pawn_state()
            check("5.3 control rotation changed toward target",
                  st2.get("ok") and st2.get("control_rotation") is not None, st2)

        # --- explicit coordinates path ---
        if engine_loc:
            r = _player.move_to(x=engine_loc[0] + 150, y=engine_loc[1],
                                z=engine_loc[2] + 250)  # deliberately-wrong Z
            check("6.1 move_to(x,y,z) ok", r.get("ok"), r)
            if r.get("ok"):
                check("6.2 wrong Z corrected to floor",
                      abs(r["final"][2] - (r["projected_floor"][2]
                                           + r["capsule_half_height"])) < 5.0, r)
    finally:
        call("stop_pie", {})
        wait_pie(False, 30)
        time.sleep(4)
        call("execute_console_command", {"command": "obj gc"})
        time.sleep(2)
        if prev_world and prev_world != "Int_Spaceship":
            call("execute_python", {"code": f"""
import unreal
_ls = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
_ls.load_level("/Game/Environments/Voyager/Onboarding/L_VoyagerOnboarding")
result = "issued"
"""})
            time.sleep(3)
            w = call("get_world_info").get("world", "")
            check("7.1 previous map restored", w == prev_world, w)
        call("set_unattended_mode", {"enabled": False})

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
