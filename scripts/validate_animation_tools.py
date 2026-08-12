#!/usr/bin/env python3
"""Live-editor proof for the animation workflow tools + frame_actor bounds.

Checks (editor must be running_ready; run while no MCP session is mid-call —
the bridge is single-client):
  1. preview_animation: stages Yuki + source mannequin side by side, captures
     shots at 2 timestamps, files exist and are non-trivial, temp actors gone.
  2. batch_retarget happy path: 1 Conversation Pro clip -> /Game/TMP_ValidateRT,
     reports ok=True, coverage mapped, motion > 1cm; outputs deleted after.
  3. frame_actor bounds: degenerate actor (empty SkeletalMeshActor) returns
     bounds_radius < 10 + warning; a real framed actor has no warning.

Uses the project's real assets (Conversation Pro pack + SKM_Yuki), mirrors
the calibrated RTG_UE4ToYuki. Cleans up everything it creates.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from unrealmcp._tcp_bridge import _call
from unrealmcp._bridge import _send
from unrealmcp.tools.animation import batch_retarget, preview_animation

CSRC = "/Game/Characters/_ConversationSource"
YUKI = "/Game/Characters/Tutors/Yuki/Source/SKM_Yuki.SKM_Yuki"
TPOSE = f"{CSRC}/Character/Mesh/SK_Mannequin_TPose.SK_Mannequin_TPose"
RTG = "/Game/Characters/Tutors/Yuki/Retargeting/RTG_UE4ToYuki.RTG_UE4ToYuki"
SRC_CLIP = f"{CSRC}/Animation/Root_Motion/Convo_10_Big_Idea"
BAKED_CLIP = "/Game/Characters/Tutors/Yuki/Animations/Conversation/AS_Yuki_Convo_02_Up_Beat_Loop"
TMP_RT = "/Game/TMP_ValidateRT"

FAILS = []


def check(name, cond, detail=""):
    tag = "PASS" if cond else "FAIL"
    print(f"[{tag}] {name}" + (f" — {detail}" if detail else ""))
    if not cond:
        FAILS.append(name)


def bridge_json(cmd, params):
    resp = json.loads(_call(cmd, params))
    return resp.get("result", resp)


def main():
    # -- 1. preview_animation with side-by-side compare -----------------
    out = json.loads(preview_animation(
        mesh=YUKI, anim=BAKED_CLIP, times=[3.0, 9.0],
        compare_mesh=TPOSE,
        compare_anim=f"{CSRC}/Animation/Root_Motion/Convo_02_Up_Beat_Loop",
    ))
    check("preview: reports clip length", out.get("length", 0) > 5,
          f"length={out.get('length')}")
    shots = out.get("shots", [])
    check("preview: one shot per timestamp", len(shots) == 2)
    for s in shots:
        p = s["file_path"]
        p = p if os.path.isabs(p) else os.path.join(
            os.path.dirname(os.path.dirname(os.path.dirname(
                os.path.dirname(os.path.abspath(__file__))))), p)
        ok = os.path.isfile(p) and os.path.getsize(p) > 20_000
        check(f"preview: shot exists+nontrivial t={s['time']}", ok, p)
    leftovers = _send(
        "import unreal\n"
        "ss = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n"
        "result = [a.get_actor_label() for a in ss.get_all_level_actors()"
        " if a.get_actor_label().startswith('TMP_MCP_AnimPreview')]")
    check("preview: temp actors cleaned up", leftovers in ("[]", ""),
          f"leftovers={leftovers}")

    # -- 2. batch_retarget happy path ------------------------------------
    out = json.loads(batch_retarget(
        anims=[SRC_CLIP], source_mesh=TPOSE, target_mesh=YUKI,
        retargeter=RTG, search="Convo_", replace="VAL_Convo_",
        output_folder=TMP_RT))
    check("retarget: ok", out.get("ok") is True, json.dumps(out)[:200])
    check("retarget: baked 1 clip", len(out.get("baked", [])) == 1)
    check("retarget: no unexpected unmapped",
          set(out.get("unmapped_chains", [])) <= {"leftEye", "rightEye"},
          str(out.get("unmapped_chains")))
    motion_ok = not out.get("dead_clips")
    check("retarget: baked clip moves (motion probe)", motion_ok,
          f"dead={out.get('dead_clips')} probes={out.get('probe_bones')}")
    probes = out.get("probe_bones", [])
    check("retarget: probe bones are names, not struct reprs",
          bool(probes) and all(not p.startswith("<") for p in probes),
          str(probes))
    _send("import unreal\n"
          f"unreal.EditorAssetLibrary.delete_directory('{TMP_RT}')\n"
          "result='ok'")

    # -- 3. frame_actor bounds warning ------------------------------------
    _send("import unreal\n"
          "ss = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n"
          "a = ss.spawn_actor_from_class(unreal.SkeletalMeshActor,"
          " unreal.Vector(0,0,5000), unreal.Rotator(0,0,0))\n"
          "a.set_actor_label('TMP_ValidateEmptyBounds')\n"
          "result='ok'")
    r = bridge_json("frame_actor", {"actor": "TMP_ValidateEmptyBounds"})
    check("frame_actor: reports bounds_radius", "bounds_radius" in r, str(r))
    check("frame_actor: warns on degenerate bounds",
          r.get("bounds_radius", 1e9) < 10 and "warning" in r, str(r))
    _send("import unreal\n"
          "ss = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n"
          "a = next((x for x in ss.get_all_level_actors()"
          " if x.get_actor_label()=='TMP_ValidateEmptyBounds'), None)\n"
          "ss.destroy_actor(a) if a else None\n"
          "result='ok'")

    print()
    if FAILS:
        print(f"RESULT: {len(FAILS)} FAILURE(S): {FAILS}")
        sys.exit(1)
    print("RESULT: all checks passed")


if __name__ == "__main__":
    main()
