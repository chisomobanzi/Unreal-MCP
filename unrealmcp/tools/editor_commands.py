"""Editor commands via C++ TCP bridge — actors, materials, physics, screenshots."""

from typing import Any, Dict, List, Optional

from unrealmcp._bridge import mcp
from unrealmcp._tcp_bridge import _call


# -- Actors ------------------------------------------------------------------

@mcp.tool()
def get_actors_in_level(class_filter: str = "", max_results: int = 200) -> str:
    """List actors in the current EDITOR level (never the PIE world).

    Args:
        class_filter: Case-sensitive substring on the actor's class name
            (e.g. "Light", "StaticMeshActor") — filters server-side.
        max_results: Cap on returned actors (default 200); total_matched
            reports the full count.
    """
    params = {}
    if class_filter:
        params["class_filter"] = class_filter
    if max_results != 200:
        params["max_results"] = max_results
    return _call("get_actors_in_level", params)


@mcp.tool()
def find_actors_by_name(pattern: str, max_results: int = 100) -> str:
    """Find actors whose object name OR display label contains the pattern."""
    params = {"pattern": pattern}
    if max_results != 100:
        params["max_results"] = max_results
    return _call("find_actors_by_name", params)


@mcp.tool()
def spawn_actor(
    name: str,
    actor_type: str = "StaticMeshActor",
    location: List[float] = None,
    rotation: List[float] = None,
    scale: List[float] = None,
    static_mesh: str = "",
) -> str:
    """Spawn an actor in the level.

    Args:
        name: Actor name
        actor_type: StaticMeshActor, PointLight, etc.
        location/rotation/scale: [x,y,z] arrays
        static_mesh: Path to static mesh asset (for mesh actors)
    """
    params: Dict[str, Any] = {"name": name, "type": actor_type}
    if location:
        params["location"] = location
    if rotation:
        params["rotation"] = rotation
    if scale:
        params["scale"] = scale
    if static_mesh:
        params["static_mesh"] = static_mesh
    return _call("spawn_actor", params)


@mcp.tool()
def delete_actor(name: str) -> str:
    """Delete an actor from the level by name."""
    return _call("delete_actor", {"name": name})


@mcp.tool()
def set_actor_transform(
    name: str,
    location: List[float] = None,
    rotation: List[float] = None,
    scale: List[float] = None,
) -> str:
    """Set an actor's location, rotation, and/or scale."""
    params: Dict[str, Any] = {"name": name}
    if location:
        params["location"] = location
    if rotation:
        params["rotation"] = rotation
    if scale:
        params["scale"] = scale
    return _call("set_actor_transform", params)


@mcp.tool()
def spawn_blueprint_actor(
    blueprint_path: str,
    location: List[float] = None,
    rotation: List[float] = None,
    scale: List[float] = None,
    name: str = "",
) -> str:
    """Spawn a Blueprint actor instance in the level."""
    params: Dict[str, Any] = {"blueprint_path": blueprint_path}
    if location:
        params["location"] = location
    if rotation:
        params["rotation"] = rotation
    if scale:
        params["scale"] = scale
    if name:
        params["name"] = name
    return _call("spawn_blueprint_actor", params)


# -- Blueprint Component/Material Tools -------------------------------------

@mcp.tool()
def set_static_mesh_properties(blueprint_name: str, component_name: str,
                               static_mesh: str = "/Engine/BasicShapes/Cube.Cube") -> str:
    """Set the static mesh on a StaticMeshComponent in a Blueprint."""
    return _call("set_static_mesh_properties", {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
        "static_mesh": static_mesh,
    })


@mcp.tool()
def set_physics_properties(
    blueprint_name: str,
    component_name: str,
    simulate_physics: bool = True,
    gravity_enabled: bool = True,
    mass: float = 1.0,
    linear_damping: float = 0.01,
    angular_damping: float = 0.0,
) -> str:
    """Set physics properties on a Blueprint component."""
    return _call("set_physics_properties", {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
        "simulate_physics": simulate_physics,
        "gravity_enabled": gravity_enabled,
        "mass": mass,
        "linear_damping": linear_damping,
        "angular_damping": angular_damping,
    })


@mcp.tool()
def set_mesh_material_color(
    blueprint_name: str,
    component_name: str,
    color: List[float],
    material_path: str = "/Engine/BasicShapes/BasicShapeMaterial",
    parameter_name: str = "BaseColor",
    material_slot: int = 0,
) -> str:
    """Set material color on a mesh component.

    color: [R, G, B, A] values 0-1
    """
    return _call("set_mesh_material_color", {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
        "color": color,
        "material_path": material_path,
        "parameter_name": parameter_name,
        "material_slot": material_slot,
    })


