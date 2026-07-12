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


WIDGET_HELPERS = r'''
def _widget_comp_info(c):
    resp = c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)
    info = {"component": c.get_name(),
            "widget_class": None, "collision_enabled": str(c.get_collision_enabled()),
            "blocks_visibility": resp == unreal.CollisionResponseType.ECR_BLOCK,
            "visible": c.is_visible()}
    wobj = c.get_widget()
    if wobj:
        info["widget_class"] = wobj.get_class().get_name()
    return info
'''


def get_runtime_state(actor_filter: str = "", class_filter: str = "",
                      include_components: bool = False,
                      include_anim: bool = False,
                      max_actors: int = 40) -> Dict[str, Any]:
    """Compact PIE snapshot: actors (+components/anim opt-in) with widget-
    component hit-testability, plus viewport widget classes. For full widget
    trees (text content), see get_widget_tree()."""
    return _exec(PRELUDE + WIDGET_HELPERS + f'''
out = {{"ok": False}}
w = _world()
if not w:
    out["error"] = "PIE not running"
else:
    af, cf = {actor_filter!r}.lower(), {class_filter!r}.lower()
    actors = []
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
        lbl, cls = _label(a), a.get_class().get_name()
        if af and af not in lbl.lower() and af not in a.get_name().lower():
            continue
        if cf and cf not in cls.lower():
            continue
        entry = {{"label": lbl, "class": cls, "location": _v(a.get_actor_location())}}
        if {bool(include_components)}:
            comps = []
            for c in a.get_components_by_class(unreal.ActorComponent):
                comps.append(c.get_class().get_name())
            entry["components"] = comps[:15]
        for c in a.get_components_by_class(unreal.WidgetComponent):
            entry.setdefault("widget_components", []).append(_widget_comp_info(c))
        if {bool(include_anim)}:
            for c in a.get_components_by_class(unreal.SkeletalMeshComponent):
                anim = c.get_anim_instance()
                if anim:
                    m = anim.get_current_active_montage()
                    entry.setdefault("anim", []).append(
                        {{"mesh": c.get_name(),
                          "montage": m.get_name() if m else None}})
        actors.append(entry)
        if len(actors) >= {int(max_actors)}:
            out["truncated"] = True
            break
    out["ok"] = True
    out["world"] = w.get_name()
    out["actors"] = actors
    vw = []
    try:
        for uw in unreal.WidgetLibrary.get_all_widgets_of_class(w, unreal.UserWidget, True):
            vw.append({{"class": uw.get_class().get_name(),
                        "visible": str(uw.get_visibility())}})
    except Exception as e:
        out["viewport_widgets_error"] = str(e)
    out["viewport_widgets"] = vw[:30]
result = json.dumps(out)
''')


HAND_RAY_SETUP = r'''
def _find_ray(pawn, ray_name):
    comps = pawn.get_components_by_class(unreal.WidgetInteractionComponent)
    for c in comps:
        if c.get_name() == ray_name:
            return c
    return comps[0] if comps else None

def _aim_ray(ray, target_pt):
    src = ray.get_world_location()
    rot = unreal.MathLibrary.find_look_at_rotation(src, target_pt)
    ray.set_world_rotation(rot, False, True)

def _target_point(a):
    # Prefer the widget component's own location (the clickable face).
    wcs = a.get_components_by_class(unreal.WidgetComponent)
    if wcs:
        return wcs[0].get_world_location()
    return a.get_actor_location()
'''


def _hand_ray_step(step_code: str) -> Dict[str, Any]:
    return _exec(PRELUDE + HAND_RAY_SETUP + step_code)


