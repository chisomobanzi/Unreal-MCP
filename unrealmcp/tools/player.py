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
def teleport_to_room(room: str) -> str:
    """Teleport the pawn to a named room (e.g. "Observatory", "Engine").

    Resolves the destination from PlayerStarts (label/tag match), then
    RoomHub actors, then any label-matching actor; nav-projects to the
    floor and aligns the pawn to the target's facing.
    """
    return json.dumps(_player.teleport_to_room(room), indent=2)
