#!/usr/bin/env python3
"""Voyager onboarding playthrough + button audit (the REAL user path).

Walks first-run onboarding in PIE the way a player does — screen CTA /
select / shared nav-bar Next (Button_NextScreen) — asserting from the
manager's own state (GetCurrentStage / GetSelected*).

Check classes:
  [WORKS] behavior that exists today — hard regressions if they break.
  [SPEC]  known gaps, red by design until the onboarding wiring lands:
          - selections (language/level/goals) never update the manager
            in-session (they persist somewhere, but GetSelected* / skip
            logic / invitation personalization read stale state)
          - the mode selection reaches the manager only after a ~2-4s lag,
            so Confidence + user-speed Next shows the language screen the
            C++ explicitly wants skipped (ShouldSkipStage line ~620)
          - nav-bar Button_PrevPage / Button_NextPage have no handlers

Safety: backs up Saved/SaveGames/MainSave.sav and restores it afterwards;
runs on a CLEAN save (moved aside) so it's a true first-run. Refuses if
PIE is already running.
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
        print(f"SPEC  {name}  (known gap)  ->  {str(detail)[:220]}")
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


def owner_of(path):
    """The WBP instance whose WidgetTree declares this widget."""
    parts = path.replace(":", ".").split(".")
    for i, p in enumerate(parts):
        if p.startswith("WidgetTree") and i > 0:
            owner = parts[i - 1]
    return owner


def load_buttons():
    tree = _player.get_widget_tree(actor=PANEL_ACTOR, max_widgets=2000)
    entries = []
    for comp in tree.get("widget_components", []):
        if comp.get("widget_class") == PANEL_CLASS:
            entries = comp.get("tree", [])
    buttons = [(owner_of(e["path"]), e["name"], e["path"])
               for e in entries if e.get("class") == "Button"]
    return buttons, tree


def path_for(buttons, name, owner_contains=""):
    for owner, n, p in buttons:
        if n == name and (not owner_contains or owner_contains in owner):
            return p
    return None


def click_path(p, settle=1.2):
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
        shutil.move(save, backup)  # clean first-run: no save at all
        print(f"(save moved aside: {backup})")

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

        st = mgr_state()
        check("0.3 fresh save starts at Arrival", st.get("stage") == "EXTERIOR_ARRIVAL", st)

        buttons, tree = load_buttons()
        check("0.4 panel widget tree complete (not truncated)",
              tree.get("truncated") is False, tree.get("truncated"))

        bound = exec_py("import unreal, json\n"
                        + f"paths = {json.dumps([p for _, _, p in buttons])}\n" + """
out = []
for p in paths:
    b = unreal.find_object(None, p)
    out.append(bool(b.on_clicked.is_bound()) if b else None)