def stand_before(actor: str, distance: float = 250.0) -> Dict[str, Any]:
    """Position the pawn in front of the actor's widget face (its forward
    vector), floor-projected, facing the widget — the stance from which a
    hand-ray can actually hit it."""
    return _exec(PRELUDE + f'''
out = {{"ok": False}}
w = _world()
if not w:
    out["error"] = "PIE not running"
else:
    pawn, pc = _pawn_pc(w)
    a = _find_actor(w, {actor!r})
    if not pawn or a is None:
        out["error"] = "pawn or target missing: " + {actor!r}
    else:
        wcs = a.get_components_by_class(unreal.WidgetComponent)
        face = wcs[0] if wcs else None
        base = face.get_world_location() if face else a.get_actor_location()
        fwd = face.get_forward_vector() if face else a.get_actor_forward_vector()
        stand = unreal.Vector(base.x + fwd.x * {float(distance)},
                              base.y + fwd.y * {float(distance)},
                              base.z)
        dest, info = _project_to_floor(w, pawn, stand)
        pawn.set_actor_location_and_rotation(dest, pawn.get_actor_rotation(), False, True)
        _face(pc, pawn, base)
        out = {{"ok": True, "widget_face": _v(base), "final": _v(pawn.get_actor_location())}}
        out.update(info)
result = json.dumps(out)
''')


def hand_ray_select(target: str, button: str = "LeftMouseButton") -> Dict[str, Any]:
    """Point the widget-interaction ray at `target` and pull the trigger.

    Backed by the C++ hand_ray_click command: the ray is posed like a raised
    VR hand just off the widget face (aiming from the pawn's eyes gets
    blocked by the target's own mesh), the interaction simulation is driven
    deterministically, and the pointer key is pressed + released — the same
    engine pipeline (WidgetInteractionComponent) the real VR hand-ray uses.
    """
    from unrealmcp._tcp_bridge import _tcp_send_raw
    resp = _tcp_send_raw("hand_ray_click", {"target": target, "button": button})
    if resp.get("status") == "error":
        return {"ok": False, "error": resp.get("error")}
    out = resp.get("result", {})
    out["ok"] = bool(out.get("success")) and bool(out.get("clicked"))
    if not out["ok"] and not out.get("hovered"):
        # No line of sight — retry pressing "through" the target's own
        # occluding mesh (some authored actors enclose their readouts;
        # report it so the project issue is visible).
        resp = _tcp_send_raw("hand_ray_click",
                             {"target": target, "button": button,
                              "ignore_target_occluders": True})
        if resp.get("status") != "error":
            out = resp.get("result", {})
            out["ok"] = bool(out.get("success")) and bool(out.get("clicked"))
            out["needed_occluder_bypass"] = True
    return out


