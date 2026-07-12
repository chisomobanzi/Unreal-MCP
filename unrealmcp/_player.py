"""Agent-as-player: pawn control + runtime readback inside a PIE session.

All logic runs in-editor as Python snippets over execute_python — no C++
rebuild needed to iterate. Each public function returns a parsed dict.

Conventions baked in (project-hardened):
- Destinations are ALWAYS projected to the navmesh floor
  (ProjectPointToNavigation, extent 250/250/400 — the shipped VR-teleport
  fix), then offset up by the pawn's capsule half-height when it has one:
  a Character stands half-height above the floor, a VR pawn's origin IS the
  floor. Never raw-set an authored Z.
- Rotation goes through SetControlRotation as well as the pawn, matching
  RoomNavWingWidget::ExecuteTeleport.
"""
from __future__ import annotations

import json
from typing import Any, Dict, Optional


def _exec(code: str, timeout: Optional[float] = None) -> Dict[str, Any]:
    from unrealmcp._tcp_bridge import _tcp_send_raw
    resp = _tcp_send_raw("execute_python", {"code": code})
    if resp.get("status") == "error":
        return {"ok": False, "error": resp.get("error", "unknown")}
    result = resp.get("result", {})
    payload = result.get("result") if isinstance(result, dict) else result
    if isinstance(payload, str):
        try:
            return json.loads(payload)
        except json.JSONDecodeError:
            return {"ok": False, "error": f"unparseable snippet result: {payload[:300]}"}
    if isinstance(payload, dict):
        return payload
    return {"ok": False, "error": f"unexpected result shape: {str(result)[:300]}"}


# Shared in-editor helpers, prepended to every snippet.
PRELUDE = r'''
import unreal, json, math

def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()

def _pawn_pc(w):
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    return pawn, pc

def _v(vec):
    return [round(vec.x, 1), round(vec.y, 1), round(vec.z, 1)]

def _r(rot):
    return [round(rot.pitch, 1), round(rot.yaw, 1), round(rot.roll, 1)]

def _label(a):
    try:
        return a.get_actor_label()
    except Exception:
        return a.get_name()

def _find_actor(w, query):
    q = query.lower()
    best = None
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
        lbl = _label(a).lower()
        nm = a.get_name().lower()
        if q == lbl or q == nm:
            return a
        if best is None and (q in lbl or q in nm):
            best = a
    return best

def _project_to_floor(w, pawn, target):
    """Navmesh-project target, then add the pawn's capsule half-height.
    Returns (dest_vector, info_dict)."""
    proj = unreal.NavigationSystemV1.project_point_to_navigation(
        w, target, None, None, query_extent=unreal.Vector(250.0, 250.0, 400.0))
    dest = unreal.Vector(proj.x, proj.y, proj.z)
    half = 0.0
    cap = pawn.get_component_by_class(unreal.CapsuleComponent)
    if cap:
        half = cap.get_scaled_capsule_half_height()
        dest.z += half
    return dest, {"projected_floor": _v(proj), "capsule_half_height": round(half, 1)}

def _face(pc, pawn, target):
    src = pawn.get_actor_location()
    rot = unreal.MathLibrary.find_look_at_rotation(src, target)
    rot.roll = 0.0
    if pc:
        pc.set_control_rotation(rot)
    pawn.set_actor_rotation(unreal.Rotator(0.0, rot.yaw, 0.0), False)
    return rot
'''


def get_pawn_state() -> Dict[str, Any]:
    return _exec(PRELUDE + r'''
out = {"ok": False}
w = _world()
if not w:
    out["error"] = "PIE not running"
else:
    pawn, pc = _pawn_pc(w)
    if not pawn:
        out["error"] = "no player pawn"
    else:
        out = {
            "ok": True,
            "world": w.get_name(),
            "pawn_class": pawn.get_class().get_name(),
            "pawn_label": _label(pawn),
            "location": _v(pawn.get_actor_location()),
            "rotation": _r(pawn.get_actor_rotation()),
            "control_rotation": _r(pc.get_control_rotation()) if pc else None,
            "velocity": _v(pawn.get_velocity()),
        }
        cam = pc.player_camera_manager if pc else None
        if cam:
            out["camera_location"] = _v(cam.get_camera_location())
            out["camera_rotation"] = _r(cam.get_camera_rotation())
result = json.dumps(out)
''')


