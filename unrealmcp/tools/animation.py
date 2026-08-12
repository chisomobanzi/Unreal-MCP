"""Animation workflow tools — clip preview capture and guarded batch retargeting.

Both tools bundle multi-call editor-Python workflows that are easy to get
silently wrong by hand:

- Editor-world skeletal components do not evaluate animation unless
  update_animation_in_editor is enabled, so a naive stage-and-screenshot
  loop captures the ref pose (T-pose) without erroring.
- A freshly created IKRetargeter has an empty ops stack: auto_map_chains
  maps nothing, duplicate_and_retarget "succeeds", and every baked clip is
  a static ref-pose dud. batch_retarget makes those failures loud.
"""
import json

from unrealmcp._animtools import evaluate_retarget_output, plan_preview_times
from unrealmcp._bridge import mcp, _send
from unrealmcp._tcp_bridge import _call

_PREVIEW_LABEL = "TMP_MCP_AnimPreview"


def _py(template: str, **subs: object) -> str:
    """Fill a python payload template using @name@ placeholders (no brace hell)."""
    out = template
    for key, val in subs.items():
        out = out.replace(f"@{key}@", json.dumps(val))
    return out


def _send_json(code: str) -> dict:
    """_send, parsed; raises RuntimeError on bridge/editor error strings."""
    raw = _send(code)
    if raw.startswith("Error"):
        raise RuntimeError(raw)
    try:
        return json.loads(raw)
    except (json.JSONDecodeError, TypeError):
        return {"raw": raw}


_STAGE = '''
import unreal
def _stage(label, mesh_path, anim_path, y_off):
    ss = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = next((a for a in ss.get_all_level_actors()
                  if a.get_actor_label() == label), None)
    if not actor:
        actor = ss.spawn_actor_from_class(
            unreal.SkeletalMeshActor, unreal.Vector(0, y_off, 5000),
            unreal.Rotator(0, 0, 90))
        actor.set_actor_label(label)
    comp = actor.skeletal_mesh_component
    comp.set_editor_property('skeletal_mesh_asset',
                             unreal.load_object(None, mesh_path))
    anim = unreal.load_object(None, anim_path)
    if not anim:
        raise RuntimeError('anim not found: ' + anim_path)
    # Without this, editor-world components hold ref pose and every
    # screenshot is a silent T-pose lie.
    comp.set_update_animation_in_editor(True)
    comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    comp.set_animation(anim)
    comp.set_play_rate(1.0)
    comp.play(True)
    return anim.get_editor_property('sequence_length')
length = _stage(@label@, @mesh@, @anim@, 0)
if @compare_anim@:
    _stage(@label@ + '_Src', @compare_mesh@ or @mesh@, @compare_anim@, 150)
result = {'length': round(length, 3),
          'saved_dir': unreal.Paths.convert_relative_path_to_full(
              unreal.Paths.project_saved_dir())}
'''

_SCRUB = '''
import unreal
ss = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for label in @labels@:
    a = next((x for x in ss.get_all_level_actors()
              if x.get_actor_label() == label), None)
    if a:
        a.skeletal_mesh_component.set_position(@time@, False)
result = 'ok'
'''

_CLEANUP = '''
import unreal
ss = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for label in @labels@:
    a = next((x for x in ss.get_all_level_actors()
              if x.get_actor_label() == label), None)
    if a:
        ss.destroy_actor(a)
result = 'ok'
'''


