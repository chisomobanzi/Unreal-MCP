"""Agent-as-player tools: drive the possessed pawn in a live PIE session.

All of these require PIE to be running (start_pie first). Logic lives in
unrealmcp._player and executes in-editor over execute_python.
"""
import json

from unrealmcp._bridge import mcp
from unrealmcp import _player


@mcp.tool()
def get_pawn_state() -> str:
    """Snapshot the possessed PIE pawn: class, location, rotation,
    control rotation, velocity, and camera pose.

    The baseline read before/after any movement or interaction — verify
    moves from this state, not from screenshots.
    """
    return json.dumps(_player.get_pawn_state(), indent=2)


@mcp.tool()
def move_to(x: float = 0.0, y: float = 0.0, z: float = 0.0,
            actor: str = "", face_target: bool = True) -> str:
    """Teleport-move the pawn to a point or a named actor, floor-safe.

    The destination is ALWAYS projected onto the navmesh floor and then
    raised by the pawn's capsule half-height — passing an authored/elevated
    Z is fine and gets corrected (the "half a body too high" bug is
    impossible by construction). `actor` matches label or name,
    case-insensitive substring; when set, x/y/z are ignored and the pawn
    also turns to face it (face_target).
    """
    return json.dumps(_player.move_to(x, y, z, actor, face_target), indent=2)


@mcp.tool()
def look_at(x: float = 0.0, y: float = 0.0, z: float = 0.0,
            actor: str = "") -> str:
    """Turn the pawn + controller to face a point or a named actor.

    Sets both SetControlRotation and the pawn yaw (roll zeroed), the same
    convention as the project's room-nav teleport.
    """
    return json.dumps(_player.look_at(x, y, z, actor), indent=2)


@mcp.tool()
def stand_before(actor: str, distance: float = 250.0) -> str:
    """Position the pawn in front of an actor's widget face, floor-projected,
    facing it — the stance a player takes before interacting with a panel."""
    return json.dumps(_player.stand_before(actor, distance), indent=2)


@mcp.tool()
def hand_ray_select(target: str, button: str = "LeftMouseButton") -> str:
    """Point the VR-style interaction ray at a target's widget and pull the
    trigger (press + release the pointer key).

    Drives the same engine pipeline as the VR hand-ray
    (WidgetInteractionComponent). The ray is posed just off the widget face;
    if the target's own mesh occludes its widget on the interaction channel,
    the click is retried with the occluders bypassed and the result carries
    needed_occluder_bypass: true — that means a REAL player's ray cannot
    reach this widget: surface it as a project finding.
    Verify the outcome via get_runtime_state / get_widget_tree, not
    screenshots.
    """
    return json.dumps(_player.hand_ray_select(target, button), indent=2)


@mcp.tool()
def interact(target: str, button_name: str = "") -> str:
    """Non-VR fallback interaction: directly fire a Button's OnClicked inside
    the target actor's widget (bypasses hit-testing entirely). Use when you
    need the gameplay effect regardless of physical reachability."""
    return json.dumps(_player.interact(target, button_name), indent=2)


@mcp.tool()
def get_runtime_state(actor_filter: str = "", class_filter: str = "",
                      include_components: bool = False,
                      include_anim: bool = False, max_actors: int = 40) -> str:
    """Compact PIE snapshot: actors matching the filters with locations,
    optional component/animation info, and widget-component hit-testability
    (collision + blocks-interaction-channel). The primary way to VERIFY
    interactions and game state — prefer this over screenshots."""
    return json.dumps(_player.get_runtime_state(
        actor_filter, class_filter, include_components, include_anim,
        max_actors), indent=2)


@mcp.tool()
def get_widget_tree_runtime(actor: str = "", include_viewport: bool = False) -> str:
    """Live widget tree(s) for an actor's widget components (and/or viewport
    widgets): every widget's name, class, visibility, enabled state, text
    content, and object path. Assert UI outcomes from this (e.g. 'the detail
    panel now shows Fluency')."""
    return json.dumps(_player.get_widget_tree(actor, include_viewport), indent=2)


@mcp.tool()
def playthrough(flow_json: str, max_restarts: int = 1) -> str:
    """Run a scripted VR flow unattended, beat by beat, crash-safe.

    flow_json: {"name": ..., "beats": [{"action": ..., "args": {...}}, ...]}
    Actions: load_map, start_pie, stop_pie, wait, capture, exec, raw,
    get_pawn_state, teleport_to_room, move_to, look_at, stand_before,
    hand_ray_select, interact, assert_state.

    assert_state args: pie_active, pawn_near {x,y,z,tolerance},
    widget_text_contains {actor,text}, log_contains (project log since the
    previous action). Unattended mode is enabled for the run (dialogs
    auto-default); an editor crash triggers the watchdog and one full
    replay. Returns the per-beat report + bridge events.
    """
    from unrealmcp._playthrough import run_playthrough
    try:
        flow = json.loads(flow_json)
        return json.dumps(run_playthrough(flow, max_restarts=max_restarts),
                          indent=2, default=str)
    except Exception as exc:
        return f"Error: {exc}"


@mcp.tool()
def teleport_to_room(room: str) -> str:
    """Teleport the pawn to a named room (e.g. "Observatory", "Engine").

    Resolves the destination from PlayerStarts (label/tag match), then
    RoomHub actors, then any label-matching actor; nav-projects to the
    floor and aligns the pawn to the target's facing.
    """
    return json.dumps(_player.teleport_to_room(room), indent=2)