def _legacy_hand_ray_select(target: str, button: str = "LeftMouseButton") -> Dict[str, Any]:
    """Python-stepped variant kept for reference; superseded by hand_ray_click."""
    import time as _time
    from unrealmcp._tcp_bridge import _tcp_send_raw

    ray_resp = _tcp_send_raw("attach_interaction_ray", {})
    ray_result = ray_resp.get("result", {}) if ray_resp.get("status") != "error" else {}
    ray_name = ray_result.get("component", "")
    if not ray_name:
        return {"ok": False,
                "error": f"attach_interaction_ray failed: {ray_resp.get('error', ray_resp)}"}

    # Place the ray like a raised VR hand: ~120uu off the widget face along
    # the panel's own normal, firing straight at it. Absolute transform so
    # the camera (its attach parent) can't stomp the pose, and the short
    # normal-aligned gap guarantees line of sight — aiming from the pawn's
    # eyes gets blocked by the target's own mesh (bit us live: the pod's
    # StaticMesh ate the visibility trace).
    aim = _hand_ray_step(f'''
out = {{"ok": False}}
w = _world()
if not w:
    out["error"] = "PIE not running"
else:
    pawn, pc = _pawn_pc(w)
    a = _find_actor(w, {target!r})
    ray = _find_ray(pawn, {ray_name!r}) if pawn else None
    if not pawn or a is None or ray is None:
        out["error"] = "pawn/ray/target missing: " + {target!r}
    else:
        _face(pc, pawn, a.get_actor_location())   # body turns like a player
        wcs = a.get_components_by_class(unreal.WidgetComponent)
        face_comp = wcs[0] if wcs else None
        pt = face_comp.get_world_location() if face_comp else a.get_actor_location()
        fwd = face_comp.get_forward_vector() if face_comp else a.get_actor_forward_vector()
        hand = unreal.Vector(pt.x + fwd.x * 120.0, pt.y + fwd.y * 120.0,
                             pt.z + fwd.z * 120.0)
        ray.set_absolute(True, True, False)
        ray.set_world_location(hand, False, True)
        _aim_ray(ray, pt)
        out = {{"ok": True, "ray": ray.get_name(), "kind": {ray_result.get("kind", "?")!r},
                "target": _label(a), "aim_point": _v(pt), "hand": _v(hand)}}
result = json.dumps(out)
''')
    if not aim.get("ok"):
        return aim
    _time.sleep(0.4)  # let hover update over real ticks

    press = _hand_ray_step(f'''
out = {{"ok": False}}
w = _world()
pawn, pc = _pawn_pc(w)
ray = _find_ray(pawn, {ray_name!r})
hovered = ray.get_hovered_widget_component() if ray else None
if ray:
    _k = unreal.Key()
    _k.set_editor_property("key_name", {button!r})
    ray.press_pointer_key(_k)
    out = {{"ok": True,
            "hovered_before_press": _label(hovered.get_owner()) if hovered else None}}
    out["ray_active"] = ray.is_active()
    out["ray_ticking"] = ray.is_component_tick_enabled()
    out["ray_loc"] = _v(ray.get_world_location())
    out["ray_fwd"] = _v(ray.get_forward_vector())
    try:
        hit = ray.get_last_hit_result()
        vals = hit.to_tuple()
        ha = next((x for x in vals if isinstance(x, unreal.Actor)), None)
        hc = next((x for x in vals if isinstance(x, unreal.PrimitiveComponent)), None)
        out["last_hit_actor"] = _label(ha) if ha else None
        out["last_hit_component"] = hc.get_name() if hc else None
    except Exception as e:
        out["hit_debug_err"] = str(e)[:150]
else:
    out["error"] = "ray vanished"
result = json.dumps(out)
''')
    if not press.get("ok"):
        return press
    _time.sleep(0.25)

    release = _hand_ray_step(f'''
out = {{"ok": False}}
w = _world()
pawn, pc = _pawn_pc(w)
ray = _find_ray(pawn, {ray_name!r})
if ray:
    _k = unreal.Key()
    _k.set_editor_property("key_name", {button!r})
    ray.release_pointer_key(_k)
    hovered = ray.get_hovered_widget_component()
    out = {{"ok": True, "hovered": _label(hovered.get_owner()) if hovered else None}}
else:
    out["error"] = "ray vanished"
result = json.dumps(out)
''')
    return {"ok": bool(release.get("ok")),
            "aim": aim, "press": press, "release": release}


def get_widget_tree(actor: str = "", include_viewport: bool = False) -> Dict[str, Any]:
    """Runtime widget tree(s) with text + hit-testability, via the C++
    get_runtime_widget_tree command (live UserWidget trees aren't reachable
    from Python)."""
    from unrealmcp._tcp_bridge import _tcp_send_raw
    resp = _tcp_send_raw("get_runtime_widget_tree",
                         {"actor": actor, "include_viewport": include_viewport})
    if resp.get("status") == "error":
        return {"ok": False, "error": resp.get("error")}
    out = resp.get("result", {})
    out["ok"] = bool(out.get("success"))
    return out


def interact(target: str, button_name: str = "") -> Dict[str, Any]:
    """Non-VR fallback: directly fire a Button's OnClicked inside the
    target actor's widget component (bypasses hit-testing).

    Live widget trees aren't walkable from Python, so the button is found
    by name via the C++ tree readback, then resolved as a subobject of the
    widget instance (Outer chain: UserWidget → WidgetTree → Button)."""
    tree = get_widget_tree(actor=target)
    if not tree.get("ok"):
        return tree
    button_path = None
    for comp in tree.get("widget_components", []):
        for e in comp.get("tree", []):
            if e.get("class") == "Button" and e.get("path") and (
                    not button_name or button_name.lower() in e.get("name", "").lower()):
                button_path = e["path"]
                break
        if button_path:
            break
    if not button_path:
        return {"ok": False, "error": "no Button found in target widgets",
                "tree": tree}

    return _exec(PRELUDE + f'''
out = {{"ok": False}}
btn = unreal.find_object(None, {button_path!r})
if btn is None:
    out["error"] = "button object not resolvable: " + {button_path!r}
else:
    btn.on_clicked.broadcast()
    out = {{"ok": True, "clicked_button": btn.get_name(), "path": {button_path!r}}}
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