@mcp.tool()
def get_available_materials(
    search_path: str = "/Game/",
    include_engine_materials: bool = True,
    filter: str | None = None,
    max_results: int = 100,
) -> str:
    """List materials that can be applied to objects.

    Args:
        search_path: Content Browser root to scan (e.g. "/Game/Weapons/")
        include_engine_materials: Include /Engine/ materials in results
        filter: Case-insensitive substring match on material asset name
        max_results: Cap on returned entries. 0 = unlimited (use sparingly)
    """
    params: dict = {
        "search_path": search_path,
        "include_engine_materials": include_engine_materials,
        "max_results": max_results,
    }
    if filter is not None:
        params["filter"] = filter
    return _call("get_available_materials", params)


@mcp.tool()
def apply_material_to_actor(actor_name: str, material_path: str, material_slot: int = 0) -> str:
    """Apply a material to a level actor."""
    return _call("apply_material_to_actor", {
        "actor_name": actor_name,
        "material_path": material_path,
        "material_slot": material_slot,
    })


@mcp.tool()
def apply_material_to_blueprint(
    blueprint_name: str, component_name: str,
    material_path: str, material_slot: int = 0,
) -> str:
    """Apply a material to a Blueprint component."""
    return _call("apply_material_to_blueprint", {
        "blueprint_name": blueprint_name,
        "component_name": component_name,
        "material_path": material_path,
        "material_slot": material_slot,
    })


@mcp.tool()
def get_actor_material_info(actor_name: str) -> str:
    """Get material slot info for a level actor."""
    return _call("get_actor_material_info", {"actor_name": actor_name})


@mcp.tool()
def get_blueprint_material_info(blueprint_name: str) -> str:
    """Get material slot info for a Blueprint's mesh components."""
    return _call("get_blueprint_material_info", {"blueprint_name": blueprint_name})


# -- Screenshot --------------------------------------------------------------

@mcp.tool()
def take_screenshot(file_path: str = "", mode: str = "viewport", max_width: int = 0) -> str:
    """Capture the editor to a PNG.

    mode "viewport" captures the level viewport; "window" captures the
    entire active editor window (material editor, BP graph, widget designer, etc.).

    The level viewport is redrawn and the render queue flushed before
    capture, so scene changes made just beforehand are included (no stale
    frame). max_width (>= 64) downscales the PNG proportionally before
    encoding — use ~1280 for verify loops to cut image-token cost.

    Returns the file path, width, and height. Use Read tool to view the image.
    """
    params = {}
    if file_path:
        params["file_path"] = file_path
    if mode != "viewport":
        params["mode"] = mode
    if max_width:
        params["max_width"] = max_width
    return _call("take_screenshot", params)


# -- PIE control --------------------------------------------------------------

@mcp.tool()
def start_pie(simulate: bool = False) -> str:
    """Start a Play-In-Editor session (async — starts on the next editor tick).

    simulate=True starts Simulate-in-Editor (keeps the editor camera, no pawn
    possession) — usually what you want for visual verification. Poll
    get_pie_status to confirm the session is up, then take_screenshot to see
    the running world.
    """
    return _call("start_pie", {"simulate": simulate} if simulate else {})


@mcp.tool()
def stop_pie() -> str:
    """End the current Play-In-Editor session (async — ends on the next tick)."""
    return _call("stop_pie")


@mcp.tool()
def get_pie_status() -> str:
    """Whether a PIE session is running, its world name, and simulate state."""
    return _call("get_pie_status")


# -- Viewport control ---------------------------------------------------------

@mcp.tool()
def frame_actor(actor: str) -> str:
    """Aim the level viewport at an actor (same as selecting it and pressing F).

    Accepts the actor's display label, object name, or path. Follow with
    take_screenshot to see the result.
    """
    return _call("frame_actor", {"actor": actor})


@mcp.tool()
def set_viewport_camera(
    location: list[float],
    rotation: list[float] | None = None,
    look_at: list[float] | None = None,
) -> str:
    """Place the level viewport camera exactly.

    Args:
        location: [X, Y, Z] world position.
        rotation: [Pitch, Yaw, Roll] degrees — ignored if look_at is given.
        look_at: [X, Y, Z] target; the camera is rotated to face it.
    """
    params: dict = {"location": location}
    if look_at is not None:
        params["look_at"] = look_at
    elif rotation is not None:
        params["rotation"] = rotation
    return _call("set_viewport_camera", params)


# -- Console ------------------------------------------------------------------

@mcp.tool()
def execute_console_command(command: str) -> str:
    """Run an editor console command (stat fps, r.ScreenPercentage 50, ...).

    Returns handled=false when the engine didn't recognize the command (note:
    some cvar writes report unhandled but still apply via the cvar sink).
    """
    return _call("execute_console_command", {"command": command})
