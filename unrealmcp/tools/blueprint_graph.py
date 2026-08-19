"""Blueprint graph tools — node manipulation via C++ TCP bridge.

Wraps commands requiring direct K2Node API access not available through
the Python scripting plugin.
"""

from typing import Any, Dict, Optional

from unrealmcp._bridge import mcp
from unrealmcp._tcp_bridge import _call


# -- Graph Node Tools -------------------------------------------------------

@mcp.tool()
def add_blueprint_node(
    blueprint_name: str,
    node_type: str,
    pos_x: float = 0,
    pos_y: float = 0,
    message: str = "",
    event_type: str = "",
    variable_name: str = "",
    target_function: str = "",
    target_blueprint: str = "",
    function_name: str = "",
    component_name: str = "",
    delegate_name: str = "",
    subsystem_class: str = "",
) -> str:
    """Add a node to a Blueprint graph.

    Node types by category:
      FLOW:      Branch, Comparison, Switch, SwitchEnum, SwitchInteger, ExecutionSequence
      DATA:      VariableGet, VariableSet, MakeArray
      CASTING:   DynamicCast, ClassDynamicCast, CastByteToEnum
      UTILITY:   Print, CallFunction, Select, SpawnActor, ComponentBoundEvent, GetSubsystem
      SPECIAL:   Timeline, GetDataTableRow, AddComponentByClass, Self, Knot
      EVENT:     Event (specify event_type: BeginPlay, Tick, Destroyed, etc.)

    Extra args are only needed for specific node types (message for Print,
    variable_name for Variable nodes, target_function for CallFunction,
    component_name + delegate_name for ComponentBoundEvent,
    subsystem_class for GetSubsystem, etc.).
    """
    params: Dict[str, Any] = {"pos_x": pos_x, "pos_y": pos_y}
    # Only include optional fields when set — keeps the wire payload small
    for key, val in [
        ("message", message),
        ("event_type", event_type),
        ("variable_name", variable_name),
        ("target_function", target_function),
        ("target_blueprint", target_blueprint),
        ("function_name", function_name),
        ("component_name", component_name),
        ("delegate_name", delegate_name),
        ("subsystem_class", subsystem_class),
    ]:
        if val:
            params[key] = val

    return _call("add_blueprint_node", {
        "blueprint_name": blueprint_name,
        "node_type": node_type,
        "node_params": params,
    })


@mcp.tool()
def connect_blueprint_nodes(
    blueprint_name: str,
    source_node_id: str,
    source_pin_name: str,
    target_node_id: str,
    target_pin_name: str,
    function_name: str = "",
) -> str:
    """Connect two nodes in a Blueprint graph.

    Connections go through the K2 schema: invalid links are rejected with the
    schema's reason, and single-link pins (exec outputs, data inputs) get
    their existing link auto-broken instead of double-linking — any broken
    links come back under "broken_links". Pin-not-found errors list the
    node's available pins.

    Args:
        source_node_id: GUID of the source node
        source_pin_name: Output pin name on source
        target_node_id: GUID of the target node
        target_pin_name: Input pin name on target
        function_name: Function graph (empty = EventGraph)
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "source_node_id": source_node_id,
        "source_pin_name": source_pin_name,
        "target_node_id": target_node_id,
        "target_pin_name": target_pin_name,
    }
    if function_name:
        params["function_name"] = function_name
    return _call("connect_nodes", params)


@mcp.tool()
def disconnect_blueprint_nodes(
    blueprint_name: str,
    source_node_id: str,
    source_pin_name: str,
    target_node_id: str = "",
    target_pin_name: str = "",
    function_name: str = "",
) -> str:
    """Break Blueprint pin connections.

    With target_node_id + target_pin_name, breaks just that one link;
    otherwise breaks every link on the named pin. The pin may be given from
    either end (output or input).
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "source_node_id": source_node_id,
        "source_pin_name": source_pin_name,
    }
    for key, val in [
        ("target_node_id", target_node_id),
        ("target_pin_name", target_pin_name),
        ("function_name", function_name),
    ]:
        if val:
            params[key] = val
    return _call("disconnect_nodes", params)