def preview_animation(
    mesh: str,
    anim: str,
    times: list[float] | None = None,
    compare_mesh: str = "",
    compare_anim: str = "",
    max_width: int = 1280,
    keep_actors: bool = False,
) -> str:
    """Stage an animation on a mesh and screenshot it at several timestamps.

    Spawns a temporary SkeletalMeshActor (plus an optional side-by-side
    comparison actor — e.g. the retarget SOURCE mesh playing the source
    clip), scrubs to each timestamp, captures the framed viewport, and
    cleans up. Handles the editor-world trap where skeletal components
    silently hold ref pose unless update-in-editor is enabled.

    times defaults to 4 samples spread across the clip's interior.
    Returns JSON: clip length + one screenshot path per timestamp.
    """
    stage = _send_json(_py(
        _STAGE, label=_PREVIEW_LABEL, mesh=mesh, anim=anim,
        compare_mesh=compare_mesh, compare_anim=compare_anim))
    length = float(stage.get("length", 0.0))
    labels = [_PREVIEW_LABEL] + ([_PREVIEW_LABEL + "_Src"] if compare_anim else [])
    shots = []
    try:
        _call("frame_actor", {"actor": _PREVIEW_LABEL})
        saved_dir = stage.get("saved_dir", "Saved/")
        for t in plan_preview_times(length, times):
            _send_json(_py(_SCRUB, labels=labels, time=t))
            # Absolute path — a relative one resolves against the editor
            # binary's CWD, scattering shots into the engine install dir.
            shot_path = f"{saved_dir.rstrip('/')}/Screenshots/AnimPreview_{t:06.2f}s.png"
            resp = json.loads(_call("take_screenshot",
                                    {"file_path": shot_path,
                                     "max_width": max_width}))
            inner = resp.get("result", resp)
            shots.append({"time": t,
                          "file_path": inner.get("file_path", shot_path)})
    finally:
        if not keep_actors:
            _send_json(_py(_CLEANUP, labels=labels))
    return json.dumps({
        "length": length,
        "shots": shots,
        "compare": bool(compare_anim),
        "note": "Use the Read tool on each file_path to view.",
    }, indent=2)


_RETARGET = '''
import unreal
ar = unreal.AssetRegistryHelpers.get_asset_registry()
eal = unreal.EditorAssetLibrary
S, T = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
rtg = unreal.load_object(None, @retargeter@)
if not rtg:
    raise RuntimeError('retargeter not found: ' + @retargeter@)
c = unreal.IKRetargeterController.get_controller(rtg)

# Guard 1: a Python-created retargeter has an EMPTY ops stack and maps nothing.
ops_added = False
if c.get_num_retarget_ops() == 0:
    c.add_default_ops()
    ops_added = True

tgt_rig = c.get_ik_rig(T)
tgt_chains = [str(ch.chain_name) for ch in
              unreal.IKRigController.get_controller(tgt_rig).get_retarget_chains()]
def _coverage():
    return {t: str(c.get_source_chain(t)) for t in tgt_chains}
cov = _coverage()
if all(s in ('None', '') for s in cov.values()):
    c.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    cov = _coverage()

def _norm(p):
    return p if '.' in p else p + '.' + p.rsplit('/', 1)[-1]
assets = [ar.get_asset_by_object_path(_norm(p)) for p in @anims@]
missing = [p for p, a in zip(@anims@, assets) if not a or not a.is_valid()]
if missing:
    raise RuntimeError('anims not found: ' + ', '.join(missing))
src_mesh = unreal.load_object(None, @source_mesh@)
tgt_mesh = unreal.load_object(None, @target_mesh@)
baked = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
    assets, src_mesh, tgt_mesh, rtg,
    search=@search@, replace=@replace@, include_referenced_assets=False)
paths = []
for a in baked:
    pkg = str(a.package_name)
    name = pkg.rsplit('/', 1)[-1]
    if @output_folder@:
        new_pkg = @output_folder@.rstrip('/') + '/' + name
        if eal.rename_asset(pkg + '.' + name, new_pkg + '.' + name):
            pkg = new_pkg
    paths.append(pkg + '.' + name)

# Motion probe: sample end bones of a few mapped chains at two times.
motion = {}
probe_bones = []
try:
    chains = unreal.IKRigController.get_controller(tgt_rig).get_retarget_chains()
    mapped = [ch for ch in chains if str(c.get_source_chain(str(ch.chain_name)))
              not in ('None', '')]
    mapped.sort(key=lambda ch: (0 if 'hand' in str(ch.chain_name).lower() else 1))
    for ch in mapped[:3]:
        # end_bone is a BoneReference struct — read its bone_name, never str()
        # the struct (that yields a repr and breaks the pose lookup silently).
        ref = ch.get_editor_property('end_bone')
        try:
            b = str(ref.get_editor_property('bone_name'))
        except Exception:
            b = str(ref)
        if b and b != 'None' and not b.startswith('<'):
            probe_bones.append(b)
except Exception:
    pass
if probe_bones:
    W = unreal.AnimPoseSpaces.WORLD
    opts = unreal.AnimPoseEvaluationOptions()
    for p in paths:
        a = unreal.load_object(None, p)
        if not a:
            continue
        ln = a.get_editor_property('sequence_length')
        t1 = min(2.0, max(0.1, ln * 0.5))
        best = 0.0
        p0 = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, opts)
        p1 = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t1, opts)
        for b in probe_bones:
            try:
                d = (unreal.AnimPoseExtensions.get_bone_pose(p1, b, W).translation
                     - unreal.AnimPoseExtensions.get_bone_pose(p0, b, W).translation).length()
                best = max(best, d)
            except Exception:
                pass
        motion[p.rsplit('/', 1)[-1].split('.')[0]] = round(best, 3)
result = {'ops_added': ops_added, 'coverage': cov, 'baked': paths,
          'motion': motion, 'probe_bones': probe_bones}
'''

