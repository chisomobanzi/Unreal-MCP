#!/usr/bin/env python3
"""Voyager onboarding playthrough + button audit.

Drives the first-run onboarding in PIE like a player and click-tests every
screen's buttons, asserting effects from the manager's own state
(GetCurrentStage / GetSelectedMode / Language / Level / Goals) and logs.

Two kinds of checks:
  [WORKS]  behavior that exists today — hard regressions if they break.
  [SPEC]   the wiring launch item #1 still has to build (interview screens'
           selections + advancement). RED TODAY BY DESIGN — this suite is
           the executable spec; it goes green as the wiring lands.

Safety: backs up Saved/SaveGames/MainSave.sav and restores it afterwards
(onboarding stages write save progress). Refuses if PIE is already running.
"""
import json
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp import _player  # noqa: E402
from unrealmcp._tcp_bridge import _tcp_send_raw, _find_project_root  # noqa: E402

MAP = "/Game/Environments/Voyager/Onboarding/L_VoyagerOnboarding"
PANEL_ACTOR = "BP_MissionSelectionManager"
PANEL_CLASS = "WBP_VoyagerOnboarding_C"

PASS, FAIL, SPEC_FAIL = 0, 0, 0


def check(name, cond, detail="", spec=False):
    global PASS, FAIL, SPEC_FAIL
    if cond:
        PASS += 1
        print(f"PASS  {name}")
    elif spec:
        SPEC_FAIL += 1
        print(f"SPEC  {name}  (not implemented yet)  ->  {str(detail)[:220]}")
    else:
        FAIL += 1
        print(f"FAIL  {name}  ->  {str(detail)[:300]}")


def call(cmd, params=None):
    r = _tcp_send_raw(cmd, params)
    return r.get("result", r) if r.get("status") != "error" else {"error": r.get("error")}


def wait_pie(active, timeout=90):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if bool(call("get_pie_status").get("pie_active")) == active:
            return True
        time.sleep(2)
    return False


def exec_py(code):
    r = call("execute_python", {"code": code})
    payload = r.get("result") if isinstance(r, dict) else r
    if isinstance(payload, str):
        try:
            return json.loads(payload)
        except (json.JSONDecodeError, TypeError):
            return payload
    return r


MGR = """
import unreal, json
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
mgr = unreal.GameplayStatics.get_actor_of_class(w, unreal.VoyagerOnboardingManager)
"""


def mgr_state():
    return exec_py(MGR + """
result = json.dumps({
  "stage": str(mgr.get_current_stage()).split(".")[-1].split(":")[0],
  "mode": str(mgr.get_selected_mode()).split(".")[-1].split(":")[0],
  "lang": mgr.get_selected_language(),
  "level": str(mgr.get_selected_experience_level()).split(".")[-1].split(":")[0],
  "goals": list(mgr.get_selected_goals()),
})""")


def goto(stage):
    exec_py(MGR + f"mgr.go_to_stage(unreal.VoyagerOnboardingStage.{stage})\nresult='ok'")
    time.sleep(1.2)


def button_paths():
    tree = _player.get_widget_tree(actor=PANEL_ACTOR, max_widgets=2000)
    out, view = {}, ""
    for comp in tree.get("widget_components", []):
        if comp.get("widget_class") != PANEL_CLASS:
            continue
        for e in comp.get("tree", []):
            if e.get("class", "").startswith("WBP_"):
                view = e["name"]
            if e.get("class") == "Button":
                out[(view, e["name"])] = e.get("path")
    return out, tree


def click(paths, view, name, settle=2.0):
    p = paths.get((view, name))
    if not p:
        return False
    exec_py(f"import unreal\nunreal.find_object(None, {p!r}).on_clicked.broadcast()\nresult='ok'")
    time.sleep(settle)
    return True