@mcp.tool()
def create_blueprint_variable(
    blueprint_name: str,
    variable_name: str,
    variable_type: str,
    default_value: str = "",
    is_public: bool = False,
    tooltip: str = "",
    category: str = "Default",
) -> str:
    """Create a variable in a Blueprint.

    variable_type: bool, int, float, string, vector, rotator
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "variable_name": variable_name,
        "variable_type": variable_type,
        "is_public": is_public,
        "tooltip": tooltip,
        "category": category,
    }
    if default_value:
        params["default_value"] = default_value
    return _call("create_variable", params)


@mcp.tool()
def set_blueprint_variable_properties(
    blueprint_name: str,
    variable_name: str,
    var_name: str = "",
    var_type: str = "",
    is_public: bool = None,
    is_editable_in_instance: bool = None,
    tooltip: str = "",
    category: str = "",
    default_value: str = "",
    expose_on_spawn: bool = None,
    replication_enabled: bool = None,
    replication_condition: int = None,
) -> str:
    """Modify properties of an existing Blueprint variable.

    All parameters besides blueprint_name and variable_name are optional —
    only provided values are changed.
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "variable_name": variable_name,
    }
    # Only send fields that were explicitly provided
    for key, val in [
        ("var_name", var_name),
        ("var_type", var_type),
        ("tooltip", tooltip),
        ("category", category),
        ("default_value", default_value),
    ]:
        if val:
            params[key] = val
    for key, val in [
        ("is_public", is_public),
        ("is_editable_in_instance", is_editable_in_instance),
        ("expose_on_spawn", expose_on_spawn),
        ("replication_enabled", replication_enabled),
        ("replication_condition", replication_condition),
    ]:
        if val is not None:
            params[key] = val
    return _call("set_blueprint_variable_properties", params)


@mcp.tool()
def add_event_node(
    blueprint_name: str,
    event_name: str,
    pos_x: float = 0,
    pos_y: float = 0,
) -> str:
    """Add an event override node to the event graph.

    Handles parent-class events (C++ BlueprintImplementableEvent /
    BlueprintNativeEvent, parent-Blueprint events) and interface events —
    the node is placed as a proper override of the declaring class. Friendly
    aliases work: BeginPlay, EndPlay, Tick, ActorBeginOverlap, Hit, etc.
    Returns the existing node if the event is already placed.

    For NEW events that don't exist on any parent, use add_custom_event_node.
    For binding to delegates (OnX events of components), use add_delegate_node.
    """
    return _call("add_event_node", {
        "blueprint_name": blueprint_name,
        "event_name": event_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    })


@mcp.tool()
def add_custom_event_node(
    blueprint_name: str,
    event_name: str,
    parameters: Optional[list] = None,
    pos_x: float = 0,
    pos_y: float = 0,
) -> str:
    """Add a custom event node, optionally with parameters.

    parameters: list of {"name": str, "type": str, "is_array": bool} dicts.
    Types: bool, byte, int, int64, float, string, name, text, vector,
    rotator, transform, linearcolor, object:<Class>, class:<Class>,
    struct:<Path>, enum:<Path>.

    Example: parameters=[{"name": "Amount", "type": "float"},
                         {"name": "Source", "type": "object:Actor"}]
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "event_name": event_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }
    if parameters:
        params["parameters"] = parameters
    return _call("add_custom_event_node", params)


@mcp.tool()
def add_delegate_node(
    blueprint_name: str,
    delegate_name: str,
    owner_class: str = "self",
    mode: str = "Assign",
    event_name: str = "",
    function_name: str = "",
    pos_x: float = 0,
    pos_y: float = 0,
) -> str:
    """Add a multicast-delegate node (Bind/Unbind/Call/Assign).

    Modes:
      Assign (default) — Bind node + a matching custom event created from the
        delegate signature, pre-connected. The classic "Assign OnX" action.
      Add / Remove / Call — just that delegate node.

    owner_class: class owning the delegate property. "self" = this Blueprint;
    otherwise a C++ class name (e.g. "TutorAudioStreamComponent") or a
    /Game/... Blueprint path. When the delegate is not on self, connect an
    instance to the node's target pin afterwards.

    event_name: name for the bound event in Assign mode (default
    <DelegateName>_Event). Unknown delegate names return the class's
    available delegates in the error.
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "delegate_name": delegate_name,
        "owner_class": owner_class,
        "mode": mode,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }
    for key, val in [("event_name", event_name), ("function_name", function_name)]:
        if val:
            params[key] = val
    return _call("add_delegate_node", params)


