#!/usr/bin/env python3
"""Drill: duplicating a panel must not rename the widgets it was copied from.

`duplicate_widget` deep-copies a subtree, and DuplicateObject hands every copied
child the name of the widget it came from. Those names are already taken in the
WidgetTree. Re-outering a copy under its inherited name walks it into a collision,
and UObject::Rename settles a collision by moving the EXISTING object aside — so
the ORIGINALS pick up the generated suffix and every graph node, variable binding
and event that named them stops resolving.

That failure is silent. The duplicate looks right in the Designer, the Blueprint
still compiles, and the damage only surfaces when something looks a widget up by
name — by which point the editor has usually auto-saved it to disk.

So the drill duplicates a real panel with named, bound children and asserts the
sources survive under their own names, before checking the copy at all.

Works on a throwaway copy of the asset; the original is never opened.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp._tcp_bridge import _tcp_send_raw  # noqa: E402

SOURCE = ("/Game/UI/Voyager/01_MissionSelection/MissionSelectionScreenViews"
          "/CustomPanel/WBP_Custom_SkillFocus")
SCRATCH = "/Game/Dev/MCPScratch/WBP_WidgetDupDrill"

PANEL = "HorizontalBox_MainContent"
COPY = "HorizontalBox_DupDrillCopy"
PARENT = "WidgetSwitcher_SkillFocus"

# Named children of PANEL that the EventGraph binds to. If any of these stops
# resolving after the duplicate, the graph is broken.
BOUND_CHILDREN = (
    "Button_Skill_Lang_Balanced",
    "Button_Skill_Lang_Fluency",
    "Button_Skill_Lang_Vocabulary",
    "Button_Skill_Lang_Pronunciation",
    "Button_Skill_Lang_Grammar",
)

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
    return _tcp_send_raw(cmd, params or {})


def ok(res):
    return isinstance(res, dict) and res.get("status") == "success"


def widget_exists(path, name):
    return ok(call("get_widget_properties",
                   {"widget_blueprint_path": path, "widget_name": name}))


def _find_root(payload):
    """get_widget_tree nests its payload under one or more 'result' layers."""
    while isinstance(payload, dict) and "root" not in payload:
        payload = payload.get("result")
        if isinstance(payload, str):
            import json
            payload = json.loads(payload)
    return payload.get("root") if isinstance(payload, dict) else None


def _walk_names(node, out=None):
    if out is None:
        out = []
    if isinstance(node, dict):
        out.append(node.get("name"))
        for child in node.get("children") or []:
            _walk_names(child, out)
    return out


def cleanup():
    call("execute_python", {"code": (
        "import unreal\n"
        f"p = {SCRATCH!r}\n"
        "if unreal.EditorAssetLibrary.does_asset_exist(p):\n"
        "    unreal.EditorAssetLibrary.delete_asset(p)\n"
    )})


def main():
    cleanup()

    dup = call("execute_python", {"code": (
        "import unreal\n"
        f"src, dst = {SOURCE!r}, {SCRATCH!r}\n"
        "result = bool(unreal.EditorAssetLibrary.duplicate_asset(src, dst))\n"
    )})
    if not ok(dup):
        print(f"ABORT: could not stage a scratch copy of {SOURCE} -> {dup}")
        sys.exit(2)

    # Sanity: the drill is only meaningful if the sources are there to begin with.
    missing = [c for c in BOUND_CHILDREN if not widget_exists(SCRATCH, c)]
    if missing:
        print(f"ABORT: scratch copy is missing expected children: {missing}")
        cleanup()
        sys.exit(2)

    res = call("duplicate_widget", {
        "widget_blueprint_path": SCRATCH,
        "widget_name": PANEL,
        "new_name": COPY,
        "parent_widget_name": PARENT,
    })
    check("1.1 duplicate_widget reported success", ok(res), res)

    # The regression. Every source child must still answer to its own name.
    for child in BOUND_CHILDREN:
        check(f"1.2 source '{child}' survives the duplicate",
              widget_exists(SCRATCH, child),
              "renamed out from under the graph")

    check("1.3 source panel survives the duplicate",
          widget_exists(SCRATCH, PANEL), PANEL)

    # And the copy has to actually be there, under the name that was asked for.
    check("1.4 the copy exists under the requested name",
          widget_exists(SCRATCH, COPY), COPY)

    # The copy's children must be distinct objects, not the originals moved.
    tree = call("get_widget_tree", {"widget_blueprint_path": SCRATCH})
    blob = str(tree)
    check("1.5 copied children got their own suffixed names",
          any(f"{c}_Dup" in blob for c in BOUND_CHILDREN),
          "no suffixed copies found in the tree")

    # A shallow copy shows up as the same widget appearing under both panels, so no
    # name may occur twice in the tree.
    names = _walk_names(_find_root(tree))
    repeats = sorted({n for n in names if names.count(n) > 1})
    check("1.6 no widget appears in both rows (copy is not shallow)",
          not repeats, repeats)

    cleanup()
    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