def main():
    root = _find_project_root() or os.getcwd()
    save = os.path.join(root, "Saved", "SaveGames", "MainSave.sav")
    backup = save + ".onboarding-drill.bak"

    if call("get_pie_status").get("pie_active"):
        print("ABORT: PIE already running")
        sys.exit(2)
    if os.path.exists(save):
        shutil.copy(save, backup)
        print(f"(save backed up: {backup})")

    call("set_unattended_mode", {"enabled": True})
    try:
        if call("get_world_info").get("world") != "L_VoyagerOnboarding":
            exec_py(f"import unreal\nunreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level({MAP!r})\nresult='ok'")
            time.sleep(4)
        check("0.1 onboarding map loaded",
              call("get_world_info").get("world") == "L_VoyagerOnboarding")
        call("start_pie", {})
        check("0.2 PIE active", wait_pie(True))
        time.sleep(8)

        # fresh run
        exec_py(MGR + "mgr.start_onboarding(True)\nresult='ok'")
        time.sleep(2)
        st = mgr_state()
        check("0.3 onboarding restarted at Arrival", st.get("stage") == "EXTERIOR_ARRIVAL", st)

        paths, tree = button_paths()
        check("0.4 panel widget tree complete (not truncated)",
              tree.get("truncated") is False, tree.get("truncated"))
        bound = exec_py("import unreal, json\n" + f"paths = {json.dumps(list(paths.values()))}\n" + """
out = []
for p in paths:
    b = unreal.find_object(None, p)
    out.append(bool(b.on_clicked.is_bound()) if b else None)
result = json.dumps(out)
""")
        unbound = [k for k, b in zip(paths.keys(), bound) if not b]
        check("0.5 all buttons have OnClicked handlers bound", not unbound, unbound)

        # physical reachability: the panel must be hand-ray hittable
        ray = _player.hand_ray_select(PANEL_ACTOR)
        check("0.6 panel physically hittable by hand-ray (no occluder bypass)",
              ray.get("ok") and not ray.get("needed_occluder_bypass"), ray)
        exec_py(MGR + "mgr.start_onboarding(True)\nresult='ok'")  # ray click may have advanced
        time.sleep(1.5)

        # ---- [WORKS] the two live transitions --------------------------------
        click(paths, "WBP_VO_GreetingView", "Button_StartMission")
        st = mgr_state()
        check("1.1 [WORKS] Greeting 'Get Started' advances to Voice Check",
              st.get("stage") == "EXTERIOR_VOICE_CHECK", st)

        click(paths, "WBP_VO_SpeechTestView", "Button_Skip")
        st = mgr_state()
        check("1.2 [WORKS] Speech-test Skip advances to Interview: Mode",
              st.get("stage") == "EXTERIOR_INTERVIEW_MODE", st)

        # ---- [SPEC] interview wiring (red until launch item #1 lands) --------
        click(paths, "WBP_VO_ModeSelect", "Button_Mode_Confidence", settle=4)
        st = mgr_state()
        check("2.1 [SPEC] mode button records the selection (SelectMode)",
              st.get("mode") == "CONFIDENCE", st, spec=True)
        check("2.2 [SPEC] mode screen has a path forward (advance on select or a continue button)",
              st.get("stage") != "EXTERIOR_INTERVIEW_MODE", st, spec=True)

        # NOTE these assert the MANAGER's in-session state (SelectLanguage etc.),
        # not the save file: the invitation screen's personalized outcome reads
        # the manager, so save-only writes are not enough. Delta-based — a
        # different target than the current value is clicked each time, so
        # stale values loaded from a previous save can't fake a pass.
        goto("EXTERIOR_INTERVIEW_LANGUAGE")
        before = mgr_state().get("lang")
        for card in ("WBP_LanguageCard_C_2", "WBP_LanguageCard_C_3"):
            click(paths, card, "Button_Card", settle=3)
            if mgr_state().get("lang") != before:
                break
        st = mgr_state()
        check("2.3 [SPEC] language card updates the manager in-session (SelectLanguage)",
              st.get("lang") != before, {"before": before, "after": st}, spec=True)

        goto("EXTERIOR_INTERVIEW_LEVEL")
        before = mgr_state().get("level")
        target_btn = ("Button_Language_Advanced" if before != "ADVANCED"
                      else "Button_Language_Beginner")
        expected = "ADVANCED" if before != "ADVANCED" else "BEGINNER"
        click(paths, "WBP_VO_DifficultySelector", target_btn, settle=3)
        st = mgr_state()
        check("2.4 [SPEC] level button updates the manager in-session (SelectExperienceLevel)",
              st.get("level") == expected, {"before": before, "after": st}, spec=True)

        goto("EXTERIOR_INTERVIEW_GOALS")
        before = sorted(mgr_state().get("goals", []))
        click(paths, "WBP_GoalCard_C_1", "Button_Card", settle=1)
        click(paths, "WBP_GoalCard_C_4", "Button_Card", settle=2)
        st = mgr_state()
        check("2.5 [SPEC] goal cards update the manager in-session (SelectGoals)",
              sorted(st.get("goals", [])) != before,
              {"before": before, "after": st}, spec=True)

        # ---- structural coverage of the later stages --------------------------
        views = {e.get("class") for comp in tree.get("widget_components", [])
                 for e in comp.get("tree", []) if e.get("class", "").startswith("WBP_")}
        check("3.1 [SPEC] Invitation stage has a screen",
              any("Invit" in v for v in views), sorted(views), spec=True)
        check("3.2 account screens present (login/link UI exists)",
              any("Account" in v for v in views), sorted(views))

        # stage navigation itself is sound end to end
        goto("INTERIOR_ACCOUNT_LOGIN")
        st = mgr_state()
        check("3.3 [WORKS] GoToStage reaches Account Login (stage nav sound)",
              st.get("stage") == "INTERIOR_ACCOUNT_LOGIN", st)
        goto("EXTERIOR_ARRIVAL")

    finally:
        call("stop_pie", {})
        wait_pie(False, 30)
        time.sleep(3)
        call("set_unattended_mode", {"enabled": False})
        if os.path.exists(backup):
            shutil.copy(backup, save)
            os.remove(backup)
            print("(save restored)")

    print(f"\n=== {PASS} passed, {FAIL} failed, {SPEC_FAIL} spec-pending ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