@mcp.tool()
def add_parent_call_node(
    blueprint_name: str,
    function_name: str,
    graph_function_name: str = "",
    pos_x: float = 0,
    pos_y: float = 0,
) -> str:
    """Add a "Call to Parent Function" node (e.g. Parent: BeginPlay).

    function_name: the parent-class function to call.
    graph_function_name: which graph to place it in (empty = EventGraph).
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }
    if graph_function_name:
        params["graph_function_name"] = graph_function_name
    return _call("add_parent_call_node", params)


@mcp.tool()
def trace_blueprint_graph(
    blueprint_name: str,
    start_node_id: str = "",
    event_name: str = "",
    function_name: str = "",
    direction: str = "forward",
    max_depth: int = 50,
    include_data: bool = True,
) -> str:
    """Trace the execution path through a Blueprint graph.

    Follows exec pins from a start node or event and returns the ordered
    chain — "what runs after X?" (forward) or "what leads to X?" (backward)
    — without dumping the whole graph.

    Give either start_node_id (searched across all graphs) or event_name
    (matches overridden events, custom events, and aliases like "BeginPlay").
    Returns a nested trace tree plus linear_sequence, the flat straight-line
    prefix of titles. include_data adds each node's data inputs (literals or
    the feeding node).
    """
    params: Dict[str, Any] = {
        "blueprint_name": blueprint_name,
        "direction": direction,
        "max_depth": max_depth,
        "include_data": include_data,
    }
    for key, val in [
        ("start_node_id", start_node_id),
        ("event_name", event_name),
        ("function_name", function_name),
    ]:
        if val:
            params[key] = val
    return _call("trace_graph", params)


@mcp.tool()
def delete_blueprint_node(
    blueprint_name: str,
    node_id: str,
    function_name: str = "",
) -> str:
    """Delete a node from a Blueprint graph by GUID."""
    params: Dict[str, Any] = {"blueprint_name": blueprint_name, "node_id": node_id}
    if function_name:
        params["function_name"] = function_name
    return _call("delete_node", params)


@mcp.tool()
def set_blueprint_node_property(
    blueprint_name: str,
    node_id: str,
    property_name: str = "",
    property_value: str = "",
    function_name: str = "",
    action: str = "",
    pin_type: str = "",
    pin_name: str = "",
    enum_type: str = "",
    new_type: str = "",
    target_type: str = "",
    target_function: str = "",
    target_class: str = "",
    event_type: str = "",
) -> str:
    """Set a node property or perform semantic editing.

    Semantic actions (pass via 'action'):
      add_pin, remove_pin, set_enum_type, set_pin_type,
      set_value_type, set_cast_target, set_function_call, set_event_type

    Each action has associated params (pin_type, pin_name, enum_type, new_type,
    target_type, target_function, target_class, event_type).
    """
    params: Dict[str, Any] = {"blueprint_name": blueprint_name, "node_id": node_id}
    for key, val in [
        ("property_name", property_name),
        ("property_value", property_value),
        ("function_name", function_name),
        ("action", action),
        ("pin_type", pin_type),
        ("pin_name", pin_name),
        ("enum_type", enum_type),
        ("new_type", new_type),
        ("target_type", target_type),
        ("target_function", target_function),
        ("target_class", target_class),
        ("event_type", event_type),
    ]:
        if val:
            params[key] = val
    return _call("set_node_property", params)


# -- Function Tools ----------------------------------------------------------

@mcp.tool()
def create_blueprint_function(blueprint_name: str, function_name: str, return_type: str = "void") -> str:
    """Create a new function in a Blueprint."""
    return _call("create_function", {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
        "return_type": return_type,
    })


@mcp.tool()
def add_function_input(
    blueprint_name: str, function_name: str,
    param_name: str, param_type: str, is_array: bool = False,
) -> str:
    """Add an input parameter to a Blueprint function."""
    return _call("add_function_input", {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
        "param_name": param_name,
        "param_type": param_type,
        "is_array": is_array,
    })


@mcp.tool()
def add_function_output(
    blueprint_name: str, function_name: str,
    param_name: str, param_type: str, is_array: bool = False,
) -> str:
    """Add an output parameter to a Blueprint function."""
    return _call("add_function_output", {
        "blueprint_name": blueprint_name,
        "function_name": function_name,
        "param_name": param_name,
        "param_type": param_type,
        "is_array": is_array,
    })


@mcp.tool()
def delete_blueprint_function(blueprint_name: str, function_name: str) -> str:
    """Delete a function from a Blueprint."""
    return _call("delete_function", {"blueprint_name": blueprint_name, "function_name": function_name})


@mcp.tool()
def rename_blueprint_function(blueprint_name: str, old_function_name: str, new_function_name: str) -> str:
    """Rename a function in a Blueprint."""
    return _call("rename_function", {
        "blueprint_name": blueprint_name,
        "old_function_name": old_function_name,
        "new_function_name": new_function_name,
    })


# -- Inspection Tools --------------------------------------------------------

@mcp.tool()
def read_blueprint_content(
    blueprint_path: str,
    include_event_graph: bool = True,
    include_functions: bool = True,
    include_variables: bool = True,
    include_components: bool = True,
    include_interfaces: bool = True,
) -> str:
    """Read complete Blueprint content: event graph, functions, variables, components."""
    return _call("read_blueprint_content", {
        "blueprint_path": blueprint_path,
        "include_event_graph": include_event_graph,
        "include_functions": include_functions,
        "include_variables": include_variables,
        "include_components": include_components,
        "include_interfaces": include_interfaces,
    })


@mcp.tool()
def analyze_blueprint_graph(
    blueprint_path: str,
    graph_name: str = "EventGraph",
    include_node_details: bool = True,
    include_pin_connections: bool = True,
    trace_execution_flow: bool = True,
) -> str:
    """Analyze a specific graph — nodes, connections, and execution flow."""
    return _call("analyze_blueprint_graph", {
        "blueprint_path": blueprint_path,
        "graph_name": graph_name,
        "include_node_details": include_node_details,
        "include_pin_connections": include_pin_connections,
        "trace_execution_flow": trace_execution_flow,
    })


@mcp.tool()
def query_blueprint_graph(
    blueprint_path: str,
    graph_scope: str = "all",
    node_filter: str = "",
    include_pins: bool = True,
    include_connections: bool = False,
    max_nodes: int = 50,
    offset: int = 0,
) -> str:
    """Query Blueprint graph nodes WITH pin defaults — the forensic reader.

    Scope to one graph ("event_graph", an exact function/macro name, or "all"),
    filter nodes by a case-insensitive substring (matched against node class,
    title, and pin default values/objects — so "SphereTrace" or "GrabComponent"
    both hit), and get back nodes with their pins' types, DEFAULT VALUES (enum
    literals like ObjectTypeQuery2, class pickers, object refs) and optional
    connections. Paged via max_nodes/offset; total_matched tells you if more
    remain. Use this instead of read_blueprint_content when you need to know
    what a node is actually configured to do.
    """
    return _call("query_blueprint_graph", {
        "blueprint_path": blueprint_path,
        "graph_scope": graph_scope,
        "node_filter": node_filter,
        "include_pins": include_pins,
        "include_connections": include_connections,
        "max_nodes": max_nodes,
        "offset": offset,
    })


@mcp.tool()
def get_blueprint_variable_details(blueprint_path: str, variable_name: str = "") -> str:
    """Get detailed info about Blueprint variables (empty name = all variables)."""
    params: Dict[str, Any] = {"blueprint_path": blueprint_path}
    if variable_name:
        params["variable_name"] = variable_name
    return _call("get_blueprint_variable_details", params)


@mcp.tool()
def get_blueprint_function_details(
    blueprint_path: str,
    function_name: str = "",
    include_graph: bool = True,
) -> str:
    """Get detailed info about Blueprint functions (empty name = all functions)."""
    params: Dict[str, Any] = {"blueprint_path": blueprint_path, "include_graph": include_graph}
    if function_name:
        params["function_name"] = function_name
    return _call("get_blueprint_function_details", params)


@mcp.tool()
def compile_blueprint(blueprint_name: str) -> str:
    """Compile a Blueprint."""
    return _call("compile_blueprint", {"blueprint_name": blueprint_name})
