#!/usr/bin/env python3
"""Phase 2b+2c drill: simulated hand-ray + structured runtime readback.

In Int_Spaceship PIE: read the skill pods' widget state (hit-testability
included), drive the pawn to the Engine Room, hand-ray a pod, pull the
trigger, and confirm the detail panel opened — from runtime state and the
analytics log line, not screenshots. Also proves the non-VR interact()
fallback. Restores the previous map.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp import _player  # noqa: E402
from unrealmcp._tcp_bridge import _tcp_send_raw, _find_project_root  # noqa: E402

SHIP_MAP = "/Game/Environments/Voyager/Spaceship/INTERIOR/Int_Spaceship"

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
        time.sleep(5)  # BeginPlay + engine room wiring settle

        # --- 2c: structured readback of the pods --------------------------
        st = _player.get_runtime_state(actor_filter="SkillPod",
                                       include_components=True, max_actors=8)
        check("2.1 get_runtime_state ok", st.get("ok"), st)
        pods = st.get("actors", [])
        check("2.2 skill pods found", len(pods) >= 2, [p.get("label") for p in pods])
        with_widgets = [p for p in pods if p.get("widget_components")]
        check("2.3 pods expose widget components", bool(with_widgets), pods[:1])

        # --- drive to the engine room FIRST: the hit-test collision is
        # wired by EngineRoomManager at runtime (eb568f91), so hit-
        # testability is only true once the room logic has run.
        r = _player.teleport_to_room("Engine")
        check("3.1 teleported to Engine Room", r.get("ok"), r)
        time.sleep(4)  # room-visited reveal / click wiring

        st = _player.get_runtime_state(actor_filter="SkillPod", max_actors=8)
        pods = st.get("actors", [])
        hit_testable = [p for p in pods if p.get("widget_components") and
                        any(wc.get("blocks_visibility") and
                            "NoCollision" not in wc.get("collision_enabled", "")
                            and "NO_COLLISION" not in wc.get("collision_enabled", "")
                            for wc in p["widget_components"])]
        check("2.4 pods hit-testable after room entry",
              bool(hit_testable),
              [(p["label"], p.get("widget_components")) for p in pods][:2])

        target = hit_testable[0]["label"] if hit_testable else "SkillPod"
        tree = _player.get_widget_tree(actor=target)
        btn_found = any(
            e.get("class") == "Button" and "Button_Base" in e.get("name", "")
            for wc in tree.get("widget_components", []) for e in wc.get("tree", []))
        check("2.5 widget tree readback reaches Button_Base", btn_found,
              tree.get("widget_components", [{}])[0].get("tree", [])[:6] if tree.get("ok") else tree)

        r = _player.stand_before(target, distance=250)
        check("3.2 standing before pod's widget face", r.get("ok"), r)

        # --- 2b: hand-ray + trigger ----------------------------------------
        log_path, off = log_tail_offset()
        sel = _player.hand_ray_select(target)
        check("4.1 hand_ray_select clicked (trigger pulled on the pod)",
              sel.get("ok"), sel)
        hovered = sel.get("hovered", "")
        check("4.2 ray hovered the pod's widget at press",
              bool(hovered) and target.lower() in str(hovered).lower(),
              sel)
        if sel.get("needed_occluder_bypass"):
            # Harness capability proven, but a PLAYER's ray cannot reach the
            # readout: the pod's own mesh (BlockAll/BlockAllDynamic) encloses
            # the widget on the interaction channel. Project bug, not a
            # harness failure — surfaced for Chisomo.
            print("      WARN  project finding: pod readout is fully occluded by its "
                  "own mesh on ECC_Visibility — a real VR ray cannot click it "
                  f"(occluders bypassed: {sel.get('occluders_ignored')})")
        time.sleep(1.5)
        clicks = log_since(log_path, off, "pod-click -> detail")
        check("4.3 pod click reached ShowPodDetail (analytics log)",
              bool(clicks), log_since(log_path, off, "Engine Room")[:3])

        # --- outcome from structured state ---------------------------------
        panel = _player.get_widget_tree(actor="EngineRoomHub")
        if not panel.get("ok"):
            panel = _player.get_widget_tree(actor="EngineHub")
        if not panel.get("ok"):
            panel = _player.get_widget_tree(actor="RoomHub")
        texts = [e.get("text", "") for wc in panel.get("widget_components", [])
                 for e in wc.get("tree", []) if e.get("text")]
        skill_named = clicks and clicks[0].split("detail for ")[-1].split(" (")[0]
        check("5.1 detail panel state readable (texts present)",
              bool(texts), panel.get("actors", [])[:1])
        if skill_named:
            check("5.2 panel shows the clicked skill",
                  any(skill_named.lower() in t.lower() for t in texts),
                  {"skill": skill_named, "texts": texts[:12]})

        # --- non-VR fallback ------------------------------------------------
        log_path, off = log_tail_offset()
        other = next((p["label"] for p in hit_testable if p["label"] != target), target)
        r = _player.interact(other)
        check("6.1 interact() clicked a pod button", r.get("ok"), r)
        time.sleep(1.0)
        check("6.2 fallback click also reached ShowPodDetail",
              bool(log_since(log_path, off, "pod-click -> detail")), r)
    finally:
        call("stop_pie", {})
        wait_pie(False, 30)
        time.sleep(4)
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
            check("7.1 previous map restored",
                  call("get_world_info").get("world") == prev_world)
        call("set_unattended_mode", {"enabled": False})

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