_DELETE_BAKED = '''
import unreal
for p in @paths@:
    unreal.EditorAssetLibrary.delete_asset(p)
result = 'deleted'
'''


def batch_retarget(
    anims: list[str],
    source_mesh: str,
    target_mesh: str,
    retargeter: str,
    search: str = "",
    replace: str = "",
    output_folder: str = "",
    delete_on_fail: bool = True,
) -> str:
    """Duplicate-and-retarget animations through an IK Retargeter, loudly.

    Wraps IKRetargetBatchOperation with the guards UE omits: installs the
    default ops stack if the retargeter has none (else auto_map_chains
    silently maps nothing), auto-maps chains when the mapping is empty,
    reports chain coverage, and motion-probes every baked clip so a silent
    ref-pose bake becomes a hard error instead of 28 junk assets. Failed
    output is deleted unless delete_on_fail=False.

    anims are /Game object or package paths; output_folder (optional)
    moves the baked clips there. Does not touch retarget poses — arm-pose
    calibration is rig setup (see RTG_UE4ToYuki for the pattern).
    """
    rep = _send_json(_py(
        _RETARGET, retargeter=retargeter, anims=anims,
        source_mesh=source_mesh, target_mesh=target_mesh,
        search=search, replace=replace, output_folder=output_folder))
    if "raw" in rep:
        return json.dumps({"ok": False, "errors": [rep["raw"]]}, indent=2)
    verdict = evaluate_retarget_output(rep.get("coverage", {}),
                                       rep.get("motion", {}))
    out = {
        "ok": verdict["ok"],
        "baked": rep.get("baked", []),
        "errors": verdict["errors"],
        "warnings": verdict["warnings"],
        "unmapped_chains": verdict["unmapped"],
        "dead_clips": verdict["dead_clips"],
        "ops_stack_installed": rep.get("ops_added", False),
        "probe_bones": rep.get("probe_bones", []),
    }
    if not verdict["ok"] and delete_on_fail and rep.get("baked"):
        _send_json(_py(_DELETE_BAKED, paths=rep["baked"]))
        out["baked"] = []
        out["errors"].append(
            f"{len(rep['baked'])} junk output(s) deleted (delete_on_fail=True).")
    return json.dumps(out, indent=2)


# Explicit registration keeps the module attributes as plain callables
# so validate scripts can drive the tools without the MCP layer.
mcp.tool()(preview_animation)
mcp.tool()(batch_retarget)