def move_to(x: Optional[float] = None, y: Optional[float] = None,
            z: Optional[float] = None, actor: str = "",
            face_target: bool = True) -> Dict[str, Any]:
    target_expr = (
        f"_find_actor(w, {actor!r})" if actor else
        f"unreal.Vector({float(x or 0)}, {float(y or 0)}, {float(z or 0)})")
    return _exec(PRELUDE + f'''
out = {{"ok": False}}
w = _world()
if not w:
    out["error"] = "PIE not running"
else:
    pawn, pc = _pawn_pc(w)
    if not pawn:
        out["error"] = "no player pawn"
    else:
        tgt = {target_expr}
        if tgt is None:
            out["error"] = "target actor not found: " + {actor!r}
        else:
            is_actor = isinstance(tgt, unreal.Actor)
            raw = tgt.get_actor_location() if is_actor else tgt
            dest, info = _project_to_floor(w, pawn, raw)
            rot = pawn.get_actor_rotation()
            pawn.set_actor_location_and_rotation(dest, unreal.Rotator(0.0, rot.yaw, 0.0), False, True)
            if {bool(face_target)} and is_actor:
                _face(pc, pawn, raw)
            final = pawn.get_actor_location()
            out = {{"ok": True, "requested": _v(raw), "dest": _v(dest),
                    "final": _v(final),
                    "target_actor": _label(tgt) if is_actor else None}}
            out.update(info)
result = json.dumps(out)
''')


def look_at(x: Optional[float] = None, y: Optional[float] = None,
            z: Optional[float] = None, actor: str = "") -> Dict[str, Any]:
    target_expr = (
        f"_find_actor(w, {actor!r})" if actor else
        f"unreal.Vector({float(x or 0)}, {float(y or 0)}, {float(z or 0)})")
    return _exec(PRELUDE + f'''
out = {{"ok": False}}
w = _world()
if not w:
    out["error"] = "PIE not running"
else:
    pawn, pc = _pawn_pc(w)
    if not pawn:
        out["error"] = "no player pawn"
    else:
        tgt = {target_expr}
        if tgt is None:
            out["error"] = "target actor not found: " + {actor!r}
        else:
            raw = tgt.get_actor_location() if isinstance(tgt, unreal.Actor) else tgt
            rot = _face(pc, pawn, raw)
            out = {{"ok": True, "target": _v(raw), "control_rotation": _r(rot)}}
result = json.dumps(out)
''')


def teleport_to_room(room: str) -> Dict[str, Any]:
    return _exec(PRELUDE + f'''
out = {{"ok": False}}
w = _world()
if not w:
    out["error"] = "PIE not running"
else:
    pawn, pc = _pawn_pc(w)
    if not pawn:
        out["error"] = "no player pawn"
    else:
        q = {room!r}.lower().replace(" ", "")
        def _match(a):
            s = (_label(a) + " " + a.get_name()).lower().replace(" ", "").replace("_", "")
            return q.replace("_", "") in s
        # PlayerStarts first (authored spawn points), then RoomHub-ish actors,
        # then anything whose label matches.
        cand, kind = None, None
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerStart):
            tag = str(a.get_editor_property("player_start_tag")).lower().replace(" ", "")
            if _match(a) or q in tag:
                cand, kind = a, "PlayerStart"
                break
        if cand is None:
            hub_cls = getattr(unreal, "RoomHub", None)
            if hub_cls:
                for a in unreal.GameplayStatics.get_all_actors_of_class(w, hub_cls):
                    if _match(a):
                        cand, kind = a, "RoomHub"
                        break
        if cand is None:
            for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
                if _match(a):
                    cand, kind = a, a.get_class().get_name()
                    break
        if cand is None:
            out["error"] = "no actor matches room: " + {room!r}
        else:
            raw = cand.get_actor_location()
            dest, info = _project_to_floor(w, pawn, raw)
            rot = cand.get_actor_rotation()
            pawn.set_actor_location_and_rotation(dest, unreal.Rotator(0.0, rot.yaw, 0.0), False, True)
            if pc:
                pc.set_control_rotation(unreal.Rotator(0.0, rot.yaw, 0.0))
            out = {{"ok": True, "resolved": _label(cand), "kind": kind,
                    "raw_target": _v(raw), "final": _v(pawn.get_actor_location())}}
            out.update(info)
result = json.dumps(out)
''')