result = json.dumps(out)
""")
        unbound = [(o, n) for (o, n, _), b in zip(buttons, bound) if not b]
        known_dead = {("WBP_VoyagerOnboarding_C_0", "Button_PrevPage"),
                      ("WBP_VoyagerOnboarding_C_0", "Button_NextPage")}
        check("0.5 [SPEC] nav-bar PrevPage/NextPage have handlers",
              not (set(unbound) & known_dead), sorted(set(unbound) & known_dead),
              spec=True)
        check("0.6 no OTHER buttons unbound",
              not (set(unbound) - known_dead), sorted(set(unbound) - known_dead))

        ray = _player.hand_ray_select(PANEL_ACTOR)
        check("0.7 panel physically hittable by hand-ray (no occluder bypass)",
              ray.get("ok") and not ray.get("needed_occluder_bypass"), ray)
        exec_py(MGR + "mgr.start_onboarding(True)\nresult='ok'")
        time.sleep(1.5)

        start = path_for(buttons, "Button_StartMission")
        skip = path_for(buttons, "Button_Skip")
        nxt = path_for(buttons, "Button_NextScreen")
        back = path_for(buttons, "Button_Back")
        confidence = path_for(buttons, "Button_Mode_Confidence")

        # ---- [WORKS] the real user path ----------------------------------
        click_path(start, 1.5)
        check("1.1 [WORKS] Greeting 'Get Started' -> Voice Check",
              mgr_state().get("stage") == "EXTERIOR_VOICE_CHECK", mgr_state())

        click_path(skip, 1.5)
        check("1.2 [WORKS] Speech-test Skip -> Interview: Mode",
              mgr_state().get("stage") == "EXTERIOR_INTERVIEW_MODE", mgr_state())

        # Confidence + user-speed Next: the C++ wants the language stage
        # skipped for confidence mode, but the selection reaches the manager
        # ~2-4s late, so a normal user lands on the language screen anyway.
        click_path(confidence, 1.0)
        click_path(nxt, 2.0)
        st = mgr_state()
        check("2.1 [SPEC] Confidence + user-speed Next skips the language screen",
              st.get("stage") == "EXTERIOR_INTERVIEW_LEVEL", st, spec=True)

        # Whatever stage we're on, the nav Next keeps advancing [WORKS]:
        st0 = mgr_state().get("stage")
        click_path(nxt, 2.0)
        st1 = mgr_state().get("stage")
        check("1.3 [WORKS] nav Next advances a stage", st1 != st0,
              {"from": st0, "to": st1})
        click_path(back, 2.0)
        check("1.4 [WORKS] nav Back returns", mgr_state().get("stage") == st0,
              {"expected": st0, "got": mgr_state().get("stage")})

        # ---- [SPEC] in-session manager sync (patient 5s, delta-based) -----
        goto("EXTERIOR_INTERVIEW_LANGUAGE")
        before = mgr_state().get("lang")
        card = path_for(buttons, "Button_Card", owner_contains="LanguageCard_C_2")
        click_path(card, 5.0)
        st = mgr_state()
        check("2.2 [SPEC] language card updates the manager in-session",
              st.get("lang") != before, {"before": before, "after": st.get("lang")},
              spec=True)

        goto("EXTERIOR_INTERVIEW_LEVEL")
        before = mgr_state().get("level")
        btn = ("Button_Language_Advanced" if before != "ADVANCED"
               else "Button_Language_Beginner")
        click_path(path_for(buttons, btn), 5.0)
        st = mgr_state()
        check("2.3 [SPEC] level button updates the manager in-session",
              st.get("level") != before, {"before": before, "after": st.get("level")},
              spec=True)

        goto("EXTERIOR_INTERVIEW_GOALS")
        before = sorted(mgr_state().get("goals", []))
        click_path(path_for(buttons, "Button_Card", owner_contains="GoalCard_C_1"), 5.0)
        st = mgr_state()
        check("2.4 [SPEC] goal card updates the manager in-session",
              sorted(st.get("goals", [])) != before,
              {"before": before, "after": st.get("goals")}, spec=True)

        # ---- structure ------------------------------------------------------
        views = {e.get("class") for comp in tree.get("widget_components", [])
                 for e in comp.get("tree", []) if e.get("class", "").startswith("WBP_")}
        check("3.1 [SPEC] Invitation stage has a screen",
              any("Invit" in v for v in views), sorted(views), spec=True)
        check("3.2 account screens present", any("Account" in v for v in views))
        goto("INTERIOR_ACCOUNT_LOGIN")
        check("3.3 [WORKS] GoToStage reaches Account Login",
              mgr_state().get("stage") == "INTERIOR_ACCOUNT_LOGIN", mgr_state())

    finally:
        call("stop_pie", {})
        wait_pie(False, 30)
        time.sleep(3)
        call("set_unattended_mode", {"enabled": False})
        if os.path.exists(backup):
            shutil.move(backup, save)
            print("(save restored)")

    print(f"\n=== {PASS} passed, {FAIL} failed, {SPEC_FAIL} spec-pending ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
